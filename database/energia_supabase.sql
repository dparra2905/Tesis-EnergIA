-- =====================================================================
-- EnergIA — Esquema de base de datos para Supabase (PostgreSQL)
-- Versión 9 (D-011, D-012, D-014, D-015) = migraciones/001_inicial.sql + 002_serie_unida_rejilla.sql
--                     + 003_identidad_corrida_y_perfil.sql + 004_equipos.sql
--                     + 005_serie_unida_fase_y_pasos.sql
--                     + 006_cerrar_inserts_tras_subida.sql
--                     + 007_auditoria_y_nota.sql
--                     + 008_inferencia.sql
--                     + 009_metricas_modelo.sql
--
-- Cómo usarlo: en el panel de Supabase → SQL Editor → pegar TODO el
-- archivo → Run. Se ejecuta una sola vez sobre un proyecto NUEVO.
-- Un proyecto con una versión anterior se actualiza con las migraciones
-- que le falten, en orden (no con este archivo).
--
-- Decisiones que implementa:
--   * Login con registro abierto (cualquiera con el ejecutable crea cuenta).
--   * Roles: 'investigador' (por defecto) y 'admin' (ve todas las corridas).
--   * Visibilidad mixta: cada corrida es 'privada' (por defecto) o
--     'compartida' (la ve cualquier usuario con sesión).
--   * Solo el dueño cambia la visibilidad o la nota de condiciones, o borra
--     su corrida. Los datos medidos NO se pueden editar después de subidos
--     (integridad), tampoco el veredicto de la auditoría: el
--     detalle de una corrida solo se inserta dentro de la transacción de
--     subir_corrida que la creó (corrida_en_subida, subida_en).
--   * Nivel 3: configuración + resumen + serie unida por segundo.
--   * Los CSV crudos NO se suben: se quedan en el PC donde se midió.
--   * La subida es UNA transacción (función subir_corrida): o se guarda
--     la corrida completa o no se guarda nada.
-- =====================================================================


-- ---------------------------------------------------------------------
-- 1. PERFILES DE USUARIO
--    Supabase guarda las cuentas en auth.users. Aquí se guarda el rol.
-- ---------------------------------------------------------------------
create table public.perfil (
  id         uuid primary key references auth.users(id) on delete cascade,
  nombre     text,
  rol        text not null default 'investigador'
             check (rol in ('investigador', 'admin')),
  creado_en  timestamptz not null default now()
);

-- Crea el perfil automáticamente cuando alguien se registra.
-- La app puede enviar el nombre en los metadatos del registro
-- (data: {"nombre": "..."}). Si no lo envía, se usa 'usuario-' + los 8
-- primeros caracteres del id: el correo NUNCA se copia al perfil, porque
-- todos los usuarios con sesión pueden leer los nombres (perfil_ver).
create or replace function public.crear_perfil()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  insert into public.perfil (id, nombre)
  values (new.id, coalesce(nullif(btrim(new.raw_user_meta_data->>'nombre'), ''),
                           'usuario-' || left(new.id::text, 8)));
  return new;
end;
$$;

create trigger al_crear_usuario
after insert on auth.users
for each row execute function public.crear_perfil();

-- ¿El usuario con sesión es admin?
create or replace function public.es_admin()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (
    select 1 from public.perfil
    where id = auth.uid() and rol = 'admin'
  );
$$;


-- ---------------------------------------------------------------------
-- 2. DATASETS
--    Solo metadatos (no los datos). La huella sha256 prueba que dos
--    corridas usaron exactamente el mismo CSV.
-- ---------------------------------------------------------------------
create table public.dataset (
  id                bigint generated always as identity primary key,
  sha256            text not null unique,
  nombre            text not null,
  filas             bigint,
  columnas          int,
  columna_objetivo  text,
  n_clases          int,
  creado_por        uuid not null default auth.uid() references public.perfil(id),
  creado_en         timestamptz not null default now()
);


