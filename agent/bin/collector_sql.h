/*
 * collector_sql.h
 *
 * Copyright (c) 2010-2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#ifndef COLLECTOR_SQL_H
#define COLLECTOR_SQL_H

/*----------------------------------------------------------------------------
 * snapshots per instance
 *----------------------------------------------------------------------------
 */

/* database */
#if PG_VERSION_NUM >= 90100
#define SQL_SELECT_DATABASE "\
SELECT \
	d.oid AS dbid, \
	d.datname, \
	pg_database_size(d.oid), \
	CASE WHEN pg_is_in_recovery() THEN 0 ELSE age(d.datfrozenxid) END, \
	pg_stat_get_db_xact_commit(d.oid) AS xact_commit, \
	pg_stat_get_db_xact_rollback(d.oid) AS xact_rollback, \
	pg_stat_get_db_blocks_fetched(d.oid) - pg_stat_get_db_blocks_hit(d.oid) AS blks_read, \
	pg_stat_get_db_blocks_hit(d.oid) AS blks_hit, \
	pg_stat_get_db_tuples_returned(d.oid) AS tup_returned, \
	pg_stat_get_db_tuples_fetched(d.oid) AS tup_fetched, \
	pg_stat_get_db_tuples_inserted(d.oid) AS tup_inserted, \
	pg_stat_get_db_tuples_updated(d.oid) AS tup_updated, \
	pg_stat_get_db_tuples_deleted(d.oid) AS tup_deleted, \
	pg_stat_get_db_conflict_tablespace(d.oid) AS confl_tablespace, \
	pg_stat_get_db_conflict_lock(d.oid) AS confl_lock, \
	pg_stat_get_db_conflict_snapshot(d.oid) AS confl_snapshot, \
	pg_stat_get_db_conflict_bufferpin(d.oid) AS confl_bufferpin, \
	pg_stat_get_db_conflict_startup_deadlock(d.oid) AS confl_deadlock \
FROM \
	pg_database d \
WHERE datallowconn \
  AND datname <> ALL (('{' || $1 || '}')::text[]) \
ORDER BY 1"
#elif PG_VERSION_NUM >= 90000
#define SQL_SELECT_DATABASE "\
SELECT \
	d.oid AS dbid, \
	d.datname, \
	pg_database_size(d.oid), \
	CASE WHEN pg_is_in_recovery() THEN 0 ELSE age(d.datfrozenxid) END, \
	pg_stat_get_db_xact_commit(d.oid) AS xact_commit, \
	pg_stat_get_db_xact_rollback(d.oid) AS xact_rollback, \
	pg_stat_get_db_blocks_fetched(d.oid) - pg_stat_get_db_blocks_hit(d.oid) AS blks_read, \
	pg_stat_get_db_blocks_hit(d.oid) AS blks_hit, \
	pg_stat_get_db_tuples_returned(d.oid) AS tup_returned, \
	pg_stat_get_db_tuples_fetched(d.oid) AS tup_fetched, \
	pg_stat_get_db_tuples_inserted(d.oid) AS tup_inserted, \
	pg_stat_get_db_tuples_updated(d.oid) AS tup_updated, \
	pg_stat_get_db_tuples_deleted(d.oid) AS tup_deleted, \
	NULL::bigint AS confl_tablespace, \
	NULL::bigint AS confl_lock, \
	NULL::bigint AS confl_snapshot, \
	NULL::bigint AS confl_bufferpin, \
	NULL::bigint AS confl_deadlock \
FROM \
	pg_database d \
WHERE datallowconn \
  AND datname <> ALL (('{' || $1 || '}')::text[]) \
