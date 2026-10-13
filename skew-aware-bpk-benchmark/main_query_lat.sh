#!/bin/bash

runs=$1

for i in `seq 1 ${runs}`
do
        echo "[Fig 12] ⏳ Preparing workloads for Run ${i}..."
	cd ../workload_generator_scripts/
	../workload_generator_scripts/workload_type_II.sh
	cd -
	./exp_query_lat.sh
	mkdir -p "output-bpk${i}"
	mv output-bpk/*.txt output-bpk${i}/
done
mkdir -p "agg_output/"
# this list must match bpk_list in exp_query_lat.sh
python3 merge_by_short_bpk_with_rw_latency.py ${runs} agg_output/ output-bpk --bpk-list 2 3 4 5 6 7 8 9 10 11 12
