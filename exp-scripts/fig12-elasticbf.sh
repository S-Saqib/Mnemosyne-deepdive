#!/bin/bash
# Figure 12 of the Mnemosyne paper (workload type II, read latency and read
# bytes vs bits-per-key) with ElasticBF (Li et al., ATC'19) added as a fourth
# line. Uses the same workloads and parameters as fig12.sh.

set -e
ulimit -n 65535

RUNS=${RUNS:-1}

cd ../skew-aware-bpk-benchmark
echo "[Fig 12 + ElasticBF] Compiling query_lat_exp"
make query_lat_exp
echo "[Fig 12 + ElasticBF] Running RocksDB / Mnemosyne / Mnemosyne+ / ElasticBF"
./main_query_lat_elasticbf.sh ${RUNS}

cd ../plot_scripts/
AGG=../skew-aware-bpk-benchmark/agg_output_elasticbf
i=0
for metric_file in query_latency read_bytes; do
	metric=read_latency
	[ "${metric_file}" = "read_bytes" ] && metric=read_bytes
	for ZD in 0 1; do
		for Z in 0.0 0.5 1.0; do
			legend=""
			[ "${Z}" = "0.0" ] && [ "${ZD}" = "0" ] && legend="--showLegend"
			python3 mnemosyneplus-main-exp-plot.py --path=${AGG}/${metric_file}_Z${Z}_ZD${ZD}.txt \
				--name=../exp-figures/Fig12-elasticbf-${metric_file}-Z${Z}-ZD${ZD}.pdf --metric=${metric} --autoY ${legend}
		done
	done
done
cd -
echo "[Fig 12 + ElasticBF] Done. Figures are in exp-figures/."
