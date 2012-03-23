#!/bin/sh

. ./sql/environment.sh

PGCONFIG_EXT=${PGDATA}/postgresql-ext.conf

echo "include '${PGCONFIG_EXT}'" >> ${PGDATA}/postgresql.conf
touch ${PGCONFIG_EXT}

echo "--- FU-0001 (statsinfo.cpu, statsinfo.device [num of tablespace: 0]) ---"
psql -ac "SELECT statsinfo.snapshot('comment')"
sleep 5
psql -aAc "SELECT * FROM statsrepo.cpu" | sed 's#\(|[0-9]\+\)\{4\}#|xxx|xxx|xxx|xxx#'
psql -aAc "SELECT * FROM statsrepo.device" | sed 's#\(|[0-9]\+\)\{2\}|[^|]\+\(|[0-9]\+\)\{6\}#|xxx|xxx|xxx|xxx|xxx|xxx|xxx|xxx|xxx#'

echo "--- FU-0002 (statsinfo.cpu, statsinfo.device [num of tablespace: 1]) ---"
mkdir -p ${PGDATA}/tablespace/tblspc01
psql -Ac "CREATE TABLESPACE tblspc01 LOCATION '${PGDATA}/tablespace/tblspc01'"
psql -ac "SELECT statsinfo.snapshot('comment')"
sleep 5
psql -aAc "SELECT * FROM statsrepo.device" | sed 's#\(|[0-9]\+\)\{2\}|[^|]\+\(|[0-9]\+\)\{6\}#|xxx|xxx|xxx|xxx|xxx|xxx|xxx|xxx|xxx#'

echo "--- FU-0003 (statsinfo.cpu, statsinfo.device [num of tablespace: 2]) ---"
mkdir -p ${PGDATA}/tablespace/tblspc02
psql -Ac "CREATE TABLESPACE tblspc02 LOCATION '${PGDATA}/tablespace/tblspc02'"
psql -ac "SELECT statsinfo.snapshot('comment')"
sleep 5
psql -aAc "SELECT * FROM statsrepo.device" | sed 's#\(|[0-9]\+\)\{2\}|[^|]\+\(|[0-9]\+\)\{6\}#|xxx|xxx|xxx|xxx|xxx|xxx|xxx|xxx|xxx#'

echo "--- FU-0004 (statsinfo.cpu, statsinfo.device [num of tablespace: 64]) ---"
for i in $(seq 3 64)
do
	spcname=$(printf "tblspc%02d" ${i})
	location=${PGDATA}/tablespace/${spcname}
	
	mkdir -p ${location}
	psql -Ac "CREATE TABLESPACE ${spcname} LOCATION '${location}'"
done
psql -ac "SELECT statsinfo.snapshot('comment')"
sleep 5
psql -aAc "SELECT * FROM statsrepo.device" | sed 's#\(|[0-9]\+\)\{2\}|[^|]\+\(|[0-9]\+\)\{6\}#|xxx|xxx|xxx|xxx|xxx|xxx|xxx|xxx|xxx#'

echo "--- FU-0012 (adjust log level [adjust_log_level = off]) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = off
pg_statsinfo.adjust_log_warning = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM aaa"
sleep 3
grep -q 'ERROR:  relation "aaa" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0013 (adjust log level [adjust_log_level is omitted]) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_warning = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM bbb"
sleep 3
grep -q 'ERROR:  relation "bbb" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0014 (adjust log level [adjust_log_info = '42P01']) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_info = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM ccc"
sleep 3
grep -q 'INFO:  relation "ccc" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0015 (adjust log level [adjust_log_notice = '42P01']) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_notice = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM ddd"
sleep 3
grep -q 'NOTICE:  relation "ddd" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0016 (adjust log level [adjust_log_warning = '42P01']) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_warning = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM eee"
sleep 3
grep -q 'WARNING:  relation "eee" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0017 (adjust log level [adjust_log_error = '00000']) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_error = '00000'
log_statement = 'all'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT statsinfo.sample()"
sleep 3
grep -q 'ERROR:  statement: SELECT statsinfo.sample()' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0018 (adjust log level [adjust_log_log = '42P01']) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_log = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM fff"
sleep 3
grep -q 'LOG:  relation "fff" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0019 (adjust log level [adjust_log_fatal = '42P01']) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_fatal = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM ggg"
sleep 3
grep -q 'FATAL:  relation "ggg" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0020 (adjust log level [adjust_log_log = '']) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_log = ''
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM hhh"
sleep 3
grep -q 'ERROR:  relation "hhh" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0021 (adjust log level [adjust_log_log = '42P01']) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_log = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM iii"
sleep 3
grep -q 'LOG:  relation "iii" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0022 (adjust log level [adjust_log_log = '42P01,42703']) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_log = '42P01,42703'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM jjj"
psql -ac "SELECT jjj FROM pg_class"
sleep 3
grep -q 'LOG:  relation "jjj" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(1): ${?}"
grep -q 'LOG:  column "jjj" does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(2): ${?}"

