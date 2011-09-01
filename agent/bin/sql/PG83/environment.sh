#!/bin/sh

BASE_PATH=$(pwd)

export PGDATA=${BASE_PATH}/results/pgdata
export PGHOST=localhost
export PGPORT=5444
export PGUSER=statsinfo
export PGDATABASE=postgres
export PGDATESTYLE='ISO, MDY'
export PGTZ=JST-9
