#!/bin/bash

# ==============================================================================
# run_ycsb_scale.sh
#
# Description:
#   Runs YCSB benchmarks across various scales (1x to 5x).
#   At each scale, it varies operation count, record count, and block cache size.
#   Output is saved to directories indicating the scale, thread count, and
#   dynamic compaction status.
#   Finally, it calls merge_ycsb_scale.py to aggregate the results.
#
# Usage:
#   ./run_ycsb_scale.sh [num_threads] [dynamic_compact]
#
#   [num_threads]: (Optional) The number of threads for the benchmark.
#                  Defaults to 16.
#   [dynamic_compact]: (Optional) 'true' or 'false' to enable/disable
#                      dynamic compaction. Defaults to 'true'.
#
# ==============================================================================
# --- Configuration ---

# Default values
DEFAULT_THREADS=16
DEFAULT_DYNAMIC_COMPACT="true"

# Override defaults with user-provided arguments
NUM_THREADS=${1:-$DEFAULT_THREADS}
DYNAMIC_COMPACT=${2:-$DEFAULT_DYNAMIC_COMPACT}

# --- Pre-run Checks ---

# Remember to specify your database path here to use a dedicated storage device
DB_HOME="${FAST_DB_HOME:-./ycsb_working_home}"

if [ ! -d "$(dirname "$DB_HOME")" ]; then
    echo "Cannot find database parent directory $(dirname "$DB_HOME")"
    mkdir -p "$(dirname "$DB_HOME")"
    echo "Created the database parent directory using 'mkdir -p $(dirname "$DB_HOME")'"
fi

# Download and compile the origin rocksdb
if [ ! -e ../rocksdb-8.9.1/librocksdb.so.8.9 ]; then
  ./download_and_compile_default_rocksdb.sh
fi

# Compile the Mnemosyne
if [ ! -e ../skew-aware-rocksdb-8.9.1/librocksdb.so.8.9 ]; then
  cd ../skew-aware-rocksdb-8.9.1
  make clean && make -j 32 shared_lib
  cd -
fi

ORIGIN_EXTRA_CXXFLAGS=${EXTRA_CXXFLAGS}
ORIGIN_EXTRA_LDFLAGS=${EXTRA_LDFLAGS}
# Ensure the script is run from the repository's root directory
if [ ! -f "./ycsb" ]; then
    echo "Error: The 'ycsb' executable is not found. Compiling ycsb..."
    make EXTRA_CXXFLAGS="${ORIGIN_EXTRA_CXXFLAGS} -I../skew-aware-rocksdb-8.9.1/include" EXTRA_LDFLAGS="${ORIGIN_EXTRA_LDFLAGS} -L../skew-aware-rocksdb-8.9.1"
fi

if [ $? != 0 ]; then
    echo "Error during pre-check and command execution"
    exit 1
fi

# --- Scaling Parameters ---
SCALES=(1 2 3 4 5)
BASE_OP_COUNT=100000000
BASE_REC_COUNT=100000000
BASE_CACHE_SIZE=$((512 * 1024 * 1024)) # 512MB in bytes

# --- Base Experiment Settings ---
workload_types=("b")
runs=3
fieldlength=9
field_len_dist="uniform"
dynamic_cmpct=${DYNAMIC_COMPACT}
bpk=2
threads=${NUM_THREADS}

methods=("mnemosyne-plus" "mnemosyne" "default")
compiled_by_default="false"

# --- Determine Output Path Suffix ---
if [ "$DYNAMIC_COMPACT" = "true" ]; then
    OUTPUT_DIR_SUFFIX="_dynamic_cmpact"
else
    OUTPUT_DIR_SUFFIX="_no_dynamic_cmpact"
fi
# Base directory for all scale experiments for this configuration
EXP_BASE_DIR="exp-th${NUM_THREADS}${OUTPUT_DIR_SUFFIX}"
mkdir -p ${EXP_BASE_DIR}

cp rocksdb/rocksdb.properties rocksdb/rocksdb.origin_properties
sed -i "s|rocksdb\.dbname=.*|rocksdb.dbname=${DB_HOME}|g" rocksdb/rocksdb.properties

