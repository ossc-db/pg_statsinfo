/*
 * bin/pg_statsrepo.sql
 *
 * Create a repository schema for PostgreSQL 8.3.
 *
 * Copyright (c) 2010-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
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
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid),
	FOREIGN KEY (snapid, dbid, tbl) REFERENCES statsrepo.table (snapid, dbid, tbl)
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
	FOREIGN KEY (snapid, dbid) REFERENCES statsrepo.database (snapid, dbid),
	FOREIGN KEY (snapid, dbid, tbl) REFERENCES statsrepo.table (snapid, dbid, tbl)
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
	total_time		double precision,
	self_time		double precision,
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
	page_hit		integer,
	page_miss		integer,
	page_dirty		integer,
	read_rate		double precision,
	write_rate		double precision,
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
	datname				name,
	nspname				name,
	relname				name,
	blocker_appname		text,
	blocker_addr		inet,
	blocker_hostname	text,
	blocker_port		integer,
	blockee_pid			integer,
	blocker_pid			integer,
	blocker_gid			text,
	duration			interval,
	blockee_query		text,
	blocker_query		text,
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
	state				text,
	current_location	text,
	sent_location		text,
	write_location		text,
	flush_location		text,
	replay_location		text,
	sync_priority		integer,
	sync_state			text,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

CREATE TABLE statsrepo.xlog
(
	snapid		bigint,
	location	text,
	xlogfile	text,
	FOREIGN KEY (snapid) REFERENCES statsrepo.snapshot (snapid) ON DELETE CASCADE
);

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

-- function to create partition-tables (nothing to do because does not partitioned)
CREATE FUNCTION statsrepo.create_partition(timestamptz) RETURNS void AS
$$
	/* do nothing */
$$ LANGUAGE sql;

------------------------------------------------------------------------------
-- utility function for reporter.
------------------------------------------------------------------------------

-- get_version() - version of statsrepo schema
CREATE FUNCTION statsrepo.get_version() RETURNS text AS
'SELECT CAST(''20400'' AS TEXT)'
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

-- convert_hex() - convert a hexadecimal string to a decimal number
CREATE FUNCTION statsrepo.convert_hex(text)
RETURNS bigint AS
$$
	SELECT
		(sum((16::numeric ^ (length($1) - i)) *
			position(upper(substring($1 from i for 1)) in '123456789ABCDEF')))::bigint
	FROM
		generate_series(1, length($1)) AS t(i);
$$
LANGUAGE sql IMMUTABLE STRICT;

-- xlog_location_diff() - compute the difference in bytes between two WAL locations
CREATE FUNCTION statsrepo.xlog_location_diff(text, text)
RETURNS numeric AS
$$
	/* XLogFileSize * (xlogid1 - xlogid2) + xrecoff1 - xrecoff2 */
	SELECT
		(X'FF000000'::bigint * (t.xlogid1 - t.xlogid2)::numeric + t.xrecoff1 - t.xrecoff2)
	FROM
	(
		SELECT
			statsrepo.convert_hex(lsn1[1]) AS xlogid1,
			statsrepo.convert_hex(lsn1[2]) AS xrecoff1,
			statsrepo.convert_hex(lsn2[1]) AS xlogid2,
			statsrepo.convert_hex(lsn2[2]) AS xrecoff2
		 FROM
			regexp_matches($1, '^([0-F]{1,8})/([0-F]{1,8})$') AS lsn1,
			regexp_matches($2, '^([0-F]{1,8})/([0-F]{1,8})$') AS lsn2
	) t;
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

-- repository database information
CREATE FUNCTION statsrepo.get_information(
	IN  host_in		text,
	IN  port_in		text,
	OUT host		text,
	OUT port		text,
	OUT "database"	name,
	OUT encoding	name,
	OUT start		timestamp(0),
	OUT version		text
) RETURNS record AS
$$
	SELECT
		$1,
		$2,
		datname,
		pg_encoding_to_char(encoding),
		pg_postmaster_start_time()::timestamp(0),
		version()
	FROM
		pg_catalog.pg_database
	WHERE
		datname = current_database();
$$
LANGUAGE sql;

-- repository database setting parameters
CREATE FUNCTION statsrepo.get_repositorydb_setting(
	OUT name	text,
	OUT setting	text,
	OUT unit	text,
	OUT source	text
) RETURNS SETOF record AS
$$
	SELECT
		name,
		setting,
		unit,
		source
	FROM
		pg_catalog.pg_settings
	WHERE
		source NOT IN ('default', 'session', 'override')
	ORDER BY lower(name);
$$
LANGUAGE sql;

