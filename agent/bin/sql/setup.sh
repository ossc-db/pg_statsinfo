#!/bin/bash

. ./sql/environment.sh
. ./sql/utility.sh

echo "/*---- Initialize repository DB ----*/"
setup_repository
