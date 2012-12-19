#!/bin/bash

SCRIPT_DIR="./script"
RESULTS_DIR="./results"
EXPECTED_DIR="./expected"

function server_version()
{
	local version=""
	local version_num=0
	local vmaj=0
	local vmin=0
	local vrev=0

	version=$(postgres --version | sed 's/postgres\s(PostgreSQL)\s//')
	vmaj=$(echo ${version} | cut -d '.' -f 1)
	vmin=$(echo ${version} | cut -d '.' -f 2)
	vrev=$(echo ${version} | cut -d '.' -f 3)

	version_num=$(expr \( 100 \* ${vmaj} + ${vmin} \) \* 100 + ${vrev})
	echo ${version_num}
}

function verify_libraries()
{
	local libdir=$(pg_config --libdir)
	local failed=0

	pg_config --configure | grep -q -- "--with-libxml"

	if [ ${?} -eq 0 ] ; then
		printf "%-35s ... ok\n" "PostgreSQL build with libxml"
	else
		printf "%-35s ... not installed\n" "PostgreSQL build with libxml"
		failed=1
	fi

	if [ $(server_version) -ge 80400 ] ; then
		if [ -f "${libdir}/pg_stat_statements.so" ] ; then
			printf "%-35s ... ok\n" "pg_stat_statements"
		else
			printf "%-35s ... not installed\n" "pg_stat_statements"
			failed=1
		fi
	fi

	if [ ${failed} -eq 1 ] ; then
		exit 1
	fi
}

function do_test()
{
	local regress_list=("${@}")

	for regress in "${regress_list[@]}"
	do
		local script="${SCRIPT_DIR}/${regress}.sh"
		local result="${RESULTS_DIR}/${regress}.out"
		local diff="${RESULTS_DIR}/${regress}.diff"
		local ret="FAILED"

		printf "test %-30s ... " "${regress}"

		( eval "${script}" > "${result}" 2>&1 )

		for expect in $(find "${EXPECTED_DIR}" -type f -regex ".*${regress}\(_[0-9]+\)?\.out\$" | sort)
		do
			diff "${result}" "${expect}" > ${diff}

			if [ ${?} -eq 0 ] ; then
				ret="ok"
				success=$(expr ${success} + 1)
				break
			fi
		done
		echo "${ret}"
	done
}

regress_list=("${@}")
total=${#regress_list[@]}
success=0

which pg_config > /dev/null 2>&1

if [ ${?} -ne 0 ] ; then
	echo "ERROR: pg_config is not in the path" 1>&2
	exit 1
fi

echo "=== system information ============================================="
uname -a
lsb_release -a 2> /dev/null

echo "=== verify the required libraries are installed ===================="
verify_libraries

echo "=== cleanup working directory ======================================"
rm -fr ${RESULTS_DIR} && mkdir -pv ${RESULTS_DIR}

echo "=== running regression test scripts ================================"
do_test "${regress_list[@]}"

cat << __EOF__
=== regression test finish =========================================

========================
 ${success} of ${total} tests passed.
========================

__EOF__

if [ ${success} -eq ${total} ] ; then
	exit 0
else
	exit 1
fi
