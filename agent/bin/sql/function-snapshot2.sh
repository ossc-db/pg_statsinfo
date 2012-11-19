#!/bin/bash

. ./sql/environment.sh
. ./sql/utility.sh

[ $(get_version) -lt 90000 ] &&
	exit 0

PGDATA_ACT=${DBCLUSTER_DIR}/pgdata-act
PGPORT_ACT=5441
PGUSER_ACT=postgres
PGCONFIG_ACT=${CONFIG_DIR}/postgresql-replication-act.conf
HBACONF_REPLICATION=${CONFIG_DIR}/pg_hba-replication.conf
ARCHIVE_DIR=${PGDATA_ACT}/archivelog

PGDATA_SBY=${DBCLUSTER_DIR}/pgdata-sby
PGPORT_SBY=5442
PGUSER_SBY=postgres
PGCONFIG_SBY=${CONFIG_DIR}/postgresql-replication-sby.conf

echo "/*---- Initialize monitored instance (replication configuration) ----*/"
setup_dbcluster ${PGDATA_ACT} ${PGUSER_ACT} ${PGPORT_ACT} ${PGCONFIG_ACT} ${ARCHIVE_DIR} "" ${HBACONF_REPLICATION}
sleep 3
psql -p ${PGPORT_ACT} -U ${PGUSER_ACT} -c "SELECT pg_start_backup('', true)" > /dev/null
rsync -a --delete --exclude=postmaster.pid ${PGDATA_ACT}/* ${PGDATA_SBY} > /dev/null 2>&1
psql -p ${PGPORT_ACT} -U ${PGUSER_ACT} -c "SELECT pg_stop_backup()" > /dev/null
chmod 700 ${PGDATA_SBY}
set_pgconfig ${PGCONFIG_SBY} ${PGDATA_SBY} ${ARCHIVE_DIR}
cat << EOF > ${PGDATA_SBY}/recovery.conf
standby_mode = 'on'
primary_conninfo = 'host=127.0.0.1 port=${PGPORT_ACT} user=${PGUSER_ACT}'
restore_command = 'cp ${ARCHIVE_DIR}/%f %p'
trigger_file = '${PGDATA_SBY}/trigger'
EOF
pg_ctl start -w -D ${PGDATA_SBY} -o "-p ${PGPORT_SBY}" > /dev/null
sleep 3

echo "/***-- Statistics of WAL (MASTER) --***/"
do_snapshot "" ${PGPORT_ACT} ${PGUSER_ACT}
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

echo "/***-- Statistics of replication (MASTER) --***/"
send_query << EOF
SELECT
	snapid,
	CASE WHEN procpid IS NOT NULL THEN 'xxx' END AS procpid,
	CASE WHEN usesysid IS NOT NULL THEN 'xxx' END AS usesysid,
	usename,
	CASE WHEN application_name IS NOT NULL THEN 'xxx' END AS application_name,
	CASE WHEN client_addr IS NOT NULL THEN 'xxx' END AS client_addr,
	CASE WHEN client_hostname IS NOT NULL THEN 'xxx' END AS client_hostname,
	CASE WHEN client_port IS NOT NULL THEN 'xxx' END AS client_port,
	CASE WHEN backend_start IS NOT NULL THEN 'xxx' END AS backend_start,
	state,
	CASE WHEN current_location IS NOT NULL THEN 'xxx' END AS current_location,
	CASE WHEN sent_location IS NOT NULL THEN 'xxx' END AS sent_location,
	CASE WHEN write_location IS NOT NULL THEN 'xxx' END AS write_location,
	CASE WHEN flush_location IS NOT NULL THEN 'xxx' END AS flush_location,
	CASE WHEN replay_location IS NOT NULL THEN 'xxx' END AS replay_location,
	sync_priority,
	sync_state
FROM
	statsrepo.replication
WHERE
	snapid = (SELECT max(snapid) FROM statsrepo.snapshot)
EOF

echo "/***-- Statistics of WAL (STANDBY) --***/"
do_snapshot "" ${PGPORT_SBY} ${PGUSER_SBY}
send_query -c "SELECT * FROM statsrepo.xlog WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"

echo "/***-- Statistics of replication (STANDBY) --***/"
send_query -c "SELECT * FROM statsrepo.replication WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"

pg_ctl stop -D ${PGDATA_SBY} > /dev/null
pg_ctl stop -D ${PGDATA_ACT} > /dev/null