ORDER BY 1"
#else
#define SQL_SELECT_DATABASE "\
SELECT \
	d.oid AS dbid, \
	d.datname, \
	pg_database_size(d.oid), \
	age(d.datfrozenxid), \
	pg_stat_get_db_xact_commit(d.oid) AS xact_commit, \
	pg_stat_get_db_xact_rollback(d.oid) AS xact_rollback, \
	pg_stat_get_db_blocks_fetched(d.oid) - pg_stat_get_db_blocks_hit(d.oid) AS blks_read, \
	pg_stat_get_db_blocks_hit(d.oid) AS blks_hit, \
	pg_stat_get_db_tuples_returned(d.oid) AS tup_returned, \
	pg_stat_get_db_tuples_fetched(d.oid) AS tup_fetched, \
	pg_stat_get_db_tuples_inserted(d.oid) AS tup_inserted, \
	pg_stat_get_db_tuples_updated(d.oid) AS tup_updated, \
	pg_stat_get_db_tuples_deleted(d.oid) AS tup_deleted, \
	NULL::bigint AS confl_tablespace, \
	NULL::bigint AS confl_lock, \
	NULL::bigint AS confl_snapshot, \
	NULL::bigint AS confl_bufferpin, \
	NULL::bigint AS confl_deadlock \
FROM \
	pg_database d \
WHERE datallowconn \
  AND datname <> ALL (('{' || $1 || '}')::text[]) \
ORDER BY 1"
#endif

/* activity */
#define SQL_SELECT_ACTIVITY		"SELECT * FROM statsinfo.activity()"

/* tablespace */
#define SQL_SELECT_TABLESPACE	"SELECT * FROM statsinfo.tablespaces"

/* setting */
#if PG_VERSION_NUM >= 80400
#define SQL_SELECT_SETTING "\
SELECT \
	name, \
	setting, \
	source \
FROM \
	pg_settings \
WHERE \
	source NOT IN ('client', 'default', 'session') \
AND \
	setting <> boot_val"
#else
#define SQL_SELECT_SETTING "\
SELECT \
	name, \
	setting, \
	source \
FROM \
	pg_settings \
WHERE \
	source NOT IN ('client', 'default', 'session')"
#endif

/* role */
#define SQL_SELECT_ROLE "\
SELECT \
	oid, \
	rolname \
FROM \
	pg_roles"

/* statement */
#if PG_VERSION_NUM < 90000

#define SQL_SELECT_STATEMENT "\
SELECT \
	dbid, \
	userid, \
	query, \
	calls, \
	total_time, \
	rows, \
	NULL::bigint AS shared_blks_hit, \
	NULL::bigint AS shared_blks_read, \
	NULL::bigint AS shared_blks_written, \
	NULL::bigint AS local_blks_hit, \
	NULL::bigint AS local_blks_read, \
	NULL::bigint AS local_blks_written, \
	NULL::bigint AS temp_blks_read, \
	NULL::bigint AS temp_blks_written \
FROM \
	pg_stat_statements \
ORDER BY total_time DESC LIMIT 30"

#else

#define SQL_SELECT_STATEMENT "\
SELECT \
	dbid, \
	userid, \
	query, \
	calls, \
	total_time, \
	rows, \
	shared_blks_hit, \
	shared_blks_read, \
	shared_blks_written, \
	local_blks_hit, \
	local_blks_read, \
	local_blks_written, \
	temp_blks_read, \
	temp_blks_written \
FROM \
	pg_stat_statements \
ORDER BY total_time DESC LIMIT 30"

#endif

/* lock */
#if PG_VERSION_NUM >= 80400
#define SQL_SELECT_LOCK_XID_CAST			"transactionid"
#define SQL_SELECT_LOCK_BLOCKER_QUERY		"lx.queries"
#else
#define SQL_SELECT_LOCK_XID_CAST			"CAST(transactionid AS text)"
#define SQL_SELECT_LOCK_BLOCKER_QUERY		"'(N/A)'"
#endif
#if PG_VERSION_NUM >= 90000
#define SQL_SELECT_LOCK_APPNAME				"sa.application_name"
#else
#define SQL_SELECT_LOCK_APPNAME				"'(N/A)'"
#endif
#if PG_VERSION_NUM >= 90100
#define SQL_SELECT_LOCK_CLIENT_HOSTNAME		"sa.client_hostname"
#else
#define SQL_SELECT_LOCK_CLIENT_HOSTNAME		"'(N/A)'"
#endif

