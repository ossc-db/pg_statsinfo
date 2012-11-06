#!/bin/bash

BASE_DIR=$(pwd)
DBCLUSTER_DIR=${BASE_DIR}/results/dbcluster
CONFIG_DIR=${BASE_DIR}/sql/config

export PGDATA=${DBCLUSTER_DIR}/pgdata
export PGPORT=5440
export PGUSER=postgres
export LANG=C
export PGTZ=JST-9
export PGDATESTYLE='ISO, MDY'

REPOSITORY_DATA=${DBCLUSTER_DIR}/repository
REPOSITORY_PORT=5450
REPOSITORY_USER=postgres

alias send_query="psql -p ${REPOSITORY_PORT} -U ${REPOSITORY_USER}"
