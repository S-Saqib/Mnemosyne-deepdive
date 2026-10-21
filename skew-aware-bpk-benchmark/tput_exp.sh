#!/bin/bash

P="${P:-1024}"
B=64
T=4
E=128
R="${R:-3}"
BCC="${BCC:-131072}"
#BCC=262144
# Remember to specify a path for your dedicated storage device
DB_HOME="${FAST_DB_HOME:-./db_working_home}"
throughput_interval="${TPUT_INTERVAL:-100000}"
bpk_list=("2.0" "4.0" "6.0")
#bpk_list=("20.0")
dir="exp-throughputs-and-bpk-for-mixed-empty-workloads/"
WORKLOAD="${WORKLOAD:-../workload_generator_scripts/workload_type_I.txt}"
#WORKLOAD="../workload_generator_scripts/small_workload_type_I.txt"
WORKLOAD_NAME="workload_type_I"
# ElasticBF knobs (see ../ELASTICBF.md), which can be overridden from the environment.
# An empty EBF_UNIT_BPK means "one unit = the whole budget b", as in the paper.
EBF_SEG_SIZE="${EBF_SEG_SIZE:-4194304}"
EBF_UNITS="${EBF_UNITS:-6}"
EBF_LIFETIME="${EBF_LIFETIME:-10000}"
EBF_FLAGS="--ebf --ebf_seg_size ${EBF_SEG_SIZE} --ebf_units ${EBF_UNITS} --ebf_lifetime ${EBF_LIFETIME}"
if [ -n "${EBF_UNIT_BPK}" ]; then
	EBF_FLAGS="${EBF_FLAGS} --ebf_unit_bpk ${EBF_UNIT_BPK}"
fi
if [ -n "${EBF_EXTRA_FLAGS}" ]; then
	EBF_FLAGS="${EBF_FLAGS} ${EBF_EXTRA_FLAGS}"
fi
# SHaMBa knobs (see ../SHAMBA.md), which can be overridden from the environment.
# SHAMBA_SKIP is none, P or F.
SHAMBA_MODULES="${SHAMBA_MODULES:-2}"
SHAMBA_SKIP="${SHAMBA_SKIP:-P}"
SHAMBA_TH1="${SHAMBA_TH1:-0.01}"
SHAMBA_TH2="${SHAMBA_TH2:-0.02}"
SHAMBA_FLAGS="--shamba --shamba_modules ${SHAMBA_MODULES} --shamba_skip ${SHAMBA_SKIP} --shamba_th1 ${SHAMBA_TH1} --shamba_th2 ${SHAMBA_TH2}"
mkdir -p "${dir}"
#cp output/*.sh ${dir}
for bpk in ${bpk_list[@]}
do
	echo "./runtime_tput_exp -T ${T} -E ${E} --dd --no_dynamic_cmpct -p ${DB_HOME} --qwp ${WORKLOAD} -B ${B} -P ${P} -b ${bpk} --BCC ${BCC} -V 1 --dw --dr --clct-tputi ${throughput_interval} ${EBF_FLAGS} ${SHAMBA_FLAGS} --run-stats-op ${dir}/${WORKLOAD_NAME}_output_bpk-${bpk}.txt"
	./runtime_tput_exp -T ${T} -E ${E} --dd --no_dynamic_cmpct -p ${DB_HOME} --qwp ${WORKLOAD} -B ${B} -P ${P} -b ${bpk} --BCC ${BCC} -R ${R} -V 1 --dw --dr --clct-tputi ${throughput_interval} ${EBF_FLAGS} ${SHAMBA_FLAGS} --run-stats-op ${dir}/${WORKLOAD_NAME}_output_bpk-${bpk}.txt
	mv throughputs.txt ${dir}/${WORKLOAD_NAME}-bpk-${bpk}_throughputs.txt
	mv tracked_avg_bpk.txt ${dir}/${WORKLOAD_NAME}-bpk-${bpk}_tracked_avg_bpk.txt

	rm ${DB_HOME}-monkey-top-down/*
	rm ${DB_HOME}-monkey-bottom-up/*
	rm ${DB_HOME}-mnemosyne/*
	rm ${DB_HOME}-elasticbf/*
	rm ${DB_HOME}-shamba/*
done
#cp ../workload_generator_scripts/ingestion_workload.txt ${dir}/
