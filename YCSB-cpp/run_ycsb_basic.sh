#!/bin/bash

# ==============================================================================
# run_ycsb_basic.sh
#
# Description:
#   Runs the YCSB benchmark experiment with configurable parameters. It saves
#   the output to a directory named with a suffix that indicates whether
#   dynamic compaction was enabled.
#
# Usage:
#   ./run_ycsb_basic.sh [num_threads] [dynamic_compact]
#
#   [num_threads]: (Optional) The number of threads for the benchmark.
#                  Defaults to 1.
#   [dynamic_compact]: (Optional) 'true' or 'false' to enable/disable
#                      dynamic compaction. Defaults to 'true'.
#
# ==============================================================================
# --- Configuration ---

# Default values
DEFAULT_THREADS=1
DEFAULT_DYNAMIC_COMPACT="true"

# Override defaults with user-provided arguments
NUM_THREADS=${1:-$DEFAULT_THREADS}
DYNAMIC_COMPACT=${2:-$DEFAULT_DYNAMIC_COMPACT}

# --- Pre-run Checks ---

# Base path for the database output
# Remember to specify your database path here to use a dedicated storage device
DB_HOME="${FAST_DB_HOME:-./ycsb_working_home}"

if [ ! -d "${DB_HOME}" ]; then
    echo "Cannot find database path ${DB_HOME}"
    mkdir -p "${DB_HOME}"
    echo "Created the database path using 'mkdir -d ${DB_HOME}' (Remember to specify the DB_HOME path using a dedicated storage device)"
fi
# Ensure the output directory is clean before running
rm -rf "$DB_HOME"
mkdir -p "$(dirname "$DB_HOME")" # Ensure the parent /tmp directory exists



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



# --- Determine Output Path Suffix ---

# Set the suffix based on the DYNAMIC_COMPACT flag
if [ "$DYNAMIC_COMPACT" = "true" ]; then
    OUTPUT_DIR_SUFFIX="_dynamic_cmpact"
else
    OUTPUT_DIR_SUFFIX="_no_dynamic_cmpact"
fi

OUTPUT_DIR="exp-th${NUM_THREADS}${OUTPUT_DIR_SUFFIX}"
mkdir -p ${OUTPUT_DIR}



# --- Execute Experiment ---

workload_types=("a" "b" "c" "d" "f")
runs=3
fieldlength=9
field_len_dist="uniform"
operationcount=60000000
recordcount=30000000
dynamic_cmpct=${DYNAMIC_COMPACT}
block_cache_size=209715200 # 200MB
bpk=2
threads=${NUM_THREADS}

echo "Starting YCSB experiment with the following settings:"
echo "  - Number of Threads: $NUM_THREADS"
echo "  - Dynamic Compaction: $DYNAMIC_COMPACT"
echo "  - DB Home:     $DB_HOME" # The path where the database is stored
echo "  - Output Folder: $OUTPUT_DIR"
echo "  - Bits-per-key: ${bpk}"
echo "  - Operations: ${operationcount}"
echo "  - Records: ${recordcount}"
echo "  - Runs: ${runs}"
# --- Pre-run Checks ---
echo "----------------------------------------------------"