#define SQL_SELECT_LOCK "\
SELECT \
	db.datname, \
	nb.nspname, \
	cb.relname, \
	" SQL_SELECT_LOCK_APPNAME ", \
	sa.client_addr, \
	" SQL_SELECT_LOCK_CLIENT_HOSTNAME ", \
	sa.client_port, \
	lb.pid AS blockee_pid, \
	la.pid AS blocker_pid, \
	(statement_timestamp() - sb.query_start)::interval(0), \
	sb.current_query, \
	" SQL_SELECT_LOCK_BLOCKER_QUERY " \
FROM \
	(SELECT DISTINCT pid, relation, " SQL_SELECT_LOCK_XID_CAST " \
	 FROM pg_locks WHERE granted = true) la LEFT JOIN \
	 statsinfo.last_xact_activity() lx ON la.pid = lx.pid LEFT JOIN \
	 pg_stat_activity sa ON la.pid = sa.procpid, \
	(SELECT DISTINCT pid, relation, " SQL_SELECT_LOCK_XID_CAST " \
	 FROM pg_locks WHERE granted = false) lb LEFT JOIN \
	 pg_stat_activity sb ON lb.pid = sb.procpid LEFT JOIN \
	 pg_database db ON sb.datid = db.oid LEFT JOIN \
	 pg_class cb ON lb.relation = cb.oid LEFT JOIN \
	 pg_namespace nb ON cb.relnamespace = nb.oid \
WHERE \
	(la.transactionid = lb.transactionid OR la.relation = lb.relation) AND \
	sb.query_start < statement_timestamp() - current_setting('" GUC_PREFIX ".long_lock_threashold')::interval"

/* replication */
#define SQL_SELECT_REPLICATION "\
SELECT \
	procpid, \
	usesysid, \
	usename, \
	application_name, \
	client_addr, \
	client_hostname, \
	client_port, \
	backend_start, \
	state, \
	pg_current_xlog_location(), \
	sent_location, \
	write_location, \
	flush_location, \
	replay_location, \
	sync_priority, \
	sync_state \
FROM \
	pg_stat_replication"

/* cpu */
#define SQL_SELECT_CPU "\
SELECT * FROM statsinfo.cpustats()"

/* device */
#define SQL_SELECT_DEVICE	"SELECT * FROM statsinfo.devicestats()"

/* profile */
#define SQL_SELECT_PROFILE	"SELECT * FROM statsinfo.profile()"

/* repository size */
#define SQL_SELECT_REPOSIZE "\
SELECT \
	sum(pg_relation_size(oid)) \
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
	pg_namespace"

/* table */
#define SQL_SELECT_TABLE "\
SELECT \
	c.oid AS relid, \
	c.relnamespace, \
	c.reltablespace, \
	c.relname, \
	c.reltoastrelid, \
	c.reltoastidxid, \
	c.relkind, \
	c.relpages, \
	c.reltuples, \
	c.reloptions, \
	pg_relation_size(c.oid), \
	pg_stat_get_numscans(c.oid) AS seq_scan, \
	pg_stat_get_tuples_returned(c.oid) AS seq_tup_read, \
	sum(pg_stat_get_numscans(i.indexrelid))::bigint AS idx_scan, \
	sum(pg_stat_get_tuples_fetched(i.indexrelid))::bigint + \
		pg_stat_get_tuples_fetched(c.oid) AS idx_tup_fetch, \
	pg_stat_get_tuples_inserted(c.oid) AS n_tup_ins, \
	pg_stat_get_tuples_updated(c.oid) AS n_tup_upd, \
	pg_stat_get_tuples_deleted(c.oid) AS n_tup_del, \
	pg_stat_get_tuples_hot_updated(c.oid) AS n_tup_hot_upd, \
	pg_stat_get_live_tuples(c.oid) AS n_live_tup, \
	pg_stat_get_dead_tuples(c.oid) AS n_dead_tup,\
	pg_stat_get_blocks_fetched(c.oid) - \
		pg_stat_get_blocks_hit(c.oid) AS heap_blks_read, \
	pg_stat_get_blocks_hit(c.oid) AS heap_blks_hit, \
	sum(pg_stat_get_blocks_fetched(i.indexrelid) - \
		pg_stat_get_blocks_hit(i.indexrelid))::bigint AS idx_blks_read, \
	sum(pg_stat_get_blocks_hit(i.indexrelid))::bigint AS idx_blks_hit, \
	pg_stat_get_blocks_fetched(t.oid) - \
		pg_stat_get_blocks_hit(t.oid) AS toast_blks_read, \
	pg_stat_get_blocks_hit(t.oid) AS toast_blks_hit, \
	pg_stat_get_blocks_fetched(x.oid) - \
		pg_stat_get_blocks_hit(x.oid) AS tidx_blks_read, \
	pg_stat_get_blocks_hit(x.oid) AS tidx_blks_hit, \
	pg_stat_get_last_vacuum_time(c.oid) as last_vacuum, \
	pg_stat_get_last_autovacuum_time(c.oid) as last_autovacuum, \
	pg_stat_get_last_analyze_time(c.oid) as last_analyze, \
	pg_stat_get_last_autoanalyze_time(c.oid) as last_autoanalyze \
