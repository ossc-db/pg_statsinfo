#!/bin/sh

. ./sql/environment.sh

function exec_command()
{
	command="${1}"
	
	eval "${command}"
	echo "check exit-val: ${?}"
}

function restore_repository()
{
	restore_file="${1}"
	
	psql -c "DROP SCHEMA IF EXISTS statsrepo CASCADE" > /dev/null 2>&1
	gzip -d -c "${restore_file}" | psql > /dev/null 2>&1
	echo "restore done"
}

echo "--- Initialize for pgbench ---"
pgbench -i > /dev/null 2>&1

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- RE-0001 (report generate mode [intermittently during a transaction]) ---"
pgbench_pid=$(pgbench -t 10000000 -n > /dev/null 2>&1 & echo ${!})
exec_command "pg_statsinfo -r All"
echo "--- RE-0002 (snapshot list mode [intermittently during a transaction]) ---"
exec_command "pg_statsinfo -l"
echo "--- RE-0003 (snapshot size mode [intermittently during a transaction]) ---"
exec_command "pg_statsinfo -s"
echo "--- RE-0004 (get snapshot mode [intermittently during a transaction]) ---"
exec_command "pg_statsinfo -S COMMENT"
echo "--- RE-0005 (delete snapshot mode [intermittently during a transaction]) ---"
exec_command "pg_statsinfo -D 1"
sleep 5
pg_statsinfo -l > ${BASE_PATH}/results/SnapshotList-04.txt
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"
kill ${pgbench_pid}

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=1000) ---"
restore_repository "${BASE_PATH}/sql/create_repo_1000snap.sql.gz"

echo "--- RE-0006 (report generate mode [INSTANCE=1, SNAPSHOT=1000]) ---"
exec_command "pg_statsinfo -r All"
echo "--- RE-0007 (snapshot list mode [INSTANCE=1, SNAPSHOT=1000]) ---"
exec_command "pg_statsinfo -l"
echo "--- RE-0008 (snapshot size mode [INSTANCE=1, SNAPSHOT=1000]) ---"
exec_command "pg_statsinfo -s"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- RE-0009 (execute several command at same time [--report, --report]) ---"
exec_command "pg_statsinfo -r All" > ${BASE_PATH}/results/Report-04.txt &
exec_command "pg_statsinfo -r All"
wait ${!}
cat ${BASE_PATH}/results/Report-04.txt
echo "--- RE-0010 (execute several command at same time [--report, --list]) ---"
exec_command "pg_statsinfo -r All" > ${BASE_PATH}/results/Report-05.txt &
exec_command "pg_statsinfo -l"
wait ${!}
cat ${BASE_PATH}/results/Report-05.txt
echo "--- RE-0011 (execute several command at same time [--report, --size]) ---"
exec_command "pg_statsinfo -r All" > ${BASE_PATH}/results/Report-06.txt &
exec_command "pg_statsinfo -s"
wait ${!}
cat ${BASE_PATH}/results/Report-06.txt
echo "--- RE-0012 (execute several command at same time [--report, --snapshot]) ---"
exec_command "pg_statsinfo -r All" > ${BASE_PATH}/results/Report-07.txt &
exec_command "pg_statsinfo -S COMMENT"
wait ${!}
cat ${BASE_PATH}/results/Report-07.txt
sleep 3
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- RE-0013 (execute several command at same time [--report, --delete]) ---"
exec_command "pg_statsinfo -r All" > ${BASE_PATH}/results/Report-08.txt &
exec_command "pg_statsinfo -D 1"
wait ${!}
diff ${BASE_PATH}/results/Report-08.txt ${BASE_PATH}/expected/Report-08-1.txt > /dev/null
if [ ${?} -ne 0 ] ; then
	diff ${BASE_PATH}/results/Report-08.txt ${BASE_PATH}/expected/Report-08-2.txt > /dev/null
	if [ ${?} -ne 0 ] ; then
		echo "report is incorrect"
	fi
