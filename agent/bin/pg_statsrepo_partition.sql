/*
 * bin/pg_statsrepo_partition.sql
 *
 * Create a repository schema for PostgreSQL 8.4 and later.
 *
 * Copyright (c) 2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

-- Adjust this setting to control where the objects get created.
SET search_path = public;

BEGIN;

SET LOCAL client_min_messages = WARNING;

CREATE SCHEMA statsrepo;

CREATE TABLE statsrepo.instance
(
	instid			bigserial,
	name			text NOT NULL,
	hostname		text NOT NULL,
	port			integer NOT NULL,
	pg_version		text,
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
	snapid			bigint,
	dbid			oid,
	name			name,
	size			bigint,
	age				integer,
	xact_commit		bigint,
	xact_rollback	bigint,
	blks_read		bigint,
	blks_hit		bigint,
	tup_returned	bigint,
	tup_fetched		bigint,
	tup_inserted	bigint,
	tup_updated		bigint,
	tup_deleted		bigint,
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
	snapid			bigint,
	dbid			oid,
	tbl				oid,
	nsp				oid,
	date			date,
	tbs				oid,
	name			name,
	toastrelid		oid,
	toastidxid		oid,
	relkind			"char",
	relpages		integer,
	reltuples		real,
	reloptions		text[],
	size			bigint,
	seq_scan		bigint,
	seq_tup_read	bigint,
	idx_scan		bigint,
	idx_tup_fetch	bigint,
	n_tup_ins		bigint,
	n_tup_upd		bigint,
	n_tup_del		bigint,
	n_tup_hot_upd	bigint,
	n_live_tup		bigint,
	n_dead_tup		bigint,
	heap_blks_read	bigint,
	heap_blks_hit	bigint,
	idx_blks_read	bigint,
	idx_blks_hit	bigint,
	toast_blks_read	bigint,
	toast_blks_hit	bigint,
	tidx_blks_read	bigint,
	tidx_blks_hit	bigint,
	last_vacuum		timestamptz,
	last_autovacuum	timestamptz,
	last_analyze	timestamptz,
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
	idle				float8,
	idle_in_xact		float8,
	waiting				float8,
	running				float8,
	max_xact_client		inet,
	max_xact_pid		integer,
	max_xact_start		timestamptz,
	max_xact_duration	float8,
	max_xact_query		text,
	PRIMARY KEY (snapid),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.setting
(
	snapid			bigint,
	name			text,
	setting			text,
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
	query				text,
	calls				bigint,
	total_time			double precision,
	rows				bigint,
	shared_blks_hit		bigint,
	shared_blks_read	bigint,
	shared_blks_written	bigint,
	local_blks_hit		bigint,
	local_blks_read		bigint,
	local_blks_written	bigint,
	temp_blks_read		bigint,
	temp_blks_written	bigint,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid)
);
CREATE INDEX statsrepo_statement_idx ON statsrepo.statement(snapid, dbid);

CREATE TABLE statsrepo.function
(
	snapid			bigint,
	dbid			oid,
	funcid			oid,
	nsp				oid,
	funcname		name,
	argtypes		text,
	calls			bigint,
	total_time		bigint,
	self_time		bigint,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE,
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid)
);
CREATE INDEX statsrepo_function_idx ON statsrepo.function(snapid, dbid);

CREATE TABLE statsrepo.autovacuum
(
	instid			bigint,
	start			timestamptz,
	database		text,
	schema			text,
	"table"			text,
	index_scans		integer,
	page_removed	integer,
	page_remain		integer,
	tup_removed		bigint,
	tup_remain		bigint,
	duration		real,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE
);
CREATE INDEX statsrepo_autovacuum_idx ON statsrepo.autovacuum(instid, start);

CREATE TABLE statsrepo.autoanalyze
(
	instid			bigint,
	start			timestamptz,
	database		text,
	schema			text,
	"table"			text,
	duration		real,
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid) ON DELETE CASCADE
);
CREATE INDEX statsrepo_autoanalyze_idx ON statsrepo.autoanalyze(instid, start);

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
	snapid			bigint,
	cpu_id			text,
	cpu_user		bigint,
	cpu_system		bigint,
	cpu_idle		bigint,
	cpu_iowait		bigint,
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
	device_tblspaces	name[],
	PRIMARY KEY (snapid, device_major, device_minor),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.profile
(
	snapid			bigint,
	processing		text,
	execute			bigint,
	total_exec_time	double precision,
	PRIMARY KEY (snapid, processing),
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

------------------------------------------------------------------------------
-- alert function
------------------------------------------------------------------------------

--
-- CREATE FUNCTION statsrepo.alert(snap_id bigint) RETURNS SETOF text AS ...
--

------------------------------------------------------------------------------
-- alert function
------------------------------------------------------------------------------

-- del_snapshot(snapid) - delete the specified snapshot.
CREATE FUNCTION statsrepo.del_snapshot(bigint) RETURNS void AS
$$
	DELETE FROM statsrepo.snapshot WHERE snapid = $1;
	DELETE FROM statsrepo.autovacuum WHERE start < (SELECT min(time) FROM statsrepo.snapshot);
	DELETE FROM statsrepo.autoanalyze WHERE start < (SELECT min(time) FROM statsrepo.snapshot);
	DELETE FROM statsrepo.checkpoint WHERE start < (SELECT min(time) FROM statsrepo.snapshot);
$$
LANGUAGE sql;

-- del_snapshot(time) - delete snapshots before the specified time.
CREATE FUNCTION statsrepo.del_snapshot(timestamptz) RETURNS void AS
$$
	DELETE FROM statsrepo.snapshot WHERE time < $1;
	DELETE FROM statsrepo.autovacuum WHERE start < (SELECT min(time) FROM statsrepo.snapshot);
	DELETE FROM statsrepo.autoanalyze WHERE start < (SELECT min(time) FROM statsrepo.snapshot);
	DELETE FROM statsrepo.checkpoint WHERE start < (SELECT min(time) FROM statsrepo.snapshot);
$$
LANGUAGE sql;

------------------------------------------------------------------------------
-- utility function for reporter.
------------------------------------------------------------------------------

-- tps() - transaction per seconds
CREATE FUNCTION statsrepo.tps(numeric, interval) RETURNS numeric AS
'SELECT ($1 / extract(epoch FROM $2))::numeric(1000, 3)'
LANGUAGE sql IMMUTABLE STRICT;

-- div() - NULL-safe operator /
CREATE FUNCTION statsrepo.div(numeric, numeric) RETURNS numeric AS
'SELECT (CASE WHEN $2 > 0 THEN $1 / $2 END)::numeric(1000, 3)'
LANGUAGE sql IMMUTABLE STRICT;

-- sub() - NULL-safe operator -
CREATE FUNCTION statsrepo.sub(anyelement, anyelement) RETURNS anyelement AS
'SELECT coalesce($1, 0) - coalesce($2, 0)'
LANGUAGE sql;

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
SELECT (regexp_matches(array_to_string($1, '/'),
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
			pg_size_pretty(sum(ed.size)::int8),
			sum(ed.xact_commit) - sum(sd.xact_commit),
			sum(ed.xact_rollback) - sum(sd.xact_rollback)
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
	OUT tup_fetch_tps		numeric
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
		statsrepo.div(
			statsrepo.sub(ed.blks_hit, sd.blks_hit),
			statsrepo.sub(ed.blks_read, sd.blks_read) +
			statsrepo.sub(ed.blks_hit, sd.blks_hit)) * 100,
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
			es.time - ss.time)
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
CREATE FUNCTION statsrepo.get_xact_tendency(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT snapid			bigint,
	OUT datname			name,
	OUT commit_tps		double precision,
	OUT rollback_tps	double precision
) RETURNS SETOF record AS
$$
	SELECT
		snapid,
		name,
		"commit/s",
		"rollback/s"
	FROM
	(
		SELECT
			d.snapid,
			d.name,
			coalesce((xact_commit - lag(xact_commit) OVER w) / duration, 0) AS "commit/s",
			coalesce((xact_rollback - lag(xact_rollback) OVER w) / duration, 0) AS "rollback/s"
		FROM
			(SELECT
				d.snapid,
				d.name,
				sum(xact_commit) AS xact_commit,
				sum(xact_rollback) AS xact_rollback
			 FROM
				statsrepo.database d,
				statsrepo.snapshot s
			 WHERE
			 	d.snapid BETWEEN $1 AND $2
				AND d.snapid = s.snapid
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
			 	d.snapid, d.name) AS d,
			(SELECT
				snapid,
				extract(epoch FROM time - lag(time) OVER (ORDER BY snapid))::float AS duration
			 FROM
				statsrepo.snapshot
			 WHERE
			 	instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)) AS s
		WHERE
			s.snapid = d.snapid
		WINDOW w AS (PARTITION BY d.name ORDER BY s.snapid)
		ORDER BY
			s.snapid, d.name
	) t
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Database Size'
CREATE FUNCTION statsrepo.get_dbsize_tendency(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT snapid		bigint,
	OUT datname		name,
	OUT size		numeric
) RETURNS SETOF record AS
$$
	SELECT
		d.snapid,
		d.name,
		sum(size) / 1024 / 1024
	FROM
		statsrepo.database d,
		statsrepo.snapshot s
	WHERE
		d.snapid BETWEEN $1 AND $2
		AND d.snapid = s.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	GROUP BY
		d.snapid, d.name
	ORDER BY
		d.snapid, d.name;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Instance Processes ratio'
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
		(100 * sum(idle)::float / sum(total)::float4)::numeric(5,2),
		(100 * sum(idle_in_xact)::float / sum(total)::float4)::numeric(5,2),
		(100 * sum(waiting)::float / sum(total)::float4)::numeric(5,2),
		(100 * sum(running)::float / sum(total)::float4)::numeric(5,2)
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
CREATE FUNCTION statsrepo.get_proc_tendency(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT snapid			bigint,
	OUT idle			double precision,
	OUT idle_in_xact	double precision,
	OUT waiting			double precision,
	OUT running			double precision
) RETURNS SETOF record AS
$$
	SELECT
		a.snapid,
		idle, 
		idle_in_xact,
		waiting,
		running
	FROM
		statsrepo.activity a,
		statsrepo.snapshot s
	WHERE
		a.snapid BETWEEN $1 AND $2
		AND a.snapid = s.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		AND idle IS NOT NULL
	ORDER BY
		a.snapid;
$$
LANGUAGE sql;

-- generate information that corresponds to 'CPU Usage'
CREATE FUNCTION statsrepo.get_cpu_usage(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT "user"		numeric,
	OUT system		numeric,
	OUT idle		numeric,
	OUT iowait		numeric
) RETURNS SETOF record AS
$$
	SELECT
		(100 * statsrepo.sub(a.cpu_user, b.cpu_user)::float / statsrepo.sub(a.total, b.total)::float4)::numeric(5,2),
		(100 * statsrepo.sub(a.cpu_system, b.cpu_system)::float / statsrepo.sub(a.total, b.total)::float4)::numeric(5,2),
		(100 * statsrepo.sub(a.cpu_idle, b.cpu_idle)::float / statsrepo.sub(a.total, b.total)::float4)::numeric(5,2),
		(100 * statsrepo.sub(a.cpu_iowait, b.cpu_iowait)::float / statsrepo.sub(a.total, b.total)::float4)::numeric(5,2)
	FROM
		(SELECT
			snapid,
			cpu_user,
			cpu_system,
			cpu_idle,
			cpu_iowait, 
			cpu_user + cpu_system + cpu_idle + cpu_iowait AS total
		 FROM
		 	statsrepo.cpu
		 WHERE
		 	snapid = $1) b,
		(SELECT
			snapid,
			cpu_user,
			cpu_system,
			cpu_idle,
			cpu_iowait, 
			cpu_user + cpu_system + cpu_idle + cpu_iowait AS total
		 FROM
		 	statsrepo.cpu
		 WHERE
		 	snapid = $2) a,
		statsrepo.snapshot s
	WHERE
		a.snapid = s.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2);
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
		(100 * statsrepo.div(t.user, t.total))::numeric(5,2),
		(100 * statsrepo.div(t.system, t.total))::numeric(5,2),
		(100 * statsrepo.div(t.idle, t.total))::numeric(5,2),
		(100 * statsrepo.div(t.iowait, t.total))::numeric(5,2)
	FROM
	(
		SELECT
			c.snapid,
			(cpu_user - lag(cpu_user) OVER w) AS user,
			(cpu_system - lag(cpu_system) OVER w) AS system,
			(cpu_idle - lag(cpu_idle) OVER w) AS idle,
			(cpu_iowait - lag(cpu_iowait) OVER w) AS iowait,
			(cpu_user + cpu_system + cpu_idle + cpu_iowait) -
				(lag(cpu_user) OVER w + lag(cpu_system) OVER w + lag(cpu_idle) OVER w + lag(cpu_iowait) OVER w ) AS total
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
	OUT io_queue			bigint,
	OUT total_io_time		bigint
) RETURNS SETOF record AS
$$
	SELECT
		a.device_name,
		a.device_tblspaces,
		statsrepo.sub(a.drs, b.drs) / 2 / 1024,
		statsrepo.sub(a.dws, b.dws) / 2 / 1024,
		statsrepo.sub(a.drt, b.drt),
		statsrepo.sub(a.dwt, b.dwt),
		statsrepo.sub(a.diq, b.diq),
		statsrepo.sub(a.dit, b.dit)
	FROM
		(SELECT
			snapid,
			device_name,
			device_tblspaces,
			device_readsector as drs,
			device_readtime as drt,
			device_writesector as dws,
			device_writetime as dwt, 
			device_ioqueue as diq,
			device_iototaltime as dit
		 FROM
		 	statsrepo.device
		 WHERE
		 	snapid = $1) b,
		(SELECT
			snapid,
			device_name,
			device_tblspaces,
			device_readsector as drs,
			device_readtime as drt,
			device_writesector as dws,
			device_writetime as dwt, 
			device_ioqueue as diq,
			device_iototaltime as dit
		 FROM
		 	statsrepo.device
		 WHERE
		 	snapid = $2) a,
		statsrepo.snapshot s
	WHERE
		a.snapid = s.snapid
		AND a.device_name = b.device_name
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2);
$$
LANGUAGE sql;

-- generate information that corresponds to 'IO Usage'
CREATE FUNCTION statsrepo.get_io_usage_tendency(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT snapid			bigint,
	OUT device_name		text,
	OUT read_size_tps	numeric,
	OUT write_size_tps	numeric,
	OUT read_time_tps	numeric,
	OUT write_time_tps	numeric
) RETURNS SETOF record AS
$$
	SELECT
		snapid,
		dev_name,
		read_size_tps,
		write_size_tps,
		read_time_tps,
		write_time_tps
	FROM
	(
		SELECT
			d.snapid,
			dev_name,
			coalesce((rs - lag(rs) OVER w) / 2 / duration, 0)::numeric(1000,2) AS read_size_tps,
			coalesce((ws - lag(ws) OVER w) / 2 / duration, 0)::numeric(1000,2) AS write_size_tps,
			coalesce((rt - lag(rt) OVER w) / duration, 0)::numeric(1000,2) AS read_time_tps,
			coalesce((wt - lag(wt) OVER w) / duration, 0)::numeric(1000,2) AS write_time_tps
		FROM
			(SELECT
				d.snapid,
				d.device_name as dev_name,
				sum(device_readsector) AS rs,
				sum(device_writesector) AS ws,
				sum(device_readtime) AS rt,
				sum(device_writetime) AS wt
			 FROM
				statsrepo.device d,
				statsrepo.snapshot s
			 WHERE
				d.snapid BETWEEN $1 AND $2
				AND d.snapid = s.snapid
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
				d.snapid, d.device_name) AS d,
			(SELECT
				snapid,
				extract(epoch FROM time - lag(time) OVER (ORDER BY snapid))::float AS duration
			 FROM
			 	statsrepo.snapshot
			 WHERE
			 	instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)) AS s
		WHERE
			s.snapid = d.snapid
		WINDOW w AS (PARTITION BY d.dev_name ORDER BY d.snapid)
		ORDER BY
			d.snapid, d.dev_name
	) t
	WHERE
		snapid > $1;
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
		(100.0 * avail / total)::numeric(1000, 3)
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
		max_xact_pid,
		max_xact_client,
		max_xact_start::timestamp(0),
		max_xact_duration::numeric(1000, 3),
		max_xact_query
	FROM
		statsrepo.activity a,
		statsrepo.snapshot s
	WHERE
		a.snapid BETWEEN $1 AND $2
		AND a.snapid = s.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		AND max_xact_pid <> 0
	ORDER BY
		max_xact_duration DESC;
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
		statsrepo.div(
			statsrepo.sub(e.n_tup_hot_upd, b.n_tup_hot_upd),
			statsrepo.sub(e.n_tup_upd, b.n_tup_upd)) * 100
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
		statsrepo.div(
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
			statsrepo.sub(e.tidx_blks_read, b.tidx_blks_read)) * 100
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
	OUT tratio			bigint
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
			WHEN 0 THEN NULL ELSE logical_pages * 100 / physical_pages END AS tratio
	FROM
	(
		SELECT
			t.database, 
			t.schema, 
	 		t.table, 
			t.n_live_tup,
			ceil(t.n_live_tup::real / (8168 * statsrepo.pg_fillfactor(t.reloptions, 0) / 100 /
				(width + 28)))::bigint AS logical_pages,
			(t.size + CASE t.toastrelid WHEN 0 THEN 0 ELSE tt.size END) / 8192 AS physical_pages
		 FROM
		 	statsrepo.tables t
		 	LEFT JOIN
		 		(SELECT
		 			snapid, dbid, tbl, sum(avg_width)::integer + 7 & ~7 AS width
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

-- generate information that corresponds to 'Fragmented Tables'
CREATE FUNCTION statsrepo.get_flagmented_tables(
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
		AND i.isclustered
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
	OUT ckpt_xlog		bigint,
	OUT avg_write_buff	numeric,
	OUT max_write_buff	numeric,
	OUT avg_duration	numeric,
	OUT max_duration	numeric
) RETURNS SETOF record AS
$$
	SELECT
		count(*),
		count(nullif(position('time' IN flags), 0)),
		count(nullif(position('xlog' IN flags), 0)),
		round(avg(num_buffers)::numeric,3),
		round(max(num_buffers)::numeric,3),
		round(avg(total_duration)::numeric,3),
		round(max(total_duration)::numeric,3)
	FROM
		statsrepo.checkpoint c,
		(SELECT min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1) b,
		(SELECT max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2) e
	WHERE
		c.start BETWEEN b.time AND e.time
		AND c.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2);
$$
LANGUAGE sql;

-- generate information that corresponds to 'Autovacuum Activity'
CREATE FUNCTION statsrepo.get_autovacuum_activity(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			text,
	OUT nspname			text,
	OUT relname			text,
	OUT "count"			bigint,
	OUT avg_index_scans	numeric,
	OUT avg_tup_removed	numeric,
	OUT avg_tup_remain	numeric,
	OUT avg_duration	numeric,
	OUT max_duration	numeric
) RETURNS SETOF record AS
$$
	SELECT
		database,
		schema,
		"table",
		count(*),
		round(avg(index_scans)::numeric,3),
		round(avg(tup_removed)::numeric,3),
		round(avg(tup_remain)::numeric,3),
		round(avg(duration)::numeric,3),
		round(max(duration)::numeric,3)
	FROM
		statsrepo.autovacuum v,
		(SELECT min(time) AS time FROM statsrepo.snapshot WHERE snapid >= $1) b,
		(SELECT max(time) AS time FROM statsrepo.snapshot WHERE snapid <= $2) e
	WHERE
		v.start BETWEEN b.time AND e.time
		AND v.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	GROUP BY
		database, schema, "table"
	ORDER BY
		5 DESC;
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
	OUT total_time		bigint,
	OUT self_time		bigint,
	OUT time_per_call	numeric
) RETURNS SETOF record AS
$$
	SELECT
		fe.funcid,
		d.name,
		s.name,
		fe.funcname,
		statsrepo.sub(fe.calls, fb.calls),
		statsrepo.sub(fe.total_time, fb.total_time),
		statsrepo.sub(fe.self_time, fb.self_time),
		statsrepo.div(
			statsrepo.sub(fe.total_time, fb.total_time),
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
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT rolname			text,
	OUT datname			name,
	OUT query			text,
	OUT calls			bigint,
	OUT total_time		numeric,
	OUT time_per_call	numeric
) RETURNS SETOF record AS
$$
	SELECT
		r.name,
		d.name,
		se.query,
		statsrepo.sub(se.calls, sb.calls),
		statsrepo.sub(se.total_time, sb.total_time)::numeric(1000, 3),
		statsrepo.div(
			statsrepo.sub(se.total_time, sb.total_time)::numeric,
			statsrepo.sub(se.calls, sb.calls))
	FROM
		statsrepo.statement se LEFT JOIN statsrepo.statement sb
			ON sb.snapid = $1 AND sb.dbid = se.dbid AND sb.query = se.query,
		statsrepo.database d,
		statsrepo.role r
	WHERE
		se.snapid = $2
		AND d.snapid = $2
		AND r.snapid = $2
		AND d.dbid = se.dbid
		AND r.userid = se.userid
	ORDER BY
		5 DESC,
		4 DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Setting Parameters'
CREATE FUNCTION statsrepo.get_setting_parameters(
	IN snapid_begin	bigint,
	IN snapid_end	bigint,
	OUT name		text,
	OUT setting		text,
	OUT source		text
) RETURNS SETOF record AS
$$
	SELECT
		so.name,
		CASE WHEN sa.setting = so.setting THEN
			so.setting
		ELSE
			coalesce(sa.setting, '(default)') || ' -> ' || coalesce(so.setting, '(default)')
		END,
		so.source
	FROM
		statsrepo.setting so LEFT JOIN statsrepo.setting sa
			ON so.name = sa.name AND sa.snapid = $1
	WHERE
		so.snapid = (SELECT MIN(snapid) FROM statsrepo.setting WHERE snapid >= $2);
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
	OUT avg_width	bigint,
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
		c.avg_width,
		e.size / 1024 / 1024,
		statsrepo.sub(e.size, b.size) / 1024 / 1024,
		statsrepo.sub(e.seq_scan, b.seq_scan),
		statsrepo.sub(e.idx_scan, b.idx_scan)
	FROM
		statsrepo.tables e LEFT JOIN statsrepo.table b
			ON e.tbl = b.tbl AND e.nsp = b.nsp AND e.dbid = b.dbid AND b.snapid = $1,
		(SELECT
			dbid,
			tbl,
			count(*) AS "columns",
			sum(avg_width) AS avg_width
		 FROM
		 	statsrepo.column
		 WHERE
		 	snapid = $2
		 GROUP BY
		 	dbid, tbl) AS c
	WHERE
		e.snapid = $2
		AND e.schema NOT IN ('pg_catalog', 'pg_toast', 'information_schema', 'statsrepo')
		AND e.tbl = c.tbl
		AND e.dbid = c.dbid
	ORDER BY
		1,
		2,
		3;
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
		(regexp_matches(e.indexdef, E'.*USING[^\\(]+\\((.*)\\)'))[1]
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

-- generate information that corresponds to 'Profiles'
CREATE FUNCTION statsrepo.get_profiles(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT processing		text,
	OUT executes		numeric
) RETURNS SETOF record AS
$$
	SELECT
		processing,
		(sum(execute)::float / ($2::bigint - $1::bigint + 1)::float)::numeric(1000,2) AS executes
	FROM
		statsrepo.profile
	WHERE
		snapid BETWEEN $1 and $2
	GROUP BY
		processing
	ORDER BY
		executes;
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
CREATE FUNCTION statsrepo.partition_new(parent_oid oid, date date)
RETURNS void AS
$$
DECLARE
	parent_name	name;
	child_name	name;
	condef		text;
BEGIN
	parent_name := relname FROM pg_class WHERE oid = parent_oid;
	child_name := parent_name || to_char(date, '_YYYYMMDD');

	/* child table already exists */
	PERFORM 1 FROM pg_inherits i LEFT JOIN pg_class c ON c.oid = i.inhrelid
	WHERE i.inhparent = parent_oid AND c.relname = child_name;
	IF FOUND THEN
		RETURN;
	END IF;

	/* create child table */
	IF NOT FOUND THEN
		EXECUTE 'CREATE TABLE statsrepo.' || child_name
			|| ' (LIKE statsrepo.' || parent_name
			|| ' INCLUDING INDEXES INCLUDING DEFAULTS INCLUDING CONSTRAINTS,'
			|| ' CHECK (date >= DATE ''' || to_char(date, 'YYYY-MM-DD') || ''''
			|| ' AND date < DATE ''' || to_char(date + 1, 'YYYY-MM-DD') || ''')'
			|| ' ) INHERITS (statsrepo.' || parent_name || ')';
		
		/* add foreign key constraint */
		FOR condef IN SELECT statsrepo.get_constraintdef(parent_oid) LOOP
		    EXECUTE 'ALTER TABLE statsrepo.' || child_name || ' ADD ' || condef;
		END LOOP;
	END IF;
END;
$$ LANGUAGE plpgsql;

-- partition_drop(date, oid) - drop partition-table.
CREATE FUNCTION statsrepo.partition_drop(date, oid)
RETURNS void AS
$$
DECLARE
	parent_name			name;
	child_name			name;
	tblname				name;
BEGIN
	parent_name := relname FROM pg_class WHERE oid = $2;
	child_name := parent_name || to_char($1, '_YYYYMMDD');

	FOR tblname IN
		SELECT c.relname FROM pg_inherits i LEFT JOIN pg_class c ON c.oid = i.inhrelid
		WHERE i.inhparent = $2 AND c.relname < child_name
	LOOP
		EXECUTE 'DROP TABLE IF EXISTS statsrepo.' || tblname;
	END LOOP;
END;
$$
LANGUAGE plpgsql;

-- function to create partition-tables
CREATE FUNCTION statsrepo.partition_create() RETURNS TRIGGER AS
$$
DECLARE
BEGIN
	SET client_min_messages = warning;
	PERFORM statsrepo.partition_new('statsrepo.table'::regclass, CAST(new.time AS DATE));
	PERFORM statsrepo.partition_new('statsrepo.index'::regclass, CAST(new.time AS DATE));
	PERFORM statsrepo.partition_new('statsrepo.column'::regclass, CAST(new.time AS DATE));
	RESET client_min_messages;
	RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- function to insert partition-table
CREATE FUNCTION statsrepo.partition_insert() RETURNS TRIGGER AS
$$
DECLARE
BEGIN
	EXECUTE 'INSERT INTO statsrepo.'
		|| TG_TABLE_NAME || to_char(new.date, '_YYYYMMDD') || ' VALUES(($1).*)' USING new;
	RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- trigger registration for partitioning
CREATE TRIGGER partition_create_snapshot AFTER INSERT ON statsrepo.snapshot FOR EACH ROW EXECUTE PROCEDURE statsrepo.partition_create();
CREATE TRIGGER partition_insert_table BEFORE INSERT ON statsrepo.table FOR EACH ROW EXECUTE PROCEDURE statsrepo.partition_insert();
CREATE TRIGGER partition_insert_index BEFORE INSERT ON statsrepo.index FOR EACH ROW EXECUTE PROCEDURE statsrepo.partition_insert();
CREATE TRIGGER partition_insert_column BEFORE INSERT ON statsrepo.column FOR EACH ROW EXECUTE PROCEDURE statsrepo.partition_insert();

-- del_snapshot2(time) - delete snapshots before the specified time.
CREATE FUNCTION statsrepo.del_snapshot2(timestamptz) RETURNS void AS
$$
	SELECT statsrepo.partition_drop(CAST($1 AS DATE), 'statsrepo.table'::regclass);
	SELECT statsrepo.partition_drop(CAST($1 AS DATE), 'statsrepo.index'::regclass);
	SELECT statsrepo.partition_drop(CAST($1 AS DATE), 'statsrepo.column'::regclass);
	SELECT statsrepo.del_snapshot($1);
$$
LANGUAGE sql;

COMMIT;
