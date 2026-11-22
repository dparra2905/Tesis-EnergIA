#!/usr/bin/env bash
# Prueba funcional de la migracion 008 (inferencia, D-014) en un PostgreSQL local desechable
# (NO el proyecto real). Mismo servidor que validar_esquema.sh. Crea y rehace la base "c80".
# Sube una corrida antes de la migracion (con pasos y metricas) para comprobar que sus filas
# toman tarea = 'entrenamiento' antes del cambio de claves y que la corrida queda con las
# columnas nuevas en null. Los patrones esperan los mensajes de PostgreSQL en ingles
# (lc_messages = C), como rls_006.sh.
set -u
export PGCLIENTENCODING=UTF8
S="$(cd "$(dirname "$0")" && pwd)"
TMP="${TMPDIR:-/tmp}"
REPO="$S/.."
PSQL="${PGBIN:-/c/Program Files/PostgreSQL/17/bin}/psql.exe"
PUERTO="${PGPUERTO:-54329}"
P() { "$PSQL" -p "$PUERTO" -U postgres -v ON_ERROR_STOP=1 -q -d c80 "$@"; }
# Consulta como postgres, por archivo (como argumento, Windows convierte las tildes a ANSI)
Q() { echo "$1;" > "$TMP/energia_q80.sql"; "$PSQL" -p "$PUERTO" -U postgres -d c80 -tA -f "$TMP/energia_q80.sql"; }
"$PSQL" -p "$PUERTO" -U postgres -q -d postgres -c "drop database if exists c80" -c "create database c80" 2>/dev/null
P -f "$S/supabase_minimo.sql"
for f in 001 002 003 004 005 006 007; do P -f "$REPO"/migraciones/${f}_*.sql >/dev/null; done
A=11111111-1111-1111-1111-111111111111
B=22222222-2222-2222-2222-222222222222
P -c "insert into auth.users (id, email, raw_user_meta_data) values ('$A','a@x','{\"nombre\":\"A\"}'), ('$B','b@x','{\"nombre\":\"B\"}')"

# Paquete de subir_corrida: $1 = t_creacion, $2 = visibilidad, $3 = campos extra de corrida; PASOS, METRICAS, SERIE
PASOS='[{"step_index": 0, "step_total": 2, "t_inicio": "2026-10-01T10:00:02Z", "t_fin": "2026-10-01T10:00:04Z"},
        {"step_index": 1, "step_total": 2, "t_inicio": "2026-10-01T10:00:04Z", "t_fin": "2026-10-01T10:00:06Z"}]'
METRICAS='[{"step_index": 0, "nombre": "train_loss", "valor": 0.9}, {"step_index": 1, "nombre": "train_loss", "valor": 0.5}]'
SERIE='[{"t": "2026-10-01T10:00:03Z", "t_rel_s": 3, "en_entrenamiento": true, "fase": "entrenamiento", "paso_desde": 0, "paso_hasta": 0}]'
paquete() {
cat <<JSON
{"experimento": {"nombre": "MLP inferencia"},
 "corrida": {"equipo": "PC", "numero": 1, "t_creacion": "$1", "estado": "completada", "modelo_dll": "EnergIA_MLP.dll",
             "visibilidad": "$2"${3:-}},
 "hiperparametros": [{"nombre": "epocas", "valor": "2"}],
 "pasos": $PASOS,
 "metricas": $METRICAS,
 "serie": $SERIE}
JSON
}
como() {
  local u=$1; shift
  { echo "set role authenticated; select set_config('request.jwt.claim.sub', '$u', false);"; echo "$*;"; } > "$TMP/energia_consulta80.sql"
  "$PSQL" -p "$PUERTO" -U postgres -q -d c80 -tA -v ON_ERROR_STOP=1 -f "$TMP/energia_consulta80.sql" 2>&1 | grep -v '^$' | grep -v "^$u$"
}
ok=0; mal=0
espera() {  # $1 descripcion, $2 patron esperado, $3 salida
  if echo "$3" | grep -q -- "$2"; then echo "[OK  ] $1"; ok=$((ok+1)); else echo "[FALLA] $1 -> $3"; mal=$((mal+1)); fi
}

