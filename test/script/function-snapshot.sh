#!/bin/bash

. ./script/common.sh

PGCONFIG=${CONFIG_DIR}/postgresql-snapshot.conf
ARCHIVE_DIR=${PGDATA}/archivelog
XLOG_DIR=${PGDATA}/xlogdir

function get_snapshot()
{
	do_snapshot ${PGUSER} ${PGPORT} ${REPOSITORY_USER} ${REPOSITORY_PORT}
}

trap stop_all_database EXIT

echo "/*---- Initialize repository DB ----*/"
setup_repository ${REPOSITORY_DATA} ${REPOSITORY_USER} ${REPOSITORY_PORT} ${REPOSITORY_CONFIG}

echo "/*---- Initialize monitored instance ----*/"
setup_dbcluster ${PGDATA} ${PGUSER} ${PGPORT} ${PGCONFIG} ${ARCHIVE_DIR} ${XLOG_DIR} ""
sleep 3
mkdir -p ${PGDATA}/tblspc01
createuser -ldrs user01
createdb db01
psql -d postgres -At << EOF
SET client_min_messages TO error;
CREATE TABLESPACE tblspc01 LOCATION '${PGDATA}/tblspc01';
EOF
psql -U user01 -d db01 -At << EOF
SET client_min_messages TO warning;
CREATE SCHEMA schema01;
CREATE TABLE schema01.tbl01 (id serial PRIMARY KEY, name text, age integer) TABLESPACE tblspc01;
CREATE TABLE schema01.tbl02 (address text) INHERITS (schema01.tbl01);
CREATE FUNCTION schema01.func01(text, integer) RETURNS void AS 'INSERT INTO schema01.tbl01 (name, age) VALUES (\$1, \$2)' LANGUAGE sql;
INSERT INTO schema01.tbl01 (name, age) VALUES ('xxx', 25);
INSERT INTO schema01.tbl02 (name, age, address) VALUES ('xxx', 25, 'xxx');
SELECT schema01.func01('xxx', 30);
GRANT ALL ON SCHEMA schema01 TO user01;  
EOF
vacuumdb -a -z
sleep 3

echo "/*---- Statistics collection function ----*/"
get_snapshot

echo "/**--- Statistics of database ---**/"
send_query << EOF
SELECT
	snapid,
	name AS database,
	CASE WHEN dbid IS NOT NULL THEN 'xxx' END AS dbid,
	CASE WHEN size IS NOT NULL THEN 'xxx' END AS size,
	CASE WHEN age IS NOT NULL THEN 'xxx' END AS age,
	CASE WHEN xact_commit IS NOT NULL THEN 'xxx' END AS xact_commit,
	CASE WHEN xact_rollback IS NOT NULL THEN 'xxx' END AS xact_rollback,
	CASE WHEN blks_read IS NOT NULL THEN 'xxx' END AS blks_read,
	CASE WHEN blks_hit IS NOT NULL THEN 'xxx' END AS blks_hit,
	CASE WHEN tup_returned IS NOT NULL THEN 'xxx' END AS tup_returned,
	CASE WHEN tup_fetched IS NOT NULL THEN 'xxx' END AS tup_fetched,
	CASE WHEN tup_inserted IS NOT NULL THEN 'xxx' END AS tup_inserted,
	CASE WHEN tup_updated IS NOT NULL THEN 'xxx' END AS tup_updated,
	CASE WHEN tup_deleted IS NOT NULL THEN 'xxx' END AS tup_deleted,
	CASE WHEN confl_tablespace IS NOT NULL THEN 'xxx' END AS confl_tablespace,
	CASE WHEN confl_lock IS NOT NULL THEN 'xxx' END AS confl_lock,
	CASE WHEN confl_snapshot IS NOT NULL THEN 'xxx' END AS confl_snapshot,
	CASE WHEN confl_bufferpin IS NOT NULL THEN 'xxx' END AS confl_bufferpin,
	CASE WHEN confl_deadlock IS NOT NULL THEN 'xxx' END AS confl_deadlock,
	CASE WHEN temp_files IS NOT NULL THEN 'xxx' END AS temp_files,
	CASE WHEN temp_bytes IS NOT NULL THEN 'xxx' END AS temp_bytes,
	CASE WHEN deadlocks IS NOT NULL THEN 'xxx' END AS deadlocks,
	CASE WHEN blk_read_time IS NOT NULL THEN 'xxx' END AS blk_read_time,
	CASE WHEN blk_write_time IS NOT NULL THEN 'xxx' END AS blk_write_time