-- ---------------------------------------------------------------------
-- 2b. EQUIPOS (hardware donde se midió)
--     El consumo depende del hardware: cada corrida apunta a una foto del
--     equipo (nombre, CPU, núcleos, RAM, sistema operativo). Si cambia
--     algo (p. ej. más RAM), es otra fila. Catálogo visible para todos.
-- ---------------------------------------------------------------------
create table public.hardware_equipo (
  id                 bigint generated always as identity primary key,
  nombre             text not null,          -- nombre del PC (corrida.equipo)
  cpu                text,                   -- modelo de CPU
  nucleos_logicos    int,
  ram_total_bytes    bigint,
  sistema_operativo  text,
  arquitectura       text,
  creado_por         uuid not null default auth.uid() references public.perfil(id),
  creado_en          timestamptz not null default now(),
  constraint hardware_equipo_unico unique nulls not distinct
    (nombre, cpu, nucleos_logicos, ram_total_bytes, sistema_operativo, arquitectura)
);


-- ---------------------------------------------------------------------
-- 3. EXPERIMENTOS (agrupan corridas; cada usuario tiene los suyos)
-- ---------------------------------------------------------------------
create table public.experimento (
  id           bigint generated always as identity primary key,
  usuario_id   uuid not null default auth.uid()
               references public.perfil(id) on delete cascade,
  nombre       text not null,
  descripcion  text,
  creado_en    timestamptz not null default now(),
  unique (usuario_id, nombre)
);


-- ---------------------------------------------------------------------
-- 4. CORRIDAS (una fila por clic en "Iniciar")
--    Configuración + resumen de consumo.
-- ---------------------------------------------------------------------
create table public.corrida (
  id                bigint generated always as identity primary key,
  usuario_id        uuid not null default auth.uid()
                    references public.perfil(id) on delete cascade,
  experimento_id    bigint not null
                    references public.experimento(id) on delete cascade,
  equipo            text not null,          -- nombre del PC (el consumo depende del hardware)
  numero            int  not null,          -- el NNN de corrida_NNN (puede repetirse si se borra la carpeta)
  t_creacion        timestamptz not null,   -- metadata.tiempos.t_creacion: identifica la corrida
  visibilidad       text not null default 'privada'
                    check (visibilidad in ('privada', 'compartida')),

  -- Configuración y estado (de metadata.json)
  estado            text not null,          -- ej. completada / cancelada / error
  status_code       int,
  modelo_dll        text not null,          -- ej. EnergIA_Arbol.dll
  contrato_version  int,
  dataset_id        bigint references public.dataset(id),
  pct_entrenamiento real,
  semilla           bigint,
  t_start           timestamptz,
  t_antes           timestamptz,
  t_despues         timestamptz,
  t_stop            timestamptz,
  listo_en_t_start  boolean,
  accuracy_val      double precision,

  -- Resumen de consumo (lo calcula la app o procesar_corrida)
  -- SUGERIDO: ajustar cuando se defina la auditoría y la métrica de comparación.
  duracion_entrenamiento_s  double precision,
  energia_total_wh          double precision,
  energia_entrenamiento_wh  double precision,
  energia_base_pre_wh       double precision,
  energia_base_post_wh      double precision,
  potencia_media_w          double precision,
  potencia_max_w            double precision,
  cpu_media_pct             double precision,

  metadata          jsonb,                  -- copia íntegra de metadata.json
  resultados        jsonb,                  -- copia íntegra de resultados.json (energía derivada, D-008)
  hardware_id       bigint references public.hardware_equipo(id),
  -- Auditoría (D-012, migración 007): informa, no bloquea. null sin auditoría
  -- (corridas subidas antes de la 007 o sin auditoria.json).
  veredicto         text
                    check (veredicto in ('valida', 'con_advertencias', 'no_valida')),
  auditoria         jsonb,                  -- copia íntegra de auditoria.json
  nota              text,                   -- nota de condiciones; editable solo por el dueño
  -- Inferencia (D-014, migración 008): null en las corridas subidas antes.
  inferencia_activada       boolean,
  inferencia_lote           int,
  inferencia_duracion_min_s int,
  t_inf_antes               timestamptz,
  t_inf_despues             timestamptz,
  inferencia_pasadas        bigint,           -- pasadas completas por todas las filas
  n_predicciones            bigint,           -- filas × pasadas completas
  energia_inferencia_wh     double precision, -- energía en bruto de la ventana inferencia
  energia_base_media_wh     double precision, -- energía en bruto de la ventana base_media
  energia_por_prediccion_j  double precision, -- null si inferir no devolvió OK o sin monitor de energía
  tiempo_por_prediccion_us  double precision, -- null si inferir no devolvió OK
  -- Métricas del modelo (D-015, migración 009): null en las corridas subidas
  -- antes, en las corridas sin métricas y en el conjunto en que predecir falló.
  metricas_modelo           jsonb,            -- copia íntegra de metricas_modelo.json
  f1_macro_val              double precision, -- F1 macro en validación
  f1_ponderado_val          double precision, -- F1 ponderado en validación
  precision_macro_val       double precision, -- precisión macro en validación
  recall_macro_val          double precision, -- recall macro en validación
  accuracy_entrenamiento    double precision, -- exactitud en entrenamiento
  f1_macro_entrenamiento    double precision, -- F1 macro en entrenamiento
  -- Dos papeles: fecha visible de subida y marca que cierra los inserts del
  -- detalle (corrida_en_subida). Nadie la fija a mano: queda fuera del
  -- permiso de insert por columnas y sin permiso de update (sección 8).
  subida_en         timestamptz not null default transaction_timestamp(),

  -- Identidad de una corrida: experimento + equipo + momento de creación.
  -- No se usa el número: el orquestador lo reutiliza si se borra la carpeta
  -- de una corrida, y una corrida nueva se confundiría con una ya subida.
  constraint corrida_unica unique (experimento_id, equipo, t_creacion)
);

