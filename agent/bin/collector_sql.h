/*
 * collector_sql.h
 *
 * Copyright (c) 2009-2024, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#ifndef COLLECTOR_SQL_H
#define COLLECTOR_SQL_H

#define PG_STAT_ACTIVITY_ATTNAME_PID		"pid"
#define PG_STAT_ACTIVITY_ATTNAME_QUERY		"query"
#define PG_STAT_REPLICATION_ATTNAME_PID		"pid"

/*----------------------------------------------------------------------------
 * snapshots per instance
 *----------------------------------------------------------------------------
 */

/* database */
#define SQL_SELECT_DATABASE "\
SELECT \
	d.oid AS dbid, \
	d.datname, \
	pg_catalog.pg_database_size(d.oid), \
	CASE WHEN pg_catalog.pg_is_in_recovery() THEN 0 ELSE pg_catalog.age(d.datfrozenxid) END, \
	pg_catalog.pg_stat_get_db_xact_commit(d.oid) AS xact_commit, \
	pg_catalog.pg_stat_get_db_xact_rollback(d.oid) AS xact_rollback, \
	pg_catalog.pg_stat_get_db_blocks_fetched(d.oid) - pg_catalog.pg_stat_get_db_blocks_hit(d.oid) AS blks_read, \
	pg_catalog.pg_stat_get_db_blocks_hit(d.oid) AS blks_hit, \
	pg_catalog.pg_stat_get_db_tuples_returned(d.oid) AS tup_returned, \
	pg_catalog.pg_stat_get_db_tuples_fetched(d.oid) AS tup_fetched, \
	pg_catalog.pg_stat_get_db_tuples_inserted(d.oid) AS tup_inserted, \
	pg_catalog.pg_stat_get_db_tuples_updated(d.oid) AS tup_updated, \
	pg_catalog.pg_stat_get_db_tuples_deleted(d.oid) AS tup_deleted, \
	pg_catalog.pg_stat_get_db_conflict_tablespace(d.oid) AS confl_tablespace, \
	pg_catalog.pg_stat_get_db_conflict_lock(d.oid) AS confl_lock, \
	pg_catalog.pg_stat_get_db_conflict_snapshot(d.oid) AS confl_snapshot, \
	pg_catalog.pg_stat_get_db_conflict_bufferpin(d.oid) AS confl_bufferpin, \
	pg_catalog.pg_stat_get_db_conflict_startup_deadlock(d.oid) AS confl_deadlock, \
	pg_catalog.pg_stat_get_db_temp_files(d.oid) AS temp_files, \
	pg_catalog.pg_stat_get_db_temp_bytes(d.oid) AS temp_bytes, \
	pg_catalog.pg_stat_get_db_deadlocks(d.oid) AS deadlocks, \
	pg_catalog.pg_stat_get_db_blk_read_time(d.oid) AS blk_read_time, \
	pg_catalog.pg_stat_get_db_blk_write_time(d.oid) AS blk_write_time \
FROM \
	pg_database d \
WHERE datallowconn \
  AND datname <> ALL (('{' || $1 || '}')::text[]) \
ORDER BY dbid"

/* activity */
#define SQL_SELECT_ACTIVITY				"SELECT * FROM statsinfo.activity()"

/* long transaction */
#define SQL_SELECT_LONG_TRANSACTION		"SELECT * FROM statsinfo.long_xact()"

/* tablespace */
#define SQL_SELECT_TABLESPACE			"SELECT * FROM statsinfo.tablespaces"

/* setting */
#define SQL_SELECT_SETTING "\
SELECT \
	name, \
	setting, \
	unit, \
	source \
FROM \
	pg_settings \
WHERE \
	source NOT IN ('client', 'default', 'session') \
AND \
	setting <> boot_val"

/* role */
#define SQL_SELECT_ROLE "\
SELECT \
	oid, \
	rolname \
FROM \
	pg_roles"

