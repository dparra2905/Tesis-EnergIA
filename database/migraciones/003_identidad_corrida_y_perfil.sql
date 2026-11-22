-- =====================================================================
-- EnergIA — Migración 003 (D-011): identidad de la corrida y perfiles sin
-- correo.
--
-- Para un proyecto con la migración 002 aplicada. SQL Editor → pegar TODO el
-- archivo → Run. Es una sola transacción.
--
-- 1. corrida.t_creacion (metadata.tiempos.t_creacion) pasa a identificar la
--    corrida junto con el experimento y el equipo. Antes era el número: el
--    orquestador lo reutiliza si se borra la carpeta de una corrida, y la app
--    tomaba una corrida nueva por una ya subida (409) y no la subía.
--    Las corridas ya subidas se completan desde su copia de metadata.json.
-- 2. Los perfiles ya no copian el correo como nombre (todos los usuarios con
--    sesión leen los nombres). Los perfiles cuyo nombre es el correo pasan a
--    'usuario-' + 8 caracteres del id; cada usuario puede cambiarlo.
-- =====================================================================

begin;

-- 1. Identidad de la corrida ---------------------------------------------
alter table public.corrida add column t_creacion timestamptz;

update public.corrida
set t_creacion = (metadata->'tiempos'->>'t_creacion')::timestamptz
where t_creacion is null;

do $$
begin
  if exists (select 1 from public.corrida where t_creacion is null) then
    raise exception 'Hay corridas sin metadata.tiempos.t_creacion: la migración no se aplica (no se cambió nada)';
  end if;
end;
$$;

alter table public.corrida alter column t_creacion set not null;
alter table public.corrida drop constraint corrida_experimento_id_equipo_numero_key;
alter table public.corrida add constraint corrida_unica unique (experimento_id, equipo, t_creacion);
create index corrida_equipo_creacion_idx on public.corrida (equipo, t_creacion);

-- subir_corrida: guarda t_creacion y la exige
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
    potencia_media_w, potencia_max_w, cpu_media_pct, metadata
  ) values (
    v_usuario, v_exp, r.equipo, r.numero, r.t_creacion, coalesce(r.visibilidad, 'privada'),
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
      message = format('La corrida %s (creada %s) del equipo %s ya fue subida en este experimento',
                       r.numero, r.t_creacion, r.equipo);
end;
$$;


grant execute on function public.subir_corrida(jsonb) to authenticated;

-- 2. Perfiles sin correo ---------------------------------------------------
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

update public.perfil p
set nombre = 'usuario-' || left(p.id::text, 8)
from auth.users u
where u.id = p.id
  and (p.nombre is null or btrim(p.nombre) = '' or lower(p.nombre) = lower(u.email));

commit;
