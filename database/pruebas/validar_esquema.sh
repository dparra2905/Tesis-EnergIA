#!/usr/bin/env bash
# Valida database/ en un PostgreSQL local desechable (NO el proyecto real de Supabase):
# las migraciones 001..N aplicadas en orden deben dar el mismo esquema y los mismos permisos
# que energia_supabase.sql. Requiere un servidor local en $PGPUERTO (54329) con usuario postgres,
# por ejemplo: initdb -D datos -U postgres -A trust -E UTF8 && pg_ctl -D datos -o "-p 54329" start
set -u
export PGCLIENTENCODING=UTF8
S="$(cd "$(dirname "$0")" && pwd)"
TMP="${TMPDIR:-/tmp}"
REPO="$S/.."
PSQL="${PGBIN:-/c/Program Files/PostgreSQL/17/bin}/psql.exe"
PUERTO="${PGPUERTO:-54329}"
P() { "$PSQL" -p "$PUERTO" -U postgres -v ON_ERROR_STOP=1 -q "$@"; }
for db in migr completo; do
  P -d postgres -c "drop database if exists $db" -c "create database $db" || exit 1
  P -d $db -f "$S/supabase_minimo.sql" || exit 1
done
for f in "$REPO"/migraciones/0*.sql; do
  echo "migr: $(basename "$f")"
  P -d migr -f "$f" || { echo "FALLA $f"; exit 1; }
done
echo "completo: energia_supabase.sql"
P -d completo -f "$REPO/energia_supabase.sql" || { echo "FALLA completo"; exit 1; }
volcar() {
  "$PSQL" -p "$PUERTO" -U postgres -d "$1" -tA -c "
  select 'col '||table_name||'.'||column_name||' '||data_type||' '||is_nullable||' '||coalesce(column_default,'') from information_schema.columns where table_schema='public'
  union all select 'pol '||tablename||'.'||policyname||' '||cmd||' '||coalesce(qual,'')||' | '||coalesce(with_check,'') from pg_policies where schemaname='public'
  union all select 'priv '||table_name||' '||grantee||' '||privilege_type from information_schema.table_privileges where table_schema='public' and grantee in ('anon','authenticated')
  union all select 'cpriv '||table_name||'.'||column_name||' '||grantee||' '||privilege_type from information_schema.column_privileges where table_schema='public' and grantee in ('anon','authenticated')
  union all select 'fn '||p.proname||' '||md5(replace(pg_get_functiondef(p.oid), chr(13), '')) from pg_proc p join pg_namespace n on n.oid=p.pronamespace where n.nspname='public'
  union all select 'con '||conrelid::regclass||' '||conname||' '||pg_get_constraintdef(oid) from pg_constraint where connamespace='public'::regnamespace
  order by 1"
}
volcar migr > "$TMP/energia_migr.txt"; volcar completo > "$TMP/energia_completo.txt"
if diff "$TMP/energia_migr.txt" "$TMP/energia_completo.txt" > "$TMP/energia_diff.txt"; then echo "MISMO ESQUEMA Y PERMISOS"; else echo "DIFERENCIAS:"; cat "$TMP/energia_diff.txt"; fi