FROM
	statsrepo.database
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
ORDER BY
	database;
EOF

echo "/**--- Statistics of schema ---**/"
send_query << EOF
SELECT
	s.snapid,
	d.name AS database,
	s.name AS schema,
	CASE WHEN nsp IS NOT NULL THEN 'xxx' END AS nsp
FROM
	statsrepo.schema s,
	statsrepo.database d
WHERE
	s.snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
	AND s.snapid = d.snapid
	AND s.dbid = d.dbid
ORDER BY
	database, schema;
EOF

echo "/**--- Statistics of table ---**/"
if [ $(server_version) -ge 90400 ] ; then
	SELECT_N_MOD_SINCE_ANALYZE="CASE WHEN t.n_mod_since_analyze IS NOT NULL THEN 'xxx' END"
else
	SELECT_N_MOD_SINCE_ANALYZE="CASE WHEN t.n_mod_since_analyze IS NULL THEN '(N/A)' END"
fi
send_query << EOF
SELECT
	t.snapid,
	d.name AS database,
	s.name AS schema,
	t.name AS table,
	ts.name AS tablespace,
	CASE WHEN t.tbl IS NOT NULL THEN 'xxx' END AS tbl,
	CASE WHEN t.date IS NOT NULL THEN 'xxx' END AS date,
	CASE WHEN t.toastrelid IS NOT NULL THEN 'xxx' END AS toastrelid,
	CASE WHEN t.toastidxid IS NOT NULL THEN 'xxx' END AS toastidxid,
	CASE WHEN t.relkind IS NOT NULL THEN 'xxx' END AS relkind,
	CASE WHEN t.relpages IS NOT NULL THEN 'xxx' END AS relpages,
	CASE WHEN t.reltuples IS NOT NULL THEN 'xxx' END AS reltuples,
	CASE WHEN t.reloptions IS NOT NULL THEN 'xxx' END AS reloptions,
	CASE WHEN t.size IS NOT NULL THEN 'xxx' END AS size,
	CASE WHEN t.seq_scan IS NOT NULL THEN 'xxx' END AS seq_scan,
	CASE WHEN t.seq_tup_read IS NOT NULL THEN 'xxx' END AS seq_tup_read,
	CASE WHEN t.idx_scan IS NOT NULL THEN 'xxx' END AS idx_scan,
	CASE WHEN t.idx_tup_fetch IS NOT NULL THEN 'xxx' END AS idx_tup_fetch,
	CASE WHEN t.n_tup_ins IS NOT NULL THEN 'xxx' END AS n_tup_ins,
	CASE WHEN t.n_tup_upd IS NOT NULL THEN 'xxx' END AS n_tup_upd,
	CASE WHEN t.n_tup_del IS NOT NULL THEN 'xxx' END AS n_tup_del,
	CASE WHEN t.n_tup_hot_upd IS NOT NULL THEN 'xxx' END AS n_tup_hot_upd,
	CASE WHEN t.n_live_tup IS NOT NULL THEN 'xxx' END AS n_live_tup,
	CASE WHEN t.n_dead_tup IS NOT NULL THEN 'xxx' END AS n_dead_tup,
	${SELECT_N_MOD_SINCE_ANALYZE} AS n_mod_since_analyze,
	CASE WHEN t.heap_blks_read IS NOT NULL THEN 'xxx' END AS heap_blks_read,
	CASE WHEN t.heap_blks_hit IS NOT NULL THEN 'xxx' END AS heap_blks_hit,
	CASE WHEN t.idx_blks_read IS NOT NULL THEN 'xxx' END AS idx_blks_read,
	CASE WHEN t.idx_blks_hit IS NOT NULL THEN 'xxx' END AS idx_blks_hit,
	CASE WHEN t.toast_blks_read IS NOT NULL THEN 'xxx' END AS toast_blks_read,
	CASE WHEN t.toast_blks_hit IS NOT NULL THEN 'xxx' END AS toast_blks_hit,
	CASE WHEN t.tidx_blks_read IS NOT NULL THEN 'xxx' END AS tidx_blks_read,
	CASE WHEN t.tidx_blks_hit IS NOT NULL THEN 'xxx' END AS tidx_blks_hit,
	CASE WHEN t.last_vacuum IS NOT NULL THEN 'xxx' END AS last_vacuum,
	CASE WHEN t.last_autovacuum IS NOT NULL THEN 'xxx' END AS last_autovacuum,
	CASE WHEN t.last_analyze IS NOT NULL THEN 'xxx' END AS last_analyze,
	CASE WHEN t.last_autoanalyze IS NOT NULL THEN 'xxx' END AS last_autoanalyze