FROM  \
	pg_class c LEFT JOIN \
	pg_index i ON c.oid = i.indrelid LEFT JOIN \
	pg_class t ON c.reltoastrelid = t.oid LEFT JOIN \
	pg_class x ON t.reltoastidxid = x.oid \
WHERE c.relkind IN ('r', 't') \
GROUP BY \
    c.oid, \
    c.relnamespace, \
    c.reltablespace, \
    c.relname, \
    c.reltoastrelid, \
    c.reltoastidxid, \
    c.relkind, \
	c.relpages, \
	c.reltuples, \
    c.reloptions, \
    t.oid, \
    x.oid"

/* column */
#if PG_VERSION_NUM >= 90000
#define SQL_SELECT_COLUMN_WHERE		"AND NOT s.stainherit "
#else
#define SQL_SELECT_COLUMN_WHERE
#endif

#define SQL_SELECT_COLUMN "\
SELECT \
	a.attrelid, \
	a.attnum, \
	a.attname, \
	format_type(atttypid, atttypmod) AS type, \
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
		ELSE NULL::real \
	END AS correlation \
FROM \
	pg_attribute a \
	LEFT JOIN pg_class c ON \
		a.attrelid = c.oid \
	LEFT JOIN pg_statistic s ON \
		a.attnum = s.staattnum \
	AND \
		a.attrelid = s.starelid " SQL_SELECT_COLUMN_WHERE "\
WHERE \
	a.attnum > 0 \
AND \
	c.relkind IN ('r', 't')"

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
    pg_get_indexdef(i.oid), \
    pg_relation_size(i.oid), \
    pg_stat_get_numscans(i.oid) AS idx_scan, \
    pg_stat_get_tuples_returned(i.oid) AS idx_tup_read, \
    pg_stat_get_tuples_fetched(i.oid) AS idx_tup_fetch, \
    pg_stat_get_blocks_fetched(i.oid) - \
        pg_stat_get_blocks_hit(i.oid) AS idx_blks_read, \
    pg_stat_get_blocks_hit(i.oid) AS idx_blks_hit \
FROM \
    pg_class c JOIN \
    pg_index x ON c.oid = x.indrelid JOIN \
    pg_class i ON i.oid = x.indexrelid \
WHERE c.relkind IN ('r', 't')"

/* inherits */
#define SQL_SELECT_INHERITS "\
SELECT \
	inhrelid, \
	inhparent, \
	inhseqno \
FROM \
	pg_inherits"

/* function */
#define SQL_SELECT_FUNCTION "\
SELECT \
	s.funcid, \
	n.oid AS nspid, \
	s.funcname, \
	pg_get_function_arguments(funcid) AS argtypes, \
	s.calls, \
	s.total_time, \
	s.self_time \
FROM \
	pg_stat_user_functions s, \
	pg_namespace n \
WHERE s.schemaname = n.nspname"

#endif
