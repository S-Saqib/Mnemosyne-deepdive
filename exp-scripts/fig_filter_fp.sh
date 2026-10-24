#!/bin/bash
# Filter misses and false positives of RocksDB, Monkey (bottom-up and top-down),
# Mnemosyne, Mnemosyne+, ElasticBF and SHaMBa:
#   2M unique random keys (64 B keys, 960 B values, 2 GB), T=10, 1 MB memtable
#   and SST files, 4 KB blocks, no compression, no direct I/O, block cache of
#   1.5x the index and filter size of RocksDB with 90% reserved for the index
#   and filter blocks (high priority, not pinned), then 1M uniform point lookups
#   with 0, 20, ..., 100% zero-result lookups interleaved; one figure per
#   bits-per-key (6 and 12).
# DIST=1 draws the lookups from a normal distribution instead (centred on the
# middle of the key range, standard deviation of about NDEV/62 of the key
# range, NDEV=1 by default). Its workloads, outputs and figures are named with
# normal-sigma<NDEV>, so the uniform ones are kept.

# Exit immediately if any command fails.
set -e
ulimit -n 65535

RUNS="${RUNS:-1}"
DIST="${DIST:-0}"
NDEV="${NDEV:-1.0}"
if [ "${DIST}" = "1" ]; then
	TAG="normal-sigma${NDEV}"
	FIG_PREFIX="FigFilterFP-${TAG}"
	TITLE_SUFFIX=", normal (NDEV=${NDEV})"
elif [ "${DIST}" = "0" ]; then
	TAG=""
	FIG_PREFIX="FigFilterFP"
	TITLE_SUFFIX=""
else
	echo "DIST must be 0 (uniform) or 1 (normal)"
	exit 1
fi
# read by the workload, experiment and merge scripts
export DIST NDEV TAG

# QUICK=1 runs a 1/100 scale version (20K keys, 10K lookups) that only checks
# that the whole pipeline works; its numbers are not meaningful.
if [ "${QUICK:-0}" = "1" ]; then
	SCALE_ENV="I=20000 Q=10000"
else
	SCALE_ENV=""
fi

echo "🚀 Starting script for the filter miss / false positive experiment..."

cd ../K-V-Workload-Generator
make load_gen
cd -
cd ../skew-aware-rocksdb-8.9.1
echo "[Filter FP] ⏳ Compiling the RocksDB library"
make -j$(nproc) static_lib
cd -
cd ../skew-aware-bpk-benchmark
echo "[Filter FP] ⏳ Compiling the executable query_lat_exp"
make -j$(nproc) query_lat_exp
echo "[Filter FP] ⏳ Executing workloads and measuring filter misses and false positives..."
env ${SCALE_ENV} ./main_filter_fp.sh ${RUNS}
echo "[Filter FP] ✅ Experiments complete"

cd ../plot_scripts/
echo "[Filter FP] ⏳ Plotting..."
for bpk in 6 12
do
	python3 filter-fp-plot.py --path=../skew-aware-bpk-benchmark/agg_output/filter_fp_${TAG:+${TAG}_}bpk${bpk}.csv --name=../exp-figures/${FIG_PREFIX}-bpk${bpk}.pdf --title="${bpk} bits-per-key${TITLE_SUFFIX}"
done
echo "[Filter FP] ✅ Plotting completes."
cd -

echo "🎉 All tasks for the filter miss / false positive experiment are finished!"
