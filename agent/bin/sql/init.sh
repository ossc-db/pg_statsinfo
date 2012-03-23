#!/bin/sh

. ./sql/environment.sh

# delete old database cluster
pg_ctl stop -m immediate > /dev/null 2>&1
rm -fr ${PGDATA}

# create new database cluster
initdb --no-locale -U ${PGUSER} > ${BASE_PATH}/results/initdb.log 2>&1
cat << EOF >> ${PGDATA}/postgresql.conf
shared_preload_libraries = 'pg_statsinfo'
autovacuum = off
max_prepared_transactions = 10
log_autovacuum_min_duration = 0

custom_variable_classes = 'pg_statsinfo'
pg_statsinfo.textlog_min_messages = info
pg_statsinfo.syslog_min_messages = info
pg_statsinfo.snapshot_interval = 1d
pg_statsinfo.long_lock_threashold = 0
EOF

# start PostgreSQL
pg_ctl start -w > /dev/null 2>&1

# wait until pg_statsinfo is available
sleep 5
