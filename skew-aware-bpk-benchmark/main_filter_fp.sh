#!/bin/bash

runs=$1

# TAG (set by ../exp-scripts/fig_filter_fp.sh) names the workload distribution;
# it is empty for the uniform workload, whose outputs keep their original names
OUTPUT_DIR="output-filter-fp${TAG:+-${TAG}}"
RUN_PREFIX="${OUTPUT_DIR}${TAG:+-run}"

for i in `seq 1 ${runs}`
do
	echo "[Filter FP] ⏳ Preparing workloads for Run ${i}..."
	cd ../workload_generator_scripts/
	../workload_generator_scripts/filter_fp_workload.sh
	cd -
	./exp_filter_fp.sh
	mkdir -p "${RUN_PREFIX}${i}"
	mv ${OUTPUT_DIR}/*.txt ${RUN_PREFIX}${i}/
done
mkdir -p "agg_output/"
python3 merge_filter_fp.py ${runs} agg_output/ ${RUN_PREFIX} ${TAG}
