#!/bin/bash

. ./sql/environment.sh
. ./sql/utility.sh

PGCONFIG_REPORT=${CONFIG_DIR}/postgresql-report.conf
SNAPSHOT_DELAY=3

[ $(get_version) -ge 80400 ] &&
	export PGOPTIONS=' -c intervalstyle=postgres'

echo "/*---- リポジトリスキーマ初期化 ----*/"
send_query -c "DROP SCHEMA statsrepo CASCADE" > /dev/null 2>&1

echo "/*---- 監視対象インスタンス初期化 ----*/"
setup_dbcluster ${PGDATA} ${PGUSER} ${PGPORT} ${PGCONFIG_REPORT} "" "" ""
sleep 3

echo "/*---- スナップショット一覧／サイズ表示モード ----*/"
echo "/**--- スナップショット件数 (0件) ---**/"
exec_command "exec_statsinfo -l"
exec_command "exec_statsinfo -s"

echo "/**--- スナップショット件数 (1件) ---**/"
send_query << EOF > /dev/null
	INSERT INTO statsrepo.instance VALUES (1, '5807946214009601530', 'statsinfo', '5432', '8.3.0');
	INSERT INTO statsrepo.snapshot VALUES (1, 1, '2012-11-01 00:00:00+09', '1st', '00:00:01', 262144);
EOF
exec_command "exec_statsinfo -l"
exec_command "exec_statsinfo -s"

echo "/**--- スナップショット件数 (2件) ---**/"
send_query << EOF > /dev/null
INSERT INTO statsrepo.snapshot VALUES (2, 1, '2012-11-01 00:01:00+09', '2nd', '00:00:02', 524288);
EOF
exec_command "exec_statsinfo -l"
exec_command "exec_statsinfo -s"

echo "/**--- インスタンスIDを指定 (スナップショット一覧表示モード) ---**/"
send_query << EOF > /dev/null
	INSERT INTO statsrepo.instance VALUES (2, '5807946214009601531', 'statsinfo', '5433', '8.4.0');
	INSERT INTO statsrepo.snapshot VALUES (3, 2, '2012-11-01 00:03:00+09', '3rd', '00:00:01', 262144);
	SELECT setval('statsrepo.instance_instid_seq', 3, false);
	SELECT setval('statsrepo.snapshot_snapid_seq', 4, false);
EOF
exec_command "exec_statsinfo -l -i 2"

echo "/**--- 存在しないインスタンスIDを指定 (スナップショット一覧表示モード) ---**/"
exec_command "exec_statsinfo -l -i 3"

echo "/**--- 複数のインスタンスが存在する (スナップショットサイズ表示モード) ---**/"
exec_command "exec_statsinfo -s"

echo "/*---- スナップショット取得モード ----*/"
echo "/**--- コメントにASCII文字を指定 ---**/"
exec_command "exec_statsinfo2 -S \"COMMENT\""
sleep ${SNAPSHOT_DELAY}

echo "/**--- コメントにマルチバイト文字を指定 ---**/"
exec_command "exec_statsinfo2 -S \"マルチバイト文字\""
sleep ${SNAPSHOT_DELAY}

echo "/**--- コメントに半角空白を指定 ---**/"
exec_command "exec_statsinfo2 -S \" \""
sleep ${SNAPSHOT_DELAY}

echo "/**--- コメントに空文字列を指定 ---**/"
exec_command "exec_statsinfo2 -S \"\""
sleep ${SNAPSHOT_DELAY}

echo "/*---- スナップショット削除モード ----*/"
echo "/**--- 存在するスナップショットIDを指定 ---**/"
exec_command "exec_statsinfo -D 3"
sleep ${SNAPSHOT_DELAY}

echo "/**--- 存在しないスナップショットIDを指定 ---**/"
exec_command "exec_statsinfo -D 999999"
sleep ${SNAPSHOT_DELAY}

send_query << EOF
SELECT
	snapid,
	instid,
	'"' || comment || '"' AS comment
FROM
	statsrepo.snapshot
ORDER BY
	snapid;
EOF

echo "/**--- 準正常系 ---**/"
echo "/***-- スナップショット取得日時が同一のものが含まれる --***/"
send_query << EOF > /dev/null
DELETE FROM statsrepo.alert WHERE instid = 3;
DELETE FROM statsrepo.instance WHERE instid = 3;
UPDATE statsrepo.snapshot SET time = '2012-11-01 00:00:00';
EOF
exec_command "exec_statsinfo -l"
exec_command "exec_statsinfo -s"

pg_ctl stop -D ${PGDATA} > /dev/null
