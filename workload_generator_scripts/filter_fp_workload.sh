#!/bin/bash
# Workloads of the filter miss / false positive experiment (../exp-scripts/fig_filter_fp.sh):
# I unique random keys to load the database (64 B keys, 960 B values), and for
# each proportion of zero-result lookups Z, Q point lookups in which existing
# and zero-result lookups are interleaved.
# DIST=0 (default) draws the lookups uniformly; DIST=1 draws them from a normal
# distribution centred on the middle of the key range, with a standard deviation
# of about NDEV/62 of the key range (see the Generator in ../K-V-Workload-Generator).
# Written to filter_fp/ (uniform) or filter_fp_<TAG>/ so that the workloads of
# the other figures are kept.
I="${I:-2000000}"
Q="${Q:-1000000}"
E="1024"
L="0.0625"
DIST="${DIST:-0}"
NDEV="${NDEV:-1.0}"
WORKLOAD_DIR="filter_fp${TAG:+_${TAG}}"
# this list must match Z_list in ../skew-aware-bpk-benchmark/exp_filter_fp.sh
Z_LIST=("0.0" "0.2" "0.4" "0.6" "0.8" "1.0")

DIST_FLAGS="--ED ${DIST} --ZD ${DIST}"
if [ "${DIST}" = "1" ]; then
	DIST_FLAGS="${DIST_FLAGS} --ED_NDEV ${NDEV} --ZD_NDEV ${NDEV}"
fi

mkdir -p ${WORKLOAD_DIR}
cd ${WORKLOAD_DIR}
../../K-V-Workload-Generator/load_gen -I${I} -E${E} -L ${L}
for Z in ${Z_LIST[@]}
do
	../../K-V-Workload-Generator/load_gen -L${L} -E${E} -Q${Q} -Z ${Z} ${DIST_FLAGS} --PL --OP Z${Z}_query_workload.txt
done
mv workload.txt ingestion_workload.txt
