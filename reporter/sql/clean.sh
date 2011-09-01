#!/bin/sh

. ./sql/environment.sh

# stop PostgreSQL
pg_ctl stop > /dev/null 2>&1