/* statement */
#define SQL_SELECT_STATEMENT "\
SELECT \
	s.dbid, \
	s.userid, \
	s.queryid, \
	s.query, \
	s.plans, \
	s.total_plan_time / 1000, \
	s.calls, \
	s.total_exec_time / 1000, \
	s.rows, \
	s.shared_blks_hit, \
	s.shared_blks_read, \
	s.shared_blks_dirtied, \
	s.shared_blks_written, \
	s.local_blks_hit, \
	s.local_blks_read, \
	s.local_blks_dirtied, \
	s.local_blks_written, \
	s.temp_blks_read, \
	s.temp_blks_written, \
	s.blk_read_time, \
	s.blk_write_time, \
	s.temp_blk_read_time, \
	s.temp_blk_write_time \
FROM \
	pg_stat_statements s \
	LEFT JOIN pg_roles r ON r.oid = s.userid \
WHERE \
	r.rolname <> ALL (('{' || $1 || '}')::text[]) \
ORDER BY \
	s.total_exec_time DESC LIMIT $2"

/* plan */
#define SQL_SELECT_PLAN "\
SELECT \
	p.dbid, \
	p.userid, \
	p.queryid, \
	p.planid, \
	p.plan, \
	p.calls, \
	p.total_time / 1000, \
	p.rows, \
	p.shared_blks_hit, \
	p.shared_blks_read, \
	p.shared_blks_dirtied, \
	p.shared_blks_written, \
	p.local_blks_hit, \
	p.local_blks_read, \
	p.local_blks_dirtied, \
	p.local_blks_written, \
	p.temp_blks_read, \
	p.temp_blks_written, \
	p.blk_read_time, \
	p.blk_write_time, \
	p.temp_blk_read_time, \
	p.temp_blk_write_time, \
	p.first_call, \
	p.last_call \
FROM \
	pg_store_plans p \
	LEFT JOIN pg_roles r ON r.oid = p.userid \
WHERE \
	r.rolname <> ALL (('{' || $1 || '}')::text[]) \
ORDER BY \
	p.total_time DESC LIMIT $2"

/* lock */
#define SQL_SELECT_LOCK_APPNAME				"sa.application_name"
#define SQL_SELECT_LOCK_CLIENT_HOSTNAME		"sa.client_hostname"

#define SQL_SELECT_LOCK "\
SELECT \
	sa.datid, \
	ns.nspname, \
	t.relation, \
	sa.application_name, \
	sa.client_addr, \
	sa.client_hostname, \
	sa.client_port, \
	t.blockee_pid, \
	t.blocker_pid, \
	px.gid AS blocker_gid, \
	sa.wait_event_type, \
	sa.wait_event, \
	(pg_catalog.statement_timestamp() - t.waitstart)::interval(0), \
	sa.query, \
	CASE \
		WHEN px.gid IS NOT NULL THEN '(xact is detached from session)' \
		WHEN lx.queries IS NULL THEN '(library might not have been loaded)' \
		ELSE lx.queries \
	END \
FROM \
	(SELECT DISTINCT \
		pid AS blockee_pid, \
		pg_catalog.unnest(pg_catalog.pg_blocking_pids(pid)) AS blocker_pid, \
		transactionid, \
		relation, \
		waitstart \
	 FROM \
		pg_locks \
	 WHERE \
		granted = false \
	) t \
	LEFT JOIN pg_prepared_xacts px ON px.transaction = t.transactionid \
	LEFT JOIN pg_stat_activity sa ON sa.pid = t.blockee_pid \
	LEFT JOIN statsinfo.last_xact_activity() lx ON lx.pid = t.blocker_pid \
	LEFT JOIN pg_database db ON db.oid = sa.datid \
	LEFT JOIN pg_class c ON c.oid = t.relation \
	LEFT JOIN pg_namespace ns ON ns.oid = c.relnamespace \
WHERE \
	t.waitstart < pg_catalog.statement_timestamp() - pg_catalog.current_setting('" GUC_PREFIX ".long_lock_threshold')::interval"

