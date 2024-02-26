/*
 * bin/pg_statsrepo.sql
 *
 * Create a repository schema.
 *
 * Copyright (c) 2009-2024, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

-- Adjust this setting to control where the objects get created.
SET search_path = public;

BEGIN;

SET LOCAL client_min_messages = WARNING;

CREATE SCHEMA statsrepo;

CREATE TYPE statsrepo.elevel AS ENUM
(
	'DEBUG',
	'INFO',
	'NOTICE',
	'WARNING',
	'ERROR',
	'LOG',
	'FATAL',
	'PANIC',
	'ALERT'
);

CREATE TABLE statsrepo.instance
(
	instid				bigserial,
	name				text NOT NULL,
	hostname			text NOT NULL,
	port				integer NOT NULL,
	pg_version			text,
	xlog_file_size		bigint,
	page_size			integer,
	page_header_size	smallint,
	htup_header_size	smallint,
	item_id_size		smallint,
	PRIMARY KEY (instid),
	UNIQUE (name, hostname, port)
);

CREATE TABLE statsrepo.snapshot
(
	snapid					bigserial,
	instid					bigint,
	time					timestamptz,
	comment					text,
	exec_time				interval,
	snapshot_increase_size	bigint,
	xid_current				xid8,
	PRIMARY KEY (snapid),
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.tablespace
(
	snapid			bigint,
	tbs				oid,
	name			name,
	location		text,
	device			text,
	avail			bigint,
	total			bigint,
	spcoptions		text[],
	PRIMARY KEY (snapid, name),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.database
(
	snapid				bigint,
	dbid				oid,
	name				name,
	size				bigint,
	age					integer,
	xact_commit			bigint,
	xact_rollback		bigint,
	blks_read			bigint,
	blks_hit			bigint,
	tup_returned		bigint,
	tup_fetched			bigint,
	tup_inserted		bigint,
	tup_updated			bigint,
	tup_deleted			bigint,
	confl_tablespace	bigint,
	confl_lock			bigint,
	confl_snapshot		bigint,
	confl_bufferpin		bigint,
	confl_deadlock		bigint,
	temp_files			bigint,
	temp_bytes			bigint,
	deadlocks			bigint,
	blk_read_time		double precision,
	blk_write_time		double precision,
	PRIMARY KEY (snapid, dbid),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.schema
(
	snapid			bigint,
	dbid			oid,
	nsp				oid,
	name			name,
	PRIMARY KEY (snapid, dbid, nsp),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid)
);

CREATE TABLE statsrepo.table
(
	snapid				bigint,
	dbid				oid,
	tbl					oid,
	nsp					oid,
	date				date,
	tbs					oid,
	name				name,
	toastrelid			oid,
	toastidxid			oid,
	relkind				"char",
	relpages			integer,
	reltuples			real,
	reloptions			text[],
	size				bigint,
	seq_scan			bigint,
	seq_tup_read		bigint,
	idx_scan			bigint,
	idx_tup_fetch		bigint,
	n_tup_ins			bigint,
	n_tup_upd			bigint,
	n_tup_del			bigint,
	n_tup_hot_upd		bigint,
	n_live_tup			bigint,
	n_dead_tup			bigint,
	n_mod_since_analyze	bigint,
	heap_blks_read		bigint,
	heap_blks_hit		bigint,
	idx_blks_read		bigint,
	idx_blks_hit		bigint,
	toast_blks_read		bigint,
	toast_blks_hit		bigint,
	tidx_blks_read		bigint,
	tidx_blks_hit		bigint,
	last_vacuum			timestamptz,
	last_autovacuum		timestamptz,
	last_analyze		timestamptz,
	last_autoanalyze	timestamptz,
	PRIMARY KEY (snapid, dbid, tbl),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid),
	FOREIGN KEY (snapid, dbid, nsp) REFERENCES statsrepo.schema (snapid, dbid, nsp)
);

CREATE TABLE statsrepo.index
(
	snapid			bigint,
	dbid			oid,
	idx				oid,
	tbl				oid,
	date			date,
	tbs				oid,
	name			name,
	relam			oid,
	relpages		integer,
	reltuples		real,
	reloptions		text[],
	isunique		bool,
	isprimary		bool,
	isclustered		bool,
	isvalid			bool,
	indkey			int2vector,
	indexdef		text,
	size			bigint,
	idx_scan		bigint,
	idx_tup_read	bigint,
	idx_tup_fetch	bigint,
	idx_blks_read	bigint,
	idx_blks_hit	bigint,
	PRIMARY KEY (snapid, dbid, idx),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid)
);

CREATE TABLE statsrepo.column
(
	snapid			bigint,
	dbid			oid,
	tbl				oid,
	attnum			smallint,
	date			date,
	name			name,
	type			text,
	stattarget		integer,
	storage			"char",
	isnotnull		bool,
	isdropped		bool,
	avg_width		integer,
	n_distinct		real,
	correlation		real,
	PRIMARY KEY (snapid, dbid, tbl, attnum),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid)
);

CREATE TABLE statsrepo.activity
(
	snapid				bigint,
	idle				integer,
	idle_in_xact		integer,
	waiting				integer,
	running				integer,
	max_backends		integer,
	PRIMARY KEY (snapid),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.xact
(
	snapid			bigint,
	client			inet,
	pid				integer,
	start			timestamptz,
	duration		float8,
	query			text,
	PRIMARY KEY (snapid, pid, start),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.setting
(
	snapid			bigint,
	name			text,
	setting			text,
	unit			text,
	source			text,
	PRIMARY KEY (snapid, name),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.role
(
	snapid			bigint,
	userid			oid,
	name			text,
	PRIMARY KEY (snapid, userid),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.inherits
(
	snapid			bigint,
	dbid			oid,
	inhrelid		oid,
	inhparent		oid,
	inhseqno		integer,
	PRIMARY KEY (snapid, dbid, inhrelid, inhseqno),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.statement
(
	snapid				bigint,
	dbid				oid,
	userid				oid,
	queryid				bigint,
	query				text,
	plans				bigint,
	total_plan_time		double precision,
	calls				bigint,
	total_exec_time		double precision,
	rows				bigint,
	shared_blks_hit		bigint,
	shared_blks_read	bigint,
	shared_blks_dirtied	bigint,
	shared_blks_written	bigint,
	local_blks_hit		bigint,
	local_blks_read		bigint,
	local_blks_dirtied	bigint,
	local_blks_written	bigint,
	temp_blks_read		bigint,
	temp_blks_written	bigint,
	blk_read_time		double precision,
	blk_write_time		double precision,
	temp_blk_read_time	double precision,
	temp_blk_write_time	double precision,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid)
);
CREATE INDEX statsrepo_statement_idx ON statsrepo.statement(snapid, dbid);

CREATE TABLE statsrepo.plan
(
	snapid					bigint,
	dbid					oid,
	userid					oid,
	queryid					bigint,
	planid					bigint,
	plan					text,
	calls					bigint,
	total_time				double precision,
	rows					bigint,
	shared_blks_hit			bigint,
	shared_blks_read		bigint,
	shared_blks_dirtied		bigint,
	shared_blks_written		bigint,
	local_blks_hit			bigint,
	local_blks_read			bigint,
	local_blks_dirtied		bigint,
	local_blks_written		bigint,
	temp_blks_read			bigint,
	temp_blks_written		bigint,
	blk_read_time			double precision,
	blk_write_time			double precision,
	temp_blk_read_time		double precision,
	temp_blk_write_time		double precision,
	first_call				timestamptz,
	last_call				timestamptz,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid)
);
CREATE INDEX statsrepo_plan_idx ON statsrepo.plan(snapid, dbid);

CREATE TABLE statsrepo.function
(
	snapid			bigint,
	dbid			oid,
	funcid			oid,
	nsp				oid,
	funcname		name,
	argtypes		text,
	calls			bigint,
	total_time		double precision,
	self_time		double precision,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid)
);
CREATE INDEX statsrepo_function_idx ON statsrepo.function(snapid, dbid);

CREATE TABLE statsrepo.autovacuum
(
	instid					bigint,
	start					timestamptz,
	database				text,
	schema					text,
	"table"					text,
	index_scans				integer,
	page_removed			bigint,
	page_remain				bigint,
	tbl_scan_pages			bigint,
	tbl_scan_pages_ratio	double precision,
	tup_removed				bigint,
	tup_remain				bigint,
	tup_dead				bigint,
	removable_cutoff		xid8,
	read_rate				double precision,
	write_rate				double precision,
	page_hit				bigint,
	page_miss				bigint,
	page_dirty				bigint,
	wal_records				bigint,
	wal_page_images			bigint,
	wal_bytes				bigint,
	duration				real,
	tup_miss_dead			bigint,
	tup_miss_dead_pages		bigint,
	new_relfrozenxid		xid8,
	new_relminmxid			xid8,
	frozen_pages			bigint,
	frozen_pages_ratio		double precision,
	frozen_tuples			bigint,
	index_scan_ptn			integer,
	dead_lp_pages			bigint,
	dead_lp_pages_ratio		double precision,
	dead_lp					bigint,
	index_names             text[],
	index_pages_total       bigint[],
	index_pages_new_del     bigint[],
	index_pages_current_del bigint[],
	index_pages_reusable    bigint[],
	io_timings_read			double precision,
	io_timings_write		double precision,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE
);
CREATE INDEX statsrepo_autovacuum_idx ON statsrepo.autovacuum(instid, start);

CREATE TABLE statsrepo.autoanalyze
(
	instid				bigint,
	start				timestamptz,
	database			text,
	schema				text,
	"table"				text,
	page_hit			bigint,
	page_miss			bigint,
	page_dirty			bigint,
	read_rate			double precision,
	write_rate			double precision,
	duration			real,
	io_timings_read		double precision,
	io_timings_write	double precision,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE
);
CREATE INDEX statsrepo_autoanalyze_idx ON statsrepo.autoanalyze(instid, start);

CREATE TABLE statsrepo.autovacuum_cancel
(
	instid			bigint,
	timestamp		timestamptz,
	database		text,
	schema			text,
	"table"			text,
	query			text,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE
);
CREATE INDEX statsrepo_autovacuum_cancel_idx ON statsrepo.autovacuum_cancel(instid, timestamp);

CREATE TABLE statsrepo.autoanalyze_cancel
(
	instid			bigint,
	timestamp		timestamptz,
	database		text,
	schema			text,
	"table"			text,
	query			text,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE
);
CREATE INDEX statsrepo_autoanalyze_cancel_idx ON statsrepo.autoanalyze_cancel(instid, timestamp);

CREATE TABLE statsrepo.checkpoint
(
	instid			bigint,
	start			timestamptz,
	flags			text,
	num_buffers		bigint,
	xlog_added		bigint,
	xlog_removed	bigint,
	xlog_recycled	bigint,
	write_duration	real,
	sync_duration	real,
	total_duration	real,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE
);
CREATE INDEX statsrepo_checkpoint_idx ON statsrepo.checkpoint(instid, start);

CREATE TABLE statsrepo.cpu
(
	snapid				bigint,
	cpu_id				text,
	cpu_user			bigint,
	cpu_system			bigint,
	cpu_idle			bigint,
	cpu_iowait			bigint,
	overflow_user		smallint,
	overflow_system		smallint,
	overflow_idle		smallint,
	overflow_iowait		smallint,
	PRIMARY KEY (snapid, cpu_id),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.device
(
	snapid				bigint,
	device_major		text,
	device_minor		text,
	device_name			text,
	device_readsector	bigint,
	device_readtime		bigint,
	device_writesector	bigint,
	device_writetime	bigint,
	device_ioqueue		bigint,
	device_iototaltime	bigint,
	device_rsps_max		float8,
	device_wsps_max		float8,
	overflow_drs		smallint,
	overflow_drt		smallint,
	overflow_dws		smallint,
	overflow_dwt		smallint,
	overflow_dit		smallint,
	device_tblspaces	name[],
	PRIMARY KEY (snapid, device_major, device_minor),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.loadavg
(
	snapid			bigint,
	loadavg1		real,
	loadavg5		real,
	loadavg15		real,
	PRIMARY KEY (snapid),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.memory
(
	snapid		bigint,
	memfree		bigint,
	buffers		bigint,
	cached		bigint,
	swap		bigint,
	dirty		bigint,
	PRIMARY KEY (snapid),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.wait_sampling (
	snapid			bigint,
	dbid			oid,
	userid			oid,
	queryid			bigint,
	backend_type	text,
	event_type		text,
	event			text,
	count			bigint,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE INDEX statsrepo_wait_sampling_idx ON statsrepo.wait_sampling(snapid);

CREATE TABLE statsrepo.profile
(
	snapid			bigint,
	processing		text,
	execute			bigint,
	total_exec_time	double precision,
	PRIMARY KEY (snapid, processing),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.lock
(
	snapid				bigint,
	dbid				oid,
	nspname				name,
	relname				name,
	blocker_appname		text,
	blocker_addr		inet,
	blocker_hostname	text,
	blocker_port		integer,
	blockee_pid			integer,
	blocker_pid			integer,
	blocker_gid			text,
	wait_event_type		text,
	wait_event			text,
	duration			interval,
	blockee_query		text,
	blocker_query		text,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.bgwriter
(
	snapid					bigint,
	buffers_clean			bigint,
	maxwritten_clean		bigint,
	buffers_backend			bigint,
	buffers_backend_fsync	bigint,
	buffers_alloc			bigint,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.replication
(
	snapid				bigint,
	procpid				integer,
	usesysid			oid,
	usename				name,
	application_name	text,
	client_addr			inet,
	client_hostname		text,
	client_port			integer,
	backend_start		timestamptz,
	backend_xmin		xid,
	state				text,
	current_location	text,
	sent_location		text,
	write_location		text,
	flush_location		text,
	replay_location		text,
	write_lag			interval,
	flush_lag			interval,
	replay_lag			interval,
	sync_priority		integer,
	sync_state			text,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.replication_slots
(
	snapid					bigint,
	slot_name				name,
	plugin					name,
	slot_type				text,
	datoid					oid,
	temporary				boolean,
	active					boolean,
	active_pid				integer,
	xact_xmin				xid,
	catalog_xmin			xid,
	restart_lsn				pg_lsn,
	confirmed_flush_lsn		pg_lsn,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.stat_replication_slots
(
	snapid					bigint,
	slot_name				text,
	spill_txns				bigint,
	spill_count				bigint,
	spill_bytes				bigint,
	stream_txns 			bigint,
	stream_count			bigint,
	stream_bytes			bigint,
	total_txns				bigint,
	total_bytes				bigint,
	stats_reset				timestamp with time zone,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.stat_io
(
	snapid					bigint,
	backend_type			text,
	object					text,
	context					text,
	reads					bigint,
	read_time				double precision,
	writes					bigint,
	write_time				double precision,
	writebacks				bigint,
	writeback_time			double precision,
	extends					bigint,
	extend_time				double precision,
	op_bytes				bigint,
	hits					bigint,
	evictions				bigint,
	reuses					bigint,
	fsyncs					bigint,
	fsync_time				double precision,
	stats_reset				timestamp with time zone,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.stat_wal
(
	snapid					bigint,
	wal_records				bigint,
	wal_fpi					bigint,
	wal_bytes				numeric,
	wal_buffers_full		bigint,
	wal_write				bigint,
	wal_sync				bigint,
	wal_write_time			double precision,
	wal_sync_time			double precision,
	stats_reset				timestamp with time zone,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.publication
(
	snapid			bigint,
	dbid			oid,
	pubid			oid,
	pubname			name,
	pubowner		oid,
	puballtables	boolean,
	pubinsert		boolean,
	pubupdate		boolean,
	pubdelete		boolean,
	PRIMARY KEY (snapid, dbid, pubid),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.publication_tables
(
	snapid			bigint,
	dbid			oid,
	pubname			name,
	schemaname		name,
	tablename		name,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.subscription
(
	snapid					bigint,
	subid					oid,
	subdbid					oid,
	subname					name,
	subowner				oid,
	subenabled				boolean,
	subconninfo				text,
	subslotname				name,
	subsynccommit			text,
	subpublications			text[],
	PRIMARY KEY (snapid, subid),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.stat_subscription
(
	snapid					bigint,
	subid					oid,
	subname					name,
	pid						integer,
	relid					oid,
	received_lsn			pg_lsn,
	last_msg_send_time		timestamptz,
	last_msg_receipt_time	timestamptz,
	latest_end_lsn			pg_lsn,
	latest_end_time			timestamptz,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.xlog
(
	snapid		bigint,
	location	pg_lsn,
	xlogfile	text,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.alert_message
(
	snapid		bigint,
	message		text,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.archive
(
	snapid				bigint,
	archived_count		bigint,
	last_archived_wal	text,
	last_archived_time	timestamptz,
	failed_count		bigint,
	last_failed_wal		text,
	last_failed_time	timestamptz,
	stats_reset			timestamptz,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.log
(
	instid				bigint,
	timestamp			timestamptz,
	username			text,
	database			text,
	pid					integer,
	client_addr			text,
	session_id			text,
	session_line_num	bigint,
	ps_display			text,
	session_start		timestamptz,
	vxid				text,
	xid					bigint,
	elevel				statsrepo.elevel,
	sqlstate			text,
	message				text,
	detail				text,
	hint				text,
	query				text,
	query_pos			integer,
	context				text,
	user_query			text,
	user_query_pos		integer,
	location			text,
	application_name	text,
	backend_type		text,
	leader_pid			integer,
	query_id			bigint,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.rusage
(
    snapid           bigint,
    dbid             oid,
    userid           oid,
    queryid          bigint,
    plan_reads       bigint,
    plan_writes      bigint,
    plan_user_time   double precision,
    plan_system_time double precision,
    plan_minflts     bigint,
    plan_majflts     bigint,
    plan_nvcsws      bigint,
    plan_nivcsws     bigint,
    exec_reads       bigint,
    exec_writes      bigint,
    exec_user_time   double precision,
    exec_system_time double precision,
    exec_minflts     bigint,
    exec_majflts     bigint,
    exec_nvcsws      bigint,
    exec_nivcsws     bigint,
    FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
    FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid)
);
CREATE INDEX statsrepo_rusage_idx ON statsrepo.rusage(snapid, dbid);

CREATE TABLE statsrepo.cpuinfo
(
	instid				bigint,
	timestamp			timestamptz,
	vendor_id			text,
	model_name			text,
	cpu_mhz				real,
	processors			integer,
	threads_per_core	integer,
	cores_per_socket	integer,
	sockets				integer,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE,
	UNIQUE (instid, timestamp, vendor_id, model_name, processors, sockets)
);

CREATE TABLE statsrepo.meminfo
(
	instid				bigint,
	timestamp			timestamptz,
	mem_total			bigint,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE,
	UNIQUE (instid, timestamp, mem_total)
);

CREATE TABLE statsrepo.ht_info
(
	snapid				bigint,
	statements_dealloc	bigint,
	statements_reset	timestamptz,
	wait_sampling_dealloc	bigint,
	wait_sampling_reset	timestamptz,
	rusage_dealloc		bigint,
	rusage_reset		timestamptz,
	PRIMARY KEY (snapid),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);


-- del_snapshot(snapid) - delete the specified snapshot.
CREATE FUNCTION statsrepo.del_snapshot(bigint) RETURNS void AS
$$
	DELETE FROM statsrepo.snapshot WHERE snapid = $1;
$$
LANGUAGE sql;

-- del_snapshot(time) - delete snapshots older than the specified timestamp.
CREATE FUNCTION statsrepo.del_snapshot(timestamptz) RETURNS void AS
$$
	DELETE FROM statsrepo.snapshot WHERE time < $1;
	DELETE FROM statsrepo.autovacuum WHERE start < (SELECT pg_catalog.min(time) FROM statsrepo.snapshot);
	DELETE FROM statsrepo.autoanalyze WHERE start < (SELECT pg_catalog.min(time) FROM statsrepo.snapshot);
	DELETE FROM statsrepo.checkpoint WHERE start < (SELECT pg_catalog.min(time) FROM statsrepo.snapshot);
	DELETE FROM statsrepo.autovacuum_cancel WHERE timestamp < (SELECT pg_catalog.min(time) FROM statsrepo.snapshot);
	DELETE FROM statsrepo.autoanalyze_cancel WHERE timestamp < (SELECT pg_catalog.min(time) FROM statsrepo.snapshot);
$$
LANGUAGE sql;

-- del_repolog(time) - delete logs older than the specified timestamp.
CREATE FUNCTION statsrepo.del_repolog(timestamptz) RETURNS void AS
$$
	DELETE FROM statsrepo.log WHERE timestamp < $1;
$$
LANGUAGE sql;

------------------------------------------------------------------------------
-- utility function for reporter.
------------------------------------------------------------------------------

-- get_version() - version of statsrepo schema
CREATE FUNCTION statsrepo.get_version() RETURNS text AS
'SELECT CAST(''160000'' AS TEXT)'
LANGUAGE sql IMMUTABLE;

-- tps() - transaction per seconds
CREATE FUNCTION statsrepo.tps(numeric, interval) RETURNS numeric AS
'SELECT (CASE WHEN extract(epoch FROM $2) > 0 THEN $1 / extract(epoch FROM $2) ELSE 0 END)::numeric(1000, 3)'
LANGUAGE sql IMMUTABLE STRICT;

-- div() - NULL-safe operator /
CREATE FUNCTION statsrepo.div(numeric, numeric) RETURNS numeric AS
'SELECT (CASE WHEN $2 > 0 THEN $1 / $2 ELSE 0 END)::numeric(1000, 3)'
LANGUAGE sql IMMUTABLE STRICT;

-- sub() - NULL-safe operator -
CREATE FUNCTION statsrepo.sub(anyelement, anyelement) RETURNS anyelement AS
'SELECT coalesce($1, 0) - coalesce($2, 0)'
LANGUAGE sql;

-- xid_sub() - subtraction xid8
CREATE or replace FUNCTION statsrepo.xid_sub(xid8, xid8) RETURNS numeric AS
'SELECT $1::text::numeric - $2::text::numeric'
LANGUAGE sql;

-- np_sub() - Similar to sub(), but if result is a negative , return first arg.
-- This is used by calculate "accumulate-val - lag(accumulate-val, 1)" on window function.
-- The difference of cumulative values according to the time series will not be negative.
-- If it becomes a negative number, it is treated as a positive number
-- because it reappears in that time series.
CREATE FUNCTION statsrepo.np_sub(anyelement, anyelement) RETURNS anyelement AS
'SELECT CASE WHEN (coalesce($1, 0) - coalesce($2, 0)) < 0 THEN coalesce($1, 0) ELSE (coalesce($1, 0) - coalesce($2, 0)) END'
LANGUAGE sql;

-- convert_hex() - convert a hexadecimal string to a decimal number
CREATE FUNCTION statsrepo.convert_hex(text)
RETURNS bigint AS
$$
	SELECT
		(pg_catalog.sum((16::numeric ^ (pg_catalog.length($1) - i)) *
			pg_catalog.strpos('123456789ABCDEF', pg_catalog.upper(pg_catalog.substr($1, i, 1)))))::bigint
	FROM
		pg_catalog.generate_series(1, pg_catalog.length($1)) AS t(i);
$$
LANGUAGE sql IMMUTABLE STRICT;

-- pg_size_pretty() - formatting with size units
CREATE FUNCTION statsrepo.pg_size_pretty(bigint)
RETURNS text AS
$$
DECLARE
	size	bigint := $1;
	buf		text;
	limit1	bigint := 10 * 1024;
	limit2	bigint := limit1 * 2 - 1;
BEGIN
	IF size < limit1 THEN
		buf := size || ' bytes';
	ELSE
		size := size >> 9;	/* keep one extra bit for rounding */
		IF size < limit2 THEN
			buf := (size + 1) / 2 || ' KiB';
		ELSE
			size := size >> 10;
			IF size < limit2 THEN
				buf := (size + 1) / 2 || ' MiB';
			ELSE
				size := size >> 10;
				IF size < limit2 THEN
					buf := (size + 1) / 2 || ' GiB';
				ELSE
					size := size >> 10;
					buf := (size + 1) / 2 || ' TiB';
				END IF;
			END IF;
		END IF;
	END IF;

	RETURN buf;
