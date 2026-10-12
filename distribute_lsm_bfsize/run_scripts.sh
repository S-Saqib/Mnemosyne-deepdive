mkdir -p output             # This directory will be directly used to store output information, 

mkdir -p build
cd build 
cmake ..
make
cd ..
TEST_DIR=../skew-aware-bpk-benchmark/output-stats-for-optimization-exp/

OUTPUT_DIR=./exp-figures    # directory to output figures
mkdir -p exp-figures

REPEAT_NUM=3                # repeated times for each dataset
TEST_BPK=(2 4 8)            # list of bits-per-key to be tested

python3 ./run_experiments.py -d $TEST_DIR -r $REPEAT_NUM --bpk ${TEST_BPK[@]} 2> exp-log.txt
python3 ./plot_N-time_foreach_bpk.py --bpk ${TEST_BPK[@]} -d ${OUTPUT_DIR} 2> plt-log.txt

mkdir -p ../exp-figures
cp ${OUTPUT_DIR}/Z0.0_uniform_bpk2.pdf ../exp-figures/Fig5-a.pdf
cp ${OUTPUT_DIR}/Z0.0_uniform_bpk8.pdf ../exp-figures/Fig5-b.pdf
