-- =====================================================================
-- EnergIA — Migración 004 (D-011): equipos (hardware) y copia de
-- resultados.json en cada corrida.
--
-- Para un proyecto con la migración 003 aplicada. SQL Editor → pegar TODO el
-- archivo → Run. Es una sola transacción.
--
-- 1. hardware_equipo: foto del equipo donde se midió (CPU, núcleos, RAM,
--    sistema operativo). corrida.hardware_id apunta a ella. Las corridas ya
--    subidas quedan sin equipo (no se puede reconstruir desde la base).
-- 2. corrida.resultados: copia de resultados.json (cobertura, método y
--    versión del cálculo de energía), para ver el detalle desde otro equipo.
-- 3. subir_corrida: guarda ambos, y crea experimento, dataset y equipo sin
--    chocar con otra subida simultánea; solo la restricción corrida_unica se
--    informa como "ya subida".
-- =====================================================================

begin;

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

alter table public.hardware_equipo enable row level security;
create policy hardware_equipo_ver on public.hardware_equipo
  for select to authenticated using (true);
create policy hardware_equipo_crear on public.hardware_equipo
  for insert to authenticated with check (creado_por = auth.uid());

revoke all on public.hardware_equipo from anon;

alter table public.corrida add column resultados jsonb;
alter table public.corrida add column hardware_id bigint references public.hardware_equipo(id);

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
    potencia_media_w, potencia_max_w, cpu_media_pct, metadata, resultados, hardware_id
  ) values (
    v_usuario, v_exp, r.equipo, r.numero, r.t_creacion, coalesce(r.visibilidad, 'privada'),
    r.estado, r.status_code, r.modelo_dll, r.contrato_version, v_ds,
    r.pct_entrenamiento, r.semilla, r.t_start, r.t_antes, r.t_despues, r.t_stop,
    r.listo_en_t_start, r.accuracy_val,
    r.duracion_entrenamiento_s, r.energia_total_wh, r.energia_entrenamiento_wh,
    r.energia_base_pre_wh, r.energia_base_post_wh,
    r.potencia_media_w, r.potencia_max_w, r.cpu_media_pct, r.metadata, r.resultados, v_hw
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

commit;
