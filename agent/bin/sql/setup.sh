#!/bin/bash

. ./sql/environment.sh
. ./sql/utility.sh

echo "/*---- リポジトリDB初期化 ----*/"
setup_repository