END;
$$
LANGUAGE plpgsql IMMUTABLE STRICT;

-- array_unique() - eliminate duplicate array values
CREATE FUNCTION statsrepo.array_unique(anyarray) RETURNS anyarray AS
'SELECT pg_catalog.array_agg(DISTINCT i ORDER BY i) FROM pg_catalog.unnest($1) AS t(i)'
LANGUAGE sql;

-- array_accum - array concatenation aggregate function
CREATE AGGREGATE statsrepo.array_accum(anycompatiblearray)
(
	sfunc = array_cat,
	stype = anycompatiblearray,
	initcond = '{}'
);

-- tables - pre-JOINed tables
CREATE VIEW statsrepo.tables AS
  SELECT t.snapid,
         d.name AS database,
         s.name AS schema,
         t.name AS table,
         t.dbid,
         t.tbl,
         t.nsp,
         t.tbs,
         t.toastrelid,
         t.toastidxid,
         t.relkind,
         t.reltuples,
         t.reloptions,
         t.size,
         t.seq_scan,
         t.seq_tup_read,
         t.idx_scan,
         t.idx_tup_fetch,
         t.n_tup_ins,
         t.n_tup_upd,
         t.n_tup_del,
         t.n_tup_hot_upd,
         t.n_live_tup,
         t.n_dead_tup,
         t.n_mod_since_analyze,
         t.heap_blks_read,
         t.heap_blks_hit,
         t.idx_blks_read,
         t.idx_blks_hit,
         t.toast_blks_read,
         t.toast_blks_hit,
         t.tidx_blks_read,
         t.tidx_blks_hit,
         t.last_vacuum,
         t.last_autovacuum,
         t.last_analyze,
         t.last_autoanalyze,
         t.relpages
  FROM statsrepo.database d,
       statsrepo.schema s,
       statsrepo.table t
 WHERE d.snapid = t.snapid
   AND s.snapid = t.snapid
   AND s.nsp = t.nsp
   AND d.dbid = t.dbid
   AND s.dbid = t.dbid;

-- indexes - pre-JOINed indexes
CREATE VIEW statsrepo.indexes AS
  SELECT i.snapid,
         d.name AS database,
         s.name AS schema,
         t.name AS table,
         i.name AS index,
         i.dbid,
         i.idx,
         i.tbl,
         i.tbs,
         i.relpages,
         i.reltuples,
         i.reloptions,
         i.isunique,
         i.isprimary,
         i.isclustered,
         i.isvalid,
         i.size,
         i.indkey,
         i.indexdef,
         i.idx_scan,
         i.idx_tup_read,
         i.idx_tup_fetch,
         i.idx_blks_read,
         i.idx_blks_hit
    FROM statsrepo.database d,
         statsrepo.schema s,
         statsrepo.table t,
         statsrepo.index i
   WHERE d.snapid = i.snapid
     AND s.snapid = i.snapid
     AND t.snapid = i.snapid
     AND i.tbl = t.tbl
     AND t.nsp = s.nsp
     AND i.dbid = d.dbid
     AND s.dbid = d.dbid;

-- function to check fillfactor 
CREATE FUNCTION statsrepo.pg_fillfactor(reloptions text[], relam OID)
RETURNS integer AS
$$
SELECT (pg_catalog.regexp_matches(pg_catalog.array_to_string($1, '/'),
        'fillfactor=([0-9]+)'))[1]::integer AS fillfactor
UNION ALL
SELECT CASE $2
       WHEN    0 THEN 100 -- heap
       WHEN  403 THEN  90 -- btree
       WHEN  405 THEN  70 -- hash
       WHEN  783 THEN  90 -- gist
       WHEN 2742 THEN 100 -- gin
       END
LIMIT 1;
$$
LANGUAGE sql STABLE;

-- get min snapshot id from date
CREATE FUNCTION statsrepo.get_min_snapid(
	IN  m_host text,
	IN  m_port text,
	IN  b_date timestamp(0),
	IN  e_date timestamp(0),
	OUT snapid bigint
) RETURNS bigint AS
$$
	SELECT
		pg_catalog.min(snapid)
	FROM statsrepo.snapshot s
		LEFT JOIN  statsrepo.instance i ON i.instid = s.instid
	WHERE i.hostname = $1 AND i.port = $2::integer
		AND time >= $3 AND time <= $4
$$
LANGUAGE sql STABLE;

-- get max snapshot id from date
CREATE FUNCTION statsrepo.get_max_snapid(
	IN  m_host text,
	IN  m_port text,
	IN  b_date timestamp(0),
	IN  e_date timestamp(0),
	OUT snapid bigint
) RETURNS bigint AS
$$
	SELECT
		pg_catalog.max(snapid)
	FROM statsrepo.snapshot s
		LEFT JOIN  statsrepo.instance i ON i.instid = s.instid
	WHERE i.hostname = $1 AND i.port = $2::integer
		AND time >= $3 AND time <= $4
$$
LANGUAGE sql STABLE;

-- get min snapshot id from date
CREATE FUNCTION statsrepo.get_min_snapid2(
	IN  instid text,
	IN  b_date timestamp(0),
	IN  e_date timestamp(0),
	OUT snapid bigint
) RETURNS bigint AS
$$
	SELECT
		pg_catalog.min(snapid)
	FROM statsrepo.snapshot s
		LEFT JOIN  statsrepo.instance i ON i.instid = s.instid
	WHERE i.instid = $1::integer
		AND time >= $2 AND time <= $3
$$
LANGUAGE sql STABLE;

-- get max snapshot id from date
CREATE FUNCTION statsrepo.get_max_snapid2(
	IN  instid text,
	IN  b_date timestamp(0),
	IN  e_date timestamp(0),
	OUT snapid bigint
) RETURNS bigint AS
$$
	SELECT
		pg_catalog.max(snapid)
	FROM statsrepo.snapshot s
		LEFT JOIN  statsrepo.instance i ON i.instid = s.instid
	WHERE i.instid = $1::integer
		AND time >= $2 AND time <= $3
$$
LANGUAGE sql STABLE;

-- generate information that corresponds to 'Summary'
CREATE FUNCTION statsrepo.get_summary(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT instname		text,
	OUT hostname		text,
	OUT port			integer,
	OUT pg_version		text,
	OUT snap_begin		timestamp,
	OUT snap_end		timestamp,
	OUT duration		interval,
	OUT total_dbsize	text,
	OUT total_commits	numeric,
	OUT total_rollbacks	numeric
) RETURNS SETOF record AS
$$
	SELECT
		i.name,
		i.hostname,
		i.port,
		i.pg_version,
		b.time::timestamp(0),
		e.time::timestamp(0),
		(e.time - b.time)::interval(0),
		d.*
	FROM
		statsrepo.instance i,
		statsrepo.snapshot b,
		statsrepo.snapshot e,
		(SELECT
			statsrepo.pg_size_pretty(pg_catalog.sum(ed.size)::int8),
			pg_catalog.sum(ed.xact_commit) - pg_catalog.sum(sd.xact_commit),
			pg_catalog.sum(ed.xact_rollback) - pg_catalog.sum(sd.xact_rollback)
		 FROM
		 	statsrepo.database sd,
			statsrepo.database ed
		 WHERE
		 	sd.snapid = $1
			AND ed.snapid = $2
			AND sd.dbid = ed.dbid) AS d
	WHERE
		i.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		AND b.snapid = $1
		AND e.snapid = $2;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Database Statistics'
CREATE FUNCTION statsrepo.get_dbstats(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT datname				name,
	OUT size				bigint,
	OUT size_incr			bigint,
	OUT xact_commit_tps		numeric,
	OUT xact_rollback_tps	numeric,
	OUT blks_hit_rate		numeric,
	OUT blks_hit_tps		numeric,
	OUT blks_read_tps		numeric,
	OUT tup_fetch_tps		numeric,
	OUT temp_files			bigint,
	OUT temp_bytes			bigint,
	OUT deadlocks			bigint,
	OUT blk_read_time		numeric,
	OUT blk_write_time		numeric
) RETURNS SETOF record AS
$$
	SELECT
		ed.name,
		ed.size / 1024 / 1024,
		statsrepo.sub(ed.size, sd.size) / 1024 / 1024,
		statsrepo.tps(
			statsrepo.sub(ed.xact_commit, sd.xact_commit),
			es.time - ss.time),
		statsrepo.tps(
			statsrepo.sub(ed.xact_rollback, sd.xact_rollback),
			es.time - ss.time),
		(statsrepo.div(
			statsrepo.sub(ed.blks_hit, sd.blks_hit),
			statsrepo.sub(ed.blks_read, sd.blks_read) +
			statsrepo.sub(ed.blks_hit, sd.blks_hit)) * 100)::numeric(1000, 1),
		statsrepo.tps(
			statsrepo.sub(ed.blks_read, sd.blks_read) +
			statsrepo.sub(ed.blks_hit, sd.blks_hit),
			es.time - ss.time),
		statsrepo.tps(
			statsrepo.sub(ed.blks_read, sd.blks_read),
		es.time - ss.time),
		statsrepo.tps(
			statsrepo.sub(ed.tup_returned, sd.tup_returned) +
			statsrepo.sub(ed.tup_fetched, sd.tup_fetched),
			es.time - ss.time),
		statsrepo.sub(ed.temp_files, sd.temp_files),
		statsrepo.sub(ed.temp_bytes, sd.temp_bytes) / 1024 / 1024,
		statsrepo.sub(ed.deadlocks, sd.deadlocks),
		statsrepo.sub(ed.blk_read_time, sd.blk_read_time)::numeric(1000, 3),
		statsrepo.sub(ed.blk_write_time, sd.blk_write_time)::numeric(1000, 3)
	FROM
		statsrepo.snapshot ss,
		statsrepo.snapshot es,
		statsrepo.database ed LEFT JOIN statsrepo.database sd
			ON sd.snapid = $1 AND sd.dbid = ed.dbid
	WHERE
		ss.snapid = $1
		AND es.snapid = $2
		AND es.snapid = ed.snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Transaction Statistics'
