#!/bin/bash
I="1000000"     # no. of insert (load)
Q="1000000"     # no. of query (during run)
U="1000000"     # no. of update (during run)
E="512"         # Element (KV Pair) size in bytes
L="0.25"        # Ratio of key to element size (e.g., 0.25 means for E = 512, key size is 512*0.25 = 128 bytes)
Z_LIST=("0.0" "0.25" "0.5" "0.75" "1.0")    # ratios of zero result point lookup query
ZD_LIST=("0" "1")   # Zero result point lookup query distributions (0: uniform, 1: normal)
ZD_NDEV_LIST=("1.0" "5.0" "10.0" "100.0")    # normal distribution standard deviation list for zero result point lookup query
../K-V-Workload-Generator/load_gen -I${I} -E${E} -L ${L}
for Z in ${Z_LIST[@]}
do
	for ZD in ${ZD_LIST[@]}
	do
		for ZD_NDEV in ${ZD_NDEV_LIST[@]}
		do
			../K-V-Workload-Generator/load_gen -L${L} -U ${U} -E${E} -Q${Q} -Z ${Z} --ZD ${ZD} --ED ${ZD} --ZD_NDEV ${ZD_NDEV} --ED_NDEV ${ZD_NDEV} --PL --OP Z${Z}_ZD${ZD}_Sigma_${ZD_NDEV}_mixed_update_query_workload.txt 
		done
	done
done
mv workload.txt ingestion_workload.txt