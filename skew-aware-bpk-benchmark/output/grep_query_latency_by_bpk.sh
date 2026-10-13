#!/bin/bash
ZD=$1
Z=$2
SIGMA=$3
str="Z${Z}_ZD${ZD}_bpk"
if [ -n "${SIGMA}" ]; then
	str="Z${Z}_ZD${ZD}_Sigma_${SIGMA}_bpk"
fi



echo uniform
grep "point query latency:" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
echo "Mnemosyne"
grep "point query latency (mnemosyne):" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
echo "Mnemosyne+"
grep "point query latency (mnemosyne-plus):" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
# ElasticBF block, only printed when ElasticBF was run (query_lat_exp --ebf)
ebf_lines=$(grep "point query latency (elasticbf):" *output.txt 2>/dev/null | grep ${str})
if [ -n "${ebf_lines}" ]; then
echo "ElasticBF"
echo "${ebf_lines}" | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
fi