create index corrida_usuario_idx     on public.corrida (usuario_id);
create index corrida_experimento_idx on public.corrida (experimento_id);
create index corrida_equipo_creacion_idx on public.corrida (equipo, t_creacion);
create index corrida_modelo_idx      on public.corrida (modelo_dll);


-- ---------------------------------------------------------------------
-- 5. DETALLE DE CADA CORRIDA
-- ---------------------------------------------------------------------

-- Hiperparámetros en formato largo: sirve para cualquier DLL
-- (árbol, MLP, futuros modelos) sin cambiar el esquema.
create table public.hiperparametro (
  corrida_id  bigint not null references public.corrida(id) on delete cascade,
  nombre      text   not null,
  valor       text,
  primary key (corrida_id, nombre)
);

-- progreso.csv: una fila por unidad (época, árbol, ...), tarea 'entrenamiento';
-- progreso_inferencia.csv: una fila por grupo de pasadas, tarea 'inferencia'
-- (D-014, migración 008). tarea va al final, en el orden de la migración.
create table public.paso (
  corrida_id  bigint not null references public.corrida(id) on delete cascade,
  step_index  int    not null,
  step_total  int,
  t_inicio    timestamptz,
  t_fin       timestamptz,
  tarea       text   not null default 'entrenamiento'
              check (tarea in ('entrenamiento', 'inferencia')),
  primary key (corrida_id, tarea, step_index)
);

-- Los hasta 4 pares nombre/valor de cada paso, en formato largo.
create table public.metrica_paso (
  corrida_id  bigint not null references public.corrida(id) on delete cascade,
  step_index  int    not null,
  nombre      text   not null,
  valor       double precision,
  tarea       text   not null default 'entrenamiento'
              check (tarea in ('entrenamiento', 'inferencia')),
  primary key (corrida_id, tarea, step_index, nombre)
);