/* bgwriter */
#define SQL_SELECT_BGWRITER "\
SELECT \
	buffers_clean, \
	maxwritten_clean, \
	buffers_backend, \
	buffers_backend_fsync, \
	buffers_alloc \
FROM \
	pg_stat_bgwriter"

/* replication */
#define SQL_SELECT_REPLICATION_BACKEND_XMIN		"backend_xmin"

#define SQL_SELECT_REPLICATION "\
SELECT \
	pid, \
	usesysid, \
	usename, \
	application_name, \
	client_addr, \
	client_hostname, \
	client_port, \
	backend_start, \
	backend_xmin, \
	state, \
	CASE WHEN pg_catalog.pg_is_in_recovery() THEN \
		pg_catalog.pg_last_wal_receive_lsn() || ' (N/A)' ELSE \
		pg_catalog.pg_current_wal_lsn() || ' (' || pg_catalog.pg_walfile_name(pg_catalog.pg_current_wal_lsn()) || ')' END, \
	CASE WHEN pg_catalog.pg_is_in_recovery() THEN \
		sent_lsn || ' (N/A)' ELSE \
		sent_lsn || ' (' || pg_catalog.pg_walfile_name(sent_lsn) || ')' END, \
	CASE WHEN pg_catalog.pg_is_in_recovery() THEN \
		write_lsn || ' (N/A)' ELSE \
		write_lsn || ' (' || pg_catalog.pg_walfile_name(write_lsn) || ')' END, \
	CASE WHEN pg_catalog.pg_is_in_recovery() THEN \
		flush_lsn || ' (N/A)' ELSE \
		flush_lsn || ' (' || pg_catalog.pg_walfile_name(flush_lsn) || ')' END, \
	CASE WHEN pg_catalog.pg_is_in_recovery() THEN \
		replay_lsn || ' (N/A)' ELSE \
		replay_lsn || ' (' || pg_catalog.pg_walfile_name(replay_lsn) || ')' END, \
	coalesce(write_lag, '00:00:00'), \
	coalesce(flush_lag, '00:00:00'), \
	coalesce(replay_lag, '00:00:00'), \
	sync_priority, \
	sync_state \
FROM \
	pg_stat_replication"

/* replication slot */
#define SQL_SELECT_REPLICATION_SLOTS "\
SELECT \
	slot_name, \
	plugin, \
	slot_type, \
	datoid, \
	temporary, \
	active, \
	active_pid, \
	xmin, \
	catalog_xmin, \
	restart_lsn, \
	confirmed_flush_lsn \
FROM \
	pg_replication_slots"

/* stat replication slots*/
#define SQL_SELECT_STAT_REPLICATION_SLOTS "\
SELECT \
	slot_name, \
	spill_txns, \
	spill_count, \
	spill_bytes, \
	stream_txns, \
	stream_count, \
	stream_bytes, \
	total_txns, \
	total_bytes, \
	stats_reset \
FROM \
	pg_stat_replication_slots"

/* stat io */
#define SQL_SELECT_STAT_IO "\
SELECT \
	backend_type, \
	object, \
	context, \
	reads, \
	read_time, \
	writes, \
	write_time, \
	writebacks, \
	writeback_time, \
	extends, \
	extend_time, \
	op_bytes, \
	hits, \
	evictions, \
	reuses, \
	fsyncs, \
	fsync_time, \
	stats_reset \
FROM \
	pg_stat_io"

/* stat wal */
#define SQL_SELECT_STAT_WAL "\
SELECT \
	wal_records, \
	wal_fpi, \
	wal_bytes, \
	wal_buffers_full, \
	wal_write, \
	wal_sync, \
	wal_write_time, \
	wal_sync_time, \
	stats_reset \
FROM \
	pg_stat_wal"

/* xlog */
#define SQL_SELECT_XLOG "\
SELECT \
	pg_catalog.pg_current_wal_lsn(), \
	pg_catalog.pg_walfile_name(pg_catalog.pg_current_wal_lsn()) \