echo "== Antes de la 008: una corrida subida con dos pasos y sus metricas"
V7=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:00:00Z compartida)'::jsonb)")
espera "subida con el esquema 7 (id $V7)" '^[0-9]' "$V7"

echo "== Migracion 008: inferencia"
P -f "$REPO/migraciones/008_inferencia.sql" >/dev/null && echo "aplicada"
espera "los pasos y metricas ya subidos toman tarea = 'entrenamiento' (default, antes de cambiar las claves)" '^2|2|entrenamiento$' \
  "$(Q "select (select count(*) from public.paso where corrida_id = $V7 and tarea = 'entrenamiento')||'|'||(select count(*) from public.metrica_paso where corrida_id = $V7 and tarea = 'entrenamiento')||'|'||(select string_agg(distinct tarea, ',') from public.paso)")"
espera "la corrida ya subida queda con las columnas de la inferencia en null" '^t$' \
  "$(Q "select inferencia_activada is null and n_predicciones is null and energia_por_prediccion_j is null and t_inf_antes is null from public.corrida where id = $V7")"
espera "clave primaria de paso: (corrida_id, tarea, step_index)" 'PRIMARY KEY (corrida_id, tarea, step_index)' \
  "$(Q "select pg_get_constraintdef(oid) from pg_constraint where conname = 'paso_pkey'")"
espera "clave primaria de metrica_paso: (corrida_id, tarea, step_index, nombre)" 'PRIMARY KEY (corrida_id, tarea, step_index, nombre)' \
  "$(Q "select pg_get_constraintdef(oid) from pg_constraint where conname = 'metrica_paso_pkey'")"

# Corrida con inferencia: pasos de entrenamiento e inferencia con los mismos step_index
PASOS='[{"tarea": "entrenamiento", "step_index": 0, "step_total": 2, "t_inicio": "2026-10-01T10:00:02Z", "t_fin": "2026-10-01T10:00:04Z"},
        {"tarea": "entrenamiento", "step_index": 1, "step_total": 2, "t_inicio": "2026-10-01T10:00:04Z", "t_fin": "2026-10-01T10:00:06Z"},
        {"tarea": "inferencia", "step_index": 0, "step_total": 0, "t_inicio": "2026-10-01T10:00:07Z", "t_fin": "2026-10-01T10:00:08Z"},
        {"tarea": "inferencia", "step_index": 1, "step_total": 0, "t_inicio": "2026-10-01T10:00:08Z", "t_fin": "2026-10-01T10:00:09Z"}]'
METRICAS='[{"tarea": "entrenamiento", "step_index": 0, "nombre": "train_loss", "valor": 0.9},
           {"tarea": "inferencia", "step_index": 0, "nombre": "pasadas", "valor": 40},
           {"tarea": "inferencia", "step_index": 0, "nombre": "predicciones", "valor": 12000}]'
SERIE='[{"t": "2026-10-01T10:00:07Z", "t_rel_s": 7, "en_entrenamiento": false, "fase": "base_media"},
        {"t": "2026-10-01T10:00:08Z", "t_rel_s": 8, "en_entrenamiento": false, "fase": "inferencia", "paso_desde": 0, "paso_hasta": 1}]'
INF=', "inferencia_activada": true, "inferencia_lote": 65536, "inferencia_duracion_min_s": 60,
      "t_inf_antes": "2026-10-01T10:00:07Z", "t_inf_despues": "2026-10-01T10:01:07.400Z", "inferencia_pasadas": 80,
      "n_predicciones": 24000, "energia_inferencia_wh": 0.75, "energia_base_media_wh": 0.2,
      "energia_por_prediccion_j": 0.1125, "tiempo_por_prediccion_us": 2516.67'
N8=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:10:00Z compartida "$INF")'::jsonb)")
espera "subir_corrida guarda las once columnas de la inferencia (id $N8): ninguna falta en el permiso de insert" '^[0-9]' "$N8"
espera "columnas de la inferencia guardadas" '^true|65536|60|80|24000|0.1125|2516.67$' \
  "$(Q "select inferencia_activada||'|'||inferencia_lote||'|'||inferencia_duracion_min_s||'|'||inferencia_pasadas||'|'||n_predicciones||'|'||energia_por_prediccion_j||'|'||tiempo_por_prediccion_us from public.corrida where id = $N8")"
