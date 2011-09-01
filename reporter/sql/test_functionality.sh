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

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- FU-0001 (report generate mode [REPORTID=Summary]) ---"
exec_command "pg_statsinfo -r Summary"
echo "--- FU-0002 (report generate mode [REPORTID=DatabaseStatistics]) ---"
exec_command "pg_statsinfo -r DatabaseStatistics"
echo "--- FU-0003 (report generate mode [REPORTID=InstanceActivity]) ---"
exec_command "pg_statsinfo -r InstanceActivity"
echo "--- FU-0004 (report generate mode [REPORTID=OSResourceUsage]) ---"
exec_command "pg_statsinfo -r OSResourceUsage"
echo "--- FU-0005 (report generate mode [REPORTID=DiskUsage]) ---"
exec_command "pg_statsinfo -r DiskUsage"
echo "--- FU-0006 (report generate mode [REPORTID=LongTransactions]) ---"
exec_command "pg_statsinfo -r LongTransactions"
echo "--- FU-0007 (report generate mode [REPORTID=NotableTables]) ---"
exec_command "pg_statsinfo -r NotableTables"
echo "--- FU-0008 (report generate mode [REPORTID=CheckpointActivity]) ---"
exec_command "pg_statsinfo -r CheckpointActivity"
echo "--- FU-0009 (report generate mode [REPORTID=AutovacuumActivity]) ---"
exec_command "pg_statsinfo -r AutovacuumActivity"
echo "--- FU-0010 (report generate mode [REPORTID=QueryActivity]) ---"
exec_command "pg_statsinfo -r QueryActivity"
echo "--- FU-0011 (report generate mode [REPORTID=SettingParameters]) ---"
exec_command "pg_statsinfo -r SettingParameters"
echo "--- FU-0012 (report generate mode [REPORTID=SchemaInformation]) ---"
exec_command "pg_statsinfo -r SchemaInformation"
echo "--- FU-0013 (report generate mode [REPORTID=Profiles]) ---"
exec_command "pg_statsinfo -r Profiles"
echo "--- FU-0014 (report generate mode [REPORTID=All]) ---"
exec_command "pg_statsinfo -r All"
echo "--- FU-0015 (report generate mode [REPORTID=s]) ---"
exec_command "pg_statsinfo -r s"

echo "--- Restore repository database (INSTANCE=2, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_2inst.sql.gz"

echo "--- FU-0016 (report generate mode [--instid=<none>]) ---"
exec_command "pg_statsinfo -r All"
echo "--- FU-0017 (report generate mode [--instid=1]) ---"
exec_command "pg_statsinfo -r All -i 1"
echo "--- FU-0018 (report generate mode [--instid=3]) ---"
exec_command "pg_statsinfo -r All -i 3"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- FU-0019 (report generate mode [--beginid=<none>, --endid=7]) ---"
exec_command "pg_statsinfo -r All -e 7"
echo "--- FU-0020 (report generate mode [--beginid=3, --endid=7]) ---"
exec_command "pg_statsinfo -r All -b 3 -e 7"
echo "--- FU-0021 (report generate mode [--beginid=5, --endid=<none>]) ---"
exec_command "pg_statsinfo -r All -b 5"
echo "--- FU-0022 (report generate mode [--beginid=<none>, --endid=<none>]) ---"
exec_command "pg_statsinfo -r All"

echo "--- FU-0023 (report generate mode [--begindate=<none>, --enddate=<7th snapshot timestamp>]) ---"
exec_command "pg_statsinfo -r All -E '2011-08-16 20:36:25'"
echo "--- FU-0024 (report generate mode [--begindate=<3rd snapshot timestamp>, --enddate=<7th snapshot timestamp>]) ---"
exec_command "pg_statsinfo -r All -B '2011-08-16 18:36:25' -E '2011-08-16 20:36:25'"
echo "--- FU-0025 (report generate mode [--begindate=<5th snapshot timestamp>, --enddate=<none>]) ---"
exec_command "pg_statsinfo -r All -B '2011-08-16 19:36:26'"

echo "--- Restore repository database (INSTANCE=2, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_2inst.sql.gz"

echo "--- FU-0027 (report generate mode [--instid=1, --beginid=1, --endid=10]) ---"
exec_command "pg_statsinfo -r All -b 1 -e 10 -i 1"
echo "--- FU-0028 (report generate mode [--instid=2, --begindate=<1st snapshot timestamp>, --enddate=<10th snapshot timestamp>]) ---"
exec_command "pg_statsinfo -r All -B '2011-08-17 16:56:26' -E '2011-08-17 18:56:33' -i 2"

