#!/usr/bin/env bash
# Prueba funcional de la migracion 009 (metricas del modelo, D-015) en un PostgreSQL local desechable
# (NO el proyecto real). Mismo servidor que validar_esquema.sh. Crea y rehace la base "c90".
# Sube una corrida antes de la migracion para comprobar que queda con las columnas nuevas en null, y
# que un paquete con las metricas subido SIN la 009 se acepta pero las pierde (jsonb_populate_record
# ignora las claves que la tabla no tiene): por eso la app no sube una corrida con metricas hasta
# detectar la 009. Los patrones esperan los mensajes de PostgreSQL en ingles (lc_messages = C).
set -u
export PGCLIENTENCODING=UTF8
S="$(cd "$(dirname "$0")" && pwd)"
TMP="${TMPDIR:-/tmp}"
REPO="$S/.."
PSQL="${PGBIN:-/c/Program Files/PostgreSQL/17/bin}/psql.exe"
PUERTO="${PGPUERTO:-54329}"
P() { "$PSQL" -p "$PUERTO" -U postgres -v ON_ERROR_STOP=1 -q -d c90 "$@"; }
# Consulta como postgres, por archivo (como argumento, Windows convierte las tildes a ANSI)
Q() { echo "$1;" > "$TMP/energia_q90.sql"; "$PSQL" -p "$PUERTO" -U postgres -d c90 -tA -f "$TMP/energia_q90.sql"; }
"$PSQL" -p "$PUERTO" -U postgres -q -d postgres -c "drop database if exists c90" -c "create database c90" 2>/dev/null
P -f "$S/supabase_minimo.sql"
for f in 001 002 003 004 005 006 007 008; do P -f "$REPO"/migraciones/${f}_*.sql >/dev/null; done
A=11111111-1111-1111-1111-111111111111
B=22222222-2222-2222-2222-222222222222
P -c "insert into auth.users (id, email, raw_user_meta_data) values ('$A','a@x','{\"nombre\":\"A\"}'), ('$B','b@x','{\"nombre\":\"B\"}')"

# Paquete de subir_corrida: $1 = t_creacion, $2 = visibilidad, $3 = campos extra de corrida
paquete() {
cat <<JSON
{"experimento": {"nombre": "Arbol metricas"},
 "corrida": {"equipo": "PC", "numero": 1, "t_creacion": "$1", "estado": "completada", "modelo_dll": "EnergIA_Arbol.dll",
             "visibilidad": "$2", "accuracy_val": 0.75${3:-}},
 "hiperparametros": [{"nombre": "max_depth", "valor": "5"}],
 "pasos": [{"tarea": "entrenamiento", "step_index": 0, "step_total": 1, "t_inicio": "2026-10-01T10:00:02Z", "t_fin": "2026-10-01T10:00:04Z"}],
 "metricas": [], "serie": []}
JSON
}
# Campos de las metricas del modelo, como los arma PaqueteCorrida (copia de metricas_modelo.json y resumen)
MET=', "metricas_modelo": {"version_formato": 1, "clases_originales": [0, 1],
        "validacion": {"status_dll": 0, "exactitud": 0.75, "matriz_confusion": [[2, 1], [0, 1]],
                       "macro": {"precision": 0.75, "recall": 0.8333, "f1": 0.7333}},
        "entrenamiento": {"status_dll": 0, "exactitud": 0.9, "macro": {"f1": 0.89}}},
      "f1_macro_val": 0.7333, "f1_ponderado_val": 0.76, "precision_macro_val": 0.75, "recall_macro_val": 0.8333,
      "accuracy_entrenamiento": 0.9, "f1_macro_entrenamiento": 0.89'
como() {
  local u=$1; shift
  { echo "set role authenticated; select set_config('request.jwt.claim.sub', '$u', false);"; echo "$*;"; } > "$TMP/energia_consulta90.sql"
  "$PSQL" -p "$PUERTO" -U postgres -q -d c90 -tA -v ON_ERROR_STOP=1 -f "$TMP/energia_consulta90.sql" 2>&1 | grep -v '^$' | grep -v "^$u$"
}
ok=0; mal=0
espera() {  # $1 descripcion, $2 patron esperado, $3 salida
  if echo "$3" | grep -q -- "$2"; then echo "[OK  ] $1"; ok=$((ok+1)); else echo "[FALLA] $1 -> $3"; mal=$((mal+1)); fi
}