-- table options
CREATE FUNCTION statsrepo.get_table_option(
	IN  name	text,
	OUT option	text
) RETURNS text AS
$$
	SELECT
		pg_catalog.array_to_string(c.reloptions, ', ')
	FROM
		pg_catalog.pg_class c
	WHERE
		c.oid = $1::regclass;
$$
LANGUAGE sql;

-- snapshot list
CREATE FUNCTION statsrepo.get_snapshot_list(
	IN  instid		bigint,
	OUT snapid		bigint,
	OUT check_box	xml,
	OUT "timestamp"	timestamp(0),
	OUT size		numeric,
	OUT diff_size	numeric,
	OUT commits		numeric(1000,3),
	OUT	rollbacks	numeric(1000,3),
	OUT comment		text
) RETURNS SETOF record AS
$$
	SELECT
		e.snapid,
		xmlelement(	name input,
					xmlattributes(	'checkbox' AS  type,
									to_char(e.snapid, '00000') AS pos,
									e.snapid AS value)
					),
		time::timestamp(0),
		round(ed.size / 1024 / 1024, 0),
		round((ed.size - sd.size) / 1024 / 1024, 0),
		statsrepo.tps(ed.commits - sd.commits, time - time0),
		statsrepo.tps(ed.rollbacks - sd.rollbacks, time - time0),
		comment
	FROM
		(SELECT s1.*,
				(SELECT s2.snapid
				   FROM statsrepo.snapshot s2
				  WHERE s1.snapid > s2.snapid
				    AND instid = $1
				 ORDER BY s2.snapid DESC LIMIT 1) AS snapid0,
				(SELECT s2.time
				   FROM statsrepo.snapshot s2
				  WHERE s1.snapid > s2.snapid
				    AND instid = $1
				 ORDER BY s2.snapid DESC LIMIT 1) AS time0
		FROM statsrepo.snapshot s1
		WHERE s1.instid = $1) e
		LEFT JOIN
			(SELECT	snapid,
					sum(size) AS size,
					sum(xact_commit) AS commits,
					sum(xact_rollback) AS rollbacks
			FROM statsrepo.database GROUP BY snapid) AS ed
			ON ed.snapid = e.snapid
		LEFT JOIN
			(SELECT snapid,
					sum(size) AS size,
					sum(xact_commit) AS commits,
					sum(xact_rollback) AS rollbacks
			FROM statsrepo.database GROUP BY snapid) AS sd
			ON sd.snapid = e.snapid0;
$$
LANGUAGE sql;

-- refine snapshot list
CREATE FUNCTION statsrepo.get_snapshot_list_refine(
	IN  begin_snapid		bigint,
	OUT snapid		bigint,
	OUT check_box	xml,
	OUT "timestamp"	timestamp(0),
	OUT size		numeric,
	OUT diff_size	numeric,
	OUT commits		numeric(1000,3),
	OUT	rollbacks	numeric(1000,3),
	OUT comment		text
) RETURNS SETOF record AS
$$
	SELECT
		e.snapid,
		xmlelement(	name input,
					xmlattributes(	'checkbox' AS  type,
									to_char(e.snapid, '00000') AS pos,
									e.snapid AS value)
					),
		time::timestamp(0),
		round(ed.size / 1024 / 1024, 0),
		round((ed.size - sd.size) / 1024 / 1024, 0),
		statsrepo.tps(ed.commits - sd.commits, time - time0),
		statsrepo.tps(ed.rollbacks - sd.rollbacks, time - time0),
		comment
	FROM
		(SELECT s1.*,
				(SELECT s2.snapid
				   FROM statsrepo.snapshot s2
				  WHERE s1.snapid > s2.snapid
				    AND instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $1)
				 ORDER BY s2.snapid DESC LIMIT 1) AS snapid0,
				(SELECT s2.time
				   FROM statsrepo.snapshot s2
				  WHERE s1.snapid > s2.snapid
				    AND instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $1)
				 ORDER BY s2.snapid DESC LIMIT 1) AS time0
		FROM statsrepo.snapshot s1
		WHERE s1.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $1)) e
		LEFT JOIN
			(SELECT	snapid,
					sum(size) AS size,
					sum(xact_commit) AS commits,
					sum(xact_rollback) AS rollbacks
			FROM statsrepo.database GROUP BY snapid) AS ed
			ON ed.snapid = e.snapid
		LEFT JOIN
			(SELECT snapid,
					sum(size) AS size,
					sum(xact_commit) AS commits,
					sum(xact_rollback) AS rollbacks
			FROM statsrepo.database GROUP BY snapid) AS sd
			ON sd.snapid = e.snapid0;
