-- =====================================================================
-- EnergIA — Migración 006 (D-011): los datos medidos solo se insertan
-- dentro de la subida.
--
-- Para el proyecto que ya tiene la versión 5 (001 a 005 aplicadas).
-- SQL Editor → pegar TODO el archivo → Run. Es una sola transacción.
--
-- Problema: la regla de insert de paso, metrica_paso, serie_unida e
-- hiperparametro solo exigía ser el dueño de la corrida. El dueño podía
-- agregar filas a una corrida ya subida llamando la API REST directamente,
-- y eso contradice "los datos medidos no se editan".
--
-- Mecanismo:
--   * corrida.subida_en cumple dos papeles: es la fecha visible de subida
--     y es la marca que cierra los inserts. Su default pasa a ser
--     transaction_timestamp() (el mismo valor que now(), escrito así para
--     que se lea qué se compara): el instante de inicio de la transacción,
--     igual en toda la transacción de subir_corrida y distinto en cualquier
--     otra petición.
--   * public.corrida_en_subida(id) (security definer, como
--     es_dueno_corrida, para no depender de la RLS de corrida) es true solo
--     si el usuario es el dueño Y subida_en = transaction_timestamp().
--   * La regla de insert de las 4 tablas usa esa función.
--   * Nadie puede fijar subida_en a mano: el permiso de insert en corrida
--     pasa a ser por columnas, con exactamente las columnas que inserta
--     subir_corrida (subida_en queda fuera), y subida_en no tiene permiso
--     de update (en corrida solo se actualiza visibilidad).
--
-- IMPORTANTE para migraciones futuras: toda columna nueva de corrida que
-- inserte subir_corrida (por ejemplo, las de inferencia más adelante) hay
-- que agregarla a este permiso de insert por columnas en su migración; si
-- falta, la subida falla con "permission denied for table corrida".
--
-- Corridas ya subidas: su subida_en es un instante pasado, nunca igual al
-- de una transacción nueva, así que quedan cerradas a inserts. No se
-- modifican (conservan su fecha de subida).
--
-- Comprobación manual en el proyecto real (después de aplicar): subir una
-- corrida desde la app y luego, con el token de esa sesión,
--   POST /rest/v1/paso  {"corrida_id": <id>, "step_index": 999}
-- debe responder 403 con el código 42501 (new row violates row-level
-- security policy). Ver D-011.
-- =====================================================================

begin;

-- 1. Marca de la subida
alter table public.corrida alter column subida_en set default transaction_timestamp();

comment on column public.corrida.subida_en is
  'Fecha de subida (visible en la app) y marca que cierra los inserts del detalle: '
  'solo dentro de la transacción que creó la corrida (migración 006).';

-- 2. Revisión en una función auxiliar (no depende de la RLS de corrida)
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

revoke execute on function public.corrida_en_subida(bigint) from public, anon;
grant  execute on function public.corrida_en_subida(bigint) to authenticated;

-- 3. Reglas de insert del detalle: dueño y dentro de la transacción de la subida
drop policy hiperparametro_crear on public.hiperparametro;
create policy hiperparametro_crear on public.hiperparametro
  for insert to authenticated with check (public.corrida_en_subida(corrida_id));

drop policy paso_crear on public.paso;
create policy paso_crear on public.paso
  for insert to authenticated with check (public.corrida_en_subida(corrida_id));

drop policy metrica_paso_crear on public.metrica_paso;
create policy metrica_paso_crear on public.metrica_paso
  for insert to authenticated with check (public.corrida_en_subida(corrida_id));

drop policy serie_unida_crear on public.serie_unida;
create policy serie_unida_crear on public.serie_unida
  for insert to authenticated with check (public.corrida_en_subida(corrida_id));

-- 4. subida_en no se puede fijar a mano: insert en corrida solo por columnas.
--    Exactamente las columnas que inserta subir_corrida (ver arriba: toda
--    columna nueva que inserte la función se agrega aquí en su migración).
revoke insert on public.corrida from authenticated;
grant insert (
  usuario_id, experimento_id, equipo, numero, t_creacion, visibilidad,
  estado, status_code, modelo_dll, contrato_version, dataset_id,
  pct_entrenamiento, semilla, t_start, t_antes, t_despues, t_stop,
  listo_en_t_start, accuracy_val,
  duracion_entrenamiento_s, energia_total_wh, energia_entrenamiento_wh,
  energia_base_pre_wh, energia_base_post_wh,
  potencia_media_w, potencia_max_w, cpu_media_pct, metadata, resultados, hardware_id
) on public.corrida to authenticated;
-- subida_en sin permiso de update (ya era así: en corrida solo se actualiza visibilidad)
revoke update on public.corrida from authenticated;
grant  update (visibilidad) on public.corrida to authenticated;

commit;

notify pgrst, 'reload schema';