echo "--- Restore repository database (INSTANCE=0, SNAPSHOT=0) ---"
restore_repository "${BASE_PATH}/sql/create_repo_0snap.sql.gz"
echo "--- FU-0029 (report generate mode [INSTANCE=0, SNAPSHOT=0) ---"
exec_command "pg_statsinfo -r All"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=1) ---"
restore_repository "${BASE_PATH}/sql/create_repo_1snap.sql.gz"
echo "--- FU-0030 (report generate mode [INSTANCE=1, SNAPSHOT=1) ---"
exec_command "pg_statsinfo -r All"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=2) ---"
restore_repository "${BASE_PATH}/sql/create_repo_2snap.sql.gz"
echo "--- FU-0031 (report generate mode [INSTANCE=1, SNAPSHOT=2) ---"
exec_command "pg_statsinfo -r All"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=100) ---"
restore_repository "${BASE_PATH}/sql/create_repo_100snap.sql.gz"
echo "--- FU-0032 (report generate mode [INSTANCE=1, SNAPSHOT=100) ---"
exec_command "pg_statsinfo -r All"

echo "--- Restore repository database (INSTANCE=2, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_2inst.sql.gz"
echo "--- FU-0033 (report generate mode [INSTANCE=2, SNAPSHOT=10) ---"
exec_command "pg_statsinfo -r All"

echo "--- Restore repository database (INSTANCE=10, SNAPSHOT=20) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10inst.sql.gz"
echo "--- FU-0034 (report generate mode [INSTANCE=10, SNAPSHOT=20) ---"
exec_command "pg_statsinfo -r All"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- FU-0035 (report generate mode [--output=<writable path>) ---"
exec_command "pg_statsinfo -r All -o ${BASE_PATH}/results/Report-01.txt"
cat ${BASE_PATH}/results/Report-01.txt
echo "--- FU-0036 (report generate mode [--output=<existing file>) ---"
echo "xxx" > ${BASE_PATH}/results/Report-02.txt
exec_command "pg_statsinfo -r All -o ${BASE_PATH}/results/Report-02.txt"
cat ${BASE_PATH}/results/Report-02.txt

echo "--- Restore repository database (INSTANCE=0, SNAPSHOT=0) ---"
restore_repository "${BASE_PATH}/sql/create_repo_0snap.sql.gz"
echo "--- FU-0037 (snapshot list mode [INSTANCE=0, SNAPSHOT=0]) ---"
exec_command "pg_statsinfo -l"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=1) ---"
restore_repository "${BASE_PATH}/sql/create_repo_1snap.sql.gz"
echo "--- FU-0038 (snapshot list mode [INSTANCE=1, SNAPSHOT=1]) ---"
exec_command "pg_statsinfo -l"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=2) ---"
restore_repository "${BASE_PATH}/sql/create_repo_2snap.sql.gz"
echo "--- FU-0039 (snapshot list mode [INSTANCE=1, SNAPSHOT=2]) ---"
exec_command "pg_statsinfo -l"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=100) ---"
restore_repository "${BASE_PATH}/sql/create_repo_100snap.sql.gz"
echo "--- FU-0040 (snapshot list mode [INSTANCE=1, SNAPSHOT=100]) ---"
exec_command "pg_statsinfo -l"

echo "--- Restore repository database (INSTANCE=2, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_2inst.sql.gz"

echo "--- FU-0041 (snapshot list mode [--instid=<none>]) ---"
exec_command "pg_statsinfo -l"
echo "--- FU-0042 (snapshot list mode [--instid=1]) ---"
exec_command "pg_statsinfo -l -i 1"
echo "--- FU-0043 (snapshot list mode [--instid=3]) ---"
exec_command "pg_statsinfo -l -i 3"

echo "--- Restore repository database (INSTANCE=0, SNAPSHOT=0) ---"
restore_repository "${BASE_PATH}/sql/create_repo_0snap.sql.gz"
echo "--- FU-0044 (snapshot size mode [INSTANCE=0, SNAPSHOT=0]) ---"
exec_command "pg_statsinfo -s"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=1) ---"
restore_repository "${BASE_PATH}/sql/create_repo_1snap.sql.gz"
echo "--- FU-0045 (snapshot size mode [INSTANCE=1, SNAPSHOT=1]) ---"
exec_command "pg_statsinfo -s"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=2) ---"
restore_repository "${BASE_PATH}/sql/create_repo_2snap.sql.gz"
echo "--- FU-0046 (snapshot size mode [INSTANCE=1, SNAPSHOT=2]) ---"
exec_command "pg_statsinfo -s"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=100) ---"
restore_repository "${BASE_PATH}/sql/create_repo_100snap.sql.gz"
echo "--- FU-0047 (snapshot size mode [INSTANCE=1, SNAPSHOT=100]) ---"
exec_command "pg_statsinfo -s"

echo "--- Restore repository database (INSTANCE=2, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_2inst.sql.gz"
echo "--- FU-0048 (snapshot size mode [INSTANCE=2, SNAPSHOT=10]) ---"
exec_command "pg_statsinfo -s"

echo "--- Restore repository database (INSTANCE=10, SNAPSHOT=20) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10inst.sql.gz"
echo "--- FU-0049 (snapshot size mode [INSTANCE=10, SNAPSHOT=20]) ---"
exec_command "pg_statsinfo -s"

echo "--- Restore repository database (INSTANCE=0, SNAPSHOT=0) ---"
restore_repository "${BASE_PATH}/sql/create_repo_0snap.sql.gz"

