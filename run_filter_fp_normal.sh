#!/bin/bash
# runs the normal-workload experiment once per standard deviation;
# NDEV_LIST="1.0" ./run_filter_fp_normal.sh runs only one of them
cd ./exp-scripts/
for NDEV in ${NDEV_LIST:-1.0 5.0}
do
	DIST=1 NDEV=${NDEV} FAST_DB_HOME=/home/cc/db_working_home ./fig_filter_fp.sh || exit 1
done
