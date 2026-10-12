#!/bin/bash

P=1024  # number of pages that can fit into the buffer
B=32    # number of entries per page
T=4     # size ratio
E=512   # entry (KV pair) size in bytes
R=1     # number of runs
bpk_list=("2" "3" "4" "5" "6" "7" "8" "9" "10" "11" "12")  # bits per key list
BCC="25000" # "524288"    # block cache size in kilobytes (e.g., 524288 KB = 512 MB)
# Remember to specify a path for your dedicated storage device
DB_HOME="${FAST_DB_HOME:-./db_working_home}"
WORKLOAD_HOME="../workload_generator_scripts"
Z_LIST=("0.0" "0.25" "0.5" "0.75" "1.0")    # ratios of zero result point lookup query
ZD_LIST=("0" "1")   # Zero result point lookup query distributions (0: uniform, 1: normal)
ZD_NDEV_LIST=("1.0" "5.0" "10.0" "100.0")    # normal distribution standard deviation list for zero result point lookup query
OUTPUT_DIR="output-bpk"
mkdir -p "${OUTPUT_DIR}"
#ulimit -n 65535
for ZD in ${ZD_LIST[@]}
do
	for Z in ${Z_LIST[@]}
    do
        for ZD_NDEV in ${ZD_NDEV_LIST[@]}
        do
            for bpk in ${bpk_list[@]}
            do
                echo "./query_lat_exp -T ${T} -E ${E} --dd -p ${DB_HOME} --iwp ${WORKLOAD_HOME}/ingestion_workload.txt --qwp ${WORKLOAD_HOME}/Z${Z}_ZD${ZD}_Sigma_${ZD_NDEV}_mixed_update_query_workload.txt -B ${B} -P ${P} -b ${bpk} --BCC ${BCC} -V 1 --dr --run-stats-op ${OUTPUT_DIR}/Z${Z}_ZD${ZD}_Sigma_${ZD_NDEV}_bpk-${bpk}-output.txt"
                ./query_lat_exp -T ${T} -E ${E} --dd -p ${DB_HOME} --iwp ${WORKLOAD_HOME}/ingestion_workload.txt --qwp ${WORKLOAD_HOME}/Z${Z}_ZD${ZD}_Sigma_${ZD_NDEV}_mixed_update_query_workload.txt -B ${B} -P ${P} -b ${bpk} --BCC ${BCC} -R ${R} -V 1 --dr --run-stats-op ${OUTPUT_DIR}/Z${Z}_ZD${ZD}_Sigma_${ZD_NDEV}_bpk-${bpk}-output.txt
                rm ${DB_HOME}-mnemosyne/*
                rm ${DB_HOME}-mnemosyne-plus/*
            done
            cd ${OUTPUT_DIR}/
            echo "../output/grep_accessed_data_blocks_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > accessed_data_blocks_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt"
            echo "../output/grep_read_bytes_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > read_bytes_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt"
            echo "../output/grep_query_latency_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > query_latency_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt"
            echo "../output/grep_avg_latency_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > avg_latency_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt"
            echo "../output/grep_write_latency_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > write_latency_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt"
            ../output/grep_accessed_data_blocks_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > accessed_data_blocks_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt
            ../output/grep_read_bytes_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > read_bytes_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt
            ../output/grep_query_latency_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > query_latency_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt
            ../output/grep_avg_latency_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > avg_latency_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt
            ../output/grep_write_latency_by_bpk.sh ${ZD} ${Z} ${ZD_NDEV} > write_latency_ZD${ZD}_Z${Z}_Sigma_${ZD_NDEV}_result.txt
            cd -
        done
    done
done
#cp ../workload_generator_scripts/ingestion_workload.txt output/
