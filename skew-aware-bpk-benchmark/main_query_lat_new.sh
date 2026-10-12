#!/bin/bash

runs=$1

for i in `seq 1 ${runs}`
do
        echo "[Fig New] ⏳ Preparing workloads for Run ${i}..."
	cd ../workload_generator_scripts/
	../workload_generator_scripts/workload_test_01.sh
	cd -
	# ./exp_query_lat.sh
    ./exp_query_lat_new.sh
	mkdir -p "output-bpk${i}"
	mv output-bpk/*.txt output-bpk${i}/
done
mkdir -p "agg_output/"
# these lists must match Z_LIST, ZD_NDEV_LIST and bpk_list in exp_query_lat_new.sh
python3 merge_by_short_bpk_with_rw_latency.py ${runs} agg_output/ output-bpk --Z-list 0.0 0.25 0.5 0.75 1.0 --sigma-list 1.0 5.0 10.0 100.0 --bpk-list 2 3 4 5 6 7 8 9 10 11 12
