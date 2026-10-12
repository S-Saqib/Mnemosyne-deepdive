#!/bin/bash

# Exit immediately if any command fails.
set -e
ulimit -n 65535

RUNS=1

# QUICK=1 runs a ~1/2000 scale version of the experiment that finishes in under
# 5 minutes. It only checks that the whole pipeline works: the database is ~5MB,
# so the numbers are not comparable to the paper.
if [ "${QUICK:-0}" = "1" ]; then
	# inserts, point queries and updates (workload_type_II.sh), then buffer size
	# in pages and block cache in KB (exp_query_lat.sh)
	SCALE_ENV="I=10500 Q=15500 U=5000 P=32 BCC=2048"
	# fit the y-axes to the data instead of using the full-scale ranges
	LATENCY_YMAX="--autoY"
	PLOT_ARGS="--autoY --bytesUnit=MB"
else
	SCALE_ENV=""
	LATENCY_YMAX="--ymax=60"
	PLOT_ARGS=""
fi

echo "🚀 Starting script for Figure 12..."
 
cd ../skew-aware-bpk-benchmark
echo "[Fig 12] ⏳ Compiling the executable query_lat_exp"
make -j$(nproc) query_lat_exp
echo "[Fig 12] ⏳ Executing workloads and measuring the query latency with different bits-per-key..."
env ${SCALE_ENV} ./main_query_lat.sh ${RUNS}
echo "[Fig 12] ✅ Experiments complete"

cd ../plot_scripts/
echo "[Fig 12] ⏳ Plotting query latency.."

echo "Plotting the query latency and read bytes with fast SSDs"
python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/query_latency_Z0.0_ZD0.txt --name=../exp-figures/Fig12-g.pdf --metric=read_latency --showLegend ${LATENCY_YMAX} ${PLOT_ARGS}
python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/query_latency_Z0.5_ZD0.txt --name=../exp-figures/Fig12-h.pdf --metric=read_latency ${LATENCY_YMAX} ${PLOT_ARGS}
python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/query_latency_Z1.0_ZD0.txt --name=../exp-figures/Fig12-i.pdf --metric=read_latency ${LATENCY_YMAX} ${PLOT_ARGS}

python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/query_latency_Z0.0_ZD1.txt --name=../exp-figures/Fig12-j.pdf --metric=read_latency ${PLOT_ARGS}
python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/query_latency_Z0.5_ZD1.txt --name=../exp-figures/Fig12-k.pdf --metric=read_latency ${PLOT_ARGS}
python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/query_latency_Z1.0_ZD1.txt --name=../exp-figures/Fig12-l.pdf --metric=read_latency ${PLOT_ARGS}

python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/read_bytes_Z0.0_ZD0.txt --name=../exp-figures/Fig12-a.pdf --metric=read_bytes --showLegend ${PLOT_ARGS}
python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/read_bytes_Z0.5_ZD0.txt --name=../exp-figures/Fig12-b.pdf --metric=read_bytes ${PLOT_ARGS}
python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/read_bytes_Z1.0_ZD0.txt --name=../exp-figures/Fig12-c.pdf --metric=read_bytes ${PLOT_ARGS}

python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/read_bytes_Z0.0_ZD1.txt --name=../exp-figures/Fig12-d.pdf --metric=read_bytes ${PLOT_ARGS}
python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/read_bytes_Z0.5_ZD1.txt --name=../exp-figures/Fig12-e.pdf --metric=read_bytes ${PLOT_ARGS}
python3 mnemosyneplus-main-exp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/read_bytes_Z1.0_ZD1.txt --name=../exp-figures/Fig12-f.pdf --metric=read_bytes ${PLOT_ARGS}

echo "[Fig 12] ✅ Plotting completes."
cd -

echo "🎉 All tasks for Figure 12 are finished!"
