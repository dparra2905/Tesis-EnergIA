-- =====================================================================
-- EnergIA — Migración 007 (D-012): auditoría y nota de condiciones.
--
-- Para el proyecto que ya tiene la versión 6 (001 a 006 aplicadas).
-- SQL Editor → pegar TODO el archivo → Run. Es una sola transacción.
--
-- Columnas nuevas de corrida:
--   veredicto  'valida' | 'con_advertencias' | 'no_valida' (de auditoria.json).
--              null en las corridas subidas antes de esta migración y en
--              las que se suben sin auditoría.
--   auditoria  copia íntegra de auditoria.json (comprobaciones, umbrales,
--              versión de las reglas). La auditoría informa; no bloquea.
--   nota       nota de condiciones (texto libre, opcional; nota.txt).
--
-- Permisos:
--   * insert por columnas (migración 006): se agregan las tres, porque las
--     inserta subir_corrida. Si faltara una, la subida fallaría con
--     "permission denied for table corrida".
--   * update por columnas: visibilidad y, ahora, nota. La nota se puede
--     editar después de subida, solo por el dueño (regla corrida_editar);
--     veredicto y auditoria no: son datos de la corrida, como lo medido.
--
-- subir_corrida inserta las tres columnas (las toma del paquete por
-- nombre). Las corridas ya subidas no se modifican.
-- =====================================================================

begin;

alter table public.corrida
  add column veredicto text
             check (veredicto in ('valida', 'con_advertencias', 'no_valida')),
  add column auditoria jsonb,
  add column nota      text;

comment on column public.corrida.veredicto is
  'Veredicto de la auditoría (D-012): valida | con_advertencias | no_valida; null sin auditoría';
comment on column public.corrida.auditoria is
  'Copia íntegra de auditoria.json (D-012)';
comment on column public.corrida.nota is
  'Nota de condiciones de la corrida (D-012); editable después de subida solo por el dueño';

-- Insert por columnas: exactamente las que inserta subir_corrida (sin subida_en)
revoke insert on public.corrida from authenticated;
grant insert (
  usuario_id, experimento_id, equipo, numero, t_creacion, visibilidad,
  estado, status_code, modelo_dll, contrato_version, dataset_id,
  pct_entrenamiento, semilla, t_start, t_antes, t_despues, t_stop,
  listo_en_t_start, accuracy_val,
  duracion_entrenamiento_s, energia_total_wh, energia_entrenamiento_wh,
  energia_base_pre_wh, energia_base_post_wh,
  potencia_media_w, potencia_max_w, cpu_media_pct, metadata, resultados, hardware_id,
  veredicto, auditoria, nota
) on public.corrida to authenticated;

-- Lo único editable de una corrida subida: visibilidad y nota (solo el dueño)
revoke update on public.corrida from authenticated;
grant  update (visibilidad, nota) on public.corrida to authenticated;

-- Subida con las columnas nuevas
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
    veredicto, auditoria, nota
  ) values (
    v_usuario, v_exp, r.equipo, r.numero, r.t_creacion, coalesce(r.visibilidad, 'privada'),
    r.estado, r.status_code, r.modelo_dll, r.contrato_version, v_ds,
    r.pct_entrenamiento, r.semilla, r.t_start, r.t_antes, r.t_despues, r.t_stop,
    r.listo_en_t_start, r.accuracy_val,
    r.duracion_entrenamiento_s, r.energia_total_wh, r.energia_entrenamiento_wh,
    r.energia_base_pre_wh, r.energia_base_post_wh,
    r.potencia_media_w, r.potencia_max_w, r.cpu_media_pct, r.metadata, r.resultados, v_hw,
    r.veredicto, r.auditoria, r.nota
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

-- La API (PostgREST) relee el esquema para ver las columnas nuevas
notify pgrst, 'reload schema';
