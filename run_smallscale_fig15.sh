#!/bin/bash
# YCSB_BPK=6 ./run_smallscale_fig15.sh runs it with 6 bits-per-key
cd ./exp-scripts/
QUICK=1 FAST_DB_HOME=/home/cc/db_working_home/ycsb_small ./fig15-16.sh
