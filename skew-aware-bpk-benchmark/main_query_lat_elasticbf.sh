#!/bin/bash
# Runs exp_query_lat_elasticbf.sh ${runs} times and aggregates the results into
# agg_output_elasticbf/ (CSV columns: uniform, Mnemosyne, Mnemosyne+, ElasticBF).
runs=${1:-1}

for i in `seq 1 ${runs}`
do
	echo "[ElasticBF vs Mnemosyne] Preparing workloads for Run ${i}..."
	cd ../workload_generator_scripts/
	../workload_generator_scripts/workload_type_II.sh
	cd -
	./exp_query_lat_elasticbf.sh
	mkdir -p "output-bpk-ebf${i}"
	mv output-bpk/*.txt output-bpk-ebf${i}/
done
mkdir -p "agg_output_elasticbf/"
python3 merge_by_short_bpk_with_rw_latency.py ${runs} agg_output_elasticbf/ output-bpk-ebf
