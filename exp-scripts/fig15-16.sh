#!/bin/bash

# Exit immediately if any command fails.
set -e
ulimit -n 65535

# QUICK=1 runs a 1/100 scale version of Figure 15 only (no Figure 16): 300K
# records and 600K operations instead of 30M and 60M, one run, and the memtable,
# arena block, SST file, L1 and block cache sizes divided by 100 as well, so that the
# LSM-tree has the same shape (number of files and levels) as at full scale.
# Its numbers are not comparable to the paper. YCSB_BPK sets the bits-per-key
# (2 by default, as in the paper). The results go to
# YCSB-cpp/exp-th{1,4}_dynamic_cmpact_small_bpk<bpk>/ and the figures to
# exp-figures/Fig15-{a,b}-small-bpk<bpk>.pdf.
if [ "${QUICK:-0}" = "1" ]; then
	BPK=${YCSB_BPK:-2}
	TAG="_small_bpk${BPK}"
	export YCSB_BPK=${BPK}
	export YCSB_OUTPUT_TAG=${TAG}
	export YCSB_RECORDS=300000
	export YCSB_OPS=600000
	export YCSB_RUNS=1
	export YCSB_BCC=2097152                     # 200MB / 100
	export YCSB_WRITE_BUFFER_SIZE=335544        # 32MB / 100
	export YCSB_ARENA_BLOCK_SIZE=10486          # 1MB / 100
	export YCSB_TARGET_FILE_SIZE_BASE=335544    # 32MB / 100
	export YCSB_MAX_BYTES_FOR_LEVEL_BASE=1342177 # 128MB / 100

	echo "🚀 Starting the small-scale version of Figure 15 (bits-per-key ${BPK})..."
	cd ../YCSB-cpp
	echo "[Fig 15a] ⏳ Experimenting different YCSB workloads with 1 thread..."
	./run_ycsb_basic.sh 1
	echo "[Fig 15b] ⏳ Experimenting different YCSB workloads with 4 threads..."
	./run_ycsb_basic.sh 4
	cd -

	cd ../plot_scripts/
	echo "[Fig 15] ⏳ Plotting throughput for different YCSB workloads..."
	python3 ycsb-plot.py --path=../YCSB-cpp/exp-th1_dynamic_cmpact${TAG}/ycsb_agg_exp.txt --name=../exp-figures/Fig15-a-small-bpk${BPK}.pdf --autoY
	python3 ycsb-plot.py --path=../YCSB-cpp/exp-th4_dynamic_cmpact${TAG}/ycsb_agg_exp.txt --name=../exp-figures/Fig15-b-small-bpk${BPK}.pdf --showLegend --autoY
	cd -
	echo "🎉 The small-scale version of Figure 15 is finished!"
	exit 0
fi

echo "🚀 Starting script for Figures 15 and 16..."

cd ../YCSB-cpp
./run_ycsb_main.sh
cd -

cd ../plot_scripts/
echo "[Fig 15] ⏳ Plotting throughput for different YCSB workloads..."
python3 ycsb-plot.py --path=../YCSB-cpp/exp-th1_dynamic_cmpact/ycsb_agg_exp.txt --name=../exp-figures/Fig15-a.pdf
python3 ycsb-plot.py --path=../YCSB-cpp/exp-th4_dynamic_cmpact/ycsb_agg_exp.txt --name=../exp-figures/Fig15-b.pdf --showLegend
echo "[Fig 15] ✅ Plotting complete."
echo "[Fig 16] ⏳ Plotting throughput for different scales with YCSB workload type B..."
python3 scalability-ycsb-plot.py --path=../YCSB-cpp/exp-th16_dynamic_cmpact/ycsb_scalability_workloadb_dynamic_exp.txt --name=../exp-figures/Fig16-a.pdf
python3 scalability-ycsb-plot.py --path=../YCSB-cpp/exp-th16_no_dynamic_cmpact/ycsb_scalability_workloadb_no_dynamic_exp.txt --name=../exp-figures/Fig16-b.pdf
echo "[Fig 16] ✅ Plotting complete."
cd -


echo "🎉 All tasks for Figures 15 and 16 are finished!"
