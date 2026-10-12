#!/bin/bash

verify_executables() {
	echo "🔎 Verifying required executables..."
	# --- List all required executables here ---
	local required_files=(
	        "K-V-Workload-Generator/load_gen"
		"skew-aware-bpk-benchmark/bpk_benchmark"
		"skew-aware-bpk-benchmark/query_lat_exp"
		"skew-aware-bpk-benchmark/runtime_tput_exp"
		"skew-aware-bpk-benchmark/query_statistics_est_benchmark"
	)

	local all_found=true
	for file in "${required_files[@]}"; do
		if [ ! -x "$file" ]; then
			echo "❌ Error: Required executable not found or not executable at: $file"
			all_found=false
		fi
	done
	if [ "$all_found" = false ]; then
		echo "👉 Please ensure all projects are compiled successfully. Exiting."
		exit 1
	else
		echo "✅ All executables found. Proceeding with experiments."
	fi
}

cd K-V-Workload-Generator/
make load_gen -j ($nproc)
cd -

cd skew-aware-rocksdb-8.9.1/
make static_lib -j ($nproc)
cd -

cd skew-aware-bpk-benchmark/
make -j ($nproc)
cd -

verify_executables