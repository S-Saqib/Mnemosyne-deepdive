#!/bin/bash
# Filter misses and false positives of RocksDB, Monkey (bottom-up and top-down),
# Mnemosyne, Mnemosyne+, ElasticBF and SHaMBa on a read-only workload, for each
# bits-per-key and proportion of zero-result lookups.

E=1024
# 4 KB data blocks
B=4
# 1 MB memtable and SST files
M=1048576
F=1048576
T=10
bpk_list=("6" "12")
# this list must match Z_LIST in ../workload_generator_scripts/filter_fp_workload.sh
Z_list=("0.0" "0.2" "0.4" "0.6" "0.8" "1.0")
# block cache = BCC_META_RATIO x index and filter size of RocksDB, with this
# proportion reserved for the (high priority) index and filter blocks
BCC_META_RATIO="${BCC_META_RATIO:-1.5}"
BCHPR="${BCHPR:-0.9}"
# Remember to specify a path for your dedicated storage device
DB_HOME="${FAST_DB_HOME:-./db_working_home}"
# TAG (set by ../exp-scripts/fig_filter_fp.sh) names the workload distribution;
# it is empty for the uniform workload
WORKLOAD_HOME="../workload_generator_scripts/filter_fp${TAG:+_${TAG}}"
OUTPUT_DIR="output-filter-fp${TAG:+-${TAG}}"
# ElasticBF knobs (see ../ELASTICBF.md): 256 KB segments keep 4 segments per
# 1 MB file, as in Figure 12
EBF_SEG_SIZE="${EBF_SEG_SIZE:-262144}"
EBF_UNITS="${EBF_UNITS:-6}"
EBF_LIFETIME="${EBF_LIFETIME:-10000}"
EBF_FLAGS="--ebf --ebf_seg_size ${EBF_SEG_SIZE} --ebf_units ${EBF_UNITS} --ebf_lifetime ${EBF_LIFETIME}"
# SHaMBa knobs (see ../SHAMBA.md). SHAMBA_SKIP is none, P or F.
SHAMBA_MODULES="${SHAMBA_MODULES:-2}"
SHAMBA_SKIP="${SHAMBA_SKIP:-P}"
SHAMBA_TH1="${SHAMBA_TH1:-0.01}"
SHAMBA_TH2="${SHAMBA_TH2:-0.02}"
SHAMBA_FLAGS="--shamba --shamba_modules ${SHAMBA_MODULES} --shamba_skip ${SHAMBA_SKIP} --shamba_th1 ${SHAMBA_TH1} --shamba_th2 ${SHAMBA_TH2}"
# no --dr/--dw: direct I/O is disabled
COMMON_FLAGS="-T ${T} -E ${E} -B ${B} -M ${M} -F ${F} --BCHPR ${BCHPR} --bcc_meta_ratio ${BCC_META_RATIO} --no_pin --no_compression --dd -p ${DB_HOME} -V 1 --monkey ${EBF_FLAGS} ${SHAMBA_FLAGS}"
mkdir -p "${OUTPUT_DIR}"
for bpk in ${bpk_list[@]}
do
	for Z in ${Z_list[@]}
	do
		CMD="./query_lat_exp ${COMMON_FLAGS} -b ${bpk} --iwp ${WORKLOAD_HOME}/ingestion_workload.txt --qwp ${WORKLOAD_HOME}/Z${Z}_query_workload.txt --run-stats-op ${OUTPUT_DIR}/bpk${bpk}_Z${Z}-output.txt"
		echo "${CMD}"
		${CMD}
		rm -rf ${DB_HOME}-monkey-bottom-up/* ${DB_HOME}-monkey-top-down/* ${DB_HOME}-mnemosyne/* ${DB_HOME}-mnemosyne-plus/* ${DB_HOME}-elasticbf/* ${DB_HOME}-shamba/*
	done
done
