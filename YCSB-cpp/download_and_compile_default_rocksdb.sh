#!/bin/bash

cd ../
if [ ! -d rocksdb-8.9.1 ]; then
  wget https://github.com/facebook/rocksdb/archive/refs/tags/v8.9.1.zip
  unzip -q v8.9.1.zip
fi
cd rocksdb-8.9.1/
# run_ycsb_basic.sh runs ycsb against librocksdb.so.8.9 (LD_LIBRARY_PATH)
make -j$(nproc) shared_lib
cd -