-- Serie unida: una fila por punto de la rejilla de 1 s (D-007). Son las
-- mismas columnas que exporta la app (Historial::exportarCsv /
-- analisis::escribirRejillaCsv); la función de subida las toma por nombre.
-- Los CSV crudos de los medidores NO se suben (D-011).
create table public.serie_unida (
  corrida_id       bigint not null references public.corrida(id) on delete cascade,
  t                timestamptz not null,       -- punto de la rejilla t_start + n·1 s (columna "timestamp" del CSV)
  t_rel_s          double precision not null,  -- segundos desde t_start
  en_entrenamiento boolean not null,           -- t_antes <= t <= t_despues
  -- Monitor de software (sistema_metrics.csv, derivados por intervalo de ~1 s)
  cpu_proceso_pct  double precision,           -- orq_cpu_s / cpu_sis_total_s * 100
  cpu_sistema_pct  double precision,           -- cpu_sis_ocupado_s / cpu_sis_total_s * 100
  ram_proceso_mb   double precision,           -- orq_ws_bytes / 2^20
  ram_sistema_pct  double precision,           -- ram_sis_usada_bytes / ram_total_bytes * 100
  -- Monitor físico de energía (energia_atorch.csv)
  potencia_w       double precision,
  voltaje_v        double precision,
  corriente_a      double precision,
  timestamp_trama  timestamptz,                -- hora real de la trama asignada a este punto (D-007)
  -- Fase y pasos (migración 005; null en las corridas subidas antes). Cada fila
  -- es el intervalo (t - 1 s, t]; reglas en src/sync/FasesCorrida.h. Van al
  -- final, en el mismo orden en que las agrega la migración.
  fase             text
                   check (fase in ('base_pre', 'entrenamiento', 'base_media', 'inferencia', 'base_post', 'transicion')),
  paso_desde       int,                        -- rango de step_index de progreso.csv que toca el intervalo
  paso_hasta       int,
  primary key (corrida_id, t)
);


-- ---------------------------------------------------------------------
-- 6. FUNCIONES DE APOYO PARA PERMISOS
-- ---------------------------------------------------------------------
create or replace function public.puede_ver_corrida(p_corrida bigint)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (
    select 1 from public.corrida c
    where c.id = p_corrida
      and (c.usuario_id = auth.uid()
           or c.visibilidad = 'compartida'
           or public.es_admin())
  );
$$;

create or replace function public.es_dueno_corrida(p_corrida bigint)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (
    select 1 from public.corrida c
    where c.id = p_corrida and c.usuario_id = auth.uid()
  );
$$;

-- El detalle de una corrida solo se inserta dentro de la transacción que la
-- creó: dueño y subida_en = inicio de esta transacción (transaction_timestamp,
-- el mismo valor que now()). Una corrida ya subida tiene un subida_en pasado.
create or replace function public.corrida_en_subida(p_corrida bigint)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (
    select 1 from public.corrida c
    where c.id = p_corrida
      and c.usuario_id = auth.uid()
      and c.subida_en = transaction_timestamp()
  );
$$;


-- ---------------------------------------------------------------------
-- 7. REGLAS DE PERMISOS (Row Level Security)
-- ---------------------------------------------------------------------
alter table public.perfil         enable row level security;
alter table public.dataset        enable row level security;
alter table public.hardware_equipo enable row level security;
create policy hardware_equipo_ver on public.hardware_equipo
  for select to authenticated using (true);
create policy hardware_equipo_crear on public.hardware_equipo
  for insert to authenticated with check (creado_por = auth.uid());
alter table public.experimento    enable row level security;
alter table public.corrida        enable row level security;
alter table public.hiperparametro enable row level security;
alter table public.paso           enable row level security;
alter table public.metrica_paso   enable row level security;
alter table public.serie_unida    enable row level security;

-- Perfiles: todos ven los nombres (para saber de quién es una corrida
-- compartida); cada uno solo puede cambiar SU nombre, nunca su rol.
create policy perfil_ver on public.perfil
  for select to authenticated using (true);
create policy perfil_editar on public.perfil
  for update to authenticated
  using (id = auth.uid()) with check (id = auth.uid());

-- Datasets: catálogo visible para todos; cualquiera registra uno nuevo.
create policy dataset_ver on public.dataset
  for select to authenticated using (true);
