#!/bin/bash

cd ../
wget https://github.com/facebook/rocksdb/archive/refs/tags/v8.9.1.zip
unzip v8.9.1.zip
cd rocksdb-8.9.1/
make -j 16
cd -
