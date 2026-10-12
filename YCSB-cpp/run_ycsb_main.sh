#!/bin/bash
echo "[Fig 15a] ⏳ Experimenting different YCSB workloads with 1 thread..."
./run_ycsb_basic.sh 1
echo "[Fig 15a] ✅ Experiments complete"
echo "[Fig 15b] ⏳ Experimenting different YCSB workloads with 4 threads..."
./run_ycsb_basic.sh 4
echo "[Fig 15b] ✅ Experiments complete"
echo "[Fig 16a] ⏳ Experimenting YCSB workload B with bottom-up construction (level_compaction_dynamic_level_bytes=true)..."
./run_ycsb_scale.sh 16 true
echo "[Fig 16a] ✅ Experiments complete"
echo "[Fig 16b] ⏳ Experimenting YCSB workload B with top-down construction (level_compaction_dynamic_level_bytes=false)..."
./run_ycsb_scale.sh 16 false
echo "[Fig 16b] ✅ Experiments complete"
