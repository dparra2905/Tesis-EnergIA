-- =====================================================================
-- EnergIA — Migración 005 (D-011): fase y pasos en serie_unida.
--
-- Para el proyecto que ya tiene la versión 4 (001 a 004 aplicadas).
-- SQL Editor → pegar TODO el archivo → Run. Es una sola transacción.
--
-- La migración 002 reemplazó fase, paso_desde y paso_hasta por un solo
-- en_entrenamiento, y con eso se perdió la correlación de la serie con
-- cada época o árbol. Se restauran (se conserva en_entrenamiento):
--
--   fase        base_pre | entrenamiento | base_post | transicion, o null.
--               Cada fila es el intervalo (t − 1 s, t]. Reglas en
--               src/sync/FasesCorrida.h: transicion si el intervalo cruza
--               t_antes o t_despues; la fase que lo contiene completo;
--               null fuera de [t_start, t_stop], entre t_despues y t_stop
--               si la fase posterior no empezó, y en todas las filas de
--               una corrida sin t_stop (detención forzada).
--   paso_desde  rango de step_index de progreso.csv cuyos intervalos
--   paso_hasta  [timestamp_inicio, timestamp_fin] tocan el de la fila;
--               null si ninguno.
--
-- Las corridas ya subidas quedan con las tres columnas en null: no se
-- recalculan (los datos medidos no se editan). Solo las corridas que se
-- suban después traen fase y pasos.
--
-- subir_corrida no cambia: inserta la serie con el tipo de fila de la
-- tabla y toma las columnas nuevas del paquete por nombre.
-- =====================================================================

begin;

alter table public.serie_unida
  add column fase       text
             check (fase in ('base_pre', 'entrenamiento', 'base_post', 'transicion')),
  add column paso_desde int,
  add column paso_hasta int;

comment on column public.serie_unida.fase is
  'base_pre | entrenamiento | base_post | transicion, o null (intervalo (t-1 s, t], reglas en FasesCorrida.h)';
comment on column public.serie_unida.paso_desde is
  'Menor step_index de progreso.csv que toca el intervalo de la fila (null si ninguno)';
comment on column public.serie_unida.paso_hasta is
  'Mayor step_index de progreso.csv que toca el intervalo de la fila (null si ninguno)';

commit;

-- La API (PostgREST) relee el esquema para ver las columnas nuevas
notify pgrst, 'reload schema';
