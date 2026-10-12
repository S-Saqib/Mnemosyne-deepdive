#!/bin/bash

# Exit immediately if any command fails.
set -e
ulimit -n 65535

RUNS=1

echo "🚀 Starting script for Figure New..."

cd ../skew-aware-bpk-benchmark
echo "[Fig New] ⏳ Compiling the executable query_lat_exp"
make query_lat_exp
echo "[Fig New] ⏳ Executing workloads and measuring the query latency with different bits-per-key..."
./main_query_lat_new.sh ${RUNS}
echo "[Fig New] ✅ Experiments complete"

cd ../plot_scripts/
echo "[Fig New] ⏳ Plotting query latency.."

# these lists must match Z_LIST, ZD_LIST and ZD_NDEV_LIST in exp_query_lat_new.sh
Z_LIST=("0.0" "0.25" "0.5" "0.75" "1.0")
ZD_LIST=("0" "1")
ZD_NDEV_LIST=("1.0" "5.0" "10.0" "100.0")
AGG_DIR="../skew-aware-bpk-benchmark/agg_output"
# save the figures without opening a window for each of them
export MPLBACKEND=Agg

echo "Plotting the query latency and read bytes with fast SSDs"
for ZD_NDEV in ${ZD_NDEV_LIST[@]}
do
	# one group of figures per normal distribution standard deviation
	FIG_DIR="../exp-figures/FigNew-Sigma_${ZD_NDEV}"
	mkdir -p "${FIG_DIR}"
	# as in Fig 12, only the first panel of each metric shows the legend
	LEGEND="--showLegend"
	for ZD in ${ZD_LIST[@]}
	do
		for Z in ${Z_LIST[@]}
		do
			python3 mnemosyneplus-main-exp-plot.py --path=${AGG_DIR}/query_latency_Z${Z}_ZD${ZD}_Sigma_${ZD_NDEV}.txt --name=${FIG_DIR}/query_latency_Z${Z}_ZD${ZD}.pdf --metric=read_latency --autoY --width=10 ${LEGEND}
			python3 mnemosyneplus-main-exp-plot.py --path=${AGG_DIR}/read_bytes_Z${Z}_ZD${ZD}_Sigma_${ZD_NDEV}.txt --name=${FIG_DIR}/read_bytes_Z${Z}_ZD${ZD}.pdf --metric=read_bytes --autoY --bytesUnit=GB --width=10 ${LEGEND}
			LEGEND=""
		done
	done
done

echo "[Fig New] ✅ Plotting completes."
cd -

echo "🎉 All tasks for Figure New are finished!"
