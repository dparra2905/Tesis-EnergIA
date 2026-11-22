-- =====================================================================
-- EnergIA — Migración 002 (D-011): serie_unida con las columnas reales
-- de la rejilla de 1 s y código 23505 para "corrida ya subida".
--
-- Para el proyecto que ya tiene la versión 1 (001_inicial.sql).
-- SQL Editor → pegar TODO el archivo → Run. Es una sola transacción.
--
-- serie_unida se rehace desde cero. Solo se permite si está vacía (en la
-- versión 1 nadie ha subido corridas todavía); si tiene filas, se detiene
-- sin cambiar nada.
-- =====================================================================

begin;

do $$
begin
  if exists (select 1 from public.serie_unida) then
    raise exception 'serie_unida ya tiene datos: esta migración no se aplica (no se cambió nada)';
  end if;
end;
$$;

drop table public.serie_unida;

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
  primary key (corrida_id, t)
);

alter table public.serie_unida enable row level security;

create policy serie_unida_ver on public.serie_unida
  for select to authenticated using (public.puede_ver_corrida(corrida_id));
create policy serie_unida_crear on public.serie_unida
  for insert to authenticated with check (public.es_dueno_corrida(corrida_id));

-- Supabase da privilegios por defecto a anon sobre las tablas nuevas
revoke all on public.serie_unida from anon;

-- subir_corrida: igual que en la versión 1, salvo el error de duplicado
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
    -- Se conserva el código 23505 para que la API responda 409 y la app
    -- sepa que la corrida ya estaba subida (no es un error para reintentar).
    raise exception using
      errcode = '23505',
      message = format('La corrida %s del equipo %s ya fue subida en este experimento',
                       r.numero, r.equipo);
end;
$$;


grant execute on function public.subir_corrida(jsonb) to authenticated;

commit;
