#!/bin/bash
ZD=$1
Z=$2
SIGMA=$3
str="Z${Z}_ZD${ZD}_bpk"
if [ -n "${SIGMA}" ]; then
	str="Z${Z}_ZD${ZD}_Sigma_${SIGMA}_bpk"
fi

echo uniform
grep "accessed data blocks:" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
echo "Mnemosyne"
grep "used data blocks (mnemosyne):" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
echo "Mnemosyne+"
grep "used data blocks (mnemosyne-plus):" *output.txt | grep ${str} | awk -F':|-' '{print $2";"$NF}' | awk -F'_|;' '{print $1,$NF}' | sort -g | awk '{print $2}'