WHERE \
	NOT pg_catalog.pg_is_in_recovery()"

/* archive */
#define SQL_SELECT_ARCHIVE "\
SELECT * FROM pg_stat_archiver"

/* cpu */
#define SQL_SELECT_CPU "\
SELECT * FROM statsinfo.cpustats($1)"

/* device */
#define SQL_SELECT_DEVICE "\
SELECT * FROM statsinfo.devicestats()"

/* loadavg */
#define SQL_SELECT_LOADAVG "\
SELECT * FROM statsinfo.loadavg()"

/* memory */
#define SQL_SELECT_MEMORY "\
SELECT * FROM statsinfo.memory()"

/* profile */
#define SQL_SELECT_PROFILE	"SELECT * FROM statsinfo.profile()"

/* repository size */
#define SQL_SELECT_REPOSIZE "\
SELECT \
	pg_catalog.sum(pg_catalog.pg_relation_size(oid)) \
FROM \
	pg_class \
WHERE \
	relnamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'statsrepo')"

/*----------------------------------------------------------------------------
 * snapshots per database
 *----------------------------------------------------------------------------
 */

/* schema */
#define SQL_SELECT_SCHEMA "\
SELECT \
	oid AS nspid, \
	nspname \
FROM \
	pg_namespace \
WHERE \
	nspname <> ALL (('{' || $1 || '}')::text[])"

/* table */
#define SQL_SELECT_TABLE_N_MOD_SINCE_ANALYZE	"pg_catalog.pg_stat_get_mod_since_analyze(c.oid)"

#define SQL_SELECT_TABLE "\
SELECT \
	c.oid AS relid, \
	c.relnamespace, \
	c.reltablespace, \
	c.relname, \
	c.reltoastrelid, \
	pg_catalog.max(x.indexrelid) AS reltoastidxid,  /*should be only one non-null indexrelid.*/ \
	c.relkind, \
	c.relpages, \
	c.reltuples, \
	c.reloptions, \
	pg_catalog.pg_relation_size(c.oid), \
	pg_catalog.pg_stat_get_numscans(c.oid) AS seq_scan, \
	pg_catalog.pg_stat_get_tuples_returned(c.oid) AS seq_tup_read, \
	pg_catalog.sum(pg_catalog.pg_stat_get_numscans(i.indexrelid))::bigint AS idx_scan, \
	pg_catalog.sum(pg_catalog.pg_stat_get_tuples_fetched(i.indexrelid))::bigint + \
		pg_catalog.pg_stat_get_tuples_fetched(c.oid) AS idx_tup_fetch, \
	pg_catalog.pg_stat_get_tuples_inserted(c.oid) AS n_tup_ins, \
	pg_catalog.pg_stat_get_tuples_updated(c.oid) AS n_tup_upd, \
	pg_catalog.pg_stat_get_tuples_deleted(c.oid) AS n_tup_del, \
	pg_catalog.pg_stat_get_tuples_hot_updated(c.oid) AS n_tup_hot_upd, \
	pg_catalog.pg_stat_get_live_tuples(c.oid) AS n_live_tup, \
	pg_catalog.pg_stat_get_dead_tuples(c.oid) AS n_dead_tup, \
	" SQL_SELECT_TABLE_N_MOD_SINCE_ANALYZE " AS n_mod_since_analyze, \
	pg_catalog.pg_stat_get_blocks_fetched(c.oid) - \
		pg_catalog.pg_stat_get_blocks_hit(c.oid) AS heap_blks_read, \
	pg_catalog.pg_stat_get_blocks_hit(c.oid) AS heap_blks_hit, \
	pg_catalog.sum(pg_catalog.pg_stat_get_blocks_fetched(i.indexrelid) - \
		pg_catalog.pg_stat_get_blocks_hit(i.indexrelid))::bigint AS idx_blks_read, \
	pg_catalog.sum(pg_catalog.pg_stat_get_blocks_hit(i.indexrelid))::bigint AS idx_blks_hit, \
	pg_catalog.pg_stat_get_blocks_fetched( pg_catalog.max(t.oid) ) - \
		pg_catalog.pg_stat_get_blocks_hit( pg_catalog.max(t.oid) ) AS toast_blks_read,       /* should be only one non-null oid */ \
	pg_catalog.pg_stat_get_blocks_hit( pg_catalog.max(t.oid) ) AS toast_blks_hit,            /* should be only one non-null oid */ \
	pg_catalog.pg_stat_get_blocks_fetched( pg_catalog.max(x.indexrelid) ) - \
		pg_catalog.pg_stat_get_blocks_hit( pg_catalog.max(x.indexrelid) ) AS tidx_blks_read, /* should be only one non-null indexrelid. */ \
	pg_catalog.pg_stat_get_blocks_hit(pg_catalog.max( x.indexrelid )) AS tidx_blks_hit,      /* should be only one non-null indexrelid. */ \
	pg_catalog.pg_stat_get_last_vacuum_time(c.oid) as last_vacuum, \
	pg_catalog.pg_stat_get_last_autovacuum_time(c.oid) as last_autovacuum, \
	pg_catalog.pg_stat_get_last_analyze_time(c.oid) as last_analyze, \
	pg_catalog.pg_stat_get_last_autoanalyze_time(c.oid) as last_autoanalyze \