FROM
	statsrepo.table t,
	statsrepo.database d,
	statsrepo.schema s,
	statsrepo.tablespace ts
WHERE
	t.snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
	AND t.snapid = d.snapid
	AND t.snapid = s.snapid
	AND t.snapid = ts.snapid
	AND t.dbid = d.dbid
	AND t.dbid = s.dbid
	AND t.nsp = s.nsp
	AND t.tbs = CASE WHEN ts.name = 'pg_default' THEN 0 ELSE ts.tbs END
ORDER BY
	database, schema, "table";
EOF

echo "/**--- Statistics of column ---**/"
send_query << EOF
SELECT
	c.snapid,
	d.name AS database,
	t.name AS table,
	c.name AS column,
	c.attnum,
	c.type,
	c.stattarget,
	c.storage,
	c.isnotnull,
	c.isdropped,
	CASE WHEN c.date IS NOT NULL THEN 'xxx' END AS date,
	CASE WHEN c.avg_width IS NOT NULL THEN 'xxx' END AS avg_width,
	CASE WHEN c.n_distinct IS NOT NULL THEN 'xxx' END AS n_distinct,
	CASE WHEN c.correlation IS NOT NULL THEN 'xxx' END AS correlation
FROM
	statsrepo.column c,
	statsrepo.database d,
	statsrepo.table t
WHERE
	c.snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
	AND c.snapid = d.snapid
	AND c.snapid = t.snapid
	AND c.dbid = d.dbid
	AND c.dbid = t.dbid
	AND c.tbl = t.tbl
ORDER BY
	database, "table", c.attnum;
EOF

echo "/**--- Statistics of index ---**/"
send_query << EOF
SELECT
	i.snapid,
	d.name AS database,
	t.name AS table,
	i.name AS index,
	ts.name AS tablespace,
	i.reloptions,
	i.isunique,
	i.isprimary,
	i.isclustered,
	i.isvalid,
	i.indkey,
	i.indexdef,
	CASE WHEN i.date IS NOT NULL THEN 'xxx' END AS date,
	CASE WHEN i.relam IS NOT NULL THEN 'xxx' END AS relam,
	CASE WHEN i.relpages IS NOT NULL THEN 'xxx' END AS relpages,
	CASE WHEN i.reltuples IS NOT NULL THEN 'xxx' END AS reltuples,
	CASE WHEN i.size IS NOT NULL THEN 'xxx' END AS size,
	CASE WHEN i.idx_scan IS NOT NULL THEN 'xxx' END AS idx_scan,
	CASE WHEN i.idx_tup_read IS NOT NULL THEN 'xxx' END AS idx_tup_read,
	CASE WHEN i.idx_tup_fetch IS NOT NULL THEN 'xxx' END AS idx_tup_fetch,
	CASE WHEN i.idx_blks_read IS NOT NULL THEN 'xxx' END AS idx_blks_read,
	CASE WHEN i.idx_blks_hit IS NOT NULL THEN 'xxx' END AS idx_blks_hit