echo "--- FU-0023 (adjust log level [all parameters]) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_info = '42P01'
pg_statsinfo.adjust_log_notice = '42703'
pg_statsinfo.adjust_log_warning = '42601'
pg_statsinfo.adjust_log_error = '00000'
pg_statsinfo.adjust_log_log = '22P02'
pg_statsinfo.adjust_log_fatal = '42883'
log_statement = 'all'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM kkk"
psql -ac "SELECT kkk FROM pg_class"
psql -ac "SELECT * kkk"
psql -ac "SELECT 'kkk'::integer"
psql -ac "SELECT kkk()"
psql -ac "SELECT 'kkk'"
sleep 3
grep -q 'INFO:  relation "kkk" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(1): ${?}"
grep -q 'NOTICE:  column "kkk" does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(2): ${?}"
grep -q 'WARNING:  syntax error at or near "kkk" at character 10' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(3): ${?}"
grep -q 'ERROR:  statement: SELECT ''kkk''' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(4): ${?}"
grep -q 'LOG:  invalid input syntax for integer: "kkk" at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(5): ${?}"
grep -q 'FATAL:  function kkk() does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(6): ${?}"

echo "--- FU-0024 (adjust log level [duplicate SQL-STATE]) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_info = '42P01'
pg_statsinfo.adjust_log_notice = '42P01'
pg_statsinfo.adjust_log_warning = '42P01'
pg_statsinfo.adjust_log_error = '42P01'
pg_statsinfo.adjust_log_log = '42P01'
pg_statsinfo.adjust_log_fatal = '42P01'
log_statement = 'all'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM lll"
sleep 3
grep -q 'FATAL:  relation "lll" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0025 (adjust log level [SQL-STATE does not exist]) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_warning = '12345,42P01,123ABC,ABCD123'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM mmm"
sleep 3
grep -q 'WARNING:  relation "mmm" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log: ${?}"

echo "--- FU-0026 (adjust log level [adjust_log_level = off ÅÀ on]) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = off
pg_statsinfo.adjust_log_info = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM nnn"
sleep 3
grep -q 'ERROR:  relation "nnn" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(1): ${?}"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_info = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM nnn"
sleep 3
grep -q 'INFO:  relation "nnn" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(2): ${?}"

echo "--- FU-0027 (adjust log level [adjust_log_level = on ÅÀ off]) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_info = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM ooo"
sleep 3
grep -q 'INFO:  relation "ooo" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(1): ${?}"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = off
pg_statsinfo.adjust_log_info = '42P01'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM ooo"
sleep 3
grep -q 'ERROR:  relation "ooo" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(2): ${?}"

