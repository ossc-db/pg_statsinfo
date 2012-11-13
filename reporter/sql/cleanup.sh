#!/bin/bash

. ./sql/environment.sh

if [ -e ${DBCLUSTER_DIR} ] ; then
	for datadir in $(find ${DBCLUSTER_DIR} -maxdepth 1 -mindepth 1 -type d)
	do
		pg_ctl stop -D ${datadir} > /dev/null 2>&1
	done
fi
