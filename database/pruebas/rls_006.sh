#!/usr/bin/env bash
# Prueba funcional de la migracion 006 en un PostgreSQL local desechable (NO el proyecto real):
# el detalle de una corrida solo se inserta dentro de subir_corrida. Mismo servidor que
# validar_esquema.sh. Crea y rehace la base "rls".
set -u
export PGCLIENTENCODING=UTF8
S="$(cd "$(dirname "$0")" && pwd)"
TMP="${TMPDIR:-/tmp}"
REPO="$S/.."
PSQL="${PGBIN:-/c/Program Files/PostgreSQL/17/bin}/psql.exe"
PUERTO="${PGPUERTO:-54329}"
P() { "$PSQL" -p "$PUERTO" -U postgres -v ON_ERROR_STOP=1 -q -d rls "$@"; }
"$PSQL" -p "$PUERTO" -U postgres -q -d postgres -c "drop database if exists rls" -c "create database rls" 2>/dev/null
P -f "$S/supabase_minimo.sql"
for f in 001 002 003 004 005; do P -f "$REPO"/migraciones/${f}_*.sql >/dev/null; done
A=11111111-1111-1111-1111-111111111111
B=22222222-2222-2222-2222-222222222222
P -c "insert into auth.users (id, email, raw_user_meta_data) values ('$A','a@x','{\"nombre\":\"A\"}'), ('$B','b@x','{\"nombre\":\"B\"}')"

paquete() {  # $1 = t_creacion
cat <<JSON
{"experimento": {"nombre": "Árbol HEPMASS"},
 "corrida": {"equipo": "PC", "numero": 1, "t_creacion": "$1", "estado": "completada", "modelo_dll": "EnergIA_Arbol.dll",
             "energia_base_pre_wh": 0.01, "subida_en": "2030-01-01T00:00:00Z"},
 "hiperparametros": [{"nombre": "max_depth", "valor": "5"}],
 "pasos": [{"step_index": 0, "step_total": 1, "t_inicio": "2026-10-01T10:00:02Z", "t_fin": "2026-10-01T10:00:06Z"}],
 "metricas": [{"step_index": 0, "nombre": "profundidad", "valor": 5}],
 "serie": [{"t": "2026-10-01T10:00:01Z", "t_rel_s": 1, "en_entrenamiento": false, "fase": "base_pre"},
           {"t": "2026-10-01T10:00:03Z", "t_rel_s": 3, "en_entrenamiento": true, "fase": "entrenamiento", "paso_desde": 0, "paso_hasta": 0}]}
JSON
}
# Ejecuta SQL como el usuario $1 (rol authenticated, como PostgREST). Devuelve la salida o el error.
como() {
  local u=$1; shift
  # Por archivo: como argumento, Windows convierte las tildes a la pagina de codigos ANSI
  { echo "set role authenticated; select set_config('request.jwt.claim.sub', '$u', false);"; echo "$*;"; } > "$TMP/energia_consulta.sql"
  "$PSQL" -p "$PUERTO" -U postgres -q -d rls -tA -v ON_ERROR_STOP=1 -f "$TMP/energia_consulta.sql" 2>&1 | grep -v '^$' | grep -v "^$u$"
}
ok=0; mal=0
espera() {  # $1 descripcion, $2 patron esperado, $3 salida
  if echo "$3" | grep -q -- "$2"; then echo "[OK  ] $1"; ok=$((ok+1)); else echo "[FALLA] $1 -> $3"; mal=$((mal+1)); fi
}

echo "== Antes de la 006: una corrida subida"
VIEJA=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:00:00Z)'::jsonb)")
espera "subida antes de la 006 (id $VIEJA)" '^[0-9]' "$VIEJA"
r=$(como $A "insert into public.paso (corrida_id, step_index) values ($VIEJA, 50) returning 'insertado'")
espera "antes de la 006 el dueño SÍ podía agregar pasos a una corrida ya subida (el problema)" 'insertado' "$r"

echo "== Se aplica la 006"
P -f "$REPO/migraciones/006_cerrar_inserts_tras_subida.sql" >/dev/null && echo "aplicada"
r=$(como postgres "select 1" >/dev/null; "$PSQL" -p "$PUERTO" -U postgres -d rls -tA -c "select subida_en is not null from public.corrida where id = $VIEJA")
espera "la corrida ya subida conserva su subida_en" '^t$' "$r"

for t in paso metrica_paso serie_unida hiperparametro; do
  case $t in
    paso) sql="insert into public.paso (corrida_id, step_index) values ($VIEJA, 99)";;
    metrica_paso) sql="insert into public.metrica_paso (corrida_id, step_index, nombre, valor) values ($VIEJA, 0, 'x', 1)";;
    serie_unida) sql="insert into public.serie_unida (corrida_id, t, t_rel_s, en_entrenamiento) values ($VIEJA, '2026-10-01T11:00:00Z', 9, false)";;
    hiperparametro) sql="insert into public.hiperparametro (corrida_id, nombre, valor) values ($VIEJA, 'nuevo', '1')";;
  esac
  r=$(como $A "$sql")
  espera "corrida subida antes de la 006: insert directo en $t rechazado (42501, RLS)" 'row-level security' "$r"