fi
sleep 3
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- RE-0014 (execute several command at same time [--list, --size]) ---"
exec_command "pg_statsinfo -l" > ${BASE_PATH}/results/SnapshotList-05.txt &
exec_command "pg_statsinfo -s"
wait ${!}
cat ${BASE_PATH}/results/SnapshotList-05.txt
echo "--- RE-0015 (execute several command at same time [--list, --snapshot]) ---"
exec_command "pg_statsinfo -l" > ${BASE_PATH}/results/SnapshotList-06.txt &
exec_command "pg_statsinfo -S COMMENT"
wait ${!}
cat ${BASE_PATH}/results/SnapshotList-06.txt
sleep 3
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- RE-0016 (execute several command at same time [--list, --delete]) ---"
exec_command "pg_statsinfo -l" > ${BASE_PATH}/results/SnapshotList-07.txt &
exec_command "pg_statsinfo -D 1"
wait ${!}
cat ${BASE_PATH}/results/SnapshotList-07.txt
sleep 3
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- RE-0017 (execute several command at same time [--size, --snapshot]) ---"
exec_command "pg_statsinfo -s" > ${BASE_PATH}/results/SnapshotSize-01.txt &
exec_command "pg_statsinfo -S COMMENT"
wait ${!}
cat ${BASE_PATH}/results/SnapshotSize-01.txt
sleep 3
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- RE-0018 (execute several command at same time [--size, --delete]) ---"
exec_command "pg_statsinfo -s" > ${BASE_PATH}/results/SnapshotSize-02.txt &
exec_command "pg_statsinfo -D 1"
wait ${!}
cat ${BASE_PATH}/results/SnapshotSize-02.txt
sleep 3
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- RE-0019 (execute several command at same time [--snapshot, --delete]) ---"
exec_command "pg_statsinfo -S COMMENT" > ${BASE_PATH}/results/Snapshot-01.txt &
exec_command "pg_statsinfo -D 1"
wait ${!}
cat ${BASE_PATH}/results/Snapshot-01.txt
sleep 3
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- RE-0020 (abnormal cases [not specify arguments and options]) ---"
exec_command "pg_statsinfo"
echo "--- RE-0021 (abnormal cases [illegal option specified: not specify REPORTID]) ---"
exec_command "pg_statsinfo -r"
echo "--- RE-0022 (abnormal cases [illegal option specified: --report=<none>]) ---"
exec_command "pg_statsinfo -r xxx"
echo "--- RE-0023 (abnormal cases [illegal option specified: --instid=xxx]) ---"
exec_command "pg_statsinfo -r All -i xxx"
echo "--- RE-0024 (abnormal cases [illegal option specified: specify option -b, -e, -B, -E together]) ---"
exec_command "pg_statsinfo -r All -b 1 -e 10 -B '2011-07-01 00:00:00' -E '2012-07-01 00:00:00'"
echo "--- RE-0025 (abnormal cases [illegal option specified: --beginid=2, --endid=1]) ---"
exec_command "pg_statsinfo -r All -b 2 -e 1"
echo "--- RE-0026 (abnormal cases [illegal option specified: --begindate=xxx]) ---"
exec_command "pg_statsinfo -r All -B xxx"
echo "--- RE-0027 (abnormal cases [illegal option specified: --snapshot=<none>]) ---"
exec_command "pg_statsinfo -S"
echo "--- RE-0028 (abnormal cases [illegal option specified: --delete=<none>]) ---"
exec_command "pg_statsinfo -D"
echo "--- RE-0029 (abnormal cases [illegal option specified: --delete=xxx]) ---"
exec_command "pg_statsinfo -D xxx"
echo "--- RE-0030 (abnormal cases [illegal option specified: specify same mode twice]) ---"
exec_command "pg_statsinfo -r All -r Summary"
echo "--- RE-0031 (abnormal cases [illegal option specified: specify two different modes at same time]) ---"
exec_command "pg_statsinfo -r All -l"
echo "--- RE-0032 (abnormal cases [repository database not working]) ---"
pg_ctl stop -s
exec_command "pg_statsinfo -r All"
echo "--- RE-0033 (abnormal cases [target database not working]) ---"
exec_command "pg_statsinfo -S COMMENT"
pg_ctl start -w -s
echo "--- RE-0034 (abnormal cases [statsrepo schema not exists]) ---"
createdb dummydb
exec_command "pg_statsinfo -r All -d dummydb"
dropdb dummydb
echo "--- RE-0035 (abnormal cases [not have permission to access the statsrepo schema]) ---"
createuser -SDR xxx
exec_command "pg_statsinfo -r All -U xxx"
dropuser xxx
echo "--- RE-0036 (abnormal cases [report destination is not writable]) ---"
exec_command "pg_statsinfo -r All -o /xxx/xxx"
