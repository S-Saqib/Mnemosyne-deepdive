#!/bin/bash
ZD=$1
Z=$2
SIGMA=$3
str="Z${Z}_ZD${ZD}_bpk"
if [ -n "${SIGMA}" ]; then
	str="Z${Z}_ZD${ZD}_Sigma_${SIGMA}_bpk"
fi



echo uniform
grep "ingestion latency:" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
echo "Mnemosyne"
grep "ingestion latency (mnemosyne):" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
echo "Mnemosyne+"
grep "ingestion latency (mnemosyne-plus):" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
# Monkey is only run when query_lat_exp is given --monkey
if grep "ingestion latency (monkey-bottom-up):" *output.txt | grep -q ${str}; then
	echo "Monkey-bottom-up"
	grep "ingestion latency (monkey-bottom-up):" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
	echo "Monkey-top-down"
	grep "ingestion latency (monkey-top-down):" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
fi
# ElasticBF block, only printed when ElasticBF was run (query_lat_exp --ebf)
ebf_lines=$(grep "ingestion latency (elasticbf):" *output.txt 2>/dev/null | grep ${str})
if [ -n "${ebf_lines}" ]; then
echo "ElasticBF"
echo "${ebf_lines}" | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
fi
# SHaMBa block, only printed when SHaMBa was run (query_lat_exp --shamba)
shamba_lines=$(grep "ingestion latency (shamba):" *output.txt 2>/dev/null | grep ${str})
if [ -n "${shamba_lines}" ]; then
echo "SHaMBa"
echo "${shamba_lines}" | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
fi
