-- =====================================================================
-- EnergIA — Migración 008 (D-014): inferencia.
--
-- Para el proyecto que ya tiene la versión 7 (001 a 007 aplicadas).
-- SQL Editor → pegar TODO el archivo → Run. Es una sola transacción.
--
-- Columnas nuevas de corrida (null en las corridas subidas antes de esta
-- migración; las corridas ya subidas no se modifican):
--   inferencia_activada        la corrida tuvo la inferencia activada
--   inferencia_lote            filas por bloque de predicción
--   inferencia_duracion_min_s  duración mínima pedida (s)
--   t_inf_antes, t_inf_despues marcas justo alrededor de inferir()
--   inferencia_pasadas         pasadas completas por todas las filas
--   n_predicciones             filas × pasadas completas
--   energia_inferencia_wh      energía en bruto de la ventana inferencia
--   energia_base_media_wh      energía en bruto de la ventana base_media
--   energia_por_prediccion_j   energía de la inferencia / n_predicciones
--                              (null si inferir no devolvió OK o sin el
--                              monitor físico de energía)
--   tiempo_por_prediccion_us   (t_inf_despues − t_inf_antes) / n_predicciones
--                              (null si inferir no devolvió OK)
--
-- serie_unida.fase: el check admite también base_media e inferencia.
--
-- paso y metrica_paso: columna tarea ('entrenamiento' | 'inferencia', not
-- null, default 'entrenamiento'). Los pasos de progreso.csv son de
-- entrenamiento y los grupos de progreso_inferencia.csv, de inferencia, y
-- los dos empiezan en step_index 0: la clave primaria pasa a
-- (corrida_id, tarea, step_index) y (corrida_id, tarea, step_index, nombre).
-- Al agregar la columna, las filas existentes toman 'entrenamiento' por el
-- default, ANTES de cambiar las claves. No hay llave foránea de
-- metrica_paso hacia paso. Las reglas RLS de las dos tablas no cambian:
-- usan corrida_en_subida(corrida_id), que no depende de la clave.
--
-- Permisos: insert por columnas de corrida (advertencia de la migración
-- 006): se agregan las once columnas nuevas, porque las inserta
-- subir_corrida. Si faltara una, la subida fallaría con "permission denied
-- for table corrida". Ninguna es editable después de subida.
--
-- subir_corrida inserta las columnas nuevas y la tarea de cada paso y
-- métrica (si el paquete no la trae, 'entrenamiento').
--
-- Sin esta migración, la app no sube una corrida con inferencia: la deja
-- rechazada con "Falta aplicar la migración 008 en la base de datos" y la
-- vuelve a pendiente sola cuando detecta la migración.
-- =====================================================================

begin;

alter table public.corrida
  add column inferencia_activada       boolean,
  add column inferencia_lote           int,
  add column inferencia_duracion_min_s int,
  add column t_inf_antes               timestamptz,
  add column t_inf_despues             timestamptz,
  add column inferencia_pasadas        bigint,
  add column n_predicciones            bigint,
  add column energia_inferencia_wh     double precision,
  add column energia_base_media_wh     double precision,
  add column energia_por_prediccion_j  double precision,
  add column tiempo_por_prediccion_us  double precision;

comment on column public.corrida.inferencia_activada is
  'La corrida tuvo la inferencia activada (D-014); null en las corridas subidas antes de la 008';
comment on column public.corrida.energia_por_prediccion_j is
  'Energía de la ventana inferencia / n_predicciones; null si inferir no devolvió OK o sin monitor de energía';
comment on column public.corrida.tiempo_por_prediccion_us is
  '(t_inf_despues - t_inf_antes) / n_predicciones en microsegundos; null si inferir no devolvió OK';

-- Fases de la serie unida (FasesCorrida.h)
alter table public.serie_unida
  drop constraint serie_unida_fase_check,
  add constraint serie_unida_fase_check
      check (fase in ('base_pre', 'entrenamiento', 'base_media', 'inferencia', 'base_post', 'transicion'));

-- Pasos y métricas por tarea. Las filas existentes quedan en 'entrenamiento'
-- (default) antes de cambiar las claves.
alter table public.paso
  add column tarea text not null default 'entrenamiento'
             check (tarea in ('entrenamiento', 'inferencia'));
alter table public.paso
  drop constraint paso_pkey,
  add primary key (corrida_id, tarea, step_index);
comment on column public.paso.tarea is
  'entrenamiento (progreso.csv) | inferencia (grupos de pasadas de progreso_inferencia.csv)';

alter table public.metrica_paso
  add column tarea text not null default 'entrenamiento'
             check (tarea in ('entrenamiento', 'inferencia'));
alter table public.metrica_paso
  drop constraint metrica_paso_pkey,
  add primary key (corrida_id, tarea, step_index, nombre);
comment on column public.metrica_paso.tarea is
  'entrenamiento | inferencia, como el paso al que pertenece la métrica';

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
  veredicto, auditoria, nota,
  inferencia_activada, inferencia_lote, inferencia_duracion_min_s, t_inf_antes, t_inf_despues,
  inferencia_pasadas, n_predicciones, energia_inferencia_wh, energia_base_media_wh,
  energia_por_prediccion_j, tiempo_por_prediccion_us
) on public.corrida to authenticated;

-- Subida con las columnas nuevas y la tarea de pasos y métricas
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
    energia_por_prediccion_j, tiempo_por_prediccion_us
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
    r.energia_por_prediccion_j, r.tiempo_por_prediccion_us
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

commit;

-- La API (PostgREST) relee el esquema para ver las columnas nuevas
notify pgrst, 'reload schema';