# --- Scaling Loop ---
for scale_factor in "${SCALES[@]}"; do
    current_operationcount=$((scale_factor * BASE_OP_COUNT))
    current_recordcount=$((scale_factor * BASE_REC_COUNT))
    current_block_cache_size=$((scale_factor * BASE_CACHE_SIZE))

    OUTPUT_DIR="${EXP_BASE_DIR}/scale${scale_factor}x"
    mkdir -p ${OUTPUT_DIR}

    echo "=============================================================================="
    echo "Starting YCSB scale ${scale_factor}x experiment with the following settings:"
    echo "  - Number of Threads: $NUM_THREADS"
    echo "  - Dynamic Compaction: $DYNAMIC_COMPACT"
    echo "  - DB Home:            $DB_HOME"
    echo "  - Output Folder:      $OUTPUT_DIR"
    echo "  - Bits-per-key:       ${bpk}"
    echo "  - Operations:         ${current_operationcount}"
    echo "  - Records:            ${current_recordcount}"
    echo "  - Block Cache Size:   $((${current_block_cache_size} / 1024 / 1024)) MB"
    echo "  - Runs:               ${runs}"
    echo "------------------------------------------------------------------------------"

    # --- Execute Experiment for this scale ---
    for i in $(seq 1 ${runs}); do
        mkdir -p "${OUTPUT_DIR}/run${i}"
        for method in ${methods[@]}; do
            cp rocksdb/options.ini rocksdb/origin_options.ini
            cp rocksdb/options-exp-${method}.ini rocksdb/options.ini
            # Replace the placeholder cache size with the current scaled value
            sed -i "s|block_cache={capacity=33554432}|block_cache={capacity=${current_block_cache_size}}|g" rocksdb/options.ini
            sed -i "s|level_compaction_dynamic_level_bytes=true|level_compaction_dynamic_level_bytes=${dynamic_cmpct}|g" rocksdb/options.ini
            sed -i "s/bloomfilter:5:false/bloomfilter:${bpk}:false/g" rocksdb/options.ini
            if [ "${method}" != "default" ]; then
                 sed -i "s|max_bits_per_key_granularity=5|max_bits_per_key_granularity=${bpk}|g" rocksdb/options.ini
            fi

            # Recompile if method changes
            if [ ${method} == "default" ]; then
                if [ "${compiled_by_default}" == "false" ]; then
                    echo "Recompiling ycsb using default rocksdb..."
                    make clean && make EXTRA_CXXFLAGS="${ORIGIN_EXTRA_CXXFLAGS} -I../rocksdb-8.9.1/include" EXTRA_LDFLAGS="${ORIGIN_EXTRA_LDFLAGS} -L../rocksdb-8.9.1 -ldl -lz -lsnappy -lzstd -lbz2 -llz4" > /dev/null 2>&1
                    compiled_by_default="true"
                fi
            else
                if [ "${compiled_by_default}" == "true" ]; then
                    echo "Recompiling ycsb using Mnemosyne (skew-aware-rocksdb)..."
                    make clean && make EXTRA_CXXFLAGS="${ORIGIN_EXTRA_CXXFLAGS} -I../skew-aware-rocksdb-8.9.1/include" EXTRA_LDFLAGS="${ORIGIN_EXTRA_LDFLAGS} -L../skew-aware-rocksdb-8.9.1 -ldl -lz -lsnappy -lzstd -lbz2 -llz4" > /dev/null 2>&1
                    compiled_by_default="false"
                fi
            fi

            for workload_type in ${workload_types[@]}; do
                # --- DB Cleanup for each workload run ---
                echo "Cleaning up DB_HOME: ${DB_HOME}"
                rm -rf "$DB_HOME"
                mkdir -p "$DB_HOME"

                cp workloads/workload${workload_type} workloads/workload-temp
                echo "fieldlength=${fieldlength}" >> workloads/workload-temp
                echo "field_len_dist=${field_len_dist}" >> workloads/workload-temp
                # Replace placeholder counts with current scaled values
                sed -i "s/operationcount=100000/operationcount=${current_operationcount}/g" workloads/workload-temp
                sed -i "s/recordcount=100000/recordcount=${current_recordcount}/g" workloads/workload-temp

                OUTPUT_PATH="${OUTPUT_DIR}/run${i}/${method}_workload${workload_type}_ycsb_result.txt"
                echo "[Scale ${scale_factor}x Run ${i}] Executing workload ${workload_type} with ${method}:"

                YCSB_CMD="./ycsb -load -run -db rocksdb -threads ${threads} -P workloads/workload-temp -P rocksdb/rocksdb.properties -s -p status.interval=3"
                echo "${YCSB_CMD} > ${OUTPUT_PATH}"

                if [ ${method} == "default" ]; then
                    LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../rocksdb-8.9.1 ${YCSB_CMD} 2>&1 | tee ${OUTPUT_PATH} | ./show_progress_bar.sh ${current_recordcount} ${current_operationcount}
                else
                    LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../skew-aware-rocksdb-8.9.1 ${YCSB_CMD} 2>&1 | tee ${OUTPUT_PATH} | ./show_progress_bar.sh ${current_recordcount} ${current_operationcount}
                fi

                if [ -d "${DB_HOME}" ] && [ -f "${DB_HOME}/LOG" ]; then
                  grep -A60 "DUMPING STATS" ${DB_HOME}/LOG > ${OUTPUT_DIR}/run${i}/${method}_workload${workload_type}_ycsb_dumped_stats.txt
                  mv ${DB_HOME}/LOG ${OUTPUT_DIR}/run${i}/${method}_workload${workload_type}_ycsb_LOG.txt
                fi
                cat workloads/workload-temp > "${OUTPUT_DIR}/run${i}/workload${workload_type}.txt"
                rm workloads/workload-temp
            done # workload_type
            mv rocksdb/origin_options.ini rocksdb/options.ini
        done # method
    done # runs
    echo "=============================================================================="
    echo ""

done # scale_factor

mv rocksdb/rocksdb.origin_properties rocksdb/rocksdb.properties

# Restore default compilation state if needed
if [ "${compiled_by_default}" == "true" ]; then
    make clean && make EXTRA_CXXFLAGS="${ORIGIN_EXTRA_CXXFLAGS} -I../skew-aware-rocksdb-8.9.1/include" EXTRA_LDFLAGS="${ORIGIN_EXTRA_LDFLAGS} -L../skew-aware-rocksdb-8.9.1 -ldl -lz -lsnappy -lzstd -lbz2 -llz4" > /dev/null 2>&1
    compiled_by_default="false"
fi

echo "----------------------------------------------------"
echo "YCSB scaling experiments finished."
echo "All raw results are in subdirectories under: ${EXP_BASE_DIR}"
echo "----------------------------------------------------"
echo "Aggregating results using merge_ycsb_scale.py..."

# Call the external Python script for aggregation
python3 merge_ycsb_large.py ${runs} ${EXP_BASE_DIR} ${DYNAMIC_COMPACT}

echo "----------------------------------------------------"
echo "Aggregation complete."
echo "Check the output from merge_ycsb_scale.py for result file locations."
echo "----------------------------------------------------"

