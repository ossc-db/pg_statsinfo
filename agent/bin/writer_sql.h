/*
 * writer_sql.h
 *
 * Copyright (c) 2009-2023, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 */

#ifndef WRITER_SQL_H
#define WRITER_SQL_H

/*
 * snapshot query
 */

#define SQL_NEW_SNAPSHOT "\
INSERT INTO statsrepo.snapshot(instid, time, comment) VALUES \
($1, $2, $3) RETURNING snapid, CAST(time AS DATE)"

#define SQL_INSERT_DATABASE "\
INSERT INTO statsrepo.database VALUES \
($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22, $23, $24)"

#define SQL_INSERT_TABLESPACE "\
INSERT INTO statsrepo.tablespace VALUES \
($1, $2, $3, $4, $5, $6, $7, $8)"

#define SQL_INSERT_ACTIVITY "\
INSERT INTO statsrepo.activity VALUES \
($1, $2, $3, $4, $5, $6)"

#define SQL_INSERT_LONG_TRANSACTION "\
INSERT INTO statsrepo.xact VALUES \
($1, $2, $3, $4, $5, $6)"

#define SQL_INSERT_STATEMENT "\
INSERT INTO statsrepo.statement \
  SELECT (($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22, $23, $24)::statsrepo.statement).* \
    FROM statsrepo.database d \
   WHERE d.snapid = $1 AND d.dbid = $2"

#define SQL_INSERT_PLAN "\
INSERT INTO statsrepo.plan \
  SELECT (($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22, $23, $24, $25)::statsrepo.plan).* \
    FROM statsrepo.database d \
   WHERE d.snapid = $1 AND d.dbid = $2"

#define SQL_INSERT_LOCK "\
INSERT INTO statsrepo.lock VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)"

#define SQL_INSERT_BGWRITER "\
INSERT INTO statsrepo.bgwriter VALUES ($1, $2, $3, $4, $5, $6)"

#define SQL_INSERT_REPLICATION "\
INSERT INTO statsrepo.replication VALUES \
($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21)"

#define SQL_INSERT_REPLICATION_SLOTS "\
INSERT INTO statsrepo.replication_slots VALUES \
($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)"

#define SQL_INSERT_STAT_REPLICATION_SLOTS "\
INSERT INTO statsrepo.stat_replication_slots VALUES \
($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)"

#define SQL_INSERT_STAT_IO "\
INSERT INTO statsrepo.stat_io VALUES \
($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19)"

#define SQL_INSERT_STAT_WAL "\
INSERT INTO statsrepo.stat_wal VALUES \
($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)"

#define SQL_INSERT_XLOG "\
INSERT INTO statsrepo.xlog VALUES ($1, $2, $3)"

#define SQL_INSERT_ARCHIVE "\
INSERT INTO statsrepo.archive VALUES ($1, $2, $3, $4, $5, $6, $7, $8)"

#define SQL_INSERT_SETTING "\
INSERT INTO statsrepo.setting VALUES ($1, $2, $3, $4, $5)"

#define SQL_INSERT_ROLE "\
INSERT INTO statsrepo.role VALUES ($1, $2, $3)"

#define SQL_INSERT_CPU "\
INSERT INTO statsrepo.cpu VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)"

#define SQL_INSERT_DEVICE "\
INSERT INTO statsrepo.device VALUES \
($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18)"

#define SQL_INSERT_LOADAVG "\
INSERT INTO statsrepo.loadavg VALUES ($1, $2, $3, $4)"

#define SQL_INSERT_MEMORY "\
INSERT INTO statsrepo.memory VALUES ($1, $2, $3, $4, $5, $6)"

#define SQL_INSERT_PROFILE "\
INSERT INTO statsrepo.profile VALUES ($1, $2, $3, $4)"

#define SQL_INSERT_RUSAGE "\
INSERT INTO statsrepo.rusage \
 SELECT (($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20)::statsrepo.rusage).* \
   FROM statsrepo.database d \
  WHERE d.snapid = $1 AND d.dbid = $2"

#define SQL_INSERT_HT_INFO "\
INSERT INTO statsrepo.ht_info VALUES ($1, $2, $3, $4, $5, $6, $7)"


/* Definition of delimiter and null identifier for COPY command */
#define COPY_DELIMITER "\t"
#define NULL_STR "null"

#define SQL_COPY_SCHEMA "\
COPY statsrepo.schema FROM STDIN with(NULL '" NULL_STR "')"

#define SQL_COPY_TABLE "\
COPY statsrepo.table FROM STDIN with(NULL '" NULL_STR "')"

#define SQL_COPY_COLUMN "\
COPY statsrepo.column FROM STDIN with(NULL '" NULL_STR "')"

#define SQL_COPY_INDEX "\
COPY statsrepo.index FROM STDIN with(NULL '" NULL_STR "')"

#define SQL_COPY_INHERITS "\
COPY statsrepo.inherits FROM STDIN with(NULL '" NULL_STR "')"

#define SQL_COPY_FUNCTION "\
COPY statsrepo.function FROM STDIN with(NULL '" NULL_STR "')"

#define SQL_INSERT_ALERT "\
INSERT INTO statsrepo.alert_message VALUES ($1, $2)"

#define SQL_INSERT_LOG "\
INSERT INTO statsrepo.log VALUES \
($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22, $23, $24, $25, $26, $27)"

#define SQL_UPDATE_SNAPSHOT "\
UPDATE \
	statsrepo.snapshot \
SET \
	exec_time = pg_catalog.age($2, $3), \
	snapshot_increase_size = ((SELECT pg_catalog.sum(pg_catalog.pg_relation_size(oid)) FROM pg_class \
								WHERE relnamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'statsrepo')) - $4), \
  xid_current = pg_catalog.pg_snapshot_xmax(pg_catalog.pg_current_snapshot()) \
WHERE \
	snapid = $1"

#define SQL_CREATE_SNAPSHOT_PARTITION "\
SELECT statsrepo.create_snapshot_partition($1)"

#define SQL_CREATE_REPOLOG_PARTITION "\
SELECT statsrepo.create_repolog_partition($1)"

#define SQL_INSERT_WAIT_SAMPLING_PROFILE "\
INSERT INTO statsrepo.wait_sampling VALUES ($1, $2, $3, $4, $5, $6, $7, $8)"

#endif