$$
LANGUAGE sql;

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
		min(snapid)
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
		max(snapid)
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
		min(snapid)
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
		max(snapid)
	FROM statsrepo.snapshot s
		LEFT JOIN  statsrepo.instance i ON i.instid = s.instid
	WHERE i.instid = $1::integer
		AND time >= $2 AND time <= $3
$$
LANGUAGE sql STABLE;

-- get date of the corresponding snapshot from 'snapid'
CREATE FUNCTION statsrepo.get_snap_date(bigint) RETURNS date AS
'SELECT CAST(time AS DATE) FROM statsrepo.snapshot WHERE snapid = $1'
LANGUAGE sql IMMUTABLE STRICT; 

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
			statsrepo.pg_size_pretty(sum(ed.size)::int8),
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
CREATE FUNCTION statsrepo.get_xact_tendency(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT snapid			bigint,
	OUT datname			name,
	OUT commit_tps		numeric,
	OUT rollback_tps	numeric
) RETURNS SETOF record AS
$$
	SELECT
		snapid,
		name,
		coalesce(statsrepo.tps(xact_commit, duration), 0)::numeric(1000,3),
		coalesce(statsrepo.tps(xact_rollback, duration), 0)::numeric(1000,3)
	FROM
	(
		SELECT
			de.snapid,
			de.name,
			de.xact_commit - ds.xact_commit AS xact_commit,
			de.xact_rollback - de.xact_rollback AS xact_rollback,
			de.time - ds.time AS duration
		FROM
			(SELECT
				d.snapid,
				d.name,
				s.time,
				s.instid,
				sum(xact_commit) AS xact_commit,
				sum(xact_rollback) AS xact_rollback,
				(SELECT max(snapid) FROM statsrepo.snapshot WHERE snapid < d.snapid AND instid = s.instid) AS prev_snapid
			 FROM
			 	statsrepo.database d,
				statsrepo.snapshot s
			 WHERE
			 	d.snapid = s.snapid
			 	AND d.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
			 	d.snapid, d.name, s.time, s.instid) AS de,
			(SELECT
				d.snapid,
				d.name,
				s.time,
				s.instid,
				sum(xact_commit) AS xact_commit,
				sum(xact_rollback) AS xact_rollback,
				(SELECT min(snapid) FROM statsrepo.snapshot WHERE snapid > d.snapid AND instid = s.instid) AS next_snapid
			 FROM
			 	statsrepo.database d,
				statsrepo.snapshot s
			 WHERE
			 	d.snapid = s.snapid
			 	AND d.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
			 	d.snapid, d.name, s.time, s.instid) AS ds
		WHERE
			ds.snapid = de.prev_snapid
			AND de.snapid = ds.next_snapid
			AND ds.name = de.name
		ORDER BY
			de.snapid, de.name
	) t
	WHERE
		snapid > $1
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
		to_char(time, 'YYYY-MM-DD HH24:MI'),
		name,
		coalesce(statsrepo.tps(xact_commit, duration), 0)::numeric(1000,3),
		coalesce(statsrepo.tps(xact_rollback, duration), 0)::numeric(1000,3)
	FROM
	(
		SELECT
			de.snapid,
			de.time,
			de.name,
			de.xact_commit - ds.xact_commit AS xact_commit,
			de.xact_rollback - de.xact_rollback AS xact_rollback,
			de.time - ds.time AS duration
		FROM
			(SELECT
				d.snapid,
				d.name,
				s.time,
				s.instid,
				sum(xact_commit) AS xact_commit,
				sum(xact_rollback) AS xact_rollback,
				(SELECT max(snapid) FROM statsrepo.snapshot WHERE snapid < d.snapid AND instid = s.instid) AS prev_snapid
			 FROM
			 	statsrepo.database d,
				statsrepo.snapshot s
			 WHERE
			 	d.snapid = s.snapid
			 	AND d.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
			 	d.snapid, d.name, s.time, s.instid) AS de,
			(SELECT
				d.snapid,
				d.name,
				s.time,
				s.instid,
				sum(xact_commit) AS xact_commit,
				sum(xact_rollback) AS xact_rollback,
				(SELECT min(snapid) FROM statsrepo.snapshot WHERE snapid > d.snapid AND instid = s.instid) AS next_snapid
			 FROM
			 	statsrepo.database d,
				statsrepo.snapshot s
			 WHERE
			 	d.snapid = s.snapid
			 	AND d.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
			 	d.snapid, d.name, s.time, s.instid) AS ds
		WHERE
			ds.snapid = de.prev_snapid
			AND de.snapid = ds.next_snapid
			AND ds.name = de.name
		ORDER BY
			de.snapid, de.name
	) t
	WHERE
		snapid > $1
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
		(sum(size) / 1024 / 1024)::numeric(1000, 3)
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
		to_char(s.time, 'YYYY-MM-DD HH24:MI'),
		d.name,
		(sum(size) / 1024 / 1024)::numeric(1000, 3)
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
		CASE WHEN sum(total)::float4 = 0 THEN 0
			ELSE (100 * sum(idle)::float / sum(total)::float4)::numeric(5,2) END,
		CASE WHEN sum(total)::float4 = 0 THEN 0
			ELSE (100 * sum(idle_in_xact)::float / sum(total)::float4)::numeric(5,2) END,
		CASE WHEN sum(total)::float4 = 0 THEN 0
			ELSE (100 * sum(waiting)::float / sum(total)::float4)::numeric(5,2) END,
		CASE WHEN sum(total)::float4 = 0 THEN 0
			ELSE (100 * sum(running)::float / sum(total)::float4)::numeric(5,2) END
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
	OUT idle			numeric,
	OUT idle_in_xact	numeric,
	OUT waiting			numeric,
	OUT running			numeric
) RETURNS SETOF record AS
$$
	SELECT
		a.snapid,
		CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
			ELSE (100 * idle / (idle + idle_in_xact + waiting + running))::numeric(5,2) END,
		CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
			ELSE (100 * idle_in_xact / (idle + idle_in_xact + waiting + running))::numeric(5,2) END,
		CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
			ELSE (100 * waiting / (idle + idle_in_xact + waiting + running))::numeric(5,2) END,
		CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
			ELSE (100 * running / (idle + idle_in_xact + waiting + running))::numeric(5,2) END
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