FROM \
	pg_class c LEFT JOIN \
	pg_index i ON c.oid = i.indrelid LEFT JOIN \
	pg_class t ON c.reltoastrelid = t.oid LEFT JOIN \
	pg_index x ON c.reltoastrelid = x.indrelid LEFT JOIN \
	pg_namespace n ON c.relnamespace = n.oid \
WHERE \
	c.relkind IN ('r', 't') AND \
	n.nspname <> ALL (('{' || $1 || '}')::text[]) AND \
	(i.indisvalid = true OR i.indisvalid IS NULL) AND \
	(x.indisvalid = true OR x.indisvalid IS NULL) \
GROUP BY \
	c.oid"

/* column */

#define SQL_SELECT_COLUMN "\
SELECT \
	a.attrelid, \
	a.attnum, \
	a.attname, \
	pg_catalog.format_type(atttypid, atttypmod) AS type, \
	a.attstattarget, \
	a.attstorage, \
	a.attnotnull, \
	a.attisdropped, \
	s.stawidth as avg_width, \
	s.stadistinct as n_distinct, \
	CASE \
		WHEN s.stakind1 = 3 THEN s.stanumbers1[1] \
		WHEN s.stakind2 = 3 THEN s.stanumbers2[1] \
		WHEN s.stakind3 = 3 THEN s.stanumbers3[1] \
		WHEN s.stakind4 = 3 THEN s.stanumbers4[1] \
		ELSE NULL \
	END AS correlation \
FROM \
	pg_attribute a \
	LEFT JOIN pg_class c ON \
		a.attrelid = c.oid \
	LEFT JOIN pg_statistic s ON \
		a.attnum = s.staattnum \
	AND \
		a.attrelid = s.starelid AND NOT s.stainherit \
	LEFT JOIN pg_namespace n ON \
		c.relnamespace = n.oid \
WHERE \
	a.attnum > 0 \
AND \
	c.relkind IN ('r', 't') \
AND \
	n.nspname <> ALL (('{' || $1 || '}')::text[])"