echo "== Antes de la 009"
V8=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:00:00Z compartida)'::jsonb)")
espera "subida sin metricas con el esquema 8 (id $V8)" '^[0-9]' "$V8"
r=$(como $A "select metricas_modelo from public.corrida limit 1")
espera "sin la 009 la columna metricas_modelo no existe (la app lo detecta asi)" 'column "metricas_modelo" does not exist' "$r"
P8=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:05:00Z privada "$MET")'::jsonb)")
espera "un paquete con metricas se acepta sin la 009 (id $P8)... " '^[0-9]' "$P8"

echo "== Migracion 009: metricas del modelo"
P -f "$REPO/migraciones/009_metricas_modelo.sql" >/dev/null && echo "aplicada"
espera "... pero las metricas se perdieron: por eso la app no sube una corrida con metricas sin la 009" '^t$' \
  "$(Q "select metricas_modelo is null and f1_macro_val is null from public.corrida where id = $P8")"
espera "la corrida subida antes de la 009 queda con las siete columnas nuevas en null y su accuracy_val" '^true|0.75$' \
  "$(Q "select (metricas_modelo is null and f1_macro_val is null and f1_ponderado_val is null and precision_macro_val is null
              and recall_macro_val is null and accuracy_entrenamiento is null and f1_macro_entrenamiento is null)||'|'||accuracy_val
         from public.corrida where id = $V8")"

N9=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:10:00Z compartida "$MET")'::jsonb)")
espera "subir_corrida guarda las siete columnas de las metricas (id $N9): ninguna falta en el permiso de insert" '^[0-9]' "$N9"
espera "columnas de resumen guardadas" '^0.7333|0.76|0.75|0.8333|0.9|0.89$' \
  "$(Q "select f1_macro_val||'|'||f1_ponderado_val||'|'||precision_macro_val||'|'||recall_macro_val||'|'||accuracy_entrenamiento||'|'||f1_macro_entrenamiento from public.corrida where id = $N9")"
espera "metricas_modelo guarda la copia integra (matriz de confusion de validacion)" '^\[\[2, 1\], \[0, 1\]\]$' \
  "$(Q "select metricas_modelo->'validacion'->'matriz_confusion' from public.corrida where id = $N9")"

# Conjunto en que predecir fallo: su columna va en null y la copia jsonb lleva el detalle
FALLO=', "metricas_modelo": {"version_formato": 1, "validacion": {"status_dll": 0, "exactitud": 0.75},
        "entrenamiento": {"status_dll": 3, "motivo": "predecir devolvio: error interno de la DLL", "exactitud": null}},
      "f1_macro_val": 0.7, "accuracy_entrenamiento": null, "f1_macro_entrenamiento": null'
F9=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:20:00Z privada "$FALLO")'::jsonb)")
espera "predecir fallo en entrenamiento: columnas del conjunto en null, motivo en la copia (id $F9)" '^true|3$' \
  "$(Q "select (accuracy_entrenamiento is null and f1_macro_entrenamiento is null and f1_macro_val = 0.7)||'|'||(metricas_modelo->'entrenamiento'->>'status_dll') from public.corrida where id = $F9")"

echo "== Lectura y edicion"
espera "otro usuario lee las metricas de una corrida compartida" '^0.7333|0.75$' \
  "$(como $B "select f1_macro_val||'|'||(metricas_modelo->'validacion'->>'exactitud') from public.corrida where id = $N9")"
espera "otro usuario no ve una corrida privada" '^0$' "$(como $B "select count(*) from public.corrida where id = $F9")"
r=$(como $A "update public.corrida set f1_macro_val = 1 where id = $N9 returning id")
espera "las columnas de las metricas no se editan (permiso denegado)" 'permission denied' "$r"
r=$(como $A "update public.corrida set metricas_modelo = '{}' where id = $N9 returning id")
espera "la copia metricas_modelo no se edita (permiso denegado)" 'permission denied' "$r"
r=$(como $A "update public.corrida set nota = 'ok' where id = $N9 returning nota")
espera "la nota sigue editable por el dueno" '^ok$' "$r"

echo
echo "COLUMNAS 009: $ok OK, $mal fallo(s)"
