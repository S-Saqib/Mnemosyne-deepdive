#!/bin/bash

# Exit immediately if any command fails.
set -e
ulimit -n 65535

echo "🚀 Starting script for Figure 5..."
 

cd ../skew-aware-bpk-benchmark
echo "[Fig 5] ⏳ Generating workloads and collect access statistics with different scales..."
./main-collect-query-stats-for-optimization-exp.sh
echo "[Fig 5] ✅ Collecting access statistics completes."

echo "[Fig 5] ⏳ Solving the bpk optimization problem with different approaches..."
echo "      (Now running scripts inside 'distribute_lsm_bfsize'...)"
cd ../distribute_lsm_bfsize
./run_scripts.sh
cd -

echo "[Fig 5] ✅ Experiments and plotting complete."
echo ""
echo "🎉 All tasks for Figure 5 are finished!"
