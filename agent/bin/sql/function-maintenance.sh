#!/bin/bash

. ./sql/environment.sh
. ./sql/utility.sh

PGCONFIG_MAINTENANCE=${CONFIG_DIR}/postgresql-maintenance.conf
RELOAD_DELAY=3

echo "/*---- リポジトリDB初期化 ----*/"
send_query -c "DROP SCHEMA statsrepo CASCADE" > /dev/null 2>&1

echo "/*---- 監視対象インスタンス初期化 ----*/"
setup_dbcluster ${PGDATA} ${PGUSER} ${PGPORT} ${PGCONFIG_MAINTENANCE} "" "" ""
sleep 3

echo "/*---- 自動メンテナンス機能 ----*/"
echo "/**--- スナップショット削除 ---**/"
do_snapshot "2 days ago"
do_snapshot "1 days ago"
do_snapshot "today"
send_query -c "SELECT snapid, comment FROM statsrepo.snapshot ORDER BY snapid"
send_query << EOF
UPDATE statsrepo.snapshot SET "time" = "time" - '2 day'::interval WHERE snapid = 1;
UPDATE statsrepo.snapshot SET "time" = "time" - '1 day'::interval WHERE snapid = 2;
EOF
maintenance_time=$(psql -Atc "SELECT (now() + '5sec')::time(0)")
update_pgconfig ${PGDATA} "<guc_prefix>.enable_maintenance" "snapshot"
update_pgconfig ${PGDATA} "<guc_prefix>.repository_keepday" "1"
update_pgconfig ${PGDATA} "<guc_prefix>.maintenance_time" "'${maintenance_time}'"
pg_ctl reload && sleep ${RELOAD_DELAY}
sleep 10
send_query -c "SELECT snapid, comment FROM statsrepo.snapshot ORDER BY snapid"

echo "/**--- ログファイル整理 ---**/"
maintenance_time=$(psql -Atc "SELECT (now() + '5sec')::time(0)")
update_pgconfig ${PGDATA} "<guc_prefix>.enable_maintenance" "log"
update_pgconfig ${PGDATA} "<guc_prefix>.log_maintenance_command" "'touch %l/ok'"
update_pgconfig ${PGDATA} "<guc_prefix>.maintenance_time" "'${maintenance_time}'"
pg_ctl reload && sleep ${RELOAD_DELAY}
sleep 10
[ -f ${PGDATA}/pg_log/ok ] &&
	echo "log maintenance command called"

pg_ctl stop -D ${PGDATA} > /dev/null
