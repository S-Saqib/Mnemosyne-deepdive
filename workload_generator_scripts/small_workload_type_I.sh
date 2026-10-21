#!/bin/bash
# ~1/1000 scale version of workload_type_I.sh, used by QUICK=1 ../exp-scripts/fig11.sh
I="20000"
U="20000"
Q="40000"

Z="1.0"
E="128"
L="0.25"
../K-V-Workload-Generator/load_gen -I ${I} -E${E} -L ${L}
../K-V-Workload-Generator/load_gen -L ${L} -U ${U} -E${E} -I${I} -Q${Q} -Z ${Z}  --PL --OP mixed_query_workload.txt

mv mixed_query_workload.txt small_workload_type_I.txt