methods=("mnemosyne-plus" "mnemosyne" "default")
compiled_by_default="false"
cp rocksdb/rocksdb.properties rocksdb/rocksdb.origin_properties
sed -i "s|rocksdb\.dbname=.*|rocksdb.dbname=${DB_HOME}|g" rocksdb/rocksdb.properties
mkdir -p "exp"
for i in `seq 1 ${runs}`
do
  mkdir -p "${OUTPUT_DIR}/run${i}"
  for method in ${methods[@]}
  do
	cp rocksdb/options.ini rocksdb/origin_options.ini
	cp rocksdb/options-exp-${method}.ini rocksdb/options.ini
	sed -i "s|block_cache={capacity=33554432}|block_cache={capacity=${block_cache_size}}|g" rocksdb/options.ini
	sed -i "s|level_compaction_dynamic_level_bytes=true|level_compaction_dynamic_level_bytes=${dynamic_cmpct}|g" rocksdb/options.ini
  	for workload_type in ${workload_types[@]}
  	do
	  cp workloads/workload${workload_type} workloads/workload-temp
	  echo "fieldlength=${fieldlength}" >> workloads/workload-temp
	  echo "field_len_dist=${field_len_dist}" >> workloads/workload-temp
	  sed -i "s/operationcount=100000/operationcount=${operationcount}/g" workloads/workload-temp
	  sed -i "s/recordcount=100000/recordcount=${recordcount}/g" workloads/workload-temp
	  sed -i "s/bloomfilter:5:false/bloomfilter:${bpk}:false/g" rocksdb/options.ini

	  OUTPUT_PATH="${OUTPUT_DIR}/run${i}/${method}_workload${workload_type}_ycsb_result.txt"
	  echo "[Run ${i}] Executing workload ${workload_type} with ${method}:"
	  if [ ${method} == "default" ]; then
	        if [ "${compiled_by_default}" == "false" ]; then
		   echo "Recompiling ycsb using default rocksdb..."
                   make clean && make EXTRA_CXXFLAGS="${ORIGIN_EXTRA_CXXFLAGS} -I../rocksdb-8.9.1/include" EXTRA_LDFLAGS="${ORIGIN_EXTRA_LDFLAGS} -L../rocksdb-8.9.1 -ldl -lz -lsnappy -lzstd -lbz2 -llz4" > /dev/null 2>&1
		   compiled_by_default="true"
		fi
	  	#echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../rocksdb-8.9.1 ./ycsb -load -run -db rocksdb -threads ${threads} -P workloads/workload-temp -P rocksdb/rocksdb.properties -s -p status.interval=3 > ${OUTPUT_PATH}"
	  	echo "./ycsb -load -run -db rocksdb -threads ${threads} -P workloads/workload-temp -P rocksdb/rocksdb.properties -s -p status.interval=3 > ${OUTPUT_PATH}"
          	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../rocksdb-8.9.1 ./ycsb -load -run -db rocksdb -threads ${threads} -P workloads/workload-temp -P rocksdb/rocksdb.properties -s -p status.interval=3 | tee ${OUTPUT_PATH} | ./show_progress_bar.sh ${recordcount} ${operationcount}
	  else
	        if [ "${compiled_by_default}" == "true" ]; then
		   echo "Recompiling ycsb using Mnemosyne (skew-aware-rocksdb)..."
                   make clean && make EXTRA_CXXFLAGS="${ORIGIN_EXTRA_CXXFLAGS} -I../skew-aware-rocksdb-8.9.1/include" EXTRA_LDFLAGS="${ORIGIN_EXTRA_LDFLAGS} -L../skew-aware-rocksdb-8.9.1 -ldl -lz -lsnappy -lzstd -lbz2 -llz4" > /dev/null 2>&1
		   compiled_by_default="false"
	        fi
	        sed -i "s|max_bits_per_key_granularity=5|max_bits_per_key_granularity=${bpk}|g" rocksdb/options.ini
          	#echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../skew-aware-rocksdb-8.9.1 ./ycsb -load -run -db rocksdb -threads ${threads} -P workloads/workload-temp -P rocksdb/rocksdb.properties -s -p status.interval=3 > ${OUTPUT_PATH}"
          	echo "./ycsb -load -run -db rocksdb -threads ${threads} -P workloads/workload-temp -P rocksdb/rocksdb.properties -s -p status.interval=3 > ${OUTPUT_PATH}"
          	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../skew-aware-rocksdb-8.9.1 ./ycsb -load -run -db rocksdb -threads ${threads} -P workloads/workload-temp -P rocksdb/rocksdb.properties -s -p status.interval=3 | tee ${OUTPUT_PATH} | ./show_progress_bar.sh ${recordcount} ${operationcount}
	  fi
	  #echo "Finished running workload ${workload_type} for method ${method} in run ${i}"
	  grep -A60 "DUMPING STATS" ${DB_HOME}/LOG > ${OUTPUT_DIR}/run${i}/${method}_workload${workload_type}_ycsb_dumped_stats.txt
	  mv ${DB_HOME}/LOG ${OUTPUT_DIR}/run${i}/${method}_workload${workload_type}_ycsb_LOG.txt
	  cat workloads/workload-temp > "${OUTPUT_DIR}/run${i}/workload${workload_type}.txt"
	  rm workloads/workload-temp
	  # Cleanup sst files under ${DB_HOME}
	  find ${DB_HOME} -maxdepth 1 -name "*.sst" -type f -delete
  	done
	mv rocksdb/origin_options.ini rocksdb/options.ini
  done
done
mv rocksdb/rocksdb.origin_properties rocksdb/rocksdb.properties

if [ "${compiled_by_default}" == "true" ]; then
    make clean && make EXTRA_CXXFLAGS="${ORIGIN_EXTRA_CXXFLAGS} -I../skew-aware-rocksdb-8.9.1/include" EXTRA_LDFLAGS="${ORIGIN_EXTRA_LDFLAGS} -L../skew-aware-rocksdb-8.9.1 -ldl -lz -lsnappy -lzstd -lbz2 -llz4" > /dev/null 2>&1
    compiled_by_default="false"
fi


echo "----------------------------------------------------"
echo "YCSB experiment finished."

echo "----------------------------------------------------"
echo "Collecting experimental results:"
python3 merge_ycsb.py ${runs} $OUTPUT_DIR
echo "Averaged throughputs (ops/s) results are located in: $OUTPUT_DIR/ycdb_agg_exp.txt"
column -s, -t -o ' | ' < "$OUTPUT_DIR/ycsb_agg_exp.txt"