FROM
	statsrepo.index i,
	statsrepo.database d,
	statsrepo.table t,
	statsrepo.tablespace ts
WHERE
	i.snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
	AND i.snapid = d.snapid
	AND i.snapid = t.snapid
	AND i.snapid = ts.snapid
	AND i.dbid = d.dbid
	AND i.dbid = t.dbid
	AND i.tbl = t.tbl
	AND i.tbs = CASE WHEN ts.name = 'pg_default' THEN 0 ELSE ts.tbs END
ORDER BY
	database, "table", index;
EOF

echo "/**--- Statistics of inherits ---**/"
send_query << EOF
SELECT
	i.snapid,
	d.name AS database,
	(SELECT name FROM statsrepo.table WHERE snapid = i.snapid AND tbl = i.inhrelid) AS table,
	(SELECT name FROM statsrepo.table WHERE snapid = i.snapid AND tbl = i.inhparent) AS parent,
	i.inhseqno
FROM
	statsrepo.inherits i,
	statsrepo.database d
WHERE
	i.snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
	AND i.snapid = d.snapid
	AND i.dbid = d.dbid
ORDER BY
	database, "table";
EOF

echo "/**--- Statistics of SQL function ---**/"
send_query << EOF
SELECT
	f.snapid,
	d.name AS database,
	s.name AS schema,
	f.funcname,
	CASE WHEN f.funcid IS NOT NULL THEN 'xxx' END AS funcid,
	CASE WHEN f.argtypes IS NOT NULL THEN 'xxx' END AS argtypes,
	CASE WHEN f.calls IS NOT NULL THEN 'xxx' END AS calls,
	CASE WHEN f.total_time IS NOT NULL THEN 'xxx' END AS total_time,
	CASE WHEN f.self_time IS NOT NULL THEN 'xxx' END AS self_time
FROM
	statsrepo.function f,
	statsrepo.database d,
	statsrepo.schema s
WHERE
	f.snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
	AND f.snapid = d.snapid
	AND f.snapid = s.snapid
	AND f.dbid = d.dbid
	AND f.dbid = s.dbid
	AND f.nsp = s.nsp
	AND s.name NOT IN ('statsinfo')
ORDER BY
	database, schema, funcname;
EOF

echo "/**--- OS resource usage (CPU) ---**/"
send_query << EOF
SELECT
	snapid,
	CASE WHEN cpu_id IS NOT NULL THEN 'xxx' END AS cpu_id,
	CASE WHEN cpu_user IS NOT NULL THEN 'xxx' END AS cpu_user,
	CASE WHEN cpu_system IS NOT NULL THEN 'xxx' END AS cpu_system,
	CASE WHEN cpu_idle IS NOT NULL THEN 'xxx' END AS cpu_idle,
	CASE WHEN cpu_iowait IS NOT NULL THEN 'xxx' END AS cpu_iowait,
	CASE WHEN overflow_user IS NOT NULL THEN 'xxx' END AS overflow_user,
	CASE WHEN overflow_system IS NOT NULL THEN 'xxx' END AS overflow_system,
	CASE WHEN overflow_idle IS NOT NULL THEN 'xxx' END AS overflow_idle,
	CASE WHEN overflow_iowait IS NOT NULL THEN 'xxx' END AS overflow_iowait
FROM
	statsrepo.cpu
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF

echo "/**--- OS resource usage (loadavg) ---**/"
send_query << EOF
SELECT
	snapid,
	CASE WHEN loadavg1 IS NOT NULL THEN 'xxx' END AS loadavg1,
	CASE WHEN loadavg5 IS NOT NULL THEN 'xxx' END AS loadavg5,
	CASE WHEN loadavg15 IS NOT NULL THEN 'xxx' END AS loadavg15
FROM
	statsrepo.loadavg
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF

