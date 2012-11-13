#!/bin/bash

. ./sql/environment.sh
. ./sql/utility.sh

[ $(get_version) -ge 80400 ] &&
	export PGOPTIONS=' -c intervalstyle=postgres'

[ $(get_version) -lt 90000 ] &&
	send_query -c "CREATE LANGUAGE plpgsql" > /dev/null

echo "/*---- リポジトリDBへのデータ登録 ----*/"
if [ $(get_version) -ge 80400 ] ; then
	send_query -qf "$(pg_config --sharedir)/contrib/pg_statsrepo_partition.sql"
	send_query -c "SELECT statsrepo.create_partition('2012-11-01')" > /dev/null
else
	send_query -qf "$(pg_config --sharedir)/contrib/pg_statsrepo83.sql"
fi
send_query -qf ${FUNCTION_INPUTDATA}
send_query << EOF > /dev/null
SELECT statsrepo.input_data(1, '5807946214009601530', 'statsinfo', 5432, '8.3.0', 1);
SELECT statsrepo.input_data(2, '5807946214009601531', 'statsinfo', 5433, '8.4.0', 5);
SELECT statsrepo.input_data(3, '5807946214009601532', 'statsinfo', 5434, '9.0.0', 9);
SELECT statsrepo.input_data(4, '5807946214009601533', 'statsinfo', 5435, '9.1.0', 13);
SELECT statsrepo.input_data(5, '5807946214009601534', 'statsinfo', 5436, '9.2.0', 17);
EOF

echo "/*---- レポート作成モード ----*/"
echo "/**--- レポート種別: Summary ---**/"
exec_command "exec_statsinfo -r Summary"

echo "/**--- レポート種別: DatabaseStatistics ---**/"
exec_command "exec_statsinfo -r DatabaseStatistics"

echo "/**--- レポート種別: InstanceActivity ---**/"
exec_command "exec_statsinfo -r InstanceActivity"

echo "/**--- レポート種別: OSResourceUsage ---**/"
exec_command "exec_statsinfo -r OSResourceUsage"

echo "/**--- レポート種別: DiskUsage ---**/"
exec_command "exec_statsinfo -r DiskUsage"

echo "/**--- レポート種別: LongTransactions ---**/"
exec_command "exec_statsinfo -r LongTransactions"

echo "/**--- レポート種別: NotableTables ---**/"
exec_command "exec_statsinfo -r NotableTables"

echo "/**--- レポート種別: CheckpointActivity ---**/"
exec_command "exec_statsinfo -r CheckpointActivity"

echo "/**--- レポート種別: AutovacuumActivity ---**/"
exec_command "exec_statsinfo -r AutovacuumActivity"

echo "/**--- レポート種別: QueryActivity ---**/"
exec_command "exec_statsinfo -r QueryActivity"

echo "/**--- レポート種別: LockConflicts ---**/"
exec_command "exec_statsinfo -r LockConflicts"

echo "/**--- レポート種別: ReplicationActivity ---**/"
exec_command "exec_statsinfo -r ReplicationActivity"

echo "/**--- レポート種別: SettingParameters ---**/"
exec_command "exec_statsinfo -r SettingParameters"

echo "/**--- レポート種別: SchemaInformation ---**/"
exec_command "exec_statsinfo -r SchemaInformation"

echo "/**--- レポート種別: Profiles ---**/"
exec_command "exec_statsinfo -r Profiles"

echo "/**--- レポート種別: All ---**/"
exec_command "exec_statsinfo -r All"

echo "/**--- インスタンス指定 (存在するインスタンスIDを指定) ---**/"
exec_command "exec_statsinfo -r Summary -i 1"

echo "/**--- インスタンス指定 (存在しないインスタンスIDを指定) ---**/"
exec_command "exec_statsinfo -r Summary -i 99"

echo "/**--- レポート範囲指定 (-e=2) ---**/"
exec_command "exec_statsinfo -r Summary -i 1 -e 2"

echo "/**--- レポート範囲指定 (-b=2, -e=3) ---**/"
exec_command "exec_statsinfo -r Summary -i 1 -b 2 -e 3"

echo "/**--- レポート範囲指定 (-b=3) ---**/"
exec_command "exec_statsinfo -r Summary -i 1 -b 3"

echo "/**--- レポート範囲指定 (-E=<snapid=2>) ---**/"
exec_command "exec_statsinfo -r Summary -i 1 -E '2012-11-01 00:01:00'"

echo "/**--- レポート範囲指定 (-B=<snapid=2>, -E=<snapid=3>) ---**/"
exec_command "exec_statsinfo -r Summary -i 1 -B '2012-11-01 00:01:00' -E '2012-11-01 00:02:00'"

echo "/**--- レポート範囲指定 (-B=<snapid=3>) ---**/"
exec_command "exec_statsinfo -r Summary -i 1 -B '2012-11-01 00:02:00'"

echo "/**--- ファイル出力指定 ---**/"
exec_command "exec_statsinfo -r Summary -i 1 -o ${REPOSITORY_DATA}/report.log"
cat ${REPOSITORY_DATA}/report.log

echo "/**--- ファイル出力指定 (既存ファイル上書き) ---**/"
exec_command "exec_statsinfo -r Summary -i 2 -o ${REPOSITORY_DATA}/report.log"
cat ${REPOSITORY_DATA}/report.log

echo "/**--- 準正常系 ---**/"
echo "/***-- スナップショット取得日時が同一のものが含まれる --***/"
send_query -c "UPDATE statsrepo.snapshot SET time = '2012-11-01 00:00:00' WHERE instid = 5"
exec_command "exec_statsinfo -r All -i 5"
