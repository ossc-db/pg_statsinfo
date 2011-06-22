#!/bin/sh

BASE_PATH=$(pwd)

export PGDATA=${BASE_PATH}/results/pgdata
export PGPORT=5444
export PGUSER=statsinfo
export PGDATABASE=postgres
export PGDATESTYLE='ISO, MDY'
export PGOPTIONS=' -c intervalstyle=postgres'
export PGTZ=JST-9
