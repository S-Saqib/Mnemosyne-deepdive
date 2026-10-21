#!/bin/bash

# Exit immediately if any command fails.
set -e
ulimit -n 65535

# QUICK=1 runs a ~1/1000 scale version of the experiment. It only checks that
# the whole pipeline works: the database is a few MB, so the numbers are not
# comparable to the paper.
if [ "${QUICK:-0}" = "1" ]; then
	# 20K inserts, 20K updates and 40K point queries instead of 20M, 20M and 40M
	WORKLOAD_SCRIPT="./small_workload_type_I.sh"
	# workload, buffer size in pages, block cache in KB, number of runs and
	# throughput sampling interval in operations (tput_exp.sh); the interval
	# keeps the same number of points per curve as at full scale
	SCALE_ENV="WORKLOAD=../workload_generator_scripts/small_workload_type_I.txt P=64 BCC=1024 R=1 TPUT_INTERVAL=100"
	# ElasticBF segment size and lifeTime (tput_exp.sh), scaled down as well:
	# 2 segments per file as at full scale, and a lifeTime of a few Gets per segment
	SCALE_ENV="${SCALE_ENV} EBF_SEG_SIZE=262144 EBF_LIFETIME=200"
	# label operations in thousands and fit the y-axes to the data
	PLOT_ARGS="--opsUnit=K --autoY"
else
	WORKLOAD_SCRIPT="./workload_type_I.sh"
	SCALE_ENV=""
	PLOT_ARGS=""
fi

echo "🚀 Starting script for Figure 11..."

echo "[Fig 11] ⏳ Preparing workloads..."

cd ../workload_generator_scripts/
bash ${WORKLOAD_SCRIPT}
cd -

cd ../skew-aware-bpk-benchmark
echo "[Fig 11] ⏳ Compiling the executable runtime_tput_exp"
make -j$(nproc) runtime_tput_exp
echo "[Fig 11] ⏳ Executing workloads and measuring the throughput with different bits-per-key (RocksDB, Monkey, Mnemosyne, ElasticBF, SHaMBa)..."
env ${SCALE_ENV} ./tput_exp.sh
echo "[Fig 11] ✅ Experiments complete"

cd ../plot_scripts/
echo "[Fig 11] ⏳ Plotting runtime throughput and average bits-per-key..."
python3 static-monkey-vs-rocksdb-throughput-exp.py --name="../exp-figures/Fig11-c.pdf" ${PLOT_ARGS}
python3 static-monkey-vs-rocksdb-throughput-exp.py --tput_path="../skew-aware-bpk-benchmark/exp-throughputs-and-bpk-for-mixed-empty-workloads/workload_type_I-bpk-4.0_throughputs.txt" --bpk_path="../skew-aware-bpk-benchmark/exp-throughputs-and-bpk-for-mixed-empty-workloads/workload_type_I-bpk-4.0_tracked_avg_bpk.txt" --name="../exp-figures/Fig11-b.pdf" ${PLOT_ARGS}
python3 static-monkey-vs-rocksdb-throughput-exp.py --tput_path="../skew-aware-bpk-benchmark/exp-throughputs-and-bpk-for-mixed-empty-workloads/workload_type_I-bpk-6.0_throughputs.txt" --bpk_path="../skew-aware-bpk-benchmark/exp-throughputs-and-bpk-for-mixed-empty-workloads/workload_type_I-bpk-6.0_tracked_avg_bpk.txt" --name="../exp-figures/Fig11-a.pdf" --showLegend ${PLOT_ARGS}
cd -

echo "[Fig 11] ✅ Plotting complete."

echo "🎉 All tasks for Figure 11 are finished!"