echo "/**--- OS resource usage (memory) ---**/"
send_query << EOF
SELECT
	snapid,
	CASE WHEN memfree IS NOT NULL THEN 'xxx' END AS memfree,
	CASE WHEN buffers IS NOT NULL THEN 'xxx' END AS buffers,
	CASE WHEN cached IS NOT NULL THEN 'xxx' END AS cached,
	CASE WHEN swap IS NOT NULL THEN 'xxx' END AS swap,
	CASE WHEN dirty IS NOT NULL THEN 'xxx' END AS dirty
FROM
	statsrepo.memory
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF

echo "/**--- OS resource usage (disk I/O) ---**/"
send_query << EOF
SELECT
	snapid,
	CASE WHEN device_major IS NOT NULL THEN 'xxx' END AS device_major,
	CASE WHEN device_minor IS NOT NULL THEN 'xxx' END AS device_minor,
	CASE WHEN device_name IS NOT NULL THEN 'xxx' ELSE 'FAILED' END AS device_name,
	CASE WHEN device_readsector IS NOT NULL THEN 'xxx' END AS device_readsector,
	CASE WHEN device_readtime IS NOT NULL THEN 'xxx' END AS device_readtime,
	CASE WHEN device_writesector IS NOT NULL THEN 'xxx' END AS device_writesector,
	CASE WHEN device_writetime IS NOT NULL THEN 'xxx' END AS device_writetime,
	CASE WHEN device_ioqueue IS NOT NULL THEN 'xxx' END AS device_ioqueue,
	CASE WHEN device_iototaltime IS NOT NULL THEN 'xxx' END AS device_iototaltime,
	CASE WHEN device_rsps_max IS NOT NULL THEN 'xxx' END AS device_rsps_max,
	CASE WHEN device_wsps_max IS NOT NULL THEN 'xxx' END AS device_wsps_max,
	CASE WHEN overflow_drs IS NOT NULL THEN 'xxx' END AS overflow_drs,
	CASE WHEN overflow_drt IS NOT NULL THEN 'xxx' END AS overflow_drt,
	CASE WHEN overflow_dws IS NOT NULL THEN 'xxx' END AS overflow_dws,
	CASE WHEN overflow_dwt IS NOT NULL THEN 'xxx' END AS overflow_dwt,
	CASE WHEN overflow_dit IS NOT NULL THEN 'xxx' END AS overflow_dit,
	device_tblspaces
FROM
	statsrepo.device
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF

echo "/**--- Statistics of tablespace ---**/"
send_query << EOF
SELECT
	snapid,
	name AS tablespace,
	CASE WHEN tbs IS NOT NULL THEN 'xxx' END AS tbs,
	regexp_replace(location, '${PGDATA}', '\$PGDATA') AS location,
	CASE WHEN device IS NOT NULL THEN 'xxx' END AS device,
	CASE WHEN avail IS NOT NULL THEN 'xxx' END AS avail,
	CASE WHEN total IS NOT NULL THEN 'xxx' END AS total,
	spcoptions
FROM
	statsrepo.tablespace
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
ORDER BY
	tablespace;
EOF

echo "/**--- Role information ---**/"
send_query << EOF
SELECT
	snapid,
	name AS role,
	CASE WHEN userid IS NOT NULL THEN 'xxx' END AS userid
FROM
	statsrepo.role
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
ORDER BY
	role;
EOF

echo "/**--- GUC setting ---**/"
send_query << EOF
SELECT
	snapid,
	name,
	setting,
	unit,
	source
FROM
	statsrepo.setting
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
	AND name IN ('logging_collector', 'shared_buffers', 'port')
ORDER BY
	name;
EOF