create policy dataset_crear on public.dataset
  for insert to authenticated with check (creado_por = auth.uid());

-- Experimentos: los míos, los que tienen alguna corrida compartida,
-- o todos si soy admin.
create policy experimento_ver on public.experimento
  for select to authenticated
  using (
    usuario_id = auth.uid()
    or public.es_admin()
    or exists (
      select 1 from public.corrida c
      where c.experimento_id = experimento.id
        and c.visibilidad = 'compartida'
    )
  );
create policy experimento_crear on public.experimento
  for insert to authenticated with check (usuario_id = auth.uid());
create policy experimento_editar on public.experimento
  for update to authenticated
  using (usuario_id = auth.uid()) with check (usuario_id = auth.uid());
create policy experimento_borrar on public.experimento
  for delete to authenticated using (usuario_id = auth.uid());

-- Corridas: modo mixto + admin.
create policy corrida_ver on public.corrida
  for select to authenticated
  using (usuario_id = auth.uid()
         or visibilidad = 'compartida'
         or public.es_admin());
create policy corrida_crear on public.corrida
  for insert to authenticated
  with check (
    usuario_id = auth.uid()
    and exists (select 1 from public.experimento e
                where e.id = experimento_id and e.usuario_id = auth.uid())
  );
create policy corrida_editar on public.corrida
  for update to authenticated
  using (usuario_id = auth.uid()) with check (usuario_id = auth.uid());
create policy corrida_borrar on public.corrida
  for delete to authenticated using (usuario_id = auth.uid());

-- Detalle: se ve si se puede ver la corrida; solo el dueño lo inserta, y solo
-- dentro de la transacción de subir_corrida que creó la corrida.
-- No hay reglas de update/delete: los datos medidos no se editan.
-- (Al borrar la corrida, su detalle se borra en cascada.)
create policy hiperparametro_ver on public.hiperparametro
  for select to authenticated using (public.puede_ver_corrida(corrida_id));
create policy hiperparametro_crear on public.hiperparametro
  for insert to authenticated with check (public.corrida_en_subida(corrida_id));

create policy paso_ver on public.paso
  for select to authenticated using (public.puede_ver_corrida(corrida_id));
create policy paso_crear on public.paso
  for insert to authenticated with check (public.corrida_en_subida(corrida_id));

create policy metrica_paso_ver on public.metrica_paso
  for select to authenticated using (public.puede_ver_corrida(corrida_id));
create policy metrica_paso_crear on public.metrica_paso
  for insert to authenticated with check (public.corrida_en_subida(corrida_id));

create policy serie_unida_ver on public.serie_unida
  for select to authenticated using (public.puede_ver_corrida(corrida_id));
create policy serie_unida_crear on public.serie_unida
  for insert to authenticated with check (public.corrida_en_subida(corrida_id));


-- ---------------------------------------------------------------------
-- 8. PRIVILEGIOS
--    Sin sesión (anon) no se ve nada. Con sesión, solo se pueden
--    editar columnas puntuales.
-- ---------------------------------------------------------------------
revoke all on all tables in schema public from anon;

-- Perfil: solo el nombre es editable (el rol lo cambia el admin desde el panel).
revoke update on public.perfil from authenticated;
grant  update (nombre) on public.perfil to authenticated;

-- Corrida: lo único editable después de subida es la visibilidad y la nota
-- de condiciones (D-012), y solo por el dueño (regla corrida_editar).
revoke update on public.corrida from authenticated;
grant  update (visibilidad, nota) on public.corrida to authenticated;