echo "--- FU-0050 (get snapshot mode [COMMENT='COMMENT']) ---"
exec_command "pg_statsinfo -S COMMENT"
sleep 3
echo "--- FU-0051 (get snapshot mode [COMMENT='マルチバイト文字']) ---"
exec_command "pg_statsinfo -S 'マルチバイト文字'"
sleep 3
echo "--- FU-0052 (get snapshot mode [COMMENT='']) ---"
exec_command "pg_statsinfo -S ''"
sleep 3
echo "--- FU-0053 (get snapshot mode [COMMENT=' ']) ---"
exec_command "pg_statsinfo -S ' '"
sleep 3
pg_statsinfo -l > ${BASE_PATH}/results/SnapshotList-01.txt
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- FU-0054 (delete snapshot mode [SNAPID=1]) ---"
exec_command "pg_statsinfo -D 1"
sleep 3
echo "--- FU-0055 (delete snapshot mode [SNAPID=999999]) ---"
exec_command "pg_statsinfo -D 999999"
sleep 3
pg_statsinfo -l > ${BASE_PATH}/results/SnapshotList-02.txt
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- FU-0056 (connection options [--dbname=xxx]) ---"
exec_command "pg_statsinfo -l -d xxx"
echo "--- FU-0057 (connection options [--host=xxx]) ---"
exec_command "pg_statsinfo -l -h xxx"
echo "--- FU-0058 (connection options [--port=xxx]) ---"
exec_command "pg_statsinfo -l -p xxx"
echo "--- FU-0059 (connection options [--username=xxx]) ---"
exec_command "pg_statsinfo -l -U xxx"

echo "--- Restore repository database (INSTANCE=2, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_2inst.sql.gz"

echo "--- FU-0066 (specify long options [report generate mode]) ---"
exec_command "pg_statsinfo --report All --instid 1 --beginid 1 --endid 10 --output ${BASE_PATH}/results/Report-02.txt"
cat ${BASE_PATH}/results/Report-02.txt
echo "--- FU-0067 (specify long options [report generate mode]) ---"
exec_command "pg_statsinfo --report All --instid 1 --begindate '2011-08-17 16:56:26' --enddate '2011-08-17 18:56:33' --output ${BASE_PATH}/results/Report-03.txt"
cat ${BASE_PATH}/results/Report-03.txt
echo "--- FU-0068 (specify long options [snapshot list mode]) ---"
exec_command "pg_statsinfo --list --instid 1"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=1) ---"
restore_repository "${BASE_PATH}/sql/create_repo_1snap.sql.gz"

echo "--- FU-0069 (specify long options [snapshot size mode]) ---"
exec_command "pg_statsinfo --size"
echo "--- FU-0070 (specify long options [get snapshot mode]) ---"
exec_command "pg_statsinfo --snapshot COMMENT"
echo "--- FU-0071 (specify long options [delete snapshot mode]) ---"
exec_command "pg_statsinfo --delete 1"
sleep 3
pg_statsinfo -l > ${BASE_PATH}/results/SnapshotList-03.txt
psql -e -c "SELECT snapid, instid, comment FROM statsrepo.snapshot"

echo "--- Restore repository database (INSTANCE=1, SNAPSHOT=10) ---"
restore_repository "${BASE_PATH}/sql/create_repo_10snap.sql.gz"

echo "--- FU-0072 (environment variable [PGDATABASE=xxx]) ---"
exec_command "( export PGDATABASE=xxx; pg_statsinfo -l )"
echo "--- FU-0073 (environment variable [PGHOST=xxx]) ---"
exec_command "( export PGHOST=xxx; pg_statsinfo -l )"
echo "--- FU-0074 (environment variable [PGPORT=xxx]) ---"
exec_command "( export PGPORT=xxx; pg_statsinfo -l )"
echo "--- FU-0075 (environment variable [PGUSER=xxx]) ---"
exec_command "( export PGUSER=xxx; pg_statsinfo -l )"
echo "--- FU-0076 (environment variable [PGDATABASE=${PGDATABASE}, PGHOST=${PGHOST}, PGPORT=${PGPORT}, PGUSER=${PGUSER}]) ---"
exec_command "pg_statsinfo -l"
echo "--- FU-0077 (specify both environment variables and connection options) ---"
exec_command "( export PGDATABASE=xxx PGHOST=xxx PGPORT=xxx PGUSER=xxx; pg_statsinfo -l -d ${PGDATABASE} -h ${PGHOST} -p ${PGPORT} -U ${PGUSER})"

echo "--- FU-0078 (other options [--help]) ---"
exec_command "pg_statsinfo --help"
echo "--- FU-0079 (other options [--help (along with other options)]) ---"
exec_command "pg_statsinfo -r All --help"
echo "--- FU-0080 (other options [--version]) ---"
exec_command "pg_statsinfo --version"
echo "--- FU-0081 (other options [--version (along with other options)]) ---"
exec_command "pg_statsinfo -r All --version"
