/*
 * bin/pg_statsrepo_partition.sql
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
-- utility function for repoter.
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