-- generate information that corresponds to 'Instance Processes'
CREATE FUNCTION statsrepo.get_proc_tendency_report(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT "timestamp"		text,
	OUT idle			numeric,
	OUT idle_in_xact	numeric,
	OUT waiting			numeric,
	OUT running			numeric
) RETURNS SETOF record AS
$$
	SELECT
		to_char(s.time, 'YYYY-MM-DD HH24:MI'),
		CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
			ELSE (100 * idle / (idle + idle_in_xact + waiting + running))::numeric(5,2) END,
		CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
			ELSE (100 * idle_in_xact / (idle + idle_in_xact + waiting + running))::numeric(5,2) END,
		CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
			ELSE (100 * waiting / (idle + idle_in_xact + waiting + running))::numeric(5,2) END,
		CASE WHEN (idle + idle_in_xact + waiting + running) = 0 THEN 0
			ELSE (100 * running / (idle + idle_in_xact + waiting + running))::numeric(5,2) END
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

-- generate information that corresponds to 'WAL Statistics'
CREATE FUNCTION statsrepo.get_xlog_tendency(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
	OUT "timestamp"			text,
	OUT location			text,
	OUT xlogfile			text,
	OUT write_size			numeric,
	OUT write_size_per_sec	numeric
) RETURNS SETOF record AS
$$
	SELECT
		to_char(time, 'YYYY-MM-DD HH24:MI'),
		location,
		xlogfile,
		(write_size / 1024 / 1024)::numeric(1000, 3),
		(statsrepo.tps(write_size, duration) / 1024 / 1024)::numeric(1000, 3)
	FROM
	(
		SELECT
			xe.snapid,
			xe.time,
			xe.location,
			xe.xlogfile,
			statsrepo.xlog_location_diff(xe.location, xs.location) AS write_size,
			xe.time - xs.time AS duration
		FROM
		 	(SELECT
		 		s.snapid,
		 		s.time,
		 		x.location,
		 		x.xlogfile,
				(SELECT max(snapid) FROM statsrepo.snapshot WHERE snapid < s.snapid AND instid = s.instid) AS prev_snapid
		 	 FROM
			 	statsrepo.xlog x,
				statsrepo.snapshot s
			 WHERE
				x.snapid BETWEEN $1 AND $2
				AND x.snapid = s.snapid
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)) AS xe,
		 	(SELECT
		 		s.snapid,
		 		s.time,
		 		x.location,
		 		x.xlogfile,
				(SELECT min(snapid) FROM statsrepo.snapshot WHERE snapid > s.snapid AND instid = s.instid) AS next_snapid
		 	 FROM
			 	statsrepo.xlog x,
				statsrepo.snapshot s
			 WHERE
				x.snapid BETWEEN $1 AND $2
				AND x.snapid = s.snapid
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)) AS xs
		WHERE
			xs.snapid = xe.prev_snapid
			AND xe.snapid = xs.next_snapid
	) t
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'WAL Statistics'
CREATE FUNCTION statsrepo.get_xlog_stats(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT write_total		numeric,
	OUT write_speed		numeric
) RETURNS SETOF record AS
$$
	SELECT
		sum(write_size)::numeric(1000, 3),
		avg(write_size_per_sec)::numeric(1000, 3)
	FROM
		statsrepo.get_xlog_tendency($1, $2);
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
		(100 * statsrepo.sub(a.cpu_user + o.cpu_user_add, b.cpu_user)::float / statsrepo.sub(a.total + o.total_add, b.total)::float)::numeric(5,2),
		(100 * statsrepo.sub(a.cpu_system + o.cpu_system_add, b.cpu_system)::float / statsrepo.sub(a.total + o.total_add, b.total)::float)::numeric(5,2),
		(100 * statsrepo.sub(a.cpu_idle + o.cpu_idle_add, b.cpu_idle)::float / statsrepo.sub(a.total + o.total_add, b.total)::float)::numeric(5,2),
		(100 * statsrepo.sub(a.cpu_iowait + o.cpu_iowait_add, b.cpu_iowait)::float / statsrepo.sub(a.total + o.total_add, b.total)::float)::numeric(5,2)
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
		(SELECT
			(sum(overflow_user) * 4294967296)::bigint AS cpu_user_add,
			(sum(overflow_system) * 4294967296)::bigint AS cpu_system_add,
			(sum(overflow_idle) * 4294967296)::bigint AS cpu_idle_add,
			(sum(overflow_iowait) * 4294967296)::bigint AS cpu_iowait_add,
			((sum(overflow_user) + sum(overflow_system) + sum(overflow_idle) + sum(overflow_iowait)) * 4294967296)::bigint AS total_add
		 FROM
			statsrepo.cpu c
			LEFT JOIN statsrepo.snapshot s ON s.snapid = c.snapid
		 WHERE
			s.snapid > $1 AND s.snapid <= $2
			AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)) o,
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
			ce.snapid,
			(CASE WHEN ce.overflow_user = 1 THEN ce.cpu_user + 4294967296 ELSE ce.cpu_user END - cs.cpu_user) AS user,
			(CASE WHEN ce.overflow_system = 1 THEN ce.cpu_system + 4294967296 ELSE ce.cpu_system END - cs.cpu_system) AS system,
			(CASE WHEN ce.overflow_idle = 1 THEN ce.cpu_idle + 4294967296 ELSE ce.cpu_idle END - cs.cpu_idle) AS idle,
			(CASE WHEN ce.overflow_iowait = 1 THEN ce.cpu_iowait + 4294967296 ELSE ce.cpu_iowait END - cs.cpu_iowait) AS iowait,
			
			(CASE WHEN ce.overflow_user = 1 THEN ce.cpu_user + 4294967296 ELSE ce.cpu_user END +
			 CASE WHEN ce.overflow_system = 1 THEN ce.cpu_system + 4294967296 ELSE ce.cpu_system END +
			 CASE WHEN ce.overflow_idle = 1 THEN ce.cpu_idle + 4294967296 ELSE ce.cpu_idle END +
			 CASE WHEN ce.overflow_iowait = 1 THEN ce.cpu_iowait + 4294967296 ELSE ce.cpu_iowait END) -
			(cs.cpu_user + cs.cpu_system + cs.cpu_idle + cs.cpu_iowait) AS total
		FROM
			(SELECT
				s.snapid,
				s.instid,
				c.cpu_user,
				c.cpu_system,
				c.cpu_idle,
				c.cpu_iowait,
				c.overflow_user,
				c.overflow_system,
				c.overflow_idle,
				c.overflow_iowait,
				(SELECT max(snapid) FROM statsrepo.snapshot WHERE snapid < s.snapid AND instid = s.instid) AS prev_snapid
			 FROM
				statsrepo.cpu c,
				statsrepo.snapshot s
			 WHERE
				c.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
				AND s.snapid = c.snapid) AS ce,
			(SELECT
				s.snapid,
				s.instid,
				c.cpu_user,
				c.cpu_system,
				c.cpu_idle,
				c.cpu_iowait,
				c.overflow_user,
				c.overflow_system,
				c.overflow_idle,
				c.overflow_iowait,
				(SELECT min(snapid) FROM statsrepo.snapshot WHERE snapid > s.snapid AND instid = s.instid) AS next_snapid
			 FROM
				statsrepo.cpu c,
				statsrepo.snapshot s
			 WHERE
				c.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
				AND s.snapid = c.snapid) AS cs
		WHERE
			cs.snapid = ce.prev_snapid
			AND cs.instid = ce.instid
		ORDER BY
			ce.snapid
	) t
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'CPU Usage'
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
		to_char(t.time, 'YYYY-MM-DD HH24:MI'),
		(100 * statsrepo.div(t.user, t.total))::numeric(5,2),
		(100 * statsrepo.div(t.system, t.total))::numeric(5,2),
		(100 * statsrepo.div(t.idle, t.total))::numeric(5,2),
		(100 * statsrepo.div(t.iowait, t.total))::numeric(5,2)
	FROM
	(
		SELECT
			ce.snapid,
			ce.time,
			(CASE WHEN ce.overflow_user = 1 THEN ce.cpu_user + 4294967296 ELSE ce.cpu_user END - cs.cpu_user) AS user,
			(CASE WHEN ce.overflow_system = 1 THEN ce.cpu_system + 4294967296 ELSE ce.cpu_system END - cs.cpu_system) AS system,
			(CASE WHEN ce.overflow_idle = 1 THEN ce.cpu_idle + 4294967296 ELSE ce.cpu_idle END - cs.cpu_idle) AS idle,
			(CASE WHEN ce.overflow_iowait = 1 THEN ce.cpu_iowait + 4294967296 ELSE ce.cpu_iowait END - cs.cpu_iowait) AS iowait,
			
			(CASE WHEN ce.overflow_user = 1 THEN ce.cpu_user + 4294967296 ELSE ce.cpu_user END +
			 CASE WHEN ce.overflow_system = 1 THEN ce.cpu_system + 4294967296 ELSE ce.cpu_system END +
			 CASE WHEN ce.overflow_idle = 1 THEN ce.cpu_idle + 4294967296 ELSE ce.cpu_idle END +
			 CASE WHEN ce.overflow_iowait = 1 THEN ce.cpu_iowait + 4294967296 ELSE ce.cpu_iowait END) -
			(cs.cpu_user + cs.cpu_system + cs.cpu_idle + cs.cpu_iowait) AS total
		FROM
			(SELECT
				s.snapid,
				s.time,
				s.instid,
				c.cpu_user,
				c.cpu_system,
				c.cpu_idle,
				c.cpu_iowait,
				c.overflow_user,
				c.overflow_system,
				c.overflow_idle,
				c.overflow_iowait,
				(SELECT max(snapid) FROM statsrepo.snapshot WHERE snapid < s.snapid AND instid = s.instid) AS prev_snapid
			 FROM
				statsrepo.cpu c,
				statsrepo.snapshot s
			 WHERE
				c.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
				AND s.snapid = c.snapid) AS ce,
			(SELECT
				s.snapid,
				s.instid,
				c.cpu_user,
				c.cpu_system,
				c.cpu_idle,
				c.cpu_iowait,
				c.overflow_user,
				c.overflow_system,
				c.overflow_idle,
				c.overflow_iowait,
				(SELECT min(snapid) FROM statsrepo.snapshot WHERE snapid > s.snapid AND instid = s.instid) AS next_snapid
			 FROM
				statsrepo.cpu c,
				statsrepo.snapshot s
			 WHERE
				c.snapid BETWEEN $1 AND $2
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
				AND s.snapid = c.snapid) AS cs
		WHERE
			cs.snapid = ce.prev_snapid
			AND cs.instid = ce.instid
		ORDER BY
			ce.snapid
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
	OUT io_queue			numeric,
	OUT total_io_time		bigint
) RETURNS SETOF record AS
$$
	SELECT
		a.device_name,
		a.device_tblspaces,
		statsrepo.sub(a.drs + o.drs_add, b.drs) / 2 / 1024,
		statsrepo.sub(a.dws + o.dws_add, b.dws) / 2 / 1024,
		statsrepo.sub(a.drt + o.drt_add, b.drt),
		statsrepo.sub(a.dwt + o.dwt_add, b.dwt),
		round((o.diq + b.diq) / (o.cnt + 1), 3),
		statsrepo.sub(a.dit + o.dit_add, b.dit)
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
		(SELECT
			d.device_name,
			(sum(d.overflow_drs) * 4294967296)::bigint AS drs_add,
			(sum(d.overflow_drt) * 4294967296)::bigint AS drt_add,
			(sum(d.overflow_dws) * 4294967296)::bigint AS dws_add,
			(sum(d.overflow_dwt) * 4294967296)::bigint AS dwt_add,
			(sum(d.overflow_dit) * 4294967296)::bigint AS dit_add,
			sum(d.device_ioqueue) AS diq,
			count(*) AS cnt
		 FROM
			statsrepo.device d
			LEFT JOIN statsrepo.snapshot s ON s.snapid = d.snapid
		 WHERE
			s.snapid > $1 AND s.snapid <= $2
			AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		 GROUP BY
		 	d.device_name) o,
		statsrepo.snapshot s
	WHERE
		a.snapid = s.snapid
		AND a.device_name = b.device_name
		AND a.device_name = o.device_name
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
		device_name,
		coalesce(statsrepo.tps(read_size, duration) / 2, 0)::numeric(1000,2),
		coalesce(statsrepo.tps(write_size, duration) / 2, 0)::numeric(1000,2),
		coalesce(statsrepo.tps(read_time, duration), 0)::numeric(1000,2),
		coalesce(statsrepo.tps(write_time, duration), 0)::numeric(1000,2)
	FROM
	(
		SELECT
			de.snapid,
			de.device_name,
			de.rs - ds.rs AS read_size,
			de.ws - ds.ws AS write_size,
			de.rt - ds.rt AS read_time,
			de.wt - ds.wt AS write_time,
			de.time - ds.time AS duration
		FROM
			(SELECT
				d.snapid,
				d.device_name,
				sum(d.device_readsector) + (sum(d.overflow_drs) * 4294967296) AS rs,
				sum(d.device_writesector) + (sum(d.overflow_dws) * 4294967296) AS ws,
				sum(d.device_readtime) + (sum(d.overflow_drt) * 4294967296) AS rt,
				sum(d.device_writetime) + (sum(d.overflow_dwt) * 4294967296) AS wt,
				s.time,
				s.instid,
				(SELECT max(snapid) FROM statsrepo.snapshot WHERE snapid < d.snapid AND instid = s.instid) AS prev_snapid
			 FROM
				statsrepo.device d,
				statsrepo.snapshot s
			 WHERE
				d.snapid BETWEEN $1 AND $2
				AND d.snapid = s.snapid
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
				d.snapid, d.device_name, s.time, s.instid) AS de,
			(SELECT
				d.snapid,
				d.device_name,
				sum(d.device_readsector) AS rs,
				sum(d.device_writesector) AS ws,
				sum(d.device_readtime) AS rt,
				sum(d.device_writetime) AS wt,
				s.time,
				s.instid,
				(SELECT min(snapid) FROM statsrepo.snapshot WHERE snapid > d.snapid AND instid = s.instid) AS next_snapid
			 FROM
				statsrepo.device d,
				statsrepo.snapshot s
			 WHERE
				d.snapid BETWEEN $1 AND $2
				AND d.snapid = s.snapid
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
				d.snapid, d.device_name, s.time, s.instid) AS ds
		WHERE
			ds.snapid = de.prev_snapid
			AND ds.device_name = de.device_name
		ORDER BY
			de.snapid, de.device_name
	) t
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'IO Usage'
CREATE FUNCTION statsrepo.get_io_usage_tendency_report(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT "timestamp"		text,
	OUT device_name		text,
	OUT read_size_tps	numeric,
	OUT write_size_tps	numeric,
	OUT read_time_tps	numeric,
	OUT write_time_tps	numeric
) RETURNS SETOF record AS
$$
	SELECT
		to_char(time, 'YYYY-MM-DD HH24:MI'),
		device_name,
		coalesce(statsrepo.tps(read_size, duration) / 2, 0)::numeric(1000,2),
		coalesce(statsrepo.tps(write_size, duration) / 2, 0)::numeric(1000,2),
		coalesce(statsrepo.tps(read_time, duration), 0)::numeric(1000,2),
		coalesce(statsrepo.tps(write_time, duration), 0)::numeric(1000,2)
	FROM
	(
		SELECT
			de.snapid,
			de.time,
			de.device_name,
			de.rs - ds.rs AS read_size,
			de.ws - ds.ws AS write_size,
			de.rt - ds.rt AS read_time,
			de.wt - ds.wt AS write_time,
			de.time - ds.time AS duration
		FROM
			(SELECT
				d.snapid,
				d.device_name,
				sum(d.device_readsector) + (sum(d.overflow_drs) * 4294967296) AS rs,
				sum(d.device_writesector) + (sum(d.overflow_dws) * 4294967296) AS ws,
				sum(d.device_readtime) + (sum(d.overflow_drt) * 4294967296) AS rt,
				sum(d.device_writetime) + (sum(d.overflow_dwt) * 4294967296) AS wt,
				s.time,
				s.instid,
				(SELECT max(snapid) FROM statsrepo.snapshot WHERE snapid < d.snapid AND instid = s.instid) AS prev_snapid
			 FROM
				statsrepo.device d,
				statsrepo.snapshot s
			 WHERE
				d.snapid BETWEEN $1 AND $2
				AND d.snapid = s.snapid
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
				d.snapid, d.device_name, s.time, s.instid) AS de,
			(SELECT
				d.snapid,
				d.device_name,
				sum(d.device_readsector) AS rs,
				sum(d.device_writesector) AS ws,
				sum(d.device_readtime) AS rt,
				sum(d.device_writetime) AS wt,
				s.time,
				s.instid,
				(SELECT min(snapid) FROM statsrepo.snapshot WHERE snapid > d.snapid AND instid = s.instid) AS next_snapid
			 FROM
				statsrepo.device d,
				statsrepo.snapshot s
			 WHERE
				d.snapid BETWEEN $1 AND $2
				AND d.snapid = s.snapid
				AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
			 GROUP BY
				d.snapid, d.device_name, s.time, s.instid) AS ds
		WHERE
			ds.snapid = de.prev_snapid
			AND ds.device_name = de.device_name
		ORDER BY
			de.snapid, de.device_name
	) t
	WHERE
		snapid > $1;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Load Average'
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
		to_char(s.time, 'YYYY-MM-DD HH24:MI'),
		l.loadavg1::numeric(1000, 3),
		l.loadavg5::numeric(1000, 3),
		l.loadavg15::numeric(1000, 3)
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
		to_char(s.time, 'YYYY-MM-DD HH24:MI'),
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
		a.max_xact_pid,
		a.max_xact_client,
		a.max_xact_start::timestamp(0),
		max(a.max_xact_duration)::numeric(1000, 3) AS duration,
		a.max_xact_query
	FROM
		statsrepo.activity a,
		statsrepo.snapshot s
	WHERE
		a.snapid BETWEEN $1 AND $2
		AND a.snapid = s.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
		AND max_xact_pid <> 0
	GROUP BY
		a.max_xact_pid,
		a.max_xact_client,
		a.max_xact_start,
		a.max_xact_query
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

