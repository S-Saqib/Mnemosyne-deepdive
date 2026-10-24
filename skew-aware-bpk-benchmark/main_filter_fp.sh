#!/bin/bash

runs=$1

for i in `seq 1 ${runs}`
do
	echo "[Filter FP] ⏳ Preparing workloads for Run ${i}..."
	cd ../workload_generator_scripts/
	../workload_generator_scripts/filter_fp_workload.sh
	cd -
	./exp_filter_fp.sh
	mkdir -p "output-filter-fp${i}"
	mv output-filter-fp/*.txt output-filter-fp${i}/
done
mkdir -p "agg_output/"
python3 merge_filter_fp.py ${runs} agg_output/ output-filter-fp
