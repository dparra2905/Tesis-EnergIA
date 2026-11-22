-- =====================================================================
-- EnergIA — Esquema de base de datos para Supabase (PostgreSQL)
-- Versión 1
--
-- Cómo usarlo: en el panel de Supabase → SQL Editor → pegar TODO el
-- archivo → Run. Se ejecuta una sola vez sobre un proyecto nuevo.
--
-- Decisiones que implementa:
--   * Login con registro abierto (cualquiera con el ejecutable crea cuenta).
--   * Roles: 'investigador' (por defecto) y 'admin' (ve todas las corridas).
--   * Visibilidad mixta: cada corrida es 'privada' (por defecto) o
--     'compartida' (la ve cualquier usuario con sesión).
--   * Solo el dueño cambia la visibilidad o borra su corrida. Los datos
--     medidos NO se pueden editar después de subidos (integridad).
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
-- (data: {"nombre": "..."}); si no, se usa el correo.
create or replace function public.crear_perfil()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  insert into public.perfil (id, nombre)
  values (new.id, coalesce(new.raw_user_meta_data->>'nombre', new.email));
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
  numero            int  not null,          -- el NNN de corrida_NNN
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
  subida_en         timestamptz not null default now(),

  -- Un mismo usuario puede medir en varios PC con el mismo experimento.
  unique (experimento_id, equipo, numero)
);

create index corrida_usuario_idx     on public.corrida (usuario_id);
create index corrida_experimento_idx on public.corrida (experimento_id);
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

-- progreso.csv: una fila por unidad (época, árbol, ...)
create table public.paso (
  corrida_id  bigint not null references public.corrida(id) on delete cascade,
  step_index  int    not null,
  step_total  int,
  t_inicio    timestamptz,
  t_fin       timestamptz,
  primary key (corrida_id, step_index)
);

-- Los hasta 4 pares nombre/valor de cada paso, en formato largo.
create table public.metrica_paso (
  corrida_id  bigint not null references public.corrida(id) on delete cascade,
  step_index  int    not null,
  nombre      text   not null,
  valor       double precision,
  primary key (corrida_id, step_index, nombre)
);

-- Serie unida: una fila por segundo de la rejilla (salida de procesar_corrida).
-- PENDIENTE: ajustar las columnas de sistema a las que realmente
-- genera procesar_corrida. Solo hay que cambiarlas aquí; la función de
-- subida las toma automáticamente por nombre.
create table public.serie_unida (
  corrida_id      bigint not null references public.corrida(id) on delete cascade,
  t               timestamptz not null,   -- punto de la rejilla t_start + n·1 s
  fase            text not null,          -- ej. base_pre / entrenamiento / transicion / base_post
  paso_desde      int,
  paso_hasta      int,
  -- Energía (Atorch S1BW)
  conectado       boolean,
  voltage_v       double precision,
  current_a       double precision,
  power_w         double precision,
  energy_kwh      double precision,
  -- Sistema (AJUSTAR)
  cpu_total_pct   double precision,
  cpu_medidor_pct double precision,
  ram_usada_mb    double precision,
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


-- ---------------------------------------------------------------------
-- 7. REGLAS DE PERMISOS (Row Level Security)
-- ---------------------------------------------------------------------
alter table public.perfil         enable row level security;
alter table public.dataset        enable row level security;
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

-- Detalle: se ve si se puede ver la corrida; solo el dueño lo inserta.
-- No hay reglas de update/delete: los datos medidos no se editan.
-- (Al borrar la corrida, su detalle se borra en cascada.)
create policy hiperparametro_ver on public.hiperparametro
  for select to authenticated using (public.puede_ver_corrida(corrida_id));
create policy hiperparametro_crear on public.hiperparametro
  for insert to authenticated with check (public.es_dueno_corrida(corrida_id));

create policy paso_ver on public.paso
  for select to authenticated using (public.puede_ver_corrida(corrida_id));
create policy paso_crear on public.paso
  for insert to authenticated with check (public.es_dueno_corrida(corrida_id));

create policy metrica_paso_ver on public.metrica_paso
  for select to authenticated using (public.puede_ver_corrida(corrida_id));
create policy metrica_paso_crear on public.metrica_paso
  for insert to authenticated with check (public.es_dueno_corrida(corrida_id));

create policy serie_unida_ver on public.serie_unida
  for select to authenticated using (public.puede_ver_corrida(corrida_id));
create policy serie_unida_crear on public.serie_unida
  for insert to authenticated with check (public.es_dueno_corrida(corrida_id));


-- ---------------------------------------------------------------------
-- 8. PRIVILEGIOS
--    Sin sesión (anon) no se ve nada. Con sesión, solo se pueden
--    editar columnas puntuales.
-- ---------------------------------------------------------------------
revoke all on all tables in schema public from anon;

-- Perfil: solo el nombre es editable (el rol lo cambia el admin desde el panel).
revoke update on public.perfil from authenticated;
grant  update (nombre) on public.perfil to authenticated;

-- Corrida: lo único editable después de subida es la visibilidad.
revoke update on public.corrida from authenticated;
grant  update (visibilidad) on public.corrida to authenticated;

-- Experimento: solo nombre y descripción.
revoke update on public.experimento from authenticated;
grant  update (nombre, descripcion) on public.experimento to authenticated;

-- Funciones: solo usuarios con sesión.
revoke execute on all functions in schema public from public, anon;
grant  execute on function public.es_admin()                 to authenticated;
grant  execute on function public.puede_ver_corrida(bigint)  to authenticated;
grant  execute on function public.es_dueno_corrida(bigint)   to authenticated;


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
--                  "visibilidad": "privada", "estado": "completada",
--                  "status_code": 0, "modelo_dll": "EnergIA_Arbol.dll",
--                  "contrato_version": 3, "pct_entrenamiento": 0.8,
--                  "semilla": 42, "t_start": "2026-10-15T14:00:00.000Z",
--                  ..., "energia_total_wh": 12.3, ..., "metadata": {...}},
--      "hiperparametros": [{"nombre": "criterio", "valor": "gini"}, ...],
--      "pasos":   [{"step_index": 0, "step_total": 1,
--                   "t_inicio": "...", "t_fin": "..."}, ...],
--      "metricas":[{"step_index": 0, "nombre": "profundidad", "valor": 12}, ...],
--      "serie":   [{"t": "...", "fase": "entrenamiento", "paso_desde": 0,
--                   "paso_hasta": 0, "power_w": 45.2, ...}, ...]
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
  r          public.corrida;