-- Corrida: insert solo por columnas, exactamente las que inserta subir_corrida.
-- subida_en queda fuera para que nadie la fije a mano (cierra los inserts del
-- detalle). IMPORTANTE: toda columna nueva de corrida que inserte subir_corrida
-- (como las de inferencia de la 008 o las de métricas de la 009) hay que
-- agregarla aquí y en el permiso de su migración; si falta, la subida falla.
revoke insert on public.corrida from authenticated;
grant insert (
  usuario_id, experimento_id, equipo, numero, t_creacion, visibilidad,
  estado, status_code, modelo_dll, contrato_version, dataset_id,
  pct_entrenamiento, semilla, t_start, t_antes, t_despues, t_stop,
  listo_en_t_start, accuracy_val,
  duracion_entrenamiento_s, energia_total_wh, energia_entrenamiento_wh,
  energia_base_pre_wh, energia_base_post_wh,
  potencia_media_w, potencia_max_w, cpu_media_pct, metadata, resultados, hardware_id,
  veredicto, auditoria, nota,
  inferencia_activada, inferencia_lote, inferencia_duracion_min_s, t_inf_antes, t_inf_despues,
  inferencia_pasadas, n_predicciones, energia_inferencia_wh, energia_base_media_wh,
  energia_por_prediccion_j, tiempo_por_prediccion_us,
  metricas_modelo, f1_macro_val, f1_ponderado_val, precision_macro_val, recall_macro_val,
  accuracy_entrenamiento, f1_macro_entrenamiento
) on public.corrida to authenticated;

-- Experimento: solo nombre y descripción.
revoke update on public.experimento from authenticated;
grant  update (nombre, descripcion) on public.experimento to authenticated;

-- Funciones: solo usuarios con sesión.
revoke execute on all functions in schema public from public, anon;
grant  execute on function public.es_admin()                 to authenticated;
grant  execute on function public.puede_ver_corrida(bigint)  to authenticated;
grant  execute on function public.es_dueno_corrida(bigint)   to authenticated;
grant  execute on function public.corrida_en_subida(bigint)  to authenticated;


-- ---------------------------------------------------------------------
-- 9. SUBIDA DE UNA CORRIDA EN UNA SOLA TRANSACCIÓN
--    La app Qt llama POST /rest/v1/rpc/subir_corrida con body {"p": {...}}
--    Corre con los permisos del usuario (aplican todas las reglas).
--
--    Estructura esperada de p:
--    {
--      "experimento": {"nombre": "Arbol HEPMASS gini", "descripcion": "..."},
--      "dataset": {"sha256": "...", "nombre": "HEPMASS all_train.csv",
--                  "filas": 7000000, "columnas": 29,
--                  "columna_objetivo": "# label", "n_clases": 2},
--      "corrida": {"equipo": "PC-DAVID", "numero": 3,
--                  "t_creacion": "2026-10-15T13:59:50.000Z",
--                  "visibilidad": "privada", "estado": "completada",
--                  "status_code": 0, "modelo_dll": "EnergIA_Arbol.dll",
--                  "contrato_version": 4, "pct_entrenamiento": 0.8,
--                  "semilla": 42, "t_start": "2026-10-15T14:00:00.000Z",
--                  ..., "energia_total_wh": 12.3, ..., "metadata": {...}},
--      "hiperparametros": [{"nombre": "criterio", "valor": "gini"}, ...],
--      "pasos":   [{"tarea": "entrenamiento", "step_index": 0, "step_total": 1,
--                   "t_inicio": "...", "t_fin": "..."},
--                  {"tarea": "inferencia", "step_index": 0, "step_total": 0, ...}, ...],
--      "metricas":[{"tarea": "entrenamiento", "step_index": 0, "nombre": "profundidad",
--                   "valor": 12}, ...],
--      "serie":   [{"t": "...", "t_rel_s": 12.0, "en_entrenamiento": true,
--                   "fase": "entrenamiento", "paso_desde": 3, "paso_hasta": 3,
--                   "cpu_proceso_pct": 6.1, "potencia_w": 45.2, ...,
--                   "timestamp_trama": "..."}, ...]
--    }
--    Devuelve el id de la corrida creada.
-- ---------------------------------------------------------------------
create or replace function public.subir_corrida(p jsonb)
returns bigint
language plpgsql
security invoker
set search_path = ''
as $$
declare
  v_usuario  uuid := auth.uid();
  v_exp      bigint;
  v_ds       bigint;
  v_corrida  bigint;
  v_hw       bigint;
  v_restr    text;
  r          public.corrida;
  v_hwr      public.hardware_equipo;