echo "/**--- Instance activity ---**/"
echo "/***-- Verify that can determine the state type of bakend --***/"
psql -Atc "CREATE TABLE xxx (col int)"
psql -Atc "\! sleep 10" &
psql -Atc "SELECT pg_sleep(10)" > /dev/null &
psql -At << EOF &
BEGIN;
LOCK TABLE xxx;
\! sleep 10
END;
EOF
sleep 1
psql -Atc "DROP TABLE xxx" &
wait
get_snapshot
send_query << EOF
SELECT
	snapid,
	CASE WHEN idle > 0 THEN 'OK' ELSE 'FAILED' END AS idle,
	CASE WHEN idle_in_xact > 0 THEN 'OK' ELSE 'FAILED' END AS idle_in_xact,
	CASE WHEN waiting > 0 THEN 'OK' ELSE 'FAILED' END AS waiting,
	CASE WHEN running > 0 THEN 'OK' ELSE 'FAILED' END AS running,
	CASE WHEN max_backends = 5 THEN 'OK' ELSE 'FAILED' END AS max_backends
FROM
	statsrepo.activity
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF

echo "/***-- There is no transaction of more than 1 second --***/"
sleep 10
get_snapshot
send_query -c "SELECT * FROM statsrepo.xact WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"

echo "/***-- There is a transaction of more than 1 second --***/"
pid=$(psql -Atc "SELECT pg_backend_pid() FROM pg_sleep(10)")
get_snapshot
send_query << EOF
SELECT
	snapid,
	CASE WHEN client IS NULL THEN 'OK' ELSE 'FAILED' END AS client,
	CASE WHEN pid = ${pid} THEN 'OK' ELSE 'FAILED' END AS pid,
	CASE WHEN start IS NOT NULL THEN 'OK' ELSE 'FAILED' END AS start,
	CASE WHEN duration > 0 THEN 'OK' ELSE 'FAILED' END AS duration,
	query
FROM
	statsrepo.xact
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF

echo "/**--- Lock conflicts ---**/"
echo "/***-- There is no lock conflicts --***/"
get_snapshot
send_query -c "SELECT * FROM statsrepo.lock WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"

echo "/***-- There are lock conflicts --***/"
psql db01 -Atq << EOF &
BEGIN;
LOCK TABLE schema01.tbl01 IN ACCESS EXCLUSIVE MODE;
\! sleep 3
END;
EOF
sleep 1
psql db01 -Atq << EOF &
BEGIN;
LOCK TABLE schema01.tbl01 IN ACCESS SHARE MODE;
END;
EOF
psql db01 -Atq << EOF &
BEGIN;
LOCK TABLE schema01.tbl01 IN SHARE UPDATE EXCLUSIVE MODE;
END;
EOF
sleep 1
get_snapshot
wait
send_query << EOF
SELECT
	snapid,
	CASE WHEN dbid IS NOT NULL THEN 'xxx' END AS dbid,
	nspname,
	CASE WHEN relname IS NOT NULL THEN 'xxx' END AS relname,
	CASE WHEN blocker_appname IS NOT NULL THEN 'xxx' END AS blocker_appname,
	CASE WHEN blocker_addr IS NOT NULL THEN 'xxx' END AS blocker_addr,
	CASE WHEN blocker_hostname IS NOT NULL THEN 'xxx' END AS blocker_hostname,
	CASE WHEN blocker_port IS NOT NULL THEN 'xxx' END AS blocker_port,
	CASE WHEN blockee_pid IS NOT NULL THEN 'xxx' END AS blockee_pid,
	CASE WHEN blocker_pid IS NOT NULL THEN 'xxx' END AS blocker_pid,
	CASE WHEN blocker_gid IS NOT NULL THEN 'xxx' END AS blocker_gid,
	wait_event_type,
	wait_event,
	CASE WHEN duration IS NOT NULL THEN 'xxx' END AS duration,
	blockee_query,
	blocker_query
FROM
	statsrepo.lock
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
ORDER BY
	dbid, nspname, relname, blockee_query;
EOF

echo "/**--- Statistics of WAL ---**/"
echo "/***-- Monitored instance is a stand-alone configuration --***/"
send_query << EOF
SELECT
	snapid,
	CASE WHEN location IS NOT NULL THEN 'xxx' END AS location,
	CASE WHEN xlogfile IS NOT NULL THEN 'xxx' END AS xlogfile
FROM
	statsrepo.xlog
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF

echo "/**--- Statistics of archive ---**/"
echo "/***-- Monitored instance is a stand-alone configuration --***/"
if [ $(server_version) -ge 90400 ] ; then
	psql << EOF > /dev/null
SELECT pg_stat_reset_shared('archiver');
SELECT ${FUNCTION_PG_SWITCH_WAL};
SELECT pg_sleep(1);
EOF
	get_snapshot
	send_query << EOF
SELECT
	snapid,
	archived_count,
	CASE WHEN last_archived_wal IS NOT NULL THEN 'xxx' END AS last_archived_wal,
	CASE WHEN last_archived_time IS NOT NULL THEN 'xxx' END AS last_archived_time,
	failed_count,
	CASE WHEN last_failed_wal IS NOT NULL THEN 'xxx' END AS last_failed_wal,
	CASE WHEN last_failed_time IS NOT NULL THEN 'xxx' END AS last_failed_time,
	CASE WHEN stats_reset IS NOT NULL THEN 'xxx' END AS stats_reset
FROM
	statsrepo.archive
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF
else
	send_query -c "SELECT * FROM statsrepo.archive"
fi

echo "/**--- Statistics of replication ---**/"
echo "/***-- Monitored instance is a stand-alone configuration --***/"
send_query -c "SELECT * FROM statsrepo.replication"

echo "/**--- Statistics of query ---**/"
echo "/***-- pg_stat_statements is not installed --***/"
send_query -c "SELECT * FROM statsrepo.statement"

echo "/***-- pg_stat_statements is installed --***/"
cat << EOF >> ${PGDATA}/postgresql-statsinfo.conf
shared_preload_libraries = 'pg_statsinfo, pg_stat_statements'
pg_statsinfo.stat_statements_max = 1
pg_statsinfo.stat_statements_exclude_users = '${PGUSER}'
EOF
pg_ctl restart -w -D ${PGDATA} -o "-p ${PGPORT}" > /dev/null
sleep 3
psql -c "CREATE EXTENSION pg_stat_statements"
for i in  {1..200}
do
	sql="SELECT schema01.func01('zzz${i}', ${i})"
	psql db01 -U user01 -Atc "${sql}" > /dev/null
done
get_snapshot
send_query << EOF
SELECT
	s.snapid,
	d.name AS database,
	r.name AS role,
	s.query,
	s.plans,
	CASE WHEN s.total_plan_time IS NOT NULL THEN 'xxx' END AS total_plan_time,
	s.calls,
	CASE WHEN s.total_exec_time IS NOT NULL THEN 'xxx' END AS total_exec_time,
	CASE WHEN s.rows IS NOT NULL THEN 'xxx' END AS rows,
	CASE WHEN s.shared_blks_hit IS NOT NULL THEN 'xxx' END AS shared_blks_hit,
	CASE WHEN s.shared_blks_read IS NOT NULL THEN 'xxx' END AS shared_blks_read,
	CASE WHEN s.shared_blks_dirtied IS NOT NULL THEN 'xxx' END AS shared_blks_dirtied,
	CASE WHEN s.shared_blks_written IS NOT NULL THEN 'xxx' END AS shared_blks_written,
	CASE WHEN s.local_blks_hit IS NOT NULL THEN 'xxx' END AS local_blks_hit,
	CASE WHEN s.local_blks_read IS NOT NULL THEN 'xxx' END AS local_blks_read,
	CASE WHEN s.local_blks_dirtied IS NOT NULL THEN 'xxx' END AS local_blks_dirtied,
	CASE WHEN s.local_blks_written IS NOT NULL THEN 'xxx' END AS local_blks_written,
	CASE WHEN s.temp_blks_read IS NOT NULL THEN 'xxx' END AS temp_blks_read,
	CASE WHEN s.temp_blks_written IS NOT NULL THEN 'xxx' END AS temp_blks_written,
	CASE WHEN s.shared_blk_read_time IS NOT NULL THEN 'xxx' END AS shared_blk_read_time,
	CASE WHEN s.shared_blk_write_time IS NOT NULL THEN 'xxx' END AS shared_blk_write_time,
	CASE WHEN s.local_blk_read_time IS NOT NULL THEN 'xxx' END AS local_blk_read_time,
	CASE WHEN s.local_blk_write_time IS NOT NULL THEN 'xxx' END AS local_blk_write_time,
	CASE WHEN s.temp_blk_read_time IS NOT NULL THEN 'xxx' END AS temp_blk_read_time,
	CASE WHEN s.temp_blk_write_time IS NOT NULL THEN 'xxx' END AS temp_blk_write_time
