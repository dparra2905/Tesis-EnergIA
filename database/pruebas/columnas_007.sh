#!/usr/bin/env bash
# Prueba funcional de la migracion 007 (auditoria y nota, D-012) en
# un PostgreSQL local desechable (NO el proyecto real). Mismo servidor que validar_esquema.sh.
# Crea y rehace la base "c78". Sube una corrida antes de la migracion para comprobar que las
# corridas ya subidas quedan con las columnas nuevas en null. Los
# patrones esperan los mensajes de PostgreSQL en ingles (lc_messages = C), como rls_006.sh.
set -u
export PGCLIENTENCODING=UTF8
S="$(cd "$(dirname "$0")" && pwd)"
TMP="${TMPDIR:-/tmp}"
REPO="$S/.."
PSQL="${PGBIN:-/c/Program Files/PostgreSQL/17/bin}/psql.exe"
PUERTO="${PGPUERTO:-54329}"
P() { "$PSQL" -p "$PUERTO" -U postgres -v ON_ERROR_STOP=1 -q -d c78 "$@"; }
# Consulta como postgres, por archivo (como argumento, Windows convierte las tildes a ANSI)
Q() { echo "$1;" > "$TMP/energia_q78.sql"; "$PSQL" -p "$PUERTO" -U postgres -d c78 -tA -f "$TMP/energia_q78.sql"; }
"$PSQL" -p "$PUERTO" -U postgres -q -d postgres -c "drop database if exists c78" -c "create database c78" 2>/dev/null
P -f "$S/supabase_minimo.sql"
for f in 001 002 003 004 005 006; do P -f "$REPO"/migraciones/${f}_*.sql >/dev/null; done
A=11111111-1111-1111-1111-111111111111
B=22222222-2222-2222-2222-222222222222
P -c "insert into auth.users (id, email, raw_user_meta_data) values ('$A','a@x','{\"nombre\":\"A\"}'), ('$B','b@x','{\"nombre\":\"B\"}')"

PASOS='[{"step_index": 0, "step_total": 1, "t_inicio": "2026-10-01T10:00:02Z", "t_fin": "2026-10-01T10:00:06Z"}]'
METRICAS='[{"step_index": 0, "nombre": "profundidad", "valor": 5}]'
SERIE='[{"t": "2026-10-01T10:00:03Z", "t_rel_s": 3, "en_entrenamiento": true, "fase": "entrenamiento"}]'
# Paquete de subir_corrida: $1 = t_creacion, $2 = campos extra de corrida; PASOS, METRICAS y SERIE
paquete() {
cat <<JSON
{"experimento": {"nombre": "Árbol HEPMASS"},
 "corrida": {"equipo": "PC", "numero": 1, "t_creacion": "$1", "estado": "completada", "modelo_dll": "EnergIA_Arbol.dll",
             "visibilidad": "compartida"${2:-}},
 "hiperparametros": [{"nombre": "max_depth", "valor": "5"}],
 "pasos": $PASOS,
 "metricas": $METRICAS,
 "serie": $SERIE}
JSON
}
como() {
  local u=$1; shift
  { echo "set role authenticated; select set_config('request.jwt.claim.sub', '$u', false);"; echo "$*;"; } > "$TMP/energia_consulta78.sql"
  "$PSQL" -p "$PUERTO" -U postgres -q -d c78 -tA -v ON_ERROR_STOP=1 -f "$TMP/energia_consulta78.sql" 2>&1 | grep -v '^$' | grep -v "^$u$"
}
ok=0; mal=0
espera() {  # $1 descripcion, $2 patron esperado, $3 salida
  if echo "$3" | grep -q -- "$2"; then echo "[OK  ] $1"; ok=$((ok+1)); else echo "[FALLA] $1 -> $3"; mal=$((mal+1)); fi
}

echo "== Antes de la 007: una corrida subida"
V6=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:00:00Z)'::jsonb)")
espera "subida con el esquema 6 (id $V6)" '^[0-9]' "$V6"

echo "== Migracion 007: veredicto, auditoria y nota"
P -f "$REPO/migraciones/007_auditoria_y_nota.sql" >/dev/null && echo "aplicada"
espera "la corrida ya subida queda con veredicto, auditoria y nota en null" '^t$' \
  "$(Q "select veredicto is null and auditoria is null and nota is null from public.corrida where id = $V6")"
AUD='{"version_reglas": 1, "veredicto": "con_advertencias", "comprobaciones": [{"id": "actividad_ajena", "resultado": "advertencia"}]}'
N7=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:10:00Z ", \"veredicto\": \"con_advertencias\", \"auditoria\": $AUD, \"nota\": \"Ventilador al máximo\"")'::jsonb)")
espera "subir_corrida guarda las columnas nuevas (id $N7): no falta ninguna en el permiso de insert" '^[0-9]' "$N7"
espera "veredicto, auditoria y nota guardados" '^con_advertencias|1|Ventilador al máximo$' \
  "$(Q "select veredicto||'|'||(auditoria->>'version_reglas')||'|'||nota from public.corrida where id = $N7")"
r=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:20:00Z ", \"veredicto\": \"dudosa\"")'::jsonb)")
espera "un veredicto fuera de los tres valores se rechaza (check)" 'violates check constraint' "$r"
r=$(como $A "update public.corrida set nota = 'Nota corregida' where id = $N7 returning nota")
espera "el dueño edita la nota despues de subida" 'Nota corregida' "$r"
r=$(como $A "update public.corrida set veredicto = 'valida' where id = $N7 returning id")
espera "el veredicto no se edita (permiso denegado)" 'permission denied' "$r"
r=$(como $A "update public.corrida set auditoria = '{}'::jsonb where id = $N7 returning id")
espera "la auditoria no se edita (permiso denegado)" 'permission denied' "$r"
r=$(como $B "update public.corrida set nota = 'No es mia' where id = $N7 returning id")
espera "otro usuario no edita la nota de una corrida compartida (0 filas, RLS)" '^$' "$r"
espera "la nota sigue siendo la del dueño" '^Nota corregida$' "$(Q "select nota from public.corrida where id = $N7")"
r=$(como $A "update public.corrida set visibilidad = 'privada' where id = $N7 returning visibilidad")
espera "la visibilidad se sigue pudiendo cambiar" 'privada' "$r"

echo
echo "COLUMNAS 007: $ok OK, $mal fallo(s)"