/* index */
#define SQL_SELECT_INDEX "\
SELECT \
    i.oid AS indexrelid, \
    c.oid AS relid, \
    i.reltablespace, \
    i.relname AS indexrelname, \
	i.relam, \
    i.relpages, \
    i.reltuples, \
    i.reloptions, \
    x.indisunique, \
    x.indisprimary, \
    x.indisclustered, \
    x.indisvalid, \
	x.indkey, \
    pg_catalog.pg_get_indexdef(i.oid), \
    pg_catalog.pg_relation_size(i.oid), \
    pg_catalog.pg_stat_get_numscans(i.oid) AS idx_scan, \
    pg_catalog.pg_stat_get_tuples_returned(i.oid) AS idx_tup_read, \
    pg_catalog.pg_stat_get_tuples_fetched(i.oid) AS idx_tup_fetch, \
    pg_catalog.pg_stat_get_blocks_fetched(i.oid) - \
        pg_catalog.pg_stat_get_blocks_hit(i.oid) AS idx_blks_read, \
    pg_catalog.pg_stat_get_blocks_hit(i.oid) AS idx_blks_hit \
FROM \
    pg_class c JOIN \
    pg_index x ON c.oid = x.indrelid JOIN \
    pg_class i ON i.oid = x.indexrelid JOIN \
    pg_namespace n ON n.oid = c.relnamespace \
WHERE \
	c.relkind IN ('r', 't') AND \
	n.nspname <> ALL (('{' || $1 || '}')::text[])"

/* inherits */
#define SQL_SELECT_INHERITS "\
SELECT \
	i.inhrelid, \
	i.inhparent, \
	i.inhseqno \
FROM \
	pg_inherits i JOIN \
	pg_class c ON i.inhrelid = c.oid JOIN \
	pg_namespace n ON c.relnamespace = n.oid \
WHERE \
	n.nspname <> ALL (('{' || $1 || '}')::text[])"

/* function */
#define SQL_SELECT_FUNCTION "\
SELECT \
	s.funcid, \
	n.oid AS nspid, \
	s.funcname, \
	pg_catalog.pg_get_function_arguments(funcid) AS argtypes, \
	s.calls, \
	s.total_time, \
	s.self_time \
FROM \
	pg_stat_user_functions s JOIN \
	pg_namespace n ON s.schemaname = n.nspname \
WHERE \
	n.nspname <> ALL (('{' || $1 || '}')::text[])"

/* wait sampling profile */
#define SQL_SELECT_WAIT_SAMPLING_PROFILE	"SELECT * FROM statsinfo.wait_sampling_profile()"

#endif

/* device */
/* TODO: exec_user_time is best key? */
#define SQL_SELECT_RUSAGE "\
SELECT \
        s.dbid, \
        s.userid, \
        s.queryid, \
	s.plan_reads, \
	s.plan_writes, \
	s.plan_user_time, \
	s.plan_system_time, \
	s.plan_minflts, \
	s.plan_majflts, \
	s.plan_nvcsws, \
	s.plan_nivcsws, \
	s.exec_reads, \
        s.exec_writes, \
        s.exec_user_time, \
        s.exec_system_time, \
        s.exec_minflts, \
        s.exec_majflts, \
        s.exec_nvcsws, \
        s.exec_nivcsws \
FROM \
        statsinfo.rusage() s \
        LEFT JOIN pg_roles r ON r.oid = s.userid \
WHERE \
        r.rolname <> ALL (('{' || $1 || '}')::text[]) \
ORDER BY \
        s.exec_user_time DESC LIMIT $2"

/* It does not have join key but ok, because each view and func have only one record. */
#define SQL_SELECT_HT_INFO "\
SELECT \
	s.dealloc, \
	s.stats_reset, \
	w.dealloc, \
	w.stats_reset, \
	r.dealloc, \
	r.stats_reset \
FROM \
	pg_stat_statements_info s, \
	statsinfo.sample_wait_sampling_info() w, \
	statsinfo.rusage_info() r"

/* If pg_stat_statements is not installed, avoid referencing pg_stat_statements_info.*/
#define SQL_SELECT_HT_INFO_EXCEPT_SS "\
SELECT \
	NULL, \
	NULL, \
	w.dealloc, \
	w.stats_reset, \
	r.dealloc, \
	r.stats_reset \
FROM \
	statsinfo.sample_wait_sampling_info() w, \
	statsinfo.rusage_info() r"