espera "pasos por tarea: 2 de entrenamiento y 2 de inferencia con los mismos step_index" '^entrenamiento:0,1 inferencia:0,1$' \
  "$(Q "select string_agg(tarea||':'||indices, ' ' order by tarea) from (select tarea, string_agg(step_index::text, ',' order by step_index) indices from public.paso where corrida_id = $N8 group by tarea) x")"
espera "metricas por tarea: la de inferencia no se mezcla con la de entrenamiento" '^entrenamiento:1 inferencia:2$' \
  "$(Q "select string_agg(tarea||':'||n, ' ' order by tarea) from (select tarea, count(*) n from public.metrica_paso where corrida_id = $N8 group by tarea) x")"
espera "serie_unida admite las fases base_media e inferencia" '^base_media,inferencia$' \
  "$(Q "select string_agg(fase, ',' order by t) from public.serie_unida where corrida_id = $N8")"

# Paquete de una app anterior a la 008 (sin tarea): todo queda como entrenamiento
PASOS='[{"step_index": 0, "step_total": 1, "t_inicio": "2026-10-01T10:00:02Z", "t_fin": "2026-10-01T10:00:06Z"}]'
METRICAS='[{"step_index": 0, "nombre": "profundidad", "valor": 5}]'
SERIE='[]'
SIN=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:20:00Z privada)'::jsonb)")
espera "paquete sin tarea (app anterior a la 008): pasos y metricas como entrenamiento (id $SIN)" '^entrenamiento|entrenamiento$' \
  "$(Q "select (select tarea from public.paso where corrida_id = $SIN)||'|'||(select tarea from public.metrica_paso where corrida_id = $SIN)")"

PASOS='[{"tarea": "inferencia", "step_index": 0}, {"tarea": "inferencia", "step_index": 0}]'
r=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:30:00Z privada)'::jsonb)")
espera "dos pasos con la misma tarea y step_index: violan la clave primaria" 'paso_pkey' "$r"
PASOS='[{"tarea": "prediccion", "step_index": 0}]'
r=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:40:00Z privada)'::jsonb)")
espera "una tarea fuera de entrenamiento/inferencia se rechaza (check)" 'paso_tarea_check' "$r"
PASOS='[]'
SERIE='[{"t": "2026-10-01T10:00:03Z", "t_rel_s": 3, "en_entrenamiento": false, "fase": "prediccion"}]'
r=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:50:00Z privada)'::jsonb)")
espera "una fase fuera de la lista se rechaza (check de serie_unida)" 'serie_unida_fase_check' "$r"

echo "== RLS de paso y metrica_paso con las claves nuevas (corrida_en_subida)"
r=$(como $A "insert into public.paso (corrida_id, tarea, step_index) values ($N8, 'inferencia', 99) returning 'insertado'")
espera "insert directo de un paso de inferencia en una corrida ya subida: rechazado (RLS)" 'row-level security' "$r"
r=$(como $A "insert into public.metrica_paso (corrida_id, tarea, step_index, nombre, valor) values ($N8, 'inferencia', 0, 'otra', 1) returning 'insertado'")
espera "insert directo de una metrica de inferencia: rechazado (RLS)" 'row-level security' "$r"
r=$(como $A "insert into public.paso (corrida_id, tarea, step_index) values ($V7, 'inferencia', 0) returning 'insertado'")
espera "insert directo en la corrida subida antes de la 008: rechazado (RLS)" 'row-level security' "$r"
espera "otro usuario ve los pasos de inferencia de una corrida compartida, filtrando por tarea" '^2$' \
  "$(como $B "select count(*) from public.paso where corrida_id = $N8 and tarea = 'inferencia'")"
espera "otro usuario no ve los pasos de una corrida privada" '^0$' \
  "$(como $B "select count(*) from public.paso where corrida_id = $SIN")"
r=$(como $A "update public.corrida set n_predicciones = 1 where id = $N8 returning id")
espera "las columnas de la inferencia no se editan (permiso denegado)" 'permission denied' "$r"

echo
echo "COLUMNAS 008: $ok OK, $mal fallo(s)"
