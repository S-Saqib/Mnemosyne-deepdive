#!/bin/bash
cd ./exp-scripts/
for NDEV in ${NDEV_LIST:-1.0 5.0}
do
	QUICK=1 DIST=1 NDEV=${NDEV} FAST_DB_HOME=/home/cc/db_working_home ./fig_filter_fp.sh || exit 1
done
