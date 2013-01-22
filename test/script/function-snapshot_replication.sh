#!/bin/bash

. ./script/common.sh

if [ $(server_version) -lt 90100 ] ; then
	echo "PostgreSQL older than 9.1 does not collect statistics about replication"
	exit 0
fi

PGDATA_ACT=${DBCLUSTER_DIR}/pgdata-act
PGCONFIG_ACT=${CONFIG_DIR}/postgresql-replication-act.conf
PGPORT_ACT=57401
HBACONF_REPLICATION=${CONFIG_DIR}/pg_hba-replication.conf
ARCHIVE_DIR=${PGDATA_ACT}/archivelog

PGDATA_SBY=${DBCLUSTER_DIR}/pgdata-sby
PGCONFIG_SBY=${CONFIG_DIR}/postgresql-replication-sby.conf
PGPORT_SBY=57402

function get_snapshot()
{
	do_snapshot ${PGUSER} ${1} ${REPOSITORY_USER} ${REPOSITORY_PORT}
}

trap stop_all_database EXIT

echo "/*---- Initialize repository DB ----*/"
setup_repository ${REPOSITORY_DATA} ${REPOSITORY_USER} ${REPOSITORY_PORT} ${REPOSITORY_CONFIG}

echo "/*---- Initialize monitored instance (replication configuration) ----*/"
setup_dbcluster ${PGDATA_ACT} ${PGUSER} ${PGPORT_ACT} ${PGCONFIG_ACT} ${ARCHIVE_DIR} "" ${HBACONF_REPLICATION}
sleep 3
psql -p ${PGPORT_ACT} -U ${PGUSER} -d postgres -c "SELECT pg_start_backup('', true)" > /dev/null
rsync -a --delete --exclude=postmaster.pid ${PGDATA_ACT}/* ${PGDATA_SBY} > /dev/null 2>&1
psql -p ${PGPORT_ACT} -U ${PGUSER} -d postgres -c "SELECT pg_stop_backup()" > /dev/null
chmod 700 ${PGDATA_SBY}
set_pgconfig ${PGCONFIG_SBY} ${PGDATA_SBY} ${ARCHIVE_DIR}
cat << EOF > ${PGDATA_SBY}/recovery.conf
standby_mode = 'on'
primary_conninfo = 'host=127.0.0.1 port=${PGPORT_ACT} user=${PGUSER}'
restore_command = 'cp ${ARCHIVE_DIR}/%f %p'
trigger_file = '${PGDATA_SBY}/trigger'
EOF
pg_ctl start -w -D ${PGDATA_SBY} -o "-p ${PGPORT_SBY}" > /dev/null
sleep 3

echo "/***-- Statistics of WAL (MASTER) --***/"
get_snapshot ${PGPORT_ACT}
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
get_snapshot ${PGPORT_SBY}
send_query -c "SELECT * FROM statsrepo.xlog WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"

echo "/***-- Statistics of replication (STANDBY) --***/"
send_query -c "SELECT * FROM statsrepo.replication WHERE snapid = (SELECT max(snapid) FROM statsrepo.snapshot)"