CREATE FUNCTION statsrepo.get_xact_tendency_report(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT "timestamp"		text,
	OUT datname			name,
	OUT commit_tps		numeric,
	OUT rollback_tps	numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(time, 'YYYY-MM-DD HH24:MI'),
		name,
		coalesce(statsrepo.tps(xact_commit, duration), 0)::numeric(1000,3),
		coalesce(statsrepo.tps(xact_rollback, duration), 0)::numeric(1000,3)
	FROM
		(SELECT
			snapid,
			time,
			name,
			CASE WHEN xact_commit >= 0 THEN xact_commit ELSE 0 END AS xact_commit,
			CASE WHEN xact_rollback >= 0 THEN xact_rollback ELSE 0 END AS xact_rollback,
			duration
		FROM
			(SELECT
				snapid,
				time,
				name,
				xact_commit - pg_catalog.lag(xact_commit) OVER w AS xact_commit,
				xact_rollback - pg_catalog.lag(xact_rollback) OVER w AS xact_rollback,
				time - pg_catalog.lag(time) OVER w AS duration
			FROM
				(SELECT
					s.snapid,
					s.time,
					d.name,
					pg_catalog.sum(xact_commit) AS xact_commit,
					pg_catalog.sum(xact_rollback) AS xact_rollback
				 FROM
					statsrepo.database d,
					statsrepo.snapshot s
				 WHERE
				 	d.snapid = s.snapid
				 	AND d.snapid BETWEEN $1 AND $2
					AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
				 GROUP BY
				 	s.snapid, s.time, d.name) AS d
			WINDOW w AS (PARTITION BY name ORDER BY snapid)
			ORDER BY
				snapid, name
		) t
	) t1
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Database Size'
CREATE FUNCTION statsrepo.get_dbsize_tendency_report(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT "timestamp"	text,
	OUT datname		name,
	OUT size		numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(s.time, 'YYYY-MM-DD HH24:MI'),
		d.name,
		(pg_catalog.sum(size) / 1024 / 1024)::numeric(1000, 3)
	FROM
		statsrepo.database d,
		statsrepo.snapshot s
	WHERE
		d.snapid BETWEEN $1 AND $2
		AND d.snapid = s.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	GROUP BY
		d.snapid, d.name, s.time
	ORDER BY
		d.snapid, d.name;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Database Resource Usage (rusdage)'
CREATE FUNCTION statsrepo.get_db_rusage_report(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT datname	name,
	OUT plan_reads	bigint,
	OUT plan_writes	bigint,
	OUT plan_utime	numeric,
	OUT plan_stime	numeric,
	OUT exec_reads	bigint,
	OUT exec_writes	bigint,
	OUT exec_utime	numeric,
	OUT exec_stime	numeric
) RETURNS SETOF record AS
$$
	SELECT
		d.name,
		sum(plan_reads) as plan_reads,
 		sum(plan_writes) as plan_writes,
		sum(plan_user_time)::numeric(12,6) as plan_user_time,
		sum(plan_system_time)::numeric(12,6) as plan_system_time,
		sum(exec_reads) as exec_reads,
		sum(exec_writes) as exec_writes,
		sum(exec_user_time)::numeric(12,6) as exec_user_time,
		sum(exec_system_time)::numeric(12,6) as exec_system_time
	FROM
		(SELECT dbid, name FROM statsrepo.database WHERE snapid = $2) d
		LEFT JOIN
		(
		SELECT  snapid, dbid, userid, queryid, 
			statsrepo.np_sub(plan_reads, lag(plan_reads,  1) OVER w) AS plan_reads,
			statsrepo.np_sub(plan_writes, lag(plan_writes, 1) OVER w) AS plan_writes,
			statsrepo.np_sub(plan_user_time, lag(plan_user_time, 1) OVER w) AS plan_user_time ,
			statsrepo.np_sub(plan_system_time, lag(plan_system_time, 1) OVER w) AS plan_system_time,
			statsrepo.np_sub(exec_reads, lag(exec_reads,  1) OVER w) AS exec_reads,
 			statsrepo.np_sub(exec_writes, lag(exec_writes, 1) OVER w) AS exec_writes,
			statsrepo.np_sub(exec_user_time, lag(exec_user_time, 1) OVER w) AS exec_user_time,
			statsrepo.np_sub(exec_system_time, lag(exec_system_time, 1) OVER w) AS exec_system_time
		FROM
			statsrepo.rusage WHERE snapid BETWEEN $1 AND $2 WINDOW w AS (PARTITION BY dbid, userid, queryid ORDER BY snapid)
		)r
	ON d.dbid = r.dbid
	-- Since calcluate cur-val - lag-val, we use "$1 + 1" snapid (which include diff $1 and $1 + 1)
	WHERE snapid BETWEEN $1 + 1 AND $2
	GROUP BY d.name;
$$
LANGUAGE sql;



-- generate information that corresponds to 'Replication Slots Statistics'
CREATE FUNCTION statsrepo.get_stat_replication_slots_report(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT slot_name			text,
	OUT slot_type			text,
	OUT slot_datname		text,
	OUT spill_txns			bigint,
	OUT spill_count			bigint,
	OUT spill_bytes			bigint,
	OUT stream_txns			bigint,
	OUT stream_count		bigint,
	OUT stream_bytes		bigint,
	OUT total_txns			bigint,
	OUT total_bytes			bigint,
	OUT stats_reset			text
) RETURNS SETOF record AS
$$
	SELECT
		e.slot_name,
		e.slot_type,
		e.name,
		(CASE WHEN e.stats_reset = f.stats_reset THEN e.spill_txns   - f.spill_txns   WHEN e.stats_reset IS NULL THEN e.spill_txns   - COALESCE(f.spill_txns  ,0) ELSE e.spill_txns   END),
		(CASE WHEN e.stats_reset = f.stats_reset THEN e.spill_count  - f.spill_count  WHEN e.stats_reset IS NULL THEN e.spill_count  - COALESCE(f.spill_count ,0) ELSE e.spill_count  END),
		(CASE WHEN e.stats_reset = f.stats_reset THEN e.spill_bytes  - f.spill_bytes  WHEN e.stats_reset IS NULL THEN e.spill_bytes  - COALESCE(f.spill_bytes ,0) ELSE e.spill_bytes  END),
		(CASE WHEN e.stats_reset = f.stats_reset THEN e.stream_txns  - f.stream_txns  WHEN e.stats_reset IS NULL THEN e.stream_txns  - COALESCE(f.stream_txns ,0) ELSE e.stream_txns  END),
		(CASE WHEN e.stats_reset = f.stats_reset THEN e.stream_count - f.stream_count WHEN e.stats_reset IS NULL THEN e.stream_count - COALESCE(f.stream_count,0) ELSE e.stream_count END),
		(CASE WHEN e.stats_reset = f.stats_reset THEN e.stream_bytes - f.stream_bytes WHEN e.stats_reset IS NULL THEN e.stream_bytes - COALESCE(f.stream_bytes,0) ELSE e.stream_bytes END),
		(CASE WHEN e.stats_reset = f.stats_reset THEN e.total_txns   - f.total_txns   WHEN e.stats_reset IS NULL THEN e.total_txns   - COALESCE(f.total_txns  ,0) ELSE e.total_txns   END),
		(CASE WHEN e.stats_reset = f.stats_reset THEN e.total_bytes  - f.total_bytes  WHEN e.stats_reset IS NULL THEN e.total_bytes  - COALESCE(f.total_bytes ,0) ELSE e.total_bytes  END),
		e.stats_reset
	FROM
		(SELECT
			a.slot_name,
			c.slot_type,
			g.name,
			a.spill_txns,
			a.spill_count,
			a.spill_bytes,
			a.stream_txns,
			a.stream_count,
			a.stream_bytes,
			a.total_txns,
			a.total_bytes,
			a.stats_reset
		FROM
			statsrepo.stat_replication_slots a
			LEFT JOIN statsrepo.replication_slots c ON a.slot_name = c.slot_name AND a.snapid = c.snapid
			LEFT JOIN statsrepo.database g ON c.datoid = g.dbid AND a.snapid = g.snapid
		WHERE
		 	a.snapid = $2) e
		LEFT JOIN
			(SELECT
				b.slot_name,
				d.slot_type,
				h.name,
				b.spill_txns,
				b.spill_count,
				b.spill_bytes,
				b.stream_txns,
				b.stream_count,
				b.stream_bytes,
				b.total_txns,
				b.total_bytes,
				b.stats_reset
			FROM
				statsrepo.stat_replication_slots b
				LEFT JOIN statsrepo.replication_slots d ON b.slot_name = d.slot_name AND b.snapid = d.snapid
				LEFT JOIN statsrepo.database h ON d.datoid = h.dbid AND b.snapid = h.snapid
			WHERE
				b.snapid = $1) f
			ON e.slot_name = f.slot_name
	ORDER BY
		11 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Recovery Conflicts'
CREATE FUNCTION statsrepo.get_recovery_conflicts(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT datname				name,
	OUT confl_tablespace	bigint,
	OUT confl_lock			bigint,
	OUT confl_snapshot		bigint,
	OUT confl_bufferpin		bigint,
	OUT confl_deadlock		bigint
) RETURNS SETOF record AS
$$
	SELECT
		de.name,
		statsrepo.sub(de.confl_tablespace, db.confl_tablespace),
		statsrepo.sub(de.confl_lock, db.confl_lock),
		statsrepo.sub(de.confl_snapshot, db.confl_snapshot),
		statsrepo.sub(de.confl_bufferpin, db.confl_bufferpin),
		statsrepo.sub(de.confl_deadlock, db.confl_deadlock)
	FROM
		statsrepo.database de LEFT JOIN statsrepo.database db
			ON db.dbid = de.dbid AND db.snapid = $1
	WHERE
		de.snapid = $2
	ORDER BY
		statsrepo.sub(de.confl_tablespace, db.confl_tablespace) +
			statsrepo.sub(de.confl_lock, db.confl_lock) +
			statsrepo.sub(de.confl_snapshot, db.confl_snapshot) +
			statsrepo.sub(de.confl_bufferpin, db.confl_bufferpin) +
			statsrepo.sub(de.confl_deadlock, db.confl_deadlock) DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Instance Processes ratio' for pg_stats_reporter
CREATE FUNCTION statsrepo.get_proc_ratio(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT idle			numeric,
	OUT idle_in_xact	numeric,
	OUT waiting			numeric,
	OUT running			numeric
) RETURNS SETOF record AS
$$
	SELECT
		CASE WHEN pg_catalog.sum(total)::float4 = 0 THEN 0
			ELSE (100 * pg_catalog.sum(idle)::float / pg_catalog.sum(total)::float4)::numeric(5,1) END,
		CASE WHEN pg_catalog.sum(total)::float4 = 0 THEN 0
			ELSE (100 * pg_catalog.sum(idle_in_xact)::float / pg_catalog.sum(total)::float4)::numeric(5,1) END,
		CASE WHEN pg_catalog.sum(total)::float4 = 0 THEN 0
			ELSE (100 * pg_catalog.sum(waiting)::float / pg_catalog.sum(total)::float4)::numeric(5,1) END,
		CASE WHEN pg_catalog.sum(total)::float4 = 0 THEN 0
			ELSE (100 * pg_catalog.sum(running)::float / pg_catalog.sum(total)::float4)::numeric(5,1) END
	FROM 
		(SELECT
			snapid,
			idle,
			idle_in_xact,
			waiting, running,
			idle + idle_in_xact + waiting + running AS total
		 FROM
		 	statsrepo.activity) a,
		statsrepo.snapshot s
	WHERE
		a.snapid BETWEEN $1 AND $2
		AND a.snapid = s.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
$$
LANGUAGE sql;

-- generate information that corresponds to 'Instance Processes'
CREATE FUNCTION statsrepo.get_proc_tendency_report(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT "timestamp"			text,
	OUT idle				integer,
	OUT idle_per			numeric,
	OUT idle_in_xact		integer,
	OUT idle_in_xact_per	numeric,
	OUT waiting				integer,
	OUT waiting_per			numeric,
	OUT running				integer,
	OUT running_per			numeric
) RETURNS SETOF record AS
$$
SELECT
	pg_catalog.to_char(time, 'YYYY-MM-DD HH24:MI'),
	CASE WHEN (lag / interval) = 0 THEN idle ELSE idle / (lag / interval) END AS idle,
	CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
		ELSE (100.0 * idle / (idle + idle_in_xact + waiting + running))::numeric(5,1) END AS idle_per,
	CASE WHEN (lag / interval) = 0 THEN idle_in_xact ELSE idle_in_xact / (lag / interval) END AS idle_in_xact,
	CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
		ELSE (100.0 * idle_in_xact / (idle + idle_in_xact + waiting + running))::numeric(5,1) END AS idle_in_xact_per,
	CASE WHEN (lag / interval) = 0 THEN waiting ELSE waiting / (lag / interval) END AS waiting,
	CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
		ELSE (100.0 * waiting / (idle + idle_in_xact + waiting + running))::numeric(5,1) END AS waiting_per,
	CASE WHEN (lag / interval) = 0 THEN running ELSE running / (lag / interval) END AS running,
	CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
		ELSE (100.0 * running / (idle + idle_in_xact + waiting + running))::numeric(5,1) END AS running_per
FROM
(
	SELECT
		a.snapid, s.time, a.idle, a.idle_in_xact, a.waiting, a.running,
		CASE WHEN s.lag_t IS NULL THEN 600 ELSE lag_t END as lag,
		CASE WHEN set.setting IS NULL THEN 5 ELSE setting::int END as interval
	FROM
		statsrepo.activity a
		JOIN
		(SELECT snapid, instid, time,  EXTRACT(EPOCH FROM ((time - lag(time, 1) OVER (ORDER BY snapid))))::int as lag_t
			FROM statsrepo.snapshot WHERE snapid BETWEEN $1 - 1 AND $2
		) s
		ON a.snapid = s.snapid
		LEFT JOIN
		(SELECT snapid, setting 
			FROM statsrepo.setting WHERE name = 'pg_statsinfo.sampling_interval'
		) set
		ON a.snapid = set.snapid
	WHERE
	a.snapid BETWEEN $1 AND $2
	AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	AND idle IS NOT NULL
)t
ORDER BY
	snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'WAL Statistics'
CREATE FUNCTION statsrepo.get_wal_tendency(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT "timestamp"			text,
	OUT location			pg_lsn,
	OUT walfile			text,
	OUT write_size			numeric,
	OUT write_size_per_sec	numeric,
	OUT last_archived_wal	text,
	OUT archive_count		bigint,
	OUT archive_failed		bigint
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(time, 'YYYY-MM-DD HH24:MI'),
		location,
		xlogfile,
		(write_size / 1024 / 1024)::numeric(1000, 3),
		(statsrepo.tps(write_size, duration) / 1024 / 1024)::numeric(1000, 3),
		last_archived_wal,
		archive_count,
		archive_failed
	FROM
	(
		SELECT
			s.snapid,
			s.time,
			x.location,
			x.xlogfile,
			pg_wal_lsn_diff(x.location, pg_catalog.lag(x.location) OVER w) AS write_size,
			s.time - pg_catalog.lag(s.time) OVER w AS duration,
			a.last_archived_wal,
			a.archived_count - pg_catalog.lag(a.archived_count) OVER w AS archive_count,
			a.failed_count - pg_catalog.lag(a.failed_count) OVER w AS archive_failed
		 FROM
			statsrepo.xlog x LEFT JOIN statsrepo.archive a
				ON x.snapid = a.snapid,
			statsrepo.snapshot s,
			statsrepo.instance i
		 WHERE
			x.snapid BETWEEN $1 AND $2
			AND x.snapid = s.snapid
			AND s.instid = i.instid
			AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		 WINDOW w AS (ORDER BY s.snapid)
		 ORDER BY
		 	s.snapid
	) t
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'WAL Statistics'
CREATE FUNCTION statsrepo.get_wal_stats(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT write_total			numeric,
	OUT write_speed			numeric,
	OUT archive_total		numeric,
	OUT archive_failed		numeric,
	OUT last_wal_file		text,
	OUT last_archive_file	text
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.sum(write_size)::numeric(1000, 3),
		pg_catalog.avg(write_size_per_sec)::numeric(1000, 3),
		pg_catalog.sum(archive_count),
		pg_catalog.sum(archive_failed),
		pg_catalog.max(walfile),
		pg_catalog.max(last_archived_wal)
	FROM
		statsrepo.get_wal_tendency($1, $2);
$$
LANGUAGE sql;

CREATE FUNCTION statsrepo.get_stat_wal(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT wal_records			bigint,
	OUT wal_fpi				bigint,
	OUT wal_bytes			numeric,
	OUT wal_buffers_full	bigint,
	OUT wal_write			bigint,
	OUT wal_sync			bigint,
	OUT wal_write_time		numeric,
	OUT wal_sync_time		numeric,
	OUT stats_reset			text
) RETURNS SETOF record AS
$$
	SELECT
		(CASE WHEN a.stats_reset = b.stats_reset THEN a.wal_records - b.wal_records ELSE a.wal_records END),
		(CASE WHEN a.stats_reset = b.stats_reset THEN a.wal_fpi - b.wal_fpi ELSE a.wal_fpi END),
		(CASE WHEN a.stats_reset = b.stats_reset THEN a.wal_bytes - b.wal_bytes ELSE a.wal_bytes END),
		(CASE WHEN a.stats_reset = b.stats_reset THEN a.wal_buffers_full - b.wal_buffers_full ELSE a.wal_buffers_full END),
		(CASE WHEN a.stats_reset = b.stats_reset THEN a.wal_write - b.wal_write ELSE a.wal_write END),
		(CASE WHEN a.stats_reset = b.stats_reset THEN a.wal_sync - b.wal_sync ELSE a.wal_sync END),
		(CASE WHEN a.stats_reset = b.stats_reset THEN a.wal_write_time - b.wal_write_time ELSE a.wal_write_time END),
		(CASE WHEN a.stats_reset = b.stats_reset THEN a.wal_sync_time - b.wal_sync_time ELSE a.wal_sync_time END),
		a.stats_reset
	FROM
		(SELECT
			wal_records,
			wal_fpi,
			wal_bytes,
			wal_buffers_full,
			wal_write,
			wal_sync,
			wal_write_time,
			wal_sync_time,
			stats_reset
		 FROM
		 	statsrepo.stat_wal
		 WHERE
		 	snapid = $1) b,
		(SELECT
			wal_records,
			wal_fpi,
			wal_bytes,
			wal_buffers_full,
			wal_write,
			wal_sync,
			wal_write_time,
			wal_sync_time,
			stats_reset
		 FROM
		 	statsrepo.stat_wal
		 WHERE
		 	snapid = $2) a;
$$
LANGUAGE sql;

-- generate information that corresponds to 'I/O Statistics' for pg_stats_reporter
CREATE FUNCTION statsrepo.get_stat_io(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT "time"				text,
	OUT backend_type		text,
	OUT object				text,
	OUT context				text,
	OUT reads				bigint,
	OUT read_time			double precision,
	OUT writes				bigint,
	OUT write_time			double precision,
	OUT writebacks			bigint,
	OUT writeback_time		double precision,
	OUT extends				bigint,
	OUT extend_time			double precision,
	OUT op_bytes			bigint,
	OUT hits				bigint,
	OUT evictions			bigint,
	OUT reuses				bigint,
	OUT fsyncs				bigint,
	OUT fsync_time			double precision,
	OUT stats_reset			text
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(s.time, 'YYYY-MM-DD HH24:MI'),
		io.backend_type,
		io.object,
		io.context,
		(io.reads - COALESCE(pg_catalog.lag(io.reads) OVER w, 0) ),
		(io.read_time - COALESCE(pg_catalog.lag(io.read_time) OVER w, 0) )::numeric(30,3),
		(io.writes - COALESCE(pg_catalog.lag(io.writes) OVER w, 0) ),
		(io.write_time - COALESCE(pg_catalog.lag(io.write_time) OVER w, 0) )::numeric(30,3),
		(io.writebacks - COALESCE(pg_catalog.lag(io.writebacks) OVER w, 0) ),
		(io.writeback_time - COALESCE(pg_catalog.lag(io.writeback_time) OVER w, 0) )::numeric(30,3),
		(io.extends - COALESCE(pg_catalog.lag(io.extends) OVER w, 0) ),
		(io.extend_time - COALESCE(pg_catalog.lag(io.extend_time) OVER w, 0) )::numeric(30,3),
		op_bytes,
		(io.hits - COALESCE(pg_catalog.lag(io.hits) OVER w, 0) ),
		(io.evictions - COALESCE(pg_catalog.lag(io.evictions) OVER w, 0) ),
		(io.reuses - COALESCE(pg_catalog.lag(io.reuses) OVER w, 0) ),
		(io.fsyncs - COALESCE(pg_catalog.lag(io.fsyncs) OVER w, 0) ),
		(io.fsync_time - COALESCE(pg_catalog.lag(io.fsync_time) OVER w, 0) )::numeric(30,3),
		stats_reset
	FROM
		statsrepo.stat_io io
		LEFT JOIN statsrepo.snapshot s ON io.snapid = s.snapid
	WHERE
		io.snapid BETWEEN $1 AND $2
		AND s.instid = (SELECT instid from statsrepo.snapshot WHERE snapid = $2)
	WINDOW
		w AS (PARTITION BY io.backend_type, io.object, io.context ORDER BY io.snapid)
	ORDER BY
		io.snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'WAL Statistics' for pg_stats_reporter
CREATE FUNCTION statsrepo.get_stat_wal2(
    IN snapid_begin			bigint,
    IN snapid_end			bigint,
    OUT "time"				text,
    OUT wal_fpi				bigint,
    OUT wal_bytes			numeric,
    OUT wal_buffers_full	bigint,
    OUT wal_write			bigint,
    OUT wal_sync			bigint,
    OUT wal_write_time		double precision,
    OUT wal_sync_time		double precision
) RETURNS SETOF record AS
$$
  SELECT
    pg_catalog.to_char(s.time, 'YYYY-MM-DD HH24:MI'),
    (sw.wal_fpi - pg_catalog.lag(sw.wal_fpi) OVER w),
    (sw.wal_bytes - pg_catalog.lag(sw.wal_bytes) OVER w),
    (sw.wal_buffers_full - pg_catalog.lag(sw.wal_buffers_full) OVER w),
    (sw.wal_write - pg_catalog.lag(sw.wal_write) OVER w),
    (sw.wal_sync - pg_catalog.lag(sw.wal_sync) OVER w),
    (sw.wal_write_time - pg_catalog.lag(sw.wal_write_time) OVER w),
    (sw.wal_sync_time - pg_catalog.lag(sw.wal_sync_time) OVER w)
  FROM
    statsrepo.stat_wal sw,
    statsrepo.snapshot s
  WHERE
    sw.snapid BETWEEN $1 AND $2
    AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
    AND sw.snapid = s.snapid
  WINDOW
    w AS (PARTITION BY s.instid ORDER BY sw.snapid)
  ORDER BY
    sw.snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Transaction Increase Tendency'
CREATE FUNCTION statsrepo.get_xid_tendency(
	IN snapid_begin     bigint,
	IN snapid_end       bigint,
	OUT "timestamp"     text,
	OUT xid_increase    numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(time, 'YYYY-MM-DD HH24:MI'),
		statsrepo.xid_sub(xid_current, xid_prev)
	FROM
		( SELECT
			snapid,
			instid,
			time,
			xid_current,
			lag(xid_current) OVER (PARTITION BY instid ORDER BY snapid) AS xid_prev
		  FROM
			statsrepo.snapshot) s
	WHERE
		s.snapid BETWEEN $1 AND $2
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	ORDER BY
		snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'CPU Usage'
CREATE FUNCTION statsrepo.get_cpu_usage_tendency(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT snapid		bigint,
	OUT "user"		numeric,
	OUT system		numeric,
	OUT idle		numeric,
	OUT iowait		numeric
) RETURNS SETOF record AS
$$
	SELECT
		t.snapid,
		(100 * statsrepo.div(t.user, t.total))::numeric(5,1),
		(100 * statsrepo.div(t.system, t.total))::numeric(5,1),
		(100 * statsrepo.div(t.idle, t.total))::numeric(5,1),
		(100 * statsrepo.div(t.iowait, t.total))::numeric(5,1)
	FROM
	(
		SELECT
			c.snapid,
			(CASE WHEN overflow_user = 1 THEN cpu_user + 4294967296 ELSE cpu_user END - pg_catalog.lag(cpu_user) OVER w) AS user,
			(CASE WHEN overflow_system = 1 THEN cpu_system + 4294967296 ELSE cpu_system END - pg_catalog.lag(cpu_system) OVER w) AS system,
			(CASE WHEN overflow_idle = 1 THEN cpu_idle + 4294967296 ELSE cpu_idle END - pg_catalog.lag(cpu_idle) OVER w) AS idle,
			(CASE WHEN overflow_iowait = 1 THEN cpu_iowait + 4294967296 ELSE cpu_iowait END - pg_catalog.lag(cpu_iowait) OVER w) AS iowait,
			(CASE WHEN overflow_user = 1 THEN cpu_user + 4294967296 ELSE cpu_user END +
			 CASE WHEN overflow_system = 1 THEN cpu_system + 4294967296 ELSE cpu_system END +
			 CASE WHEN overflow_idle = 1 THEN cpu_idle + 4294967296 ELSE cpu_idle END +
			 CASE WHEN overflow_iowait = 1 THEN cpu_iowait + 4294967296 ELSE cpu_iowait END) -
			(pg_catalog.lag(cpu_user) OVER w + pg_catalog.lag(cpu_system) OVER w + pg_catalog.lag(cpu_idle) OVER w + pg_catalog.lag(cpu_iowait) OVER w) AS total
		FROM
			statsrepo.cpu c,
			statsrepo.snapshot s
		WHERE
			c.snapid BETWEEN $1 AND $2
			AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			AND s.snapid = c.snapid
		WINDOW w AS (PARTITION BY s.instid ORDER BY c.snapid)
		ORDER BY
			c.snapid
	) t
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'CPU Usage' for pg_stats_reporter
CREATE FUNCTION statsrepo.get_cpu_usage_tendency_report(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT "timestamp"	text,
	OUT "user"		numeric,
	OUT system		numeric,
	OUT idle		numeric,
	OUT iowait		numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(t.time, 'YYYY-MM-DD HH24:MI'),
		(100 * statsrepo.div(t.user, t.total))::numeric(5,1),
		(100 * statsrepo.div(t.system, t.total))::numeric(5,1),
		(100 * statsrepo.div(t.idle, t.total))::numeric(5,1),
		(100 * statsrepo.div(t.iowait, t.total))::numeric(5,1)
	FROM
	(
		SELECT
			c.snapid,
			s.time,
			(CASE WHEN overflow_user = 1 THEN cpu_user + 4294967296 ELSE cpu_user END - pg_catalog.lag(cpu_user) OVER w) AS user,
			(CASE WHEN overflow_system = 1 THEN cpu_system + 4294967296 ELSE cpu_system END - pg_catalog.lag(cpu_system) OVER w) AS system,
			(CASE WHEN overflow_idle = 1 THEN cpu_idle + 4294967296 ELSE cpu_idle END - pg_catalog.lag(cpu_idle) OVER w) AS idle,
			(CASE WHEN overflow_iowait = 1 THEN cpu_iowait + 4294967296 ELSE cpu_iowait END - pg_catalog.lag(cpu_iowait) OVER w) AS iowait,
			(CASE WHEN overflow_user = 1 THEN cpu_user + 4294967296 ELSE cpu_user END +
			 CASE WHEN overflow_system = 1 THEN cpu_system + 4294967296 ELSE cpu_system END +
			 CASE WHEN overflow_idle = 1 THEN cpu_idle + 4294967296 ELSE cpu_idle END +
			 CASE WHEN overflow_iowait = 1 THEN cpu_iowait + 4294967296 ELSE cpu_iowait END) -
			(pg_catalog.lag(cpu_user) OVER w + pg_catalog.lag(cpu_system) OVER w + pg_catalog.lag(cpu_idle) OVER w + pg_catalog.lag(cpu_iowait) OVER w) AS total
		FROM
			statsrepo.cpu c,
			statsrepo.snapshot s
		WHERE
			c.snapid BETWEEN $1 AND $2
			AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			AND s.snapid = c.snapid
		WINDOW w AS (PARTITION BY s.instid ORDER BY c.snapid)
		ORDER BY
			c.snapid
	) t
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'CPU Usage + Load Average'
CREATE FUNCTION statsrepo.get_cpu_loadavg_tendency(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT "timestamp"		text,
	OUT "user"			numeric,
	OUT system			numeric,
	OUT idle			numeric,
	OUT iowait			numeric,
	OUT loadavg1		numeric,
	OUT loadavg5		numeric,
	OUT loadavg15		numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(s.time, 'YYYY-MM-DD HH24:MI'),
		c.user,
		c.system,
		c.idle,
		c.iowait,
		l.loadavg1::numeric(6,3),
		l.loadavg5::numeric(6,3),
		l.loadavg15::numeric(6,3)
	FROM
		statsrepo.get_cpu_usage_tendency($1, $2) c,
		statsrepo.loadavg l,
		statsrepo.snapshot s
	WHERE
		c.snapid = l.snapid
		AND c.snapid = s.snapid
	ORDER BY
		c.snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'IO Usage'
CREATE FUNCTION statsrepo.get_io_usage(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT device_name			text,
	OUT device_tblspaces	name[],
	OUT total_read			bigint,
	OUT total_write			bigint,
	OUT total_read_time		bigint,
	OUT total_write_time	bigint,
	OUT io_queue			numeric,
	OUT total_io_time		bigint,
	OUT read_size_tps_peak	numeric,
	OUT write_size_tps_peak	numeric
) RETURNS SETOF record AS
$$
	SELECT
		device_name,
		statsrepo.array_unique(statsrepo.array_accum(device_tblspaces)),
		coalesce(pg_catalog.sum(read_sector) / 2 / 1024, 0)::bigint,
		coalesce(pg_catalog.sum(write_sector) / 2 / 1024, 0)::bigint,
		coalesce(pg_catalog.sum(read_time), 0)::bigint,
		coalesce(pg_catalog.sum(write_time), 0)::bigint,
		pg_catalog.avg(device_ioqueue)::numeric(1000,3),
		coalesce(pg_catalog.sum(io_time), 0)::bigint,
		(pg_catalog.max(device_rsps_max) / 2)::numeric(1000,2),
		(pg_catalog.max(device_wsps_max) / 2)::numeric(1000,2)
	FROM
	(
		SELECT
			s.snapid,
			d.device_name,
			d.device_tblspaces,
			(d.device_readsector + (d.overflow_drs * 4294967296)) - pg_catalog.lag(d.device_readsector) OVER w AS read_sector,
			(d.device_writesector + (d.overflow_dws * 4294967296)) - pg_catalog.lag(d.device_writesector) OVER w AS write_sector,
			(d.device_readtime + (d.overflow_drt * 4294967296)) - pg_catalog.lag(d.device_readtime) OVER w AS read_time,
			(d.device_writetime + (d.overflow_dwt * 4294967296)) - pg_catalog.lag(d.device_writetime) OVER w AS write_time,
			(d.device_iototaltime + (d.overflow_dit * 4294967296)) - pg_catalog.lag(d.device_iototaltime) OVER w AS io_time,
			d.device_ioqueue,
			d.device_rsps_max,
			d.device_wsps_max
		FROM
			statsrepo.device d,
			statsrepo.snapshot s
		WHERE
			s.snapid = d.snapid
			AND s.snapid BETWEEN $1 AND $2
			AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			AND d.device_name IS NOT NULL
		WINDOW w AS (PARTITION BY d.device_name ORDER BY d.snapid)
		ORDER BY
			snapid, device_name
	) t
	WHERE
		snapid > $1
	GROUP BY
		device_name
	ORDER BY
		device_name;
$$
LANGUAGE sql;

-- generate information that corresponds to 'IO Usage'
CREATE FUNCTION statsrepo.get_io_usage_tendency_report(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT "timestamp"			text,
	OUT device_name			text,
	OUT read_size_tps		numeric,
	OUT write_size_tps		numeric,
	OUT read_time_rate		numeric,
	OUT write_time_rate		numeric,
	OUT read_size_tps_peak	numeric,
	OUT write_size_tps_peak	numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(time, 'YYYY-MM-DD HH24:MI'),
		device_name,
		coalesce(statsrepo.tps(read_size, duration) / 2, 0)::numeric(1000,2),
		coalesce(statsrepo.tps(write_size, duration) / 2, 0)::numeric(1000,2),
		coalesce(statsrepo.tps(read_time, duration) / 10, 0)::numeric(1000,1),
		coalesce(statsrepo.tps(write_time, duration) / 10, 0)::numeric(1000,1),
		(rsps_peak / 2)::numeric(1000,2),
		(wsps_peak / 2)::numeric(1000,2)
	FROM
	(
		SELECT
			snapid,
			time,
			device_name,
			(rs + (overflow_drs * 4294967296)) - pg_catalog.lag(rs) OVER w AS read_size,
			(ws + (overflow_dws * 4294967296)) - pg_catalog.lag(ws) OVER w AS write_size,
			(rt + (overflow_drt * 4294967296)) - pg_catalog.lag(rt) OVER w AS read_time,
			(wt + (overflow_dwt * 4294967296)) - pg_catalog.lag(wt) OVER w AS write_time,
			time - pg_catalog.lag(time) OVER w AS duration,
			rsps_peak,
			wsps_peak
		FROM
			(SELECT
				s.snapid,
				s.time,
				d.device_name,
				pg_catalog.sum(device_readsector) AS rs,
				pg_catalog.sum(device_writesector) AS ws,
				pg_catalog.sum(device_readtime) AS rt,
				pg_catalog.sum(device_writetime) AS wt,
				pg_catalog.sum(overflow_drs) AS overflow_drs,
				pg_catalog.sum(overflow_dws) AS overflow_dws,
				pg_catalog.sum(overflow_drt) AS overflow_drt,
				pg_catalog.sum(overflow_dwt) AS overflow_dwt,
				pg_catalog.sum(device_rsps_max) AS rsps_peak,
				pg_catalog.sum(device_wsps_max) AS wsps_peak
			 FROM
				statsrepo.device d,
				statsrepo.snapshot s
			 WHERE
			 	s.snapid = d.snapid
				AND s.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
				AND d.device_name IS NOT NULL
			 GROUP BY
				s.snapid, s.time, d.device_name) AS d
		WINDOW w AS (PARTITION BY device_name ORDER BY snapid)
		ORDER BY
			snapid, device_name
	) t
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Load Average' for pg_stats_reporter
CREATE FUNCTION statsrepo.get_loadavg_tendency(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT "timestamp"		text,
	OUT "1min"			numeric,
	OUT "5min"			numeric,
	OUT "15min"			numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(s.time, 'YYYY-MM-DD HH24:MI'),
		l.loadavg1::numeric(6,3),
		l.loadavg5::numeric(6,3),
		l.loadavg15::numeric(6,3)
	FROM
		statsrepo.loadavg l,
		statsrepo.snapshot s
	WHERE
		s.snapid BETWEEN $1 AND $2
		AND s.snapid = l.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	ORDER BY
		s.snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Memory Usage'
CREATE FUNCTION statsrepo.get_memory_tendency(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT "timestamp"		text,
	OUT memfree			numeric,
	OUT buffers			numeric,
	OUT cached			numeric,
	OUT swap			numeric,
	OUT dirty			numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(s.time, 'YYYY-MM-DD HH24:MI'),
		(m.memfree::float / 1024)::numeric(1000, 2),
		(m.buffers::float / 1024)::numeric(1000, 2),
		(m.cached::float / 1024)::numeric(1000, 2),
		(m.swap::float / 1024)::numeric(1000, 2),
		(m.dirty::float / 1024)::numeric(1000, 2)
	FROM
		statsrepo.memory m,
		statsrepo.snapshot s
	WHERE
		s.snapid BETWEEN $1 AND $2
		AND s.snapid = m.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	ORDER BY
		s.snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Disk Usage per Tablespace'
CREATE FUNCTION statsrepo.get_disk_usage_tablespace(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT spcname			name,
	OUT location		text,
	OUT device			text,
	OUT used			bigint,
	OUT avail			bigint,
	OUT remain			numeric
) RETURNS SETOF record AS
$$
	SELECT
		name,
		location,
		device,
		(total - avail) / 1024 / 1024,
		avail / 1024 / 1024,
		(100.0 * avail / total)::numeric(1000,1)
	FROM
		statsrepo.tablespace
	WHERE
		snapid = $2
	ORDER BY 1
$$
LANGUAGE sql;

-- generate information that corresponds to 'Disk Usage per Table'
CREATE FUNCTION statsrepo.get_disk_usage_table(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT datname		name,
	OUT nspname		name,
	OUT relname		name,
	OUT size		bigint,
	OUT table_reads	bigint,
	OUT index_reads	bigint,
	OUT toast_reads	bigint
) RETURNS SETOF record AS
$$
	SELECT
		e.database,
		e.schema,
		e.table,
		e.size / 1024 / 1024,
		statsrepo.sub(e.heap_blks_read, b.heap_blks_read),
		statsrepo.sub(e.idx_blks_read, b.idx_blks_read),
		statsrepo.sub(e.toast_blks_read, b.toast_blks_read) +
			statsrepo.sub(e.tidx_blks_read, b.tidx_blks_read)
	FROM
		statsrepo.tables e LEFT JOIN statsrepo.table b
			ON e.tbl = b.tbl AND e.nsp = b.nsp AND e.dbid = b.dbid AND b.snapid = $1
	WHERE
		e.snapid = $2
		AND e.schema NOT IN ('pg_catalog', 'pg_toast', 'information_schema')
	ORDER BY
		statsrepo.sub(e.heap_blks_read, b.heap_blks_read) +
			statsrepo.sub(e.idx_blks_read, b.idx_blks_read) +
			statsrepo.sub(e.toast_blks_read, b.toast_blks_read) +
			statsrepo.sub(e.tidx_blks_read, b.tidx_blks_read) DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Long Transactions'
CREATE FUNCTION statsrepo.get_long_transactions(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT pid			integer,
	OUT client		inet,
	OUT start		timestamp,
	OUT duration	numeric,
	OUT query		text
) RETURNS SETOF record AS
$$
	SELECT
		x.pid,
		x.client,
		x.start::timestamp(0),
		pg_catalog.max(x.duration)::numeric(1000, 3) AS duration,
		(SELECT query FROM statsrepo.xact WHERE snapid = pg_catalog.max(x.snapid) AND pid = x.pid AND start = x.start)
	FROM
		statsrepo.xact x,
		statsrepo.snapshot s
	WHERE
		x.snapid BETWEEN $1 AND $2
		AND x.snapid = s.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		AND x.pid <> 0
	GROUP BY
		x.pid,
		x.client,
		x.start
	ORDER BY
		duration DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Heavily Updated Tables'
CREATE FUNCTION statsrepo.get_heavily_updated_tables(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			name,
	OUT nspname			name,
	OUT relname			name,
	OUT n_tup_ins		bigint,
	OUT n_tup_upd		bigint,
	OUT n_tup_del		bigint,
	OUT n_tup_total		bigint,
	OUT hot_upd_rate	numeric	
) RETURNS SETOF record AS
$$
	SELECT
		e.database,
		e.schema,
		e.table,
		statsrepo.sub(e.n_tup_ins, b.n_tup_ins),
		statsrepo.sub(e.n_tup_upd, b.n_tup_upd),
		statsrepo.sub(e.n_tup_del, b.n_tup_del),
		statsrepo.sub(e.n_tup_ins, b.n_tup_ins) +
			statsrepo.sub(e.n_tup_upd, b.n_tup_upd) +
			statsrepo.sub(e.n_tup_del, b.n_tup_del),
		(statsrepo.div(
			statsrepo.sub(e.n_tup_hot_upd, b.n_tup_hot_upd),
			statsrepo.sub(e.n_tup_upd, b.n_tup_upd)) * 100)::numeric(1000,1)
	FROM
		statsrepo.tables e LEFT JOIN statsrepo.table b
			ON e.tbl = b.tbl AND e.nsp = b.nsp AND e.dbid = b.dbid AND b.snapid = $1
	WHERE
		e.snapid = $2
	ORDER BY
		7 DESC,
		4 DESC,
		5 DESC,
		6 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Heavily Accessed Tables'
CREATE FUNCTION statsrepo.get_heavily_accessed_tables(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			name,
	OUT nspname			name,
	OUT relname			name,
	OUT seq_scan		bigint,
	OUT seq_tup_read	bigint,
	OUT tup_per_seq		numeric,
	OUT blks_hit_rate	numeric
) RETURNS SETOF record AS
$$
	SELECT
		e.database,
		e.schema,
		e.table,
		statsrepo.sub(e.seq_scan, b.seq_scan),
		statsrepo.sub(e.seq_tup_read, b.seq_tup_read),
		statsrepo.div(
			statsrepo.sub(e.seq_tup_read, b.seq_tup_read),
			statsrepo.sub(e.seq_scan, b.seq_scan)),
		(statsrepo.div(
			statsrepo.sub(e.heap_blks_hit, b.heap_blks_hit) +
			statsrepo.sub(e.idx_blks_hit, b.idx_blks_hit) +
			statsrepo.sub(e.toast_blks_hit, b.toast_blks_hit) +
			statsrepo.sub(e.tidx_blks_hit, b.tidx_blks_hit),
			statsrepo.sub(e.heap_blks_hit, b.heap_blks_hit) +
			statsrepo.sub(e.idx_blks_hit, b.idx_blks_hit) +
			statsrepo.sub(e.toast_blks_hit, b.toast_blks_hit) +
			statsrepo.sub(e.tidx_blks_hit, b.tidx_blks_hit) +
			statsrepo.sub(e.heap_blks_read, b.heap_blks_read) +
			statsrepo.sub(e.idx_blks_read, b.idx_blks_read) +
			statsrepo.sub(e.toast_blks_read, b.toast_blks_read) +
			statsrepo.sub(e.tidx_blks_read, b.tidx_blks_read)) * 100)::numeric(1000,1)
	FROM
		statsrepo.tables e LEFT JOIN statsrepo.table b
			ON e.tbl = b.tbl AND e.nsp = b.nsp AND e.dbid = b.dbid AND b.snapid = $1
	WHERE
		e.snapid = $2
		AND e.schema NOT IN ('pg_catalog', 'pg_toast', 'information_schema')
		AND statsrepo.sub(e.seq_tup_read, b.seq_tup_read) > 0
	ORDER BY
		6 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Low Density Tables'
CREATE FUNCTION statsrepo.get_low_density_tables(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			name,
	OUT nspname			name,
	OUT relname			name,
	OUT n_live_tup		bigint,
	OUT logical_pages	bigint,
	OUT physical_pages	bigint,
	OUT tratio			numeric
) RETURNS SETOF record AS
$$
	SELECT
		database,
		schema,
		"table",
		n_live_tup,
		logical_pages,
		physical_pages,
		CASE physical_pages
			WHEN 0 THEN NULL ELSE (logical_pages * 100.0 / physical_pages)::numeric(1000,1) END AS tratio
	FROM
	(
		SELECT
			t.database, 
			t.schema, 
	 		t.table, 
			t.n_live_tup,
			pg_catalog.ceil(t.n_live_tup::real / ((i.page_size - i.page_header_size) * statsrepo.pg_fillfactor(t.reloptions, 0) / 100 /
				(width + i.htup_header_size + i.item_id_size)))::bigint AS logical_pages,
			(t.size + CASE t.toastrelid WHEN 0 THEN 0 ELSE tt.size END) / i.page_size AS physical_pages
		 FROM
		 	statsrepo.tables t
		 	LEFT JOIN statsrepo.snapshot s ON t.snapid = s.snapid
		 	LEFT JOIN statsrepo.instance i ON s.instid = i.instid
		 	LEFT JOIN
		 		(SELECT
		 			snapid, dbid, tbl, (pg_catalog.sum(avg_width)::integer + 7) & ~7 AS width
				 FROM
				 	statsrepo."column" 
				 WHERE
				 	attnum > 0
				 GROUP BY
				 	snapid, dbid, tbl) stat 
			ON t.snapid=stat.snapid AND t.dbid=stat.dbid AND t.tbl=stat.tbl
			LEFT JOIN
				(SELECT
					snapid, dbid, nsp, tbl, size
				 FROM
				 	statsrepo.tables ) tt 
			ON t.snapid=tt.snapid AND t.dbid=tt.dbid AND t.toastrelid=tt.tbl
		 WHERE
		 	t.relkind = 'r'
		 	AND t.schema NOT IN ('pg_catalog', 'information_schema', 'statsrepo')
		 	AND t.snapid = $2
	) fill
	WHERE
		physical_pages > 1000
	ORDER BY
		tratio;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Correlation'
CREATE FUNCTION statsrepo.get_correlation(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT datname		name,
	OUT nspname		name,
	OUT relname		name,
	OUT attname		name,
	OUT correlation	numeric
) RETURNS SETOF record AS
$$
	SELECT
		i.database,
		i.schema,
		i.table,
		c.name,
		c.correlation::numeric(4,3)
	FROM
		statsrepo.indexes i,
		statsrepo.column c
	WHERE
		c.snapid = $2
		AND i.snapid = c.snapid
		AND i.tbl = c.tbl
		AND c.attnum = ANY (i.indkey)
		AND i.schema NOT IN ('pg_catalog', 'pg_toast', 'information_schema')
		AND c.correlation < 1
	ORDER BY
		c.correlation;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Checkpoint Activity'
CREATE FUNCTION statsrepo.get_checkpoint_activity(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT ckpt_total		bigint,
	OUT ckpt_time		bigint,
	OUT ckpt_wal		bigint,
	OUT avg_write_buff	numeric,
	OUT max_write_buff	numeric,
	OUT avg_duration	numeric,
	OUT max_duration	numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.count(*),
		pg_catalog.count(nullif(pg_catalog.strpos(flags, 'time'), 0)),
		pg_catalog.count(nullif(pg_catalog.strpos(flags, 'wal'), 0)),
		pg_catalog.round(pg_catalog.avg(num_buffers)::numeric,3),
		pg_catalog.round(pg_catalog.max(num_buffers)::numeric,3),
		pg_catalog.round(pg_catalog.avg(total_duration)::numeric,3),
		pg_catalog.round(pg_catalog.max(total_duration)::numeric,3)
	FROM
		statsrepo.checkpoint c,
		(SELECT pg_catalog.min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1) b,
		(SELECT pg_catalog.max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2) e
	WHERE
		c.start BETWEEN b.time AND e.time
		AND c.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2);
$$
LANGUAGE sql;

-- generate information that corresponds to 'Autovacuum Activity'
CREATE FUNCTION statsrepo.get_autovacuum_activity(
	IN snapid_begin				bigint,
	IN snapid_end				bigint,
	OUT datname					text,
	OUT nspname					text,
	OUT relname					text,
	OUT "count"					bigint,
	OUT index_scanned			bigint,
	OUT index_skipped			bigint,
	OUT avg_tup_removed			numeric,
	OUT avg_tup_remain			numeric,
	OUT avg_tup_dead			numeric,
	OUT avg_tup_miss_dead		numeric,
	OUT avg_tup_miss_dead_pages	numeric,
	OUT tbl_scan_pages			numeric,
	OUT tbl_scan_pages_ratio	numeric,
	OUT dead_lp_pages			numeric,
	OUT dead_lp_pages_ratio		numeric,
	OUT removed_lp				numeric,
	OUT dead_lp					numeric,
	OUT sum_index_scans			numeric,
	OUT avg_duration			numeric,
	OUT max_duration			numeric,
	OUT cancel					bigint,
	OUT max_cutoff_xid			bigint,
	OUT max_frozen_xid			bigint,
	OUT max_relmin_mxid			bigint
) RETURNS SETOF record AS
$$
	WITH
	b AS ( SELECT pg_catalog.min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1 ),
	e AS ( SELECT pg_catalog.max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2 ),
	i AS ( SELECT instid FROM statsrepo.snapshot WHERE snapid = $2 ),
	vall AS (
		-- Select autovacuum for period / instance ID
		SELECT
			v.database,
			v.schema,
			v.table,
			v.tup_removed,
			v.tup_remain,
			v.tup_dead,
			v.tup_miss_dead,
			v.tup_miss_dead_pages,
			v.index_scans,
			v.duration,
			v.index_scan_ptn,
			v.tbl_scan_pages,
			v.tbl_scan_pages_ratio,
			v.dead_lp_pages,
			v.dead_lp_pages_ratio,
			v.dead_lp,
			v.removable_cutoff::text::numeric,
			v.new_relfrozenxid::text::numeric,
			v.new_relminmxid::text::numeric
		 FROM
			statsrepo.autovacuum v,
			b, e, i
		 WHERE
			v.start BETWEEN b.time AND e.time
			AND v.instid = i.instid
	),
	tc AS ( 
		-- Select autovacuum_cancel for period / instance ID to aggregate
		 SELECT
			c.database,
			c.schema,
			c.table,
			pg_catalog.count(*)
		 FROM
			statsrepo.autovacuum_cancel c,
			b,e,i
		 WHERE
			c.timestamp BETWEEN b.time AND e.time
			AND c.instid = i.instid
		 GROUP BY
			c.database, c.schema, c.table
	),
	tv AS (
		-- Aggregate selected autovacuum
		SELECT
			v.database,
			v.schema,
			v.table,
			pg_catalog.count(*),
			pg_catalog.avg(v.tup_removed) AS avg_tup_removed,
			pg_catalog.avg(v.tup_remain) AS avg_tup_remain,
			pg_catalog.avg(v.tup_dead) AS avg_tup_dead,
			pg_catalog.avg(v.tup_miss_dead) AS avg_tup_miss_dead,
			pg_catalog.avg(v.tup_miss_dead_pages) AS avg_tup_miss_dead_pages,
			pg_catalog.avg(v.tbl_scan_pages) AS tbl_scan_pages,
			pg_catalog.avg(v.tbl_scan_pages_ratio) AS tbl_scan_pages_ratio,
			pg_catalog.sum(v.index_scans) AS sum_index_scans,
			pg_catalog.avg(v.duration) AS avg_duration,
			pg_catalog.max(v.duration) AS max_duration,
			pg_catalog.max(v.removable_cutoff) AS max_cutoff_xid,
			pg_catalog.max(v.new_relfrozenxid) AS max_frozen_xid,
			pg_catalog.max(v.new_relminmxid) AS max_relmin_mxid
		FROM
			vall v
		GROUP BY
			v.database, v.schema, v.table
	),
	va AS (
		-- Aggregate index-scanned data
		SELECT
			v.database,
			v.schema,
			v.table,
			pg_catalog.count(*) AS index_scan,
			pg_catalog.avg(v.dead_lp_pages)       AS dead_lp_pages,
			pg_catalog.avg(v.dead_lp_pages_ratio) AS dead_lp_pages_ratio,
			pg_catalog.avg(v.dead_lp) AS removed_lp
		FROM
			vall v
		WHERE
			v.index_scan_ptn in (2)
		GROUP BY
			v.database, v.schema, v.table
	),
	vb AS (
		-- Aggregate data with index-scan bypassed by failsafes
		SELECT
			v.database,
			v.schema,
			v.table,
			pg_catalog.count(*)       AS index_skip,
			pg_catalog.avg(v.dead_lp) AS dead_lp
		FROM
			vall v
		WHERE
			v.index_scan_ptn in (4)
		GROUP BY
			v.database, v.schema, v.table
	)
	SELECT
		COALESCE(tv.database, tc.database),
		COALESCE(tv.schema, tc.schema),
		COALESCE(tv.table, tc.table),
		COALESCE(tv.count, 0),
		COALESCE(va.index_scan, 0),
		COALESCE(vb.index_skip, 0),
		pg_catalog.round(COALESCE(tv.avg_tup_removed,         0)::numeric, 1),
		pg_catalog.round(COALESCE(tv.avg_tup_remain,          0)::numeric, 1),
		pg_catalog.round(COALESCE(tv.avg_tup_dead,            0)::numeric, 1),
		pg_catalog.round(COALESCE(tv.avg_tup_miss_dead,       0)::numeric, 1),
		pg_catalog.round(COALESCE(tv.avg_tup_miss_dead_pages, 0)::numeric, 1),
		pg_catalog.round(COALESCE(tv.tbl_scan_pages,          0)::numeric, 1),
		pg_catalog.round(COALESCE(tv.tbl_scan_pages_ratio,    0)::numeric, 1),
		pg_catalog.round(COALESCE(va.dead_lp_pages,           0)::numeric, 1),
		pg_catalog.round(COALESCE(va.dead_lp_pages_ratio,     0)::numeric, 1),
		pg_catalog.round(COALESCE(va.removed_lp,              0)::numeric, 1),
		pg_catalog.round(COALESCE(vb.dead_lp,                 0)::numeric, 1),
		pg_catalog.round(COALESCE(tv.sum_index_scans,         0)::numeric, 1),
		pg_catalog.round(COALESCE(tv.avg_duration,            0)::numeric, 1),
		pg_catalog.round(COALESCE(tv.max_duration,            0)::numeric, 1),
		COALESCE(tc.count, 0),
		tv.max_cutoff_xid,
		tv.max_frozen_xid,
		tv.max_relmin_mxid
	FROM
		tv
	LEFT JOIN
		va ON tv.database = va.database AND tv.schema = va.schema AND tv.table = va.table
	LEFT JOIN
		vb ON tv.database = vb.database AND tv.schema = vb.schema AND tv.table = vb.table
	FULL JOIN
		tc ON tv.database = tc.database AND tv.schema = tc.schema AND tv.table = tc.table
	ORDER BY
		7 DESC, 1 ASC, 2 ASC, 3 ASC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Autovacuum Activity'
CREATE FUNCTION statsrepo.get_autovacuum_activity2(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT datname				text,
	OUT nspname				text,
	OUT relname				text,
	OUT avg_page_hit		numeric,
	OUT avg_page_miss		numeric,
	OUT avg_page_dirty		numeric,
	OUT avg_read_rate		numeric,
	OUT avg_write_rate		numeric,
	OUT avg_read_duration	numeric,
	OUT avg_write_duration	numeric
) RETURNS SETOF record AS
$$
	SELECT
		database,
		schema,
		"table",
		pg_catalog.round(pg_catalog.avg(page_hit)::numeric,3),
		pg_catalog.round(pg_catalog.avg(page_miss)::numeric,3),
		pg_catalog.round(pg_catalog.avg(page_dirty)::numeric,3),
		pg_catalog.round(pg_catalog.avg(read_rate)::numeric,3),
		pg_catalog.round(pg_catalog.avg(write_rate)::numeric,3),
		pg_catalog.round(pg_catalog.avg(io_timings_read)::numeric,3),
		pg_catalog.round(pg_catalog.avg(io_timings_write)::numeric,3)
	FROM
		(SELECT
			database,
			schema,
			"table",
			page_hit,
			page_miss,
			page_dirty,
			read_rate,
			write_rate,
			COALESCE(io_timings_read,  0) AS io_timings_read,
			COALESCE(io_timings_write, 0) AS io_timings_write
		 FROM
			statsrepo.autovacuum v,
			(SELECT pg_catalog.min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1) b,
			(SELECT pg_catalog.max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2) e
		 WHERE
			v.start BETWEEN b.time AND e.time
			AND v.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		) AS tv
	GROUP BY
		database, schema, "table"
	ORDER BY
		4 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Vacuum WAL Statistics (Average)'
CREATE FUNCTION statsrepo.get_autovacuum_wal_activity(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			text,
	OUT nspname			text,
	OUT relname			text,
	OUT "count"			bigint,
	OUT wal_records		numeric,
	OUT wal_fpis		numeric,
	OUT wal_bytes		numeric
) RETURNS SETOF record AS
$$
	SELECT
		tv.database,
		tv.schema,
		tv.table,
		COALESCE(tv.count, 0) AS count,
		pg_catalog.round(COALESCE(tv.wal_records, 0)::numeric, 3)     AS wal_records,
		pg_catalog.round(COALESCE(tv.wal_page_images, 0)::numeric, 3) AS wal_fpis,
		pg_catalog.round(COALESCE(tv.wal_bytes, 0)::numeric, 3)       AS wal_bytes
	FROM
		(SELECT
			v.database,
			v.schema,
			v.table,
			pg_catalog.count(*),
			pg_catalog.avg(v.wal_records) AS wal_records,
			pg_catalog.avg(v.wal_page_images) AS wal_page_images,
			pg_catalog.avg(v.wal_bytes) AS wal_bytes
		FROM
			statsrepo.autovacuum v,
			( SELECT pg_catalog.min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1 ) b,
			( SELECT pg_catalog.max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2 ) e,
			( SELECT instid FROM statsrepo.snapshot WHERE snapid = $2 ) i
		WHERE
			v.start BETWEEN b.time AND e.time
			AND v.instid = i.instid
		GROUP BY
			v.database, v.schema, v."table") tv
	ORDER BY
		7 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Vacuum WAL Statistics' for pg_stats_reporter
CREATE FUNCTION statsrepo.get_autovacuum_wal_activity_tendency(
        IN snapid_begin                 bigint,
        IN snapid_end                   bigint,
        OUT snapid                      bigint,
        OUT "timestamp"                 text,
        OUT wal_fpi                     numeric,
        OUT wal_bytes                   numeric
) RETURNS SETOF record AS
$$
        SELECT
                s.snapid,
                pg_catalog.to_char(s.t2, 'YYYY-MM-DD HH24:MI'),
                pg_catalog.sum(a.wal_page_images),
                pg_catalog.sum(a.wal_bytes)
        FROM
                (SELECT
                        snapid,
                        instid,
                        (pg_catalog.lag(time) OVER w) AS t1,
                        time AS t2
                FROM
                        statsrepo.snapshot
                WHERE
                        instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
                        AND snapid BETWEEN $1 AND $2
                WINDOW w AS (ORDER BY snapid)
                ) s,
                statsrepo.autovacuum a
        WHERE
                a.start BETWEEN s.t1 AND s.t2
                AND a.instid = s.instid
        GROUP BY
                s.snapid, s.t2
        ORDER BY
                s.snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Vacuum Index Statistics (Average)'
CREATE FUNCTION statsrepo.get_autovacuum_index_activity(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			text,
	OUT nspname			text,
	OUT relname			text,
	OUT index_name		text,
	OUT "count"			bigint,
	OUT page_total		numeric,
	OUT page_new_del	numeric,
	OUT page_cur_del	numeric,
	OUT page_reuse		numeric
) RETURNS SETOF record AS
$$
	WITH
	tv AS (
		SELECT
			v.database,
			v.schema, 
			v.table,
			pg_catalog.unnest(v.index_names)             AS index,
			pg_catalog.unnest(v.index_pages_total)       AS total,
			pg_catalog.unnest(v.index_pages_new_del)     AS new_del,
			pg_catalog.unnest(v.index_pages_current_del) AS cur_del,
			pg_catalog.unnest(v.index_pages_reusable)    AS reuse
		FROM
			statsrepo.autovacuum v,
			( SELECT pg_catalog.min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1 ) b,
			( SELECT pg_catalog.max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2 ) e,
			( SELECT instid FROM statsrepo.snapshot WHERE snapid = $2 ) i
		WHERE
			v.start BETWEEN b.time AND e.time
			AND v.instid = i.instid
	)
	SELECT
		tv.database,
		tv.schema, 
		tv.table,
		tv.index,
		count(*),
		pg_catalog.round(pg_catalog.avg(tv.total)  ,3),
		pg_catalog.round(pg_catalog.avg(tv.new_del),3),
		pg_catalog.round(pg_catalog.avg(tv.cur_del),3),
		pg_catalog.round(pg_catalog.avg(tv.reuse)  ,3)
	FROM
		tv
	GROUP BY
		tv.database,
		tv.schema, 
		tv.table,
		tv.index
	ORDER BY
		6 DESC, 7 DESC, 9 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Autovacuum Activity'
CREATE FUNCTION statsrepo.get_autoanalyze_stats(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			text,
	OUT nspname			text,
	OUT relname			text,
	OUT total_duration	numeric,
	OUT avg_duration	numeric,
	OUT max_duration	numeric,
	OUT "count"			bigint,
	OUT last_analyze	timestamp,
	OUT cancels			bigint,
	OUT mod_rows_max	bigint
) RETURNS SETOF record AS
$$
	SELECT
		t1.database,
		t1.schema,
		t1.table,
		t1.sum_duration,
		t1.avg_duration,
		t1.max_duration,
		t1.count,
		t1.last_analyze,
		t1.cancels,
		t2.mod_rows_max
	FROM
		(SELECT
			COALESCE(ta.database, tc.database) AS database,
			COALESCE(ta.schema, tc.schema) AS schema,
			COALESCE(ta.table, tc.table) AS table,
			pg_catalog.round(COALESCE(ta.sum_duration, 0)::numeric, 3) AS sum_duration,
			pg_catalog.round(COALESCE(ta.avg_duration, 0)::numeric, 3) AS avg_duration,
			pg_catalog.round(COALESCE(ta.max_duration, 0)::numeric, 3) AS max_duration,
			COALESCE(ta.count, 0) AS count,
			ta.last_analyze::timestamp(0),
			COALESCE(tc.count, 0) AS cancels
		 FROM
			(SELECT
				a.database,
				a.schema,
				a.table,
				pg_catalog.sum(a.duration) AS sum_duration,
				pg_catalog.avg(a.duration) AS avg_duration,
				pg_catalog.max(a.duration) AS max_duration,
				pg_catalog.count(*),
				pg_catalog.max(a.start) AS last_analyze
			 FROM
				statsrepo.autoanalyze a,
				(SELECT pg_catalog.min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1) b,
				(SELECT pg_catalog.max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2) e
			 WHERE
				a.start BETWEEN b.time AND e.time
				AND a.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
				a.database, a.schema, a.table) ta
			 FULL JOIN
				(SELECT
					c.database,
					c.schema,
					c.table,
					pg_catalog.count(*)
				 FROM
					statsrepo.autoanalyze_cancel c,
					(SELECT pg_catalog.min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1) b,
					(SELECT pg_catalog.max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2) e
				 WHERE
					c.timestamp BETWEEN b.time AND e.time
					AND c.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
				 GROUP BY
					c.database, c.schema, c.table) tc
			 ON ta.database = tc.database AND ta.schema = tc.schema AND ta.table = tc.table
		) t1
		LEFT JOIN
			(SELECT
				t.database,
				t.schema,
				t.table,
				pg_catalog.max(t.n_mod_since_analyze) AS mod_rows_max
			 FROM
				statsrepo.tables t,
				statsrepo.snapshot s
			 WHERE
			 	t.snapid = s.snapid
				AND t.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
			 	t.database, t.schema, t.table) t2
		ON t1.database = t2.database AND t1.schema = t2.schema AND t1.table = t2.table
	ORDER BY
		4 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Analyze I/O Summary'  for pg_stats_reporter
CREATE FUNCTION statsrepo.get_autoanalyze_activity2(
        IN snapid_begin                 bigint,
        IN snapid_end                   bigint,
        OUT datname                             text,
        OUT nspname                             text,
        OUT relname                             text,
        OUT avg_page_hit                numeric,
        OUT avg_page_miss               numeric,
        OUT avg_page_dirty              numeric,
        OUT avg_read_rate               numeric,
        OUT avg_write_rate              numeric,
        OUT avg_read_duration   numeric,
        OUT avg_write_duration  numeric
) RETURNS SETOF record AS
$$
        SELECT
                database,
                schema,
                "table",
                pg_catalog.round(pg_catalog.avg(page_hit)::numeric,3),
                pg_catalog.round(pg_catalog.avg(page_miss)::numeric,3),
                pg_catalog.round(pg_catalog.avg(page_dirty)::numeric,3),
                pg_catalog.round(pg_catalog.avg(read_rate)::numeric,3),
                pg_catalog.round(pg_catalog.avg(write_rate)::numeric,3),
                pg_catalog.round(pg_catalog.avg(io_timings_read)::numeric,3),
                pg_catalog.round(pg_catalog.avg(io_timings_write)::numeric,3)
        FROM
                (SELECT
                        database,
                        schema,
                        "table",
                        page_hit,
                        page_miss,
                        page_dirty,
                        read_rate,
                        write_rate,
                        COALESCE(io_timings_read,  0) AS io_timings_read,
                        COALESCE(io_timings_write, 0) AS io_timings_write
                 FROM
                        statsrepo.autoanalyze v,
                        (SELECT pg_catalog.min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1) b,
                        (SELECT pg_catalog.max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2) e
                 WHERE
                        v.start BETWEEN b.time AND e.time
                        AND v.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
                ) AS tv
        GROUP BY
                database, schema, "table"
        ORDER BY
                4 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Autovacuum Activity' for pg_stats_reporter
CREATE FUNCTION statsrepo.get_modified_row_ratio(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	IN table_num		integer,
	OUT "timestamp"		text,
	OUT datname			name,
	OUT nspname			name,
	OUT relname			name,
	OUT ratio			numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.to_char(s.time, 'YYYY-MM-DD HH24:MI') AS timestamp,
		t.database,
		t.schema,
		t.table,
		pg_catalog.max(statsrepo.div(t.n_mod_since_analyze, t.reltuples::bigint) * 100) AS ratio
	FROM
		statsrepo.tables t,
		statsrepo.snapshot s,
		(
			SELECT
				t.dbid,
				t.nsp,
				t.tbl
			FROM
				statsrepo.table t,
				statsrepo.snapshot s
			WHERE
				t.snapid = s.snapid
				AND t.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			GROUP BY
				t.dbid, t.nsp, t.tbl
			ORDER BY
				pg_catalog.max(t.reltuples) DESC LIMIT $3
		) t1
	WHERE
		t.snapid = s.snapid
		AND t.dbid = t1.dbid
		AND t.nsp = t1.nsp
		AND t.tbl = t1.tbl
		AND t.snapid BETWEEN $1 AND $2
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	GROUP BY
		timestamp, t.database, t.schema, t.table
	ORDER BY
		timestamp, t.database, t.schema, t.table;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Query Activity (Functions)'
CREATE FUNCTION statsrepo.get_query_activity_functions(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT funcid			oid,
	OUT datname			name,
	OUT nspname			name,
	OUT proname			name,
	OUT calls			bigint,
	OUT total_time		numeric,
	OUT self_time		numeric,
	OUT time_per_call	numeric
) RETURNS SETOF record AS
$$
	SELECT
		fe.funcid,
		d.name,
		s.name,
		fe.funcname,
		statsrepo.sub(fe.calls, fb.calls),
		statsrepo.sub(fe.total_time, fb.total_time)::numeric(1000, 3),
		statsrepo.sub(fe.self_time, fb.self_time)::numeric(1000, 3),
		statsrepo.div(
			statsrepo.sub(fe.total_time, fb.total_time)::numeric,
			statsrepo.sub(fe.calls, fb.calls))
	FROM
		statsrepo.function fe LEFT JOIN statsrepo.function fb
			ON fb.snapid = $1 AND fb.dbid = fe.dbid AND fb.nsp = fe.nsp AND fb.funcid = fe.funcid,
		statsrepo.database d,
		statsrepo.schema s
	WHERE
		fe.snapid = $2
		AND d.snapid = $2
		AND s.snapid = $2
		AND d.dbid = fe.dbid
		AND s.dbid = fe.dbid
		AND s.nsp = fe.nsp
	ORDER BY
		6 DESC,
		7 DESC,
		5 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Query Activity (Statements)'
CREATE FUNCTION statsrepo.get_query_activity_statements(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT rolname				text,
	OUT datname				name,
	OUT query				text,
	OUT plans				bigint,
	OUT total_plan_time		numeric,
	OUT time_per_plan		numeric,
	OUT calls				bigint,
	OUT total_exec_time		numeric,
	OUT time_per_call		numeric,
	OUT blk_read_time		numeric,
	OUT blk_write_time		numeric,
	OUT tmp_blk_read_time	numeric,
	OUT tmp_blk_write_time	numeric,
	OUT dbid	oid,
	OUT userid	oid,
	OUT queryid	bigint,
	OUT last	bigint
) RETURNS SETOF record AS
$$
	SELECT
		t1.rolname::text,
		t1.dbname::name,
		t1.query,
		t1.plans,
		t1.total_plan_time::numeric(1000,3),
		CASE t1.plans
			WHEN 0 THEN 0 ELSE (t1.total_plan_time / t1.plans)::numeric(1000, 3) END,
		t1.calls,
		t1.total_exec_time::numeric(1000, 3),
		CASE t1.calls
			WHEN 0 THEN 0 ELSE (t1.total_exec_time / t1.calls)::numeric(1000, 3) END,
		t1.blk_read_time::numeric(1000, 3),
		t1.blk_write_time::numeric(1000, 3),
		t1.tmp_blk_read_time::numeric(1000, 3),
		t1.tmp_blk_write_time::numeric(1000, 3),
		t1.dbid,
		t1.userid,
		t1.queryid,
		t1.last
	FROM
		(SELECT
			rol.name AS rolname,
			db.name AS dbname,
			reg.dbid,
			reg.userid,
			reg.queryid,
			reg.last,
			reg.query,
			statsrepo.sub(st2.plans, st1.plans) AS plans,
			statsrepo.sub(st2.total_plan_time, st1.total_plan_time) AS total_plan_time,
			statsrepo.sub(st2.calls, st1.calls) AS calls,
			statsrepo.sub(st2.total_exec_time, st1.total_exec_time) AS total_exec_time,
			statsrepo.sub(st2.blk_read_time, st1.blk_read_time) AS blk_read_time,
			statsrepo.sub(st2.blk_write_time, st1.blk_write_time) AS blk_write_time,
			statsrepo.sub(st2.temp_blk_read_time, st1.temp_blk_read_time) AS tmp_blk_read_time,
			statsrepo.sub(st2.temp_blk_write_time, st1.temp_blk_write_time) AS tmp_blk_write_time
		 FROM
		 	(SELECT
		 		s.dbid,
		 		s.userid,
		 		s.queryid,
		 		pg_catalog.max(s.query) AS query,
				$1 AS first,
				pg_catalog.max(s.snapid) AS last
			 FROM
			 	statsrepo.statement s
				JOIN statsrepo.snapshot ss ON (ss.snapid = s.snapid)
			 WHERE
			 	s.snapid >= $1 AND s.snapid <= $2
				AND ss.instid = (SELECT instid FROM statsrepo.snapshot ss1 WHERE ss1.snapid = $2)
			 GROUP BY
				s.dbid,
				s.userid,
				s.queryid
			) AS reg
		LEFT JOIN statsrepo.statement st1 ON
			(st1.dbid = reg.dbid AND st1.userid = reg.userid AND
			 st1.queryid = reg.queryid AND st1.snapid = reg.first)
		JOIN statsrepo.statement st2 ON
			(st2.dbid = reg.dbid AND st2.userid = reg.userid AND
			 st2.queryid = reg.queryid AND st2.snapid = reg.last)
		JOIN statsrepo.database db ON
			(db.snapid = reg.first AND db.dbid = reg.dbid)
		JOIN statsrepo.role rol ON
			(rol.snapid = reg.first AND rol.userid = reg.userid)
	) AS t1
	WHERE
		t1.calls > 0 and t1.total_exec_time > 0
	ORDER BY
		8 DESC,
		7 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Query Activity (Statements rusage)'
CREATE FUNCTION statsrepo.get_query_activity_statements_rusage(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT rolname			text,
	OUT datname			name,
	OUT query			text,
	OUT plan_reads			bigint,
	OUT plan_writes			bigint,
	OUT plan_user_times	numeric,
	OUT plan_sys_times	numeric,
	OUT exec_reads			bigint,
	OUT exec_writes			bigint,
	OUT exec_user_times	numeric,
	OUT exec_sys_times	numeric
) RETURNS SETOF record AS
$$
	SELECT
		reg.rolname,
		reg.datname,
		reg.query,
		statsrepo.sub(e.plan_reads, b.plan_reads),
		statsrepo.sub(e.plan_writes, b.plan_writes),
		statsrepo.sub(e.plan_user_time, b.plan_user_time)::numeric(12,6),
		statsrepo.sub(e.plan_system_time, b.plan_system_time)::numeric(12,6),
		statsrepo.sub(e.exec_reads, b.exec_reads),
		statsrepo.sub(e.exec_writes, b.exec_writes),
		statsrepo.sub(e.exec_user_time, b.exec_user_time)::numeric(12,6),
		statsrepo.sub(e.exec_system_time, b.exec_system_time)::numeric(12,6)
	FROM
		-- Use get_query_activity_statements, it's already have an organized query list of things.
		(SELECT * FROM statsrepo.get_query_activity_statements($1, $2)) reg
		LEFT JOIN statsrepo.rusage b ON
			(b.dbid = reg.dbid AND b.userid = reg.userid AND
			 b.queryid = reg.queryid AND b.snapid = $1)
		JOIN statsrepo.rusage e ON
			(e.dbid = reg.dbid AND e.userid = reg.userid AND
			 e.queryid = reg.queryid AND e.snapid = reg.last)
	ORDER BY
		reg.total_exec_time DESC,
		reg.calls DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Query Activity (Plans)'
CREATE FUNCTION statsrepo.get_query_activity_plans(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT queryid				bigint,
	OUT planid				bigint,
	OUT rolname				text,
	OUT datname				name,
	OUT calls				bigint,
	OUT total_time			numeric,
	OUT time_per_call		numeric,
	OUT blk_read_time		numeric,
	OUT blk_write_time		numeric,
	OUT temp_blk_read_time	numeric,
	OUT temp_blk_write_time	numeric
) RETURNS SETOF record AS
$$
	SELECT
		t1.queryid,
		t1.planid,
		t1.rolname::text,
		t1.datname::name,
		t1.calls,
		t1.total_time::Numeric(1000, 3),
		(t1.total_time / t1.calls)::numeric(1000, 3),
		t1.blk_read_time::numeric(1000, 3),
		t1.blk_write_time::numeric(1000, 3),
		t1.temp_blk_read_time::numeric(1000, 3),
		t1.temp_blk_write_time::numeric(1000, 3)
	FROM
		(SELECT
			reg.queryid,
			reg.planid,
			rol.name AS rolname,
			db.name AS datname,
			statsrepo.sub(pl2.calls, pl1.calls) AS calls,
			statsrepo.sub(pl2.total_time, pl1.total_time) AS total_time,
			statsrepo.sub(pl2.blk_read_time, pl1.blk_read_time) AS blk_read_time,
			statsrepo.sub(pl2.blk_write_time, pl1.blk_write_time) AS blk_write_time,
			statsrepo.sub(pl2.temp_blk_read_time, pl1.temp_blk_read_time) AS temp_blk_read_time,
			statsrepo.sub(pl2.temp_blk_write_time, pl1.temp_blk_write_time) AS temp_blk_write_time
		 FROM
			(SELECT
				p.queryid,
				p.planid,
				p.dbid,
				p.userid,
				$1 AS first,
				pg_catalog.max(p.snapid) AS last
			 FROM
			 	statsrepo.plan p
				JOIN statsrepo.snapshot ss ON (ss.snapid = p.snapid)
			 WHERE
			 	p.snapid >= $1 AND p.snapid <= $2
				AND ss.instid = (SELECT instid FROM statsrepo.snapshot ss1 WHERE ss1.snapid = $2)
			 GROUP BY
			 	p.queryid,
			 	p.planid,
			 	p.dbid,
			 	p.userid
			) AS reg
		LEFT JOIN statsrepo.plan pl1 ON
			(pl1.queryid = reg.queryid AND pl1.planid = reg.planid AND
			 pl1.dbid = reg.dbid AND pl1.userid = reg.userid AND
			 pl1.snapid = reg.first)
		JOIN statsrepo.plan pl2 ON
			(pl2.queryid = reg.queryid AND pl2.planid = reg.planid AND
			 pl2.dbid = reg.dbid AND pl2.userid = reg.userid AND
			 pl2.snapid = reg.last)
		JOIN statsrepo.database db ON
			(db.snapid = reg.first AND db.dbid = reg.dbid)
		JOIN statsrepo.role rol ON
			(rol.snapid = reg.first AND rol.userid = reg.userid)
	) AS t1
	WHERE
		t1.calls > 0 and t1.total_time > 0
	ORDER BY
		6 DESC,
		5 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Query Activity (Plans)' for pg_stats_reporter
CREATE FUNCTION statsrepo.get_query_activity_plans_report(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT queryid				bigint,
	OUT planid				bigint,
	OUT rolname				text,
	OUT datname				name,
	OUT calls				bigint,
	OUT total_time			numeric,
	OUT time_per_call		numeric,
	OUT blk_read_time		numeric,
	OUT blk_write_time		numeric,
	OUT temp_blk_read_time	numeric,
	OUT temp_blk_write_time	numeric,
	OUT first_call			timestamp,
	OUT last_call			timestamp,
	OUT query				text,
	OUT snapid				bigint,
	OUT dbid				oid,
	OUT userid				oid
) RETURNS SETOF record AS
$$
	SELECT
		t1.queryid,
		t1.planid,
		t1.rolname::text,
		t1.datname::name,
		t1.calls,
		t1.total_time::Numeric(1000, 3),
		(t1.total_time / t1.calls)::numeric(1000, 3),
		t1.blk_read_time::numeric(1000, 3),
		t1.blk_write_time::numeric(1000, 3),
		t1.temp_blk_read_time::numeric(1000, 3),
		t1.temp_blk_write_time::numeric(1000, 3),
		t1.first_call::timestamp(0),
		t1.last_call::timestamp(0),
		t1.query,
		t1.snapid,
		t1.dbid,
		t1.userid
	FROM
		(SELECT
			reg.queryid,
			pl2.planid,
			rol.name AS rolname,
			db.name AS datname,
			statsrepo.sub(pl2.calls, pl1.calls) AS calls,
			statsrepo.sub(pl2.total_time, pl1.total_time) AS total_time,
			statsrepo.sub(pl2.blk_read_time, pl1.blk_read_time) AS blk_read_time,
			statsrepo.sub(pl2.blk_write_time, pl1.blk_write_time) AS blk_write_time,
			statsrepo.sub(pl2.temp_blk_read_time, pl1.temp_blk_read_time) AS temp_blk_read_time,
			statsrepo.sub(pl2.temp_blk_write_time, pl1.temp_blk_write_time) AS temp_blk_write_time,
			pl2.first_call,
			pl2.last_call,
			st.query,
			pl2.snapid,
			pl2.dbid,
			pl2.userid
		 FROM
			(SELECT
				p.queryid,
				p.planid,
				p.dbid,
				p.userid,
				$1 AS first,
				pg_catalog.max(p.snapid) AS last
			 FROM
				statsrepo.plan p
				JOIN statsrepo.snapshot ss ON (ss.snapid = p.snapid)
			 WHERE
				p.snapid >= $1 AND p.snapid <= $2
				AND ss.instid = (SELECT instid FROM statsrepo.snapshot ss1 WHERE ss1.snapid = $2)
			 GROUP BY
				p.queryid,
				p.planid,
				p.dbid,
				p.userid
			) AS reg
		LEFT JOIN statsrepo.plan pl1 ON
			(pl1.queryid = reg.queryid AND pl1.planid = reg.planid AND
			 pl1.dbid = reg.dbid AND pl1.userid = reg.userid AND
			 pl1.snapid = reg.first)
		JOIN statsrepo.plan pl2 ON
			(pl2.queryid = reg.queryid AND pl2.planid = reg.planid AND
			 pl2.dbid = reg.dbid AND pl2.userid = reg.userid AND
			 pl2.snapid = reg.last)
		JOIN statsrepo.database db ON
			(db.snapid = reg.last AND db.dbid = reg.dbid)
		JOIN statsrepo.role rol ON
			(rol.snapid = reg.last AND rol.userid = reg.userid)
		JOIN
			(SELECT queryid,
					dbid,
					userid,
					pg_catalog.max(query) AS query
			 FROM
			 	statsrepo.statement s
				JOIN statsrepo.snapshot ss ON (ss.snapid = s.snapid)
			 WHERE
			 	s.snapid >= $1 AND s.snapid <= $2
				AND ss.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
			 	queryid, dbid, userid
			) AS st ON
				st.queryid = reg.queryid AND
				st.dbid = reg.dbid AND
				st.userid = reg.userid
	) AS t1
	WHERE
		t1.calls > 0 and t1.total_time > 0
	ORDER BY
		6 DESC,
		5 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Lock Conflicts'
CREATE FUNCTION statsrepo.get_lock_activity(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			name,
	OUT nspname			name,
	OUT relname			name,
	OUT duration		interval,
	OUT blockee_pid		integer,
	OUT blocker_pid		integer,
	OUT blocker_gid		text,
	OUT blockee_query	text,
	OUT blocker_query	text
) RETURNS SETOF record AS
$$
	SELECT
		t3.database,
		t3.schema,
		CASE WHEN t3.table IS NOT NULL THEN t3.table ELSE CAST('OID:[' || t1.relname || ']' AS name) END,
		t2.duration,
		t1.blockee_pid,
		t1.blocker_pid,
		t1.blocker_gid,
		t1.blockee_query,
		t2.blocker_query
	FROM
		(SELECT
			pg_catalog.max(l.snapid) AS snapid,
			l.dbid,
			l.nspname,
			l.relname,
			l.blockee_pid,
			l.blocker_pid,
			l.blocker_gid,
			l.blockee_query
		 FROM
			statsrepo.lock l,
			statsrepo.snapshot s
		 WHERE
			l.snapid > $1 AND l.snapid <= $2
			AND l.snapid = s.snapid
			AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		 GROUP BY
			l.dbid,
			l.nspname,
			l.relname,
			l.blockee_pid,
			l.blocker_pid,
			l.blocker_gid,
			l.blockee_query) t1
		LEFT JOIN statsrepo.lock t2 ON
			t2.snapid = t1.snapid
			AND coalesce(t2.dbid, 0) = coalesce(t1.dbid, 0)
			AND coalesce(t2.nspname, '') = coalesce(t1.nspname, '')
			AND coalesce(t2.relname, '') = coalesce(t1.relname, '')
			AND t2.blockee_pid = t1.blockee_pid
			AND t2.blocker_pid = t1.blocker_pid
			AND coalesce(t2.blocker_gid, '') = coalesce(t1.blocker_gid, '')
			AND t2.blockee_query = t1.blockee_query
		LEFT JOIN statsrepo.tables t3 ON
			t3.snapid =t1.snapid
			AND t3.dbid = t1.dbid
			AND t3.tbl = CAST(t1.relname AS oid)
	ORDER BY
		t2.duration DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'BGWriter Statistics'
CREATE FUNCTION statsrepo.get_bgwriter_tendency(
	IN snapid_begin				bigint,
	IN snapid_end				bigint,
	OUT "timestamp"				text,
	OUT bgwriter_write_tps		numeric,
	OUT backend_write_tps		numeric,
	OUT backend_fsync_tps		numeric,
	OUT bgwriter_stopscan_tps	numeric,
	OUT buffer_alloc_tps		numeric
) RETURNS SETOF record AS
$$
	SELECT
		t.timestamp,
		statsrepo.tps(t.bgwriter_write, t.duration),
		statsrepo.tps(t.backend_write, t.duration),
		statsrepo.tps(t.backend_fsync, t.duration),
		statsrepo.tps(t.bgwriter_stopscan, t.duration),
		statsrepo.tps(t.buffer_alloc, t.duration)
	FROM
	(
		SELECT
			s.snapid,
			pg_catalog.to_char(s.time, 'YYYY-MM-DD HH24:MI') AS timestamp,
			b.buffers_clean - pg_catalog.lag(b.buffers_clean) OVER w AS bgwriter_write,
			b.buffers_backend - pg_catalog.lag(b.buffers_backend) OVER w AS backend_write,
			b.buffers_backend_fsync - pg_catalog.lag(b.buffers_backend_fsync) OVER w AS backend_fsync,
			b.maxwritten_clean - pg_catalog.lag(b.maxwritten_clean) OVER w AS bgwriter_stopscan,
			b.buffers_alloc - pg_catalog.lag(b.buffers_alloc) OVER w AS buffer_alloc,
			s.time - pg_catalog.lag(s.time) OVER w AS duration
		FROM
			statsrepo.bgwriter b,
			statsrepo.snapshot s
		WHERE
			b.snapid BETWEEN $1 AND $2
			AND b.snapid = s.snapid
			AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		WINDOW w AS (ORDER BY s.snapid)
		ORDER BY
			s.snapid
	) t
	WHERE
		t.snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'BGWriter Statistics'
CREATE OR REPLACE FUNCTION statsrepo.get_bgwriter_stats(
	IN snapid_begin				bigint,
	IN snapid_end				bigint,
	OUT bgwriter_write_avg		numeric,
	OUT bgwriter_write_max		numeric,
	OUT backend_write_avg		numeric,
	OUT backend_write_max		numeric,
	OUT backend_fsync_avg		numeric,
	OUT backend_fsync_max		numeric,
	OUT bgwriter_stopscan_avg	numeric,
	OUT buffer_alloc_avg		numeric
) RETURNS SETOF record AS
$$
	SELECT
		pg_catalog.round(pg_catalog.avg(bgwriter_write_tps), 3),
		pg_catalog.round(pg_catalog.max(bgwriter_write_tps), 3),
		pg_catalog.round(pg_catalog.avg(backend_write_tps), 3),
		pg_catalog.round(pg_catalog.max(backend_write_tps), 3),
		pg_catalog.round(pg_catalog.avg(backend_fsync_tps), 3),
		pg_catalog.round(pg_catalog.max(backend_fsync_tps), 3),
		pg_catalog.round(pg_catalog.avg(bgwriter_stopscan_tps), 3),
		pg_catalog.round(pg_catalog.avg(buffer_alloc_tps), 3)
	FROM
		statsrepo.get_bgwriter_tendency($1, $2);
$$
LANGUAGE sql;

-- generate information that corresponds to 'Replication Delays'
CREATE FUNCTION statsrepo.get_replication_delays(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT snapid				bigint,
	OUT "timestamp"			text,
	OUT client_addr			inet,
	OUT application_name	text,
	OUT client				text,
	OUT flush_delay_size	numeric,
	OUT replay_delay_size	numeric,
	OUT sync_state			text
) RETURNS SETOF record AS
$$
	SELECT
		s.snapid,
		pg_catalog.to_char(s.time, 'YYYY-MM-DD HH24:MI'),
		r.client_addr,
		r.application_name,
		COALESCE(pg_catalog.host(r.client_addr), 'local') || ':' || COALESCE(NULLIF(r.application_name, ''), '(none)') AS client,
		pg_wal_lsn_diff(
			pg_catalog.split_part(r.current_location, ' ', 1)::pg_lsn,
			pg_catalog.split_part(r.flush_location, ' ', 1)::pg_lsn),
		pg_wal_lsn_diff(
			pg_catalog.split_part(r.current_location, ' ', 1)::pg_lsn,
			pg_catalog.split_part(r.replay_location, ' ', 1)::pg_lsn),
		r.sync_state
	FROM
		statsrepo.replication r LEFT JOIN statsrepo.replication_slots rs
			ON rs.snapid = r.snapid AND rs.active_pid = r.procpid,
		statsrepo.snapshot s,
		statsrepo.instance i
	WHERE
		r.snapid = s.snapid
		AND s.instid = i.instid
		AND r.snapid BETWEEN $1 AND $2
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		AND r.flush_location IS NOT NULL
		AND r.replay_location IS NOT NULL
		AND (rs.slot_name IS NULL OR rs.slot_type = 'physical')
	ORDER BY
		s.snapid, client;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Replication Activity'
CREATE FUNCTION statsrepo.get_replication_activity(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT snapshot_time		timestamp,
	OUT usename				name,
	OUT application_name	text,
	OUT client_addr			inet,
	OUT client_hostname		text,
	OUT client_port			integer,
	OUT backend_start		timestamp,
	OUT state				text,
	OUT current_location	text,
	OUT sent_location		text,
	OUT write_location		text,
	OUT flush_location		text,
	OUT replay_location		text,
	OUT sync_priority		integer,
	OUT sync_state			text,
	OUT replay_delay_avg	numeric,
	OUT replay_delay_peak	numeric,
	OUT write_lag_time		interval,
	OUT flush_lag_time		interval,
	OUT replay_lag_time		interval,
	OUT priority_sortkey	integer
) RETURNS SETOF record AS
$$
	SELECT
		s.time::timestamp(0),
		r.usename,
		r.application_name,
		r.client_addr,
		r.client_hostname,
		r.client_port,
		r.backend_start::timestamp(0),
		r.state,
		r.current_location,
		r.sent_location,
		r.write_location,
		r.flush_location,
		r.replay_location,
		r.sync_priority,
		r.sync_state,
		d.replay_delay_avg,
		d.replay_delay_peak,
		r.write_lag,
		r.flush_lag,
		r.replay_lag,
		NULLIF(r.sync_priority, 0) AS priority_sortkey
	FROM
		(SELECT
			client_addr,
			application_name,
			pg_catalog.max(snapid) AS snapid,
			pg_catalog.avg(replay_delay_size) AS replay_delay_avg,
			pg_catalog.max(replay_delay_size) AS replay_delay_peak
		 FROM
			statsrepo.get_replication_delays($1, $2)
		 GROUP BY
			client_addr, application_name) d
		LEFT JOIN statsrepo.replication r
			ON r.snapid = d.snapid
			AND ((d.client_addr IS NULL AND r.client_addr IS NULL) OR d.client_addr = r.client_addr)
			AND d.application_name = r.application_name
		LEFT JOIN statsrepo.snapshot s ON s.snapid = d.snapid
	ORDER BY d.snapid DESC, priority_sortkey ASC NULLS LAST;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Setting Parameters'
CREATE FUNCTION statsrepo.get_setting_parameters(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT name		text,
	OUT setting		text,
	OUT unit		text,
	OUT source		text
) RETURNS SETOF record AS
$$
	SELECT
		coalesce(se.name, sb.name),
		CASE WHEN sb.setting = se.setting THEN
			sb.setting
		ELSE
			coalesce(sb.setting, '(default)') || ' -> ' || coalesce(se.setting, '(default)')
		END,
		coalesce(se.unit, sb.unit),
		coalesce(se.source, sb.source)
	FROM
		(SELECT * FROM statsrepo.setting WHERE snapid = $1) AS sb
	FULL JOIN
		(SELECT * FROM statsrepo.setting WHERE snapid = $2) AS se
		ON sb.name = se.name
	ORDER BY
		1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Schema Information (Tables)'
CREATE FUNCTION statsrepo.get_schema_info_tables(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT datname		name,
	OUT nspname		name,
	OUT relname		name,
	OUT attnum		bigint,
	OUT tuples		bigint,
	OUT size		bigint,
	OUT size_incr	bigint,
	OUT seq_scan	bigint,
	OUT idx_scan	bigint
) RETURNS SETOF record AS
$$
	SELECT
		e.database,
		e.schema,
		e.table,
		c.columns,
		e.n_live_tup,
		e.size / 1024 / 1024,
		statsrepo.sub(e.size, b.size) / 1024 / 1024,
		statsrepo.sub(e.seq_scan, b.seq_scan),
		statsrepo.sub(e.idx_scan, b.idx_scan)
	FROM
		statsrepo.tables e LEFT JOIN
		statsrepo.table b
			ON e.tbl = b.tbl AND e.nsp = b.nsp AND e.dbid = b.dbid AND b.snapid = $1
		LEFT JOIN 
		(SELECT
		dbid,
		tbl,
		pg_catalog.count(*) AS "columns"
		FROM
		statsrepo.column
		WHERE
		snapid = $2
		GROUP BY
		dbid, tbl) AS c
		ON e.tbl = c.tbl AND e.dbid = c.dbid
	WHERE
		e.snapid = $2
		AND e.schema NOT IN ('pg_catalog', 'pg_toast', 'information_schema', 'statsrepo')
	ORDER BY
		e.database,
		e.schema,
		e.table;
	$$
LANGUAGE sql;

-- generate information that corresponds to 'Schema Information (Indexes)'
CREATE FUNCTION statsrepo.get_schema_info_indexes(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			name,
	OUT schemaname		name,
	OUT indexname		name,
	OUT tablename		name,
	OUT size			bigint,
	OUT size_incr		bigint,
	OUT scans			bigint,
	OUT rows_per_scan	numeric,
	OUT blks_read		bigint,
	OUT blks_hit		bigint,
	OUT keys			text
) RETURNS SETOF record AS
$$
	SELECT
		e.database,
		e.schema,
		e.index,
		e.table,
		e.size / 1024 / 1024,
		statsrepo.sub(e.size, b.size) / 1024 / 1024,
		statsrepo.sub(e.idx_scan, b.idx_scan),
		statsrepo.div(
			statsrepo.sub(e.idx_tup_fetch, b.idx_tup_fetch),
			statsrepo.sub(e.idx_scan, b.idx_scan)),
		statsrepo.sub(e.idx_blks_read, b.idx_blks_read),
		statsrepo.sub(e.idx_blks_hit, b.idx_blks_hit),
		(pg_catalog.regexp_matches(e.indexdef, E'.*USING[^\\(]+\\((.*)\\)'))[1]
	FROM
		statsrepo.indexes e LEFT JOIN statsrepo.index b
			ON e.idx = b.idx AND e.tbl = b.tbl AND e.dbid = b.dbid AND b.snapid = $1
	WHERE
		e.snapid = $2
		AND e.schema NOT IN ('pg_catalog', 'pg_toast', 'information_schema', 'statsrepo')
	ORDER BY
		1,
		2,
		3;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Alert'
CREATE FUNCTION statsrepo.get_alert(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT "timestamp"		timestamp,
	OUT message			text
) RETURNS SETOF record AS
$$
	SELECT
		s.time::timestamp(0),
		a.message
	FROM
		statsrepo.alert_message a LEFT JOIN statsrepo.snapshot s
			ON a.snapid = s.snapid
	WHERE
		s.snapid BETWEEN $1 AND $2
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	ORDER BY
		1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Wait Sampling'
CREATE FUNCTION statsrepo.get_wait_sampling(
	IN snapid_begin bigint,
	IN snapid_end bigint,
	OUT queryid bigint,
	OUT dbid bigint,
	OUT userid bigint,
	OUT database text,
	OUT role text,
	OUT backend_type text,
	OUT event_type text,
	OUT event text,
	OUT count bigint,
	OUT ratio numeric,
	OUT query text,
	OUT row_number integer
) RETURNS SETOF record AS
$$
	SELECT
		queryid,
		dbid,
		userid,
		datname,
		rolname,
		backend_type,
		event_type,
		event,
		cnt,
		ratio,
		query,
		row_number
	FROM
		(SELECT
			ttt.queryid AS queryid,
			ttt.dbid AS dbid,
			ttt.userid AS userid,
			st.datname AS datname,
			st.rolname AS rolname,
			ttt.backend_type AS backend_type,
			ttt.event_type AS event_type,
			ttt.event AS event,
			ttt.cnt AS cnt,
			ttt.ratio AS ratio,
			st.query AS query,
			st.total_exec_time AS total_exec_time,
			st.calls AS calls,
			ttt.row_number AS row_number
		FROM
			(SELECT * FROM statsrepo.get_query_activity_statements($1, $2) ORDER BY total_exec_time DESC, calls DESC LIMIT 20) st
		JOIN
			(SELECT *
			FROM
				(SELECT
					queryid,
					dbid,
					userid,
					backend_type,
					event_type,
					event,
					cnt,
					ratio::numeric(6,3) AS ratio,
					ROW_NUMBER() OVER ww
				FROM
					(SELECT
						we.queryid AS queryid,
						we.dbid AS dbid,
						we.userid AS userid,
						we.backend_type AS backend_type,
						we.event_type AS event_type,
						we.event AS event,
						statsrepo.sub(we.count, wb.count) AS cnt,
						statsrepo.sub(we.count, wb.count) * 100 / pg_catalog.sum(statsrepo.sub(we.count, wb.count)) OVER w AS ratio
					FROM
						statsrepo.wait_sampling we
					LEFT JOIN statsrepo.wait_sampling wb
						ON wb.dbid = we.dbid
						AND wb.userid = we.userid
						AND wb.queryid = we.queryid
						AND wb.backend_type = we.backend_type
						AND wb.event_type = we.event_type
						AND wb.event = we.event
						AND wb.snapid = $1
					WHERE
						statsrepo.sub(we.count, wb.count) > 0
						AND we.snapid = $2
					WINDOW w AS (PARTITION BY we.queryid, we.dbid, we.userid, we.backend_type) -- don't use order by to work partial summation properly
					) t
				WINDOW ww AS (PARTITION BY queryid, dbid, userid, backend_type ORDER BY cnt DESC)
				) tt
			WHERE tt.row_number <= 10
			) ttt
			ON  ttt.dbid = st.dbid
			AND ttt.userid = st.userid
			AND ttt.queryid = st.queryid
		) tttt
	ORDER BY
		total_exec_time DESC,
		calls DESC,
		dbid,
		userid,
		queryid,
		row_number
;
$$
LANGUAGE sql;

CREATE FUNCTION statsrepo.get_wait_sampling_by_instid(
	IN snapid_begin bigint,
	IN snapid_end bigint,
	OUT event_type text,
	OUT event text,
	OUT count bigint,
	OUT ratio numeric,
	OUT row_number integer
) RETURNS SETOF record AS
$$
SELECT *
FROM
	(SELECT
		event_type,
		event,
		cnt,
		ratio::numeric(6,3),
		ROW_NUMBER() OVER ww
	FROM
		(SELECT
			event_type,
			event,
			cnt,
			cnt * 100 / pg_catalog.sum(cnt) OVER w AS ratio
		FROM
			(SELECT
				we.event_type AS event_type,
				we.event AS event,
				pg_catalog.sum(statsrepo.sub(we.count, wb.count)) AS cnt
			FROM
				statsrepo.wait_sampling we LEFT JOIN statsrepo.wait_sampling wb
					ON wb.dbid = we.dbid
					AND wb.userid = we.userid
					AND wb.queryid = we.queryid
					AND wb.backend_type = we.backend_type
					AND wb.event_type = we.event_type
					AND wb.event = we.event
					AND wb.snapid = $1
			WHERE
				statsrepo.sub(we.count, wb.count) <> 0
				AND we.snapid = $2
			GROUP BY we.event_type, we.event
			) t
		WINDOW w AS ()
		)tt
	WINDOW ww AS (ORDER BY cnt DESC)
	) ttt
WHERE ttt.row_number <= 10
;
$$
LANGUAGE sql;

CREATE FUNCTION statsrepo.get_wait_sampling_by_dbid(
	IN snapid_begin bigint,
	IN snapid_end bigint,
	OUT dbid bigint,
	OUT database text,
	OUT event_type text,
	OUT event text,
	OUT count bigint,
	OUT ratio numeric,
	OUT row_number integer
) RETURNS SETOF record AS
$$
SELECT *
FROM
	(SELECT
		dbid,
		CASE WHEN database IS NULL THEN '<global>' ELSE database END,
		event_type,
		event,
		cnt,
		ratio::numeric(6,3),
		ROW_NUMBER() OVER ww
	FROM
		(SELECT
			dbid,
			database,
			event_type,
			event,
			cnt,
			cnt * 100 / pg_catalog.sum(cnt) OVER w AS ratio
		FROM
			(SELECT
				we.dbid AS dbid,
				db.name AS database,
				we.event_type AS event_type,
				we.event AS event,
				pg_catalog.sum(statsrepo.sub(we.count, wb.count)) AS cnt
			FROM
				statsrepo.wait_sampling we LEFT JOIN statsrepo.wait_sampling wb
					ON wb.dbid = we.dbid
					AND wb.userid = we.userid
					AND wb.queryid = we.queryid
					AND wb.backend_type = we.backend_type
					AND wb.event_type = we.event_type
					AND wb.event = we.event
					AND wb.snapid = $1
			LEFT JOIN statsrepo.database db
				ON we.dbid = db.dbid
				AND db.snapid = $1
			WHERE
				statsrepo.sub(we.count, wb.count) <> 0
				AND we.snapid = $2
			GROUP BY we.dbid, db.name, we.event_type, we.event
			) t
		WINDOW w AS (PARTITION BY t.dbid)
		) tt
	WINDOW ww AS (PARTITION BY tt.dbid ORDER BY cnt DESC)
	) ttt
WHERE ttt.row_number <= 10
;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Profiles'
CREATE FUNCTION statsrepo.get_profiles(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT processing		text,
	OUT executes		numeric
) RETURNS SETOF record AS
$$
	SELECT
		p.processing,
		pg_catalog.sum(p.execute) AS executes
	FROM
		statsrepo.profile p LEFT JOIN statsrepo.snapshot s
			ON p.snapid = s.snapid
	WHERE
		s.snapid BETWEEN $1 and $2
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	GROUP BY
		p.processing
	ORDER BY
		executes;
$$
LANGUAGE sql;

-- generate information that corresponds to 'CPU Information'
CREATE FUNCTION statsrepo.get_cpuinfo(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT "timestamp"			text,
	OUT vendor_id			text,
	OUT model_name			text,
	OUT cpu_mhz				real,
	OUT processors			int,
	OUT threads_per_core	int,
	OUT cores_per_socket	int,
	OUT sockets				int
) RETURNS SETOF record AS
$$
	WITH
	i AS ( SELECT instid FROM statsrepo.snapshot WHERE snapid = $2 ),
	b AS ( SELECT pg_catalog.min(time) AS time
			FROM statsrepo.snapshot s, i
			WHERE s.instid = i.instid AND snapid >= $1 ),
	e AS ( SELECT pg_catalog.max(time) AS time
			FROM statsrepo.snapshot s, i
			WHERE s.instid = i.instid AND snapid <= $2 ),
	b2 AS ( SELECT pg_catalog.max(timestamp) AS time 
			FROM statsrepo.cpuinfo c, i, b
			WHERE c.instid = i.instid AND c.timestamp <= b.time)
	SELECT
		pg_catalog.to_char(timestamp, 'YYYY-MM-DD HH24:MI'),
		vendor_id,
		model_name,
		cpu_mhz,
		processors,
		threads_per_core,
		cores_per_socket,
		sockets
	FROM
		statsrepo.cpuinfo c, i, b2, e
	WHERE
		c.timestamp BETWEEN COALESCE(b2.time, '1997-01-29') AND e.time
		AND c.instid = i.instid
	ORDER BY
		c.timestamp
$$
LANGUAGE sql;

-- generate information that corresponds to 'Memory Information'
CREATE FUNCTION statsrepo.get_meminfo(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT "timestamp"			text,
	OUT mem_total			text
) RETURNS SETOF record AS
$$
	WITH
	i AS ( SELECT instid FROM statsrepo.snapshot WHERE snapid = $2 ),
	b AS ( SELECT pg_catalog.min(time) AS time
			FROM statsrepo.snapshot s, i
			WHERE s.instid = i.instid AND snapid >= $1 ),
	e AS ( SELECT pg_catalog.max(time) AS time
			FROM statsrepo.snapshot s, i
			WHERE s.instid = i.instid AND snapid <= $2 ),
	b2 AS ( SELECT pg_catalog.max(timestamp) AS time 
			FROM statsrepo.meminfo m, i, b
			WHERE m.instid = i.instid AND m.timestamp <= b.time)
	SELECT
		pg_catalog.to_char(timestamp, 'YYYY-MM-DD HH24:MI'),
		pg_catalog.pg_size_pretty(mem_total)
	FROM
		statsrepo.meminfo m, i, b2, e
	WHERE
		m.timestamp BETWEEN COALESCE(b2.time, '1997-01-29') AND e.time
		AND m.instid = i.instid
	ORDER BY
		m.timestamp
$$
LANGUAGE sql;

-- generate information that corresponds to 'Hash table statistics information'
CREATE FUNCTION statsrepo.get_ht_info(
    IN snapid_begin         bigint,
    IN snapid_end           bigint,
    OUT statements_dealloc  bigint,
    OUT statements_reset    timestamptz,
    OUT wait_sampling_dealloc  bigint,
    OUT wait_sampling_reset    timestamptz,
    OUT rusage_dealloc 		bigint,
    OUT rusage_reset    	timestamptz
) RETURNS SETOF record AS
$$
	SELECT
		statsrepo.sub(e.statements_dealloc, b.statements_dealloc),
		e.statements_reset,
		statsrepo.sub(e.wait_sampling_dealloc, b.wait_sampling_dealloc),
		e.wait_sampling_reset,
		statsrepo.sub(e.rusage_dealloc, b.rusage_dealloc),
		e.rusage_reset
	FROM
		(SELECT * FROM statsrepo.ht_info WHERE snapid = $1) b,
		(SELECT * FROM statsrepo.ht_info WHERE snapid = $2) e
$$
LANGUAGE sql;


------------------------------------------------------------------------------
-- function for partitioning.
------------------------------------------------------------------------------

-- get date of the corresponding snapshot from 'snapid'
CREATE FUNCTION statsrepo.get_snap_date(bigint) RETURNS date AS
'SELECT CAST(time AS DATE) FROM statsrepo.snapshot WHERE snapid = $1'
LANGUAGE sql IMMUTABLE STRICT; 

-- get definition of foreign key
CREATE FUNCTION statsrepo.get_constraintdef(oid)
RETURNS SETOF text AS
$$
	SELECT
		pg_catalog.pg_get_constraintdef(oid, true) AS condef
	FROM
		pg_constraint
	WHERE
		conrelid = $1 AND contype = 'f';
$$ LANGUAGE sql IMMUTABLE STRICT;

-- function to create new partition-table
CREATE FUNCTION statsrepo.partition_new(regclass, date, text)
RETURNS void AS
$$
DECLARE
	parent_name	name;
	child_name	name;
	condef		text;
BEGIN
	parent_name := relname FROM pg_class WHERE oid = $1;
	child_name := parent_name || pg_catalog.to_char($2, '_YYYYMMDD');

	/* child table already exists */
	PERFORM 1 FROM pg_inherits i LEFT JOIN pg_class c ON c.oid = i.inhrelid
	WHERE i.inhparent = $1 AND c.relname = child_name;
	IF FOUND THEN
		RETURN;
	END IF;

	/* create child table */
	IF NOT FOUND THEN
		EXECUTE 'CREATE TABLE statsrepo.' || child_name
			|| ' (LIKE statsrepo.' || parent_name
			|| ' INCLUDING INDEXES INCLUDING DEFAULTS INCLUDING CONSTRAINTS,'
			|| ' CHECK (' || $3 || ' >= DATE ''' || pg_catalog.to_char($2, 'YYYY-MM-DD') || ''''
			|| ' AND ' || $3 || ' < DATE ''' || pg_catalog.to_char($2 + 1, 'YYYY-MM-DD') || ''')'
			|| ' ) INHERITS (statsrepo.' || parent_name || ')';

		/* add foreign key constraint */
		FOR condef IN SELECT statsrepo.get_constraintdef($1) LOOP
		    EXECUTE 'ALTER TABLE statsrepo.' || child_name || ' ADD ' || condef;
		END LOOP;
	END IF;
END;
$$ LANGUAGE plpgsql;

-- partition_drop(date, regclass) - drop partition-table.
CREATE FUNCTION statsrepo.partition_drop(date, regclass)
RETURNS void AS
$$
DECLARE
	parent_name	name;
	child_name	name;
	tblname		name;
BEGIN
	parent_name := relname FROM pg_class WHERE oid = $2;
	child_name := parent_name || pg_catalog.to_char($1, '_YYYYMMDD');

	FOR tblname IN
		SELECT c.relname FROM pg_inherits i LEFT JOIN pg_class c ON c.oid = i.inhrelid
		WHERE i.inhparent = $2 AND c.relname < child_name
	LOOP
		EXECUTE 'DROP TABLE IF EXISTS statsrepo.' || tblname;
	END LOOP;
END;
$$
LANGUAGE plpgsql;

-- function to create partition-tables for snapshot
CREATE FUNCTION statsrepo.create_snapshot_partition(timestamptz) RETURNS void AS
$$
DECLARE
BEGIN
	LOCK TABLE statsrepo.instance IN SHARE UPDATE EXCLUSIVE MODE;

	SET client_min_messages = warning;
	PERFORM statsrepo.partition_new('statsrepo.table', CAST($1 AS DATE), 'date');
	PERFORM statsrepo.partition_new('statsrepo.index', CAST($1 AS DATE), 'date');
	PERFORM statsrepo.partition_new('statsrepo.column', CAST($1 AS DATE), 'date');
	RESET client_min_messages;
END;
$$ LANGUAGE plpgsql;

-- function to create partition-tables for log
CREATE FUNCTION statsrepo.create_repolog_partition(timestamptz) RETURNS void AS
$$
DECLARE
BEGIN
	LOCK TABLE statsrepo.instance IN SHARE UPDATE EXCLUSIVE MODE;

	SET client_min_messages = warning;
	PERFORM statsrepo.partition_new('statsrepo.log', CAST($1 AS DATE), 'CAST (timestamp AS DATE)');
	RESET client_min_messages;
END;
$$ LANGUAGE plpgsql;

-- function to insert partition-table for snapshot
CREATE FUNCTION statsrepo.partition_snapshot_insert() RETURNS TRIGGER AS
$$
DECLARE
BEGIN
	EXECUTE 'INSERT INTO statsrepo.'
		|| TG_TABLE_NAME || pg_catalog.to_char(new.date, '_YYYYMMDD') || ' VALUES(($1).*)' USING new;
	RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- function to insert partition-table for log
CREATE FUNCTION statsrepo.partition_repolog_insert() RETURNS TRIGGER AS
$$
DECLARE
BEGIN
	EXECUTE 'INSERT INTO statsrepo.'
		|| TG_TABLE_NAME || pg_catalog.to_char(new.timestamp, '_YYYYMMDD') || ' VALUES(($1).*)' USING new;
	RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- trigger registration for partitioning
CREATE TRIGGER partition_insert_table BEFORE INSERT ON statsrepo.table FOR EACH ROW EXECUTE PROCEDURE statsrepo.partition_snapshot_insert();
CREATE TRIGGER partition_insert_index BEFORE INSERT ON statsrepo.index FOR EACH ROW EXECUTE PROCEDURE statsrepo.partition_snapshot_insert();
CREATE TRIGGER partition_insert_column BEFORE INSERT ON statsrepo.column FOR EACH ROW EXECUTE PROCEDURE statsrepo.partition_snapshot_insert();
CREATE TRIGGER partition_insert_log BEFORE INSERT ON statsrepo.log FOR EACH ROW EXECUTE PROCEDURE statsrepo.partition_repolog_insert();

-- del_snapshot2(time) - delete snapshots older than the specified timestamp.
CREATE FUNCTION statsrepo.del_snapshot2(timestamptz) RETURNS void AS
$$
	LOCK TABLE statsrepo.instance IN SHARE UPDATE EXCLUSIVE MODE;

	SELECT statsrepo.partition_drop(CAST($1 AS DATE), 'statsrepo.table');
	SELECT statsrepo.partition_drop(CAST($1 AS DATE), 'statsrepo.index');
	SELECT statsrepo.partition_drop(CAST($1 AS DATE), 'statsrepo.column');

	/*
	 * Note:
	 * Not use DELETE directly due to a bug in PostgreSQL (#6019).
	 * (BUG #6019: invalid cached plan on inherited table)
	 */
	SELECT statsrepo.del_snapshot($1);
$$
LANGUAGE sql;

-- del_repolog2(time) - delete logs older than the specified timestamp.
CREATE FUNCTION statsrepo.del_repolog2(timestamptz) RETURNS void AS
$$
	LOCK TABLE statsrepo.instance IN SHARE UPDATE EXCLUSIVE MODE;

	SELECT statsrepo.partition_drop(CAST($1 AS DATE), 'statsrepo.log');

	/*
	 * Note:
	 * Not use DELETE directly due to a bug in PostgreSQL (#6019).
	 * (BUG #6019: invalid cached plan on inherited table)
	 */
	SELECT statsrepo.del_repolog($1);
$$
LANGUAGE sql;

COMMIT;