begin
  if v_usuario is null then
    raise exception 'Sesión no iniciada';
  end if;

  -- Experimento: buscar o crear (por nombre, dentro del usuario)
  select id into v_exp
  from public.experimento
  where usuario_id = v_usuario and nombre = p->'experimento'->>'nombre';

  if v_exp is null then
    insert into public.experimento (usuario_id, nombre, descripcion)
    values (v_usuario, p->'experimento'->>'nombre', p->'experimento'->>'descripcion')
    returning id into v_exp;
  end if;

  -- Dataset: buscar por huella o crear
  if jsonb_typeof(p->'dataset') = 'object' then
    select id into v_ds from public.dataset where sha256 = p->'dataset'->>'sha256';
    if v_ds is null then
      insert into public.dataset (sha256, nombre, filas, columnas,
                                  columna_objetivo, n_clases, creado_por)
      select d.sha256, d.nombre, d.filas, d.columnas,
             d.columna_objetivo, d.n_clases, v_usuario
      from jsonb_populate_record(null::public.dataset, p->'dataset') d
      returning id into v_ds;
    end if;
  end if;

  -- Corrida (usuario, experimento y dataset los fija el servidor, no la app)
  r := jsonb_populate_record(null::public.corrida, p->'corrida');

  insert into public.corrida (
    usuario_id, experimento_id, equipo, numero, visibilidad,
    estado, status_code, modelo_dll, contrato_version, dataset_id,
    pct_entrenamiento, semilla, t_start, t_antes, t_despues, t_stop,
    listo_en_t_start, accuracy_val,
    duracion_entrenamiento_s, energia_total_wh, energia_entrenamiento_wh,
    energia_base_pre_wh, energia_base_post_wh,
    potencia_media_w, potencia_max_w, cpu_media_pct, metadata
  ) values (
    v_usuario, v_exp, r.equipo, r.numero, coalesce(r.visibilidad, 'privada'),
    r.estado, r.status_code, r.modelo_dll, r.contrato_version, v_ds,
    r.pct_entrenamiento, r.semilla, r.t_start, r.t_antes, r.t_despues, r.t_stop,
    r.listo_en_t_start, r.accuracy_val,
    r.duracion_entrenamiento_s, r.energia_total_wh, r.energia_entrenamiento_wh,
    r.energia_base_pre_wh, r.energia_base_post_wh,
    r.potencia_media_w, r.potencia_max_w, r.cpu_media_pct, r.metadata
  )
  returning id into v_corrida;

  -- Detalle: cada elemento se mapea por nombre de columna
  insert into public.hiperparametro
  select h.*
  from jsonb_array_elements(coalesce(p->'hiperparametros', '[]'::jsonb)) e,
       lateral jsonb_populate_record(null::public.hiperparametro,
               e || jsonb_build_object('corrida_id', v_corrida)) h;

  insert into public.paso
  select x.*
  from jsonb_array_elements(coalesce(p->'pasos', '[]'::jsonb)) e,
       lateral jsonb_populate_record(null::public.paso,
               e || jsonb_build_object('corrida_id', v_corrida)) x;

  insert into public.metrica_paso
  select m.*
  from jsonb_array_elements(coalesce(p->'metricas', '[]'::jsonb)) e,
       lateral jsonb_populate_record(null::public.metrica_paso,
               e || jsonb_build_object('corrida_id', v_corrida)) m;

  insert into public.serie_unida
  select s.*
  from jsonb_array_elements(coalesce(p->'serie', '[]'::jsonb)) e,
       lateral jsonb_populate_record(null::public.serie_unida,
               e || jsonb_build_object('corrida_id', v_corrida)) s;

  return v_corrida;

exception
  when unique_violation then
    raise exception 'La corrida % del equipo % ya fue subida en este experimento',
      r.numero, r.equipo;
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