FROM
	statsrepo.statement s,
	statsrepo.database d,
	statsrepo.role r
WHERE
	s.snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
	AND s.snapid = d.snapid
	AND s.snapid = r.snapid
	AND s.dbid = d.dbid
	AND s.userid = r.userid
ORDER BY
	database, role, query;
EOF

echo "/**--- Statistics of BGWriter ---**/"
send_query << EOF
SELECT
	snapid,
	CASE WHEN buffers_clean IS NOT NULL THEN 'xxx' END AS buffers_clean,
	CASE WHEN maxwritten_clean IS NOT NULL THEN 'xxx' END AS maxwritten_clean,
	CASE WHEN buffers_alloc IS NOT NULL THEN 'xxx' END AS buffers_alloc
FROM
	statsrepo.bgwriter
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF

echo "/**--- Statistics of rusage ---**/"
send_query << EOF
SELECT (COUNT(*) > 0) as cnt FROM statsrepo.rusage
   WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF
send_query << EOF
SELECT 
    CASE WHEN sum(exec_user_time) > 0 THEN 'xxx' ELSE 'yyy' END AS eut,
    CASE WHEN sum(exec_system_time) > 0 THEN 'xxx' ELSE 'yyy' END AS est
FROM
    statsrepo.rusage
WHERE
    snapid = (SELECT max(snapid) FROM statsrepo.snapshot);
EOF

echo "/**--- Collect statistics after database crash recovery ---**/"
psql -U user01 -d db01 -At << EOF
INSERT INTO schema01.tbl01 (name, age) VALUES ('xxx', 30);
INSERT INTO schema01.tbl02 (name, age, address) VALUES ('xxx', 30, 'xxx');
EOF
pg_ctl stop -m immediate -D ${PGDATA} > /dev/null
sleep 3
pg_ctl start -w -D ${PGDATA} -o "-p ${PGPORT}" > /dev/null
sleep 3
get_snapshot
send_query << EOF
SELECT
	snapid,
	instid,
	CASE WHEN time IS NOT NULL THEN 'xxx' END AS time,
	comment,
	CASE WHEN exec_time IS NOT NULL THEN 'xxx' END AS exec_time,
	CASE WHEN snapshot_increase_size IS NOT NULL THEN 'xxx' END AS snapshot_increase_size
FROM
	statsrepo.snapshot;
EOF

echo "/*---- do not collect column info and index info ----*/"

psql -c "ALTER SYSTEM SET pg_statsinfo.collect_column = off"
psql -c "ALTER SYSTEM SET pg_statsinfo.collect_index = off"
pg_ctl reload -D ${PGDATA} > /dev/null
get_snapshot

send_query -c "SELECT (COUNT(*) > 0) as cnt FROM statsrepo.column WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"
send_query -c "SELECT (COUNT(*) > 0) as cnt FROM statsrepo.index WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"
send_query -c "SELECT (COUNT(*) > 0) as cnt  FROM statsrepo.table WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"
send_query -c "SELECT (COUNT(*) > 0) as cnt  FROM statsrepo.inherits WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"
send_query -c "SELECT (COUNT(*) > 0) as cnt  FROM statsrepo.function WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"
send_query -c "SELECT (COUNT(*) > 0) as cnt  FROM statsrepo.schema WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"

psql -c "ALTER SYSTEM SET pg_statsinfo.collect_column = on"
psql -c "ALTER SYSTEM SET pg_statsinfo.collect_index = on"
pg_ctl reload -D ${PGDATA} > /dev/null