echo "--- FU-0028 (adjust log level [reload all parameters(1)]) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_info = '42P01'
pg_statsinfo.adjust_log_notice = '42703'
pg_statsinfo.adjust_log_warning = '42601'
pg_statsinfo.adjust_log_error = '00000'
pg_statsinfo.adjust_log_log = '22P02'
pg_statsinfo.adjust_log_fatal = '42883'
log_statement = 'all'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM ppp"
psql -ac "SELECT ppp FROM pg_class"
psql -ac "SELECT * ppp"
psql -ac "SELECT 'ppp'::integer"
psql -ac "SELECT ppp()"
psql -ac "SELECT 'ppp'"
sleep 3
grep -q 'INFO:  relation "ppp" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(1): ${?}"
grep -q 'NOTICE:  column "ppp" does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(2): ${?}"
grep -q 'WARNING:  syntax error at or near "ppp" at character 10' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(3): ${?}"
grep -q 'ERROR:  statement: SELECT ''ppp''' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(4): ${?}"
grep -q 'LOG:  invalid input syntax for integer: "ppp" at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(5): ${?}"
grep -q 'FATAL:  function ppp() does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(6): ${?}"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_info = '00000'
pg_statsinfo.adjust_log_notice = '22P02'
pg_statsinfo.adjust_log_warning = '42883'
pg_statsinfo.adjust_log_error = '42601'
pg_statsinfo.adjust_log_log = '42703'
pg_statsinfo.adjust_log_fatal = '42P01'
log_statement = 'all'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM ppp"
psql -ac "SELECT ppp FROM pg_class"
psql -ac "SELECT * ppp"
psql -ac "SELECT 'ppp'::integer"
psql -ac "SELECT ppp()"
psql -ac "SELECT 'ppp'"
sleep 3
grep -q 'FATAL:  relation "ppp" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(1): ${?}"
grep -q 'LOG:  column "ppp" does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(2): ${?}"
grep -q 'ERROR:  syntax error at or near "ppp" at character 10' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(3): ${?}"
grep -q 'INFO:  statement: SELECT ''ppp''' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(4): ${?}"
grep -q 'NOTICE:  invalid input syntax for integer: "ppp" at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(5): ${?}"
grep -q 'WARNING:  function ppp() does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(6): ${?}"

echo "--- FU-0029 (adjust log level [reload all parameters(2)]) ---"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_info = '42P01'
pg_statsinfo.adjust_log_notice = '42703'
pg_statsinfo.adjust_log_warning = '42601'
pg_statsinfo.adjust_log_error = '00000'
pg_statsinfo.adjust_log_log = '22P02'
pg_statsinfo.adjust_log_fatal = '42883'
log_statement = 'all'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM qqq"
psql -ac "SELECT qqq FROM pg_class"
psql -ac "SELECT * qqq"
psql -ac "SELECT 'qqq'::integer"
psql -ac "SELECT qqq()"
psql -ac "SELECT 'qqq'"
sleep 3
grep -q 'INFO:  relation "qqq" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(1): ${?}"
grep -q 'NOTICE:  column "qqq" does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(2): ${?}"
grep -q 'WARNING:  syntax error at or near "qqq" at character 10' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(3): ${?}"
grep -q 'ERROR:  statement: SELECT ''qqq''' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(4): ${?}"
grep -q 'LOG:  invalid input syntax for integer: "qqq" at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(5): ${?}"
grep -q 'FATAL:  function qqq() does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(6): ${?}"
cat << EOF > ${PGCONFIG_EXT}
pg_statsinfo.adjust_log_level = on
pg_statsinfo.adjust_log_info = ''
pg_statsinfo.adjust_log_notice = ''
pg_statsinfo.adjust_log_warning = ''
pg_statsinfo.adjust_log_error = ''
pg_statsinfo.adjust_log_log = ''
pg_statsinfo.adjust_log_fatal = ''
log_statement = 'all'
EOF
pg_ctl reload
sleep 3
psql -ac "SELECT * FROM qqq"
psql -ac "SELECT qqq FROM pg_class"
psql -ac "SELECT * qqq"
psql -ac "SELECT 'qqq'::integer"
psql -ac "SELECT qqq()"
psql -ac "SELECT 'qqq'"
sleep 3
grep -q 'ERROR:  relation "qqq" does not exist at character 15' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(1): ${?}"
grep -q 'ERROR:  column "qqq" does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(2): ${?}"
grep -q 'ERROR:  syntax error at or near "qqq" at character 10' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(3): ${?}"
grep -q 'LOG:  statement: SELECT ''qqq''' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(4): ${?}"
grep -q 'ERROR:  invalid input syntax for integer: "qqq" at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(5): ${?}"
grep -q 'ERROR:  function qqq() does not exist at character 8' ${PGDATA}/pg_log/postgresql.log
echo "check text-log(6): ${?}"
