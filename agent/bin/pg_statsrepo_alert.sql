/*
 * bin/pg_statsrepo_alert.sql
 *
 * Setup of an alert function.
 *
 * Copyright (c) 2011, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

-- Adjust this setting to control where the objects get created.
SET search_path = public;

BEGIN;

SET LOCAL client_min_messages = WARNING;

-- table to save the alert settings
CREATE TABLE statsrepo.alert
(
	instid					bigint,
	rollback_tps			bigint	NOT NULL DEFAULT 100,
	commit_tps				bigint	NOT NULL DEFAULT 1000,
	garbage_size			bigint	NOT NULL DEFAULT 20000,
	garbage_percent			integer	NOT NULL DEFAULT 30,
	garbage_percent_table	integer	NOT NULL DEFAULT 30,
	response_avg			bigint	NOT NULL DEFAULT 10,
	response_worst			bigint	NOT NULL DEFAULT 60,
	enable_alert			boolean	NOT NULL DEFAULT TRUE,
	PRIMARY KEY (instid),
	FOREIGN KEY (instid) REFERENCES statsrepo.instance (instid)
);

-- add alert settings when adding a new instance
CREATE FUNCTION statsrepo.regist_alert() RETURNS TRIGGER AS
$$
DECLARE
BEGIN
	INSERT INTO statsrepo.alert VALUES (NEW.instid);
	RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- trigger registration for alert
CREATE TRIGGER regist_alert AFTER INSERT ON statsrepo.instance FOR EACH ROW
EXECUTE PROCEDURE statsrepo.regist_alert();

-- alert function
CREATE OR REPLACE FUNCTION statsrepo.alert(snap_id bigint) RETURNS SETOF text AS
$$
DECLARE

  -- for threshold
  th_rollback      float8;
  th_tps           float8;
  th_gb_size       float8;
  th_gb_pct        float8;
  th_gb_pct_table  float8;
  th_res_avg       float8;
  th_res_max       float8;

  -- inner variables
  curr      statsrepo.snapshot; -- latest snapshot
  prev      statsrepo.snapshot; -- previous snapshot
  duration_in_sec  float8;
  val_rollback     float8;
  val_tps          float8;
  val_gb_size      float8;
  val_gb_pct       float8;
  val_res_avg      float8;
  val_res_max      float8;
  val_gb_pct_table record; -- relname and garbage-ratio

BEGIN
  -- retrieve latest snapshot
  SELECT * INTO curr FROM statsrepo.snapshot WHERE snapid = snap_id;

  -- retrieve previous snapshot
  SELECT * INTO prev FROM statsrepo.snapshot WHERE snapid < curr.snapid AND instid = curr.instid
   ORDER BY snapid DESC LIMIT 1;
  IF NOT FOUND THEN
    RETURN; -- no previous snapshot
  END IF;

  -- retrieve threshold from current-settings
  SELECT
    rollback_tps::float8,
    commit_tps::float8,
    (garbage_size*1024*1024::float8),
    garbage_percent::float8,
    garbage_percent_table::float8,
    response_avg::float8,
    response_worst::float8
  INTO
    th_rollback,
    th_tps,
    th_gb_size,
    th_gb_pct,
    th_gb_pct_table,
    th_res_avg,
    th_res_max
  FROM statsrepo.alert WHERE instid = curr.instid AND enable_alert = true;
  IF NOT FOUND THEN
    RETURN; -- alert is disabled
  END IF;

  -- calculate duration for the two shapshots in sec.
  duration_in_sec :=
    extract(epoch FROM curr.time) - extract(epoch FROM prev.time);

  -- alert if rollbacks/sec is higher than th_rollback.
  SELECT (c.rollbacks - p.rollbacks) / duration_in_sec INTO val_rollback
    FROM (SELECT sum(xact_rollback) AS rollbacks
            FROM statsrepo.database
           WHERE snapid = curr.snapid) AS c,
         (SELECT sum(xact_rollback) AS rollbacks
            FROM statsrepo.database
           WHERE snapid = prev.snapid) AS p;
  IF val_rollback > th_rollback THEN
     RETURN NEXT 'too many rollbacks in snapshots between ''' ||
     prev.time::timestamp(0) || ''' and ''' || curr.time::timestamp(0) ||
     ''' --- ' || val_rollback::numeric(10,2) || ' Rollbacks/sec';
  END IF;


  -- alert if throughput(commit/sec) is higher than th_tps.
  SELECT (c.commits - p.commits) / duration_in_sec INTO val_tps
    FROM (SELECT sum(xact_commit) AS commits
            FROM statsrepo.database
           WHERE snapid = curr.snapid) AS c,
         (SELECT sum(xact_commit) AS commits
            FROM statsrepo.database
           WHERE snapid = prev.snapid) AS p;
  IF val_tps > th_tps THEN
     RETURN NEXT 'too many transactions in snapshots between ''' ||
     prev.time::timestamp(0) || ''' and ''' || curr.time::timestamp(0) ||
     ''' --- ' || val_tps::numeric(10,2) || ' Transactions/sec';
  END IF;


  -- alert if garbage(ratio or size) is higher than th_gb_pct/th_ga_size.
  SELECT sum(c.garbage_size), 100 * sum(c.garbage_size)/sum(size) INTO val_gb_size, val_gb_pct
    FROM
      (SELECT 
         CASE WHEN n_live_tup=0 THEN 0 
          ELSE size * (n_dead_tup::float8/(n_live_tup+n_dead_tup)::float8)
         END AS garbage_size,
         size
       FROM statsrepo.tables WHERE snapid=curr.snapid) AS c;
  IF val_gb_size > th_gb_size THEN
     RETURN NEXT 'dead tuple size exceeds threashold in snapshots between ''' ||
     prev.time::timestamp(0) || ''' and ''' || curr.time::timestamp(0) ||
     ''' --- ' || (val_gb_size/1024/1024)::numeric(8,2) || ' MB';
  END IF;
  IF val_gb_pct > th_gb_pct THEN
     RETURN NEXT 'dead tuple ratio exceeds threashold in snapshots between ''' ||
     prev.time::timestamp(0) || ''' and ''' || curr.time::timestamp(0) ||
     ''' --- ' || val_gb_pct::numeric(5,2) || ' %';
  END IF;


  -- alert if garbage ratio of each tables is higher than th_gb_pct_table
  FOR val_gb_pct_table IN 
    SELECT "database" || '.' || "schema" || '.' || "table" AS relname,
      CASE WHEN (n_live_tup + n_dead_tup) = 0 THEN 0 
       ELSE 100 * n_dead_tup::float8/(n_live_tup+n_dead_tup)::float8 
      END AS gb_pct
      FROM statsrepo.tables WHERE relpages > 1000 AND snapid=curr.snapid
  LOOP
    IF val_gb_pct_table.gb_pct > th_gb_pct_table THEN
       RETURN NEXT 'dead tuple ratio in ' || val_gb_pct_table.relname || ' exceeds threashold in snapshots between ''' ||
       prev.time::timestamp(0) || ''' and ''' || curr.time::timestamp(0) ||
       ''' --- ' || val_gb_pct_table.gb_pct::numeric(5,2) || ' %';
    END IF;
  END LOOP;


  -- alert if query-response-time(avg or max) is higher than th_res_avg/th_res_max.
  SELECT  avg( (c.total_time - coalesce(p.total_time,0))/(c.calls - coalesce(p.calls,0)) ),
          max( (c.total_time - coalesce(p.total_time,0))/(c.calls - coalesce(p.calls,0)) )
    INTO val_res_avg, val_res_max
    FROM (SELECT dbid, userid, total_time, calls, query FROM statsrepo.statement
           WHERE snapid = curr.snapid) AS c
         LEFT OUTER JOIN
         (SELECT dbid, userid, total_time, calls, query FROM statsrepo.statement
           WHERE snapid = prev.snapid) AS p
         ON c.dbid = p.dbid AND c.userid = p.userid AND c.query = p.query
    WHERE c.calls <> coalesce(p.calls,0);
    
  IF val_res_avg > th_res_avg THEN
     RETURN NEXT 'Query average response exceeds threshold in snapshots between ''' ||
     prev.time::timestamp(0) || ''' and ''' || curr.time::timestamp(0) ||
     ''' --- ' || val_res_avg::numeric(10,2) || ' sec';
  END IF;
  IF val_res_max > th_res_max THEN
     RETURN NEXT 'Query worst response exceeds threshold in snapshots between ''' ||
     prev.time::timestamp(0) || ''' and ''' || curr.time::timestamp(0) ||
     ''' --- ' || val_res_max::numeric(10,2) || ' sec';
  END IF;


END;
$$
LANGUAGE plpgsql VOLATILE;

COMMIT;
