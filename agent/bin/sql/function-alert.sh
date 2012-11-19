#!/bin/bash

. ./sql/environment.sh
. ./sql/utility.sh

PGCONFIG_ALERT=${CONFIG_DIR}/postgresql-alert.conf
RELOAD_DELAY=3
ANALYZE_DELAY=1
WRITE_DELAY=1
echo "/*---- Initialize monitored instance ----*/"
setup_dbcluster ${PGDATA} ${PGUSER} ${PGPORT} ${PGCONFIG_ALERT} "" "" ""
sleep 3
if [ $(get_version) -ge 80400 ] ; then
	echo "shared_preload_libraries = 'pg_statsinfo, pg_stat_statements'" >> ${PGDATA}/postgresql-statsinfo.conf
	pg_ctl restart -w -D ${PGDATA} -o "-p ${PGPORT}" > /dev/null
	sleep 3
	if [ $(get_version) -ge 90100 ] ; then
		psql -c "CREATE EXTENSION pg_stat_statements"
	else
		psql -f $(pg_config --sharedir)/contrib/pg_stat_statements.sql
	fi
fi

echo "/*---- Alert Function ----*/"
echo "/**--- Alert the number of rollbacks per second ---**/"
do_snapshot
send_query -c "UPDATE statsrepo.alert SET rollback_tps = 0"
psql << EOF
BEGIN;
CREATE TABLE tbl01 (id bigint);
ROLLBACK;
ANALYZE;
EOF
sleep ${ANALYZE_DELAY}
do_snapshot
sleep ${WRITE_DELAY}
tail -n 1 ${PGDATA}/pg_log/postgresql.log |
sed "s/[0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\s[0-9]\{2\}:[0-9]\{2\}:[0-9]\{2\}/xxx/g" |
sed "s/[0-9]\+\.[0-9]\+/xxx/"
send_query -c "UPDATE statsrepo.alert SET rollback_tps = default"

echo "/**--- Alert the number of commits per second ---**/"
send_query -c "UPDATE statsrepo.alert SET commit_tps = 0"
psql << EOF
BEGIN;
CREATE TABLE tbl01 (id bigint);
COMMIT;
ANALYZE;
EOF
sleep ${ANALYZE_DELAY}
do_snapshot
sleep ${WRITE_DELAY}
tail -n 1 ${PGDATA}/pg_log/postgresql.log |
sed "s/[0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\s[0-9]\{2\}:[0-9]\{2\}:[0-9]\{2\}/xxx/g" |
sed "s/[0-9]\+\.[0-9]\+/xxx/"
send_query -c "UPDATE statsrepo.alert SET commit_tps = default"

if [ $(get_version) -ge 80400 ] ; then
	echo "/**--- Alert the response time average of query ---**/"
	send_query -c "UPDATE statsrepo.alert SET response_avg = 0"
	psql -c "SELECT pg_sleep(1)" > /dev/null
	do_snapshot
	sleep ${WRITE_DELAY}
	tail -n 1 ${PGDATA}/pg_log/postgresql.log |
	sed "s/[0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\s[0-9]\{2\}:[0-9]\{2\}:[0-9]\{2\}/xxx/g" |
	sed "s/[0-9]\+\.[0-9]\+/xxx/"
	send_query -c "UPDATE statsrepo.alert SET response_avg = default"

	echo "/**--- Alert the response time max of query ---**/"
	send_query -c "UPDATE statsrepo.alert SET response_worst = 0"
	psql -c "SELECT pg_sleep(1)" > /dev/null
	do_snapshot
	sleep ${WRITE_DELAY}
	tail -n 1 ${PGDATA}/pg_log/postgresql.log |
	sed "s/[0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\s[0-9]\{2\}:[0-9]\{2\}:[0-9]\{2\}/xxx/g" |
	sed "s/[0-9]\+\.[0-9]\+/xxx/"
	send_query -c "UPDATE statsrepo.alert SET response_worst = default"
fi

echo "/**--- Alert the dead tuple size and ratio ---**/"
send_query -c "UPDATE statsrepo.alert SET (garbage_size, garbage_percent, garbage_percent_table) = (0, 30, 60)"
psql << EOF
CREATE TABLE tbl02 (id bigint);
CREATE TABLE tbl03 (id bigint);
INSERT INTO tbl02 VALUES (generate_series(1,500000));
INSERT INTO tbl03 VALUES (generate_series(1,500000));
DELETE FROM tbl02 WHERE id <= 400000;
DELETE FROM tbl03 WHERE id <= 300000;
ANALYZE;
EOF
sleep ${ANALYZE_DELAY}
do_snapshot
sleep ${WRITE_DELAY}
tail -n 3 ${PGDATA}/pg_log/postgresql.log |
sed "s/[0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\s[0-9]\{2\}:[0-9]\{2\}:[0-9]\{2\}/xxx/g" |
sed "s/[0-9]\+\.[0-9]\+/xxx/"
send_query -c "UPDATE statsrepo.alert SET (garbage_size, garbage_percent, garbage_percent_table) = (default, default, default)"

pg_ctl stop -D ${PGDATA} > /dev/null
