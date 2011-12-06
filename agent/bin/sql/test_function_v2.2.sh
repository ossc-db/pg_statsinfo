#!/bin/sh

. ./sql/environment.sh

PGCONFIG_EXT=${PGDATA}/postgresql-ext.conf

function initialize()
{
	psql << EOF > /dev/null
CREATE TABLE backend_pid (blocker int, blockee int);
INSERT INTO backend_pid VALUES (0, 0);

CREATE VIEW show_last_lock_table AS
SELECT
	snapid,
	datname,
	nspname,
	relname,
	blocker_appname,
	blocker_addr,
	blocker_hostname,
	blocker_port,
	CASE WHEN blockee_pid IN (SELECT blockee FROM backend_pid)
		THEN 'Blockee PID is correct.' ELSE 'Blockee PID is incorrect.' END AS blockee,
	CASE WHEN (blocker_pid IS NULL AND (SELECT blocker IS NULL FROM backend_pid WHERE blockee = blockee_pid))
			OR blocker_pid IN (SELECT blocker FROM backend_pid)
		THEN 'Blocker PID is correct.' ELSE 'Blocker PID is incorrect.' END AS blocker,
	blocker_gid,
	CASE WHEN duration IS NOT NULL THEN
		'<Duration>' ELSE 'duration is NULL' END AS duration,
	blockee_query,
	blocker_query 
FROM
	statsrepo.lock
ORDER BY
	snapid DESC
LIMIT 1;

CREATE VIEW show_last_database_table AS
SELECT
	snapid,
	name,
	confl_tablespace,
	confl_lock,
	confl_snapshot,
	confl_bufferpin,
	confl_deadlock
FROM
	statsrepo.database
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.database);
EOF
}

function test_lock_in_xact()
{
	blocker_sql="${1}"
	blockee_sql="${2}"
	
	psql << EOF > /dev/null &
BEGIN;
${blocker_sql};
SELECT pg_sleep(2);
ROLLBACK;
UPDATE backend_pid SET blocker = pg_backend_pid();
EOF
	sleep 0.5
	psql << EOF > /dev/null &
BEGIN;
${blockee_sql};
ROLLBACK;
UPDATE backend_pid SET blockee = pg_backend_pid();
EOF
	sleep 0.5
	
	pg_statsinfo -S ""
	sleep 3
	
	psql -xc "SELECT * FROM show_last_lock_table"
	wait
}

function test_lock_two_phase_xact()
{
	blocker_sql="${1}"
	blockee_sql="${2}"
	
	psql << EOF > /dev/null &
BEGIN;
${blocker_sql};
PREPARE TRANSACTION 'block';
SELECT pg_sleep(2);
ROLLBACK PREPARED 'block';
UPDATE backend_pid SET blocker = NULL;
EOF
	sleep 0.5
	psql << EOF > /dev/null &
BEGIN;
${blockee_sql};
ROLLBACK;
UPDATE backend_pid SET blockee = pg_backend_pid();
EOF
	sleep 0.5

	pg_statsinfo -S ""
	sleep 3
	
	psql -xc "SELECT * FROM show_last_lock_table"
	wait
}

#
# main
#
echo "include '${PGCONFIG_EXT}'" >> ${PGDATA}/postgresql.conf
touch ${PGCONFIG_EXT}

# do snapshot for create statsrepo schema
pg_statsinfo -S ""
sleep 5

initialize

psql << EOF > /dev/null
CREATE TABLE xxx (col int);
INSERT INTO xxx VALUES (1);

CREATE INDEX idx_xxx ON xxx (col);
CREATE FUNCTION block_proc() RETURNS void AS 'INSERT INTO xxx VALUES (1)' LANGUAGE sql;
EOF

echo "--- FU-0001 (lock conflict [blocker query: SELECT]) ---"
test_lock_in_xact "SELECT * FROM xxx" "LOCK TABLE xxx"

echo "--- FU-0002 (lock conflict [blocker query: SELECT FOR UPDATE]) ---"
test_lock_in_xact "SELECT * FROM xxx FOR UPDATE" "DELETE FROM xxx"

echo "--- FU-0003 (lock conflict [blocker query: SELECT FOR SHARE]) ---"
test_lock_in_xact "SELECT * FROM xxx FOR SHARE" "DELETE FROM xxx"

echo "--- FU-0004 (lock conflict [blocker query: INSERT]) ---"
test_lock_in_xact "INSERT INTO xxx VALUES (1)" "LOCK TABLE xxx"

echo "--- FU-0005 (lock conflict [blocker query: UPDATE]) ---"
test_lock_in_xact "UPDATE xxx SET col = 0" "LOCK TABLE xxx"

echo "--- FU-0006 (lock conflict [blocker query: DELETE]) ---"
test_lock_in_xact "DELETE FROM xxx" "LOCK TABLE xxx"

echo "--- FU-0007 (lock conflict [blocker query: CREATE INDEX]) ---"
test_lock_in_xact "CREATE INDEX idx ON xxx (col)" "LOCK TABLE xxx"

echo "--- FU-0009 (lock conflict [blocker query: CLUSTER]) ---"
test_lock_in_xact "CLUSTER xxx USING idx_xxx" "LOCK TABLE xxx"

echo "--- FU-0010 (lock conflict [blocker query: ANALYZE]) ---"
test_lock_in_xact "ANALYZE xxx" "LOCK TABLE xxx"

echo "--- FU-0011 (lock conflict [blocker query: REINDEX]) ---"
test_lock_in_xact "REINDEX TABLE xxx" "LOCK TABLE xxx"

echo "--- FU-0012 (lock conflict [blocker query: ALTER TABLE]) ---"
test_lock_in_xact "ALTER TABLE xxx ADD COLUMN name text" "LOCK TABLE xxx"

echo "--- FU-0013 (lock conflict [blocker query: TRUNCATE]) ---"
test_lock_in_xact "TRUNCATE xxx" "LOCK TABLE xxx"

echo "--- FU-0014 (lock conflict [blocker query: DROP TABLE]) ---"
test_lock_in_xact "DROP TABLE xxx" "LOCK TABLE xxx"

echo "--- FU-0017 (lock conflict [blocker query: PREPARE]) ---"
test_lock_in_xact "PREPARE block AS DELETE FROM xxx; EXECUTE block" "LOCK TABLE xxx"

echo "--- FU-0018 (lock conflict [blocker query: PREPARE TRANSACTION]) ---"
test_lock_two_phase_xact "DELETE FROM xxx" "LOCK TABLE xxx"

echo "--- FU-0019 (lock conflict [blocker query: STORED PROCEDURE]) ---"
test_lock_in_xact "SELECT block_proc()" "LOCK TABLE xxx"

echo "--- FU-0030 (recovery conflict [DB: 1]) ---"
pg_statsinfo -S ""
sleep 3
psql -xc "SELECT * FROM show_last_database_table"

echo "--- FU-0031 (recovery conflict [DB: 2]) ---"
createdb testdb > /dev/null
pg_statsinfo -S ""
sleep 3
psql -xc "SELECT * FROM show_last_database_table"
dropdb testdb > /dev/null