done

echo "== Despues de la 006: subida nueva"
NUEVA=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:30:00Z)'::jsonb)")
espera "subir_corrida sigue funcionando (id $NUEVA)" '^[0-9]' "$NUEVA"
r=$("$PSQL" -p "$PUERTO" -U postgres -d rls -tA -c "select (select count(*) from public.paso where corrida_id=$NUEVA)||','||(select count(*) from public.metrica_paso where corrida_id=$NUEVA)||','||(select count(*) from public.serie_unida where corrida_id=$NUEVA)||','||(select count(*) from public.hiperparametro where corrida_id=$NUEVA)||','||(select string_agg(coalesce(fase,'-')||':'||coalesce(paso_desde::text,'-'), ' ' order by t) from public.serie_unida where corrida_id=$NUEVA)")
espera "la subida guarda pasos, métricas, serie (con fase y pasos) e hiperparámetros" '^1,1,2,1,base_pre:- entrenamiento:0$' "$r"
r=$("$PSQL" -p "$PUERTO" -U postgres -d rls -tA -c "select subida_en < '2030-01-01' and subida_en > now() - interval '1 minute' from public.corrida where id=$NUEVA")
espera "subida_en del paquete se ignora: toma el inicio de la transacción" '^t$' "$r"
r=$(como $A "insert into public.paso (corrida_id, step_index) values ($NUEVA, 7)")
espera "corrida recién subida: insert directo en paso en OTRA petición rechazado" 'row-level security' "$r"
r=$(como $A "insert into public.serie_unida (corrida_id, t, t_rel_s, en_entrenamiento) values ($NUEVA, '2026-10-01T12:00:00Z', 1, false)")
espera "corrida recién subida: insert directo en serie_unida rechazado" 'row-level security' "$r"

echo "== Nadie fija subida_en a mano"
EXP=$("$PSQL" -p "$PUERTO" -U postgres -d rls -tA -c "select experimento_id from public.corrida where id=$NUEVA")
r=$(como $A "insert into public.corrida (experimento_id, equipo, numero, t_creacion, estado, modelo_dll, subida_en) values ($EXP, 'PC', 9, '2026-10-02T00:00:00Z', 'completada', 'x.dll', transaction_timestamp()) returning id")
espera "insert directo en corrida con subida_en: permiso denegado" 'permission denied' "$r"
r=$(como $A "update public.corrida set subida_en = transaction_timestamp() where id = $VIEJA returning id")
espera "update de subida_en: permiso denegado" 'permission denied' "$r"
r=$(como $A "begin; update public.corrida set visibilidad = 'compartida' where id = $VIEJA; insert into public.paso (corrida_id, step_index) values ($VIEJA, 98); commit;")
espera "cambiar la visibilidad en la misma transacción no reabre los inserts" 'row-level security' "$r"
r=$(como $A "update public.corrida set visibilidad = 'privada' where id = $VIEJA returning visibilidad")
espera "la visibilidad se sigue pudiendo cambiar" 'privada' "$r"

echo "== Insert directo de una corrida (sin subir_corrida) y su detalle en otra petición"
DIR=$(como $A "insert into public.corrida (experimento_id, equipo, numero, t_creacion, estado, modelo_dll) values ($EXP, 'PC', 10, '2026-10-03T00:00:00Z', 'completada', 'x.dll') returning id")
espera "insert directo de una corrida (sin subida_en) se permite como antes (id $DIR)" '^[0-9]' "$DIR"
r=$(como $A "insert into public.paso (corrida_id, step_index) values ($DIR, 0)")
espera "pero su detalle en otra petición se rechaza" 'row-level security' "$r"

echo "== Otro usuario"
r=$(como $B "begin; select public.subir_corrida('$(paquete 2026-10-01T10:30:00Z)'::jsonb) > 0; insert into public.paso (corrida_id, step_index) values ($NUEVA, 5); commit;")
espera "otro usuario, aunque esté en su propia subida, no inserta en la corrida ajena" 'row-level security' "$r"
r=$(como $B "select public.corrida_en_subida($NUEVA)")
espera "corrida_en_subida es false para quien no es el dueño" '^f$' "$r"
r=$(como 00000000-0000-0000-0000-000000000000 "select 1" ; "$PSQL" -p "$PUERTO" -U postgres -d rls -tA -c "set role anon; select public.corrida_en_subida(1)" 2>&1)
espera "anon no puede ejecutar corrida_en_subida" 'permission denied' "$r"

echo "== Una subida repetida sigue respondiendo 23505"
r=$(como $A "select public.subir_corrida('$(paquete 2026-10-01T09:30:00Z)'::jsonb)")
espera "la misma corrida otra vez: 23505 (ya subida)" 'ya fue subida' "$r"

echo
echo "RLS 006: $ok OK, $mal fallo(s)"