begin
  if v_usuario is null then
    raise exception 'Sesión no iniciada';
  end if;

  -- Experimento: buscar o crear (por nombre, dentro del usuario).
  -- Los "on conflict do nothing" evitan que dos subidas simultáneas choquen
  -- al crear el mismo experimento, dataset o equipo.
  insert into public.experimento (usuario_id, nombre, descripcion)
  values (v_usuario, p->'experimento'->>'nombre', p->'experimento'->>'descripcion')
  on conflict (usuario_id, nombre) do nothing
  returning id into v_exp;
  if v_exp is null then
    select id into v_exp
    from public.experimento
    where usuario_id = v_usuario and nombre = p->'experimento'->>'nombre';
  end if;

  -- Dataset: buscar por huella o crear
  if jsonb_typeof(p->'dataset') = 'object' then
    insert into public.dataset (sha256, nombre, filas, columnas,
                                columna_objetivo, n_clases, creado_por)
    select d.sha256, d.nombre, d.filas, d.columnas,
           d.columna_objetivo, d.n_clases, v_usuario
    from jsonb_populate_record(null::public.dataset, p->'dataset') d
    on conflict (sha256) do nothing
    returning id into v_ds;
    if v_ds is null then
      select id into v_ds from public.dataset where sha256 = p->'dataset'->>'sha256';
    end if;
  end if;

  -- Equipo (hardware): buscar o crear la foto del equipo
  if jsonb_typeof(p->'hardware') = 'object' then
    v_hwr := jsonb_populate_record(null::public.hardware_equipo, p->'hardware');
    insert into public.hardware_equipo (nombre, cpu, nucleos_logicos, ram_total_bytes,
                                        sistema_operativo, arquitectura, creado_por)
    values (v_hwr.nombre, v_hwr.cpu, v_hwr.nucleos_logicos, v_hwr.ram_total_bytes,
            v_hwr.sistema_operativo, v_hwr.arquitectura, v_usuario)
    on conflict on constraint hardware_equipo_unico do nothing
    returning id into v_hw;
    if v_hw is null then
      select x.id into v_hw from public.hardware_equipo x
      where x.nombre = v_hwr.nombre
        and x.cpu is not distinct from v_hwr.cpu
        and x.nucleos_logicos is not distinct from v_hwr.nucleos_logicos
        and x.ram_total_bytes is not distinct from v_hwr.ram_total_bytes
        and x.sistema_operativo is not distinct from v_hwr.sistema_operativo
        and x.arquitectura is not distinct from v_hwr.arquitectura;
    end if;
  end if;

  -- Corrida (usuario, experimento y dataset los fija el servidor, no la app)
  r := jsonb_populate_record(null::public.corrida, p->'corrida');
  if r.t_creacion is null then
    raise exception 'Falta corrida.t_creacion (metadata.tiempos.t_creacion)';
  end if;

  insert into public.corrida (
    usuario_id, experimento_id, equipo, numero, t_creacion, visibilidad,
    estado, status_code, modelo_dll, contrato_version, dataset_id,
    pct_entrenamiento, semilla, t_start, t_antes, t_despues, t_stop,
    listo_en_t_start, accuracy_val,
    duracion_entrenamiento_s, energia_total_wh, energia_entrenamiento_wh,
    energia_base_pre_wh, energia_base_post_wh,
    potencia_media_w, potencia_max_w, cpu_media_pct, metadata, resultados, hardware_id,
    veredicto, auditoria, nota,
    inferencia_activada, inferencia_lote, inferencia_duracion_min_s, t_inf_antes, t_inf_despues,
    inferencia_pasadas, n_predicciones, energia_inferencia_wh, energia_base_media_wh,
    energia_por_prediccion_j, tiempo_por_prediccion_us,
    metricas_modelo, f1_macro_val, f1_ponderado_val, precision_macro_val, recall_macro_val,
    accuracy_entrenamiento, f1_macro_entrenamiento
  ) values (
    v_usuario, v_exp, r.equipo, r.numero, r.t_creacion, coalesce(r.visibilidad, 'privada'),
    r.estado, r.status_code, r.modelo_dll, r.contrato_version, v_ds,
    r.pct_entrenamiento, r.semilla, r.t_start, r.t_antes, r.t_despues, r.t_stop,
    r.listo_en_t_start, r.accuracy_val,
    r.duracion_entrenamiento_s, r.energia_total_wh, r.energia_entrenamiento_wh,
    r.energia_base_pre_wh, r.energia_base_post_wh,
    r.potencia_media_w, r.potencia_max_w, r.cpu_media_pct, r.metadata, r.resultados, v_hw,
    r.veredicto, r.auditoria, r.nota,
    r.inferencia_activada, r.inferencia_lote, r.inferencia_duracion_min_s, r.t_inf_antes, r.t_inf_despues,
    r.inferencia_pasadas, r.n_predicciones, r.energia_inferencia_wh, r.energia_base_media_wh,
    r.energia_por_prediccion_j, r.tiempo_por_prediccion_us,
    r.metricas_modelo, r.f1_macro_val, r.f1_ponderado_val, r.precision_macro_val, r.recall_macro_val,
    r.accuracy_entrenamiento, r.f1_macro_entrenamiento
  )
  returning id into v_corrida;

  -- Detalle: cada elemento se mapea por nombre de columna
  insert into public.hiperparametro
  select h.*
  from jsonb_array_elements(coalesce(p->'hiperparametros', '[]'::jsonb)) e,
       lateral jsonb_populate_record(null::public.hiperparametro,
               e || jsonb_build_object('corrida_id', v_corrida)) h;

  -- Pasos y métricas: tarea 'entrenamiento' (progreso.csv) o 'inferencia'
  -- (progreso_inferencia.csv). Si el paquete no la trae (app anterior a la
  -- 008), 'entrenamiento', como el default de la columna.
  insert into public.paso
  select x.*
  from jsonb_array_elements(coalesce(p->'pasos', '[]'::jsonb)) e,
       lateral jsonb_populate_record(null::public.paso,
               jsonb_build_object('tarea', 'entrenamiento') || e
               || jsonb_build_object('corrida_id', v_corrida)) x;

  insert into public.metrica_paso
  select m.*
  from jsonb_array_elements(coalesce(p->'metricas', '[]'::jsonb)) e,
       lateral jsonb_populate_record(null::public.metrica_paso,
               jsonb_build_object('tarea', 'entrenamiento') || e
               || jsonb_build_object('corrida_id', v_corrida)) m;

  insert into public.serie_unida
  select s.*
  from jsonb_array_elements(coalesce(p->'serie', '[]'::jsonb)) e,
       lateral jsonb_populate_record(null::public.serie_unida,
               e || jsonb_build_object('corrida_id', v_corrida)) s;

  return v_corrida;

exception
  when unique_violation then
    get stacked diagnostics v_restr = constraint_name;
    if v_restr <> 'corrida_unica' then
      raise;  -- otra restricción: no es "ya subida"
    end if;
    -- Se conserva el código 23505 para que la API responda 409 y la app
    -- sepa que la corrida ya estaba subida (no es un error para reintentar).
    raise exception using
      errcode = '23505',
      message = format('La corrida %s (creada %s) del equipo %s ya fue subida en este experimento',
                       r.numero, r.t_creacion, r.equipo);
end;
$$;

grant execute on function public.subir_corrida(jsonb) to authenticated;


-- =====================================================================
-- 10. DESPUÉS DE EJECUTAR (manual, una sola vez)
--
-- Para volver admin a alguien: esa persona primero se registra desde
-- la app (o se crea en Authentication → Users), y luego se ejecuta en
-- el SQL Editor, cambiando el correo:
--
--   update public.perfil set rol = 'admin'
--   where id = (select id from auth.users where email = 'correo@ejemplo.com');
-- =====================================================================