-- generate information that corresponds to 'Autovacuum Activity'
CREATE FUNCTION statsrepo.get_autovacuum_activity2(
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT datname			text,
	OUT nspname			text,
	OUT relname			text,
	OUT avg_page_hit	numeric,
	OUT avg_page_miss	numeric,
	OUT avg_page_dirty	numeric,
	OUT avg_read_rate	numeric,
	OUT avg_write_rate	numeric
) RETURNS SETOF record AS
$$
	SELECT
		database,
		schema,
		"table",
		round(avg(page_hit)::numeric,3),
		round(avg(page_miss)::numeric,3),
		round(avg(page_dirty)::numeric,3),
		round(avg(read_rate)::numeric,3),
		round(avg(write_rate)::numeric,3)
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
		4 DESC;
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
	IN snapid_begin		bigint,
	IN snapid_end		bigint,
	OUT rolname			text,
	OUT datname			name,
	OUT query			text,
	OUT calls			bigint,
	OUT total_time		numeric,
	OUT time_per_call	numeric,
	OUT blk_read_time	numeric,
	OUT blk_write_time	numeric
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
			statsrepo.sub(se.calls, sb.calls)),
		statsrepo.sub(se.blk_read_time, sb.blk_read_time)::numeric(1000, 3),
		statsrepo.sub(se.blk_write_time, sb.blk_write_time)::numeric(1000, 3)
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
		l.datname,
		l.nspname,
		l.relname,
		l.duration,
		l.blockee_pid,
		l.blocker_pid,
		l.blocker_gid,
		l.blockee_query,
		l.blocker_query
	FROM
		statsrepo.lock l,
		statsrepo.snapshot s
	WHERE
		l.snapid BETWEEN $1 AND $2
		AND l.snapid = s.snapid
		AND s.instid = (SELECT instid FROM statsrepo.snapshot WHERE snapid = $2)
	ORDER BY
		l.duration DESC;
$$
LANGUAGE sql;

-- generate information that corresponds to 'Replication Activity'
CREATE FUNCTION statsrepo.get_replication_activity(
	IN snapid_begin			bigint,
	IN snapid_end			bigint,
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
	OUT sync_state			text
) RETURNS SETOF record AS
$$
	SELECT
		usename,
		application_name,
		client_addr,
		client_hostname,
		client_port,
		backend_start::timestamp(0),
		state,
		current_location,
		sent_location,
		write_location,
		flush_location,
		replay_location,
		sync_priority,
		sync_state
	FROM
		statsrepo.replication
	WHERE
		snapid = $2
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

COMMIT;
