This repo is for testing three methods of distributing bloom filter size.

```run_scripts.sh``` is a generic script that will：
  1. compile the testing cpp,
  2. run experiments for given dataset, 
  3. generate plots.

The dataset should be given by following forms:

```
dataset_directory
|   scale1x
|   |   test1
|   |   |   Z0.0_ZD0_query_stats.txt
|   |   |   Z0.0_ZD1_query_stats.txt 
|   |   test2
|   |   test3
|   scale2x
|   scale4x
|   ...
|   scale32x
```

For each testcase (query_stats), it should follow following format:

```
#entries(N) bpk(we will not use this) #files
fileID#1 #existing_queries #non-existing_queries
fileID#2 #existing_queries #non-existing_queries
...
fileID#N #existing_queries #non-existing_queries
```

--- 

```run_experiments.py``` will scan every file with ```query_stats``` under directory, recording ```scale#x```, ```#test```, ```Z#_ZD#```, and output result to ```output/stats.csv```, where ```Z``` refer to portion of zero queries and ```ZD``` implies the distribution of the queries (0 for uniform, 1 for normal and 3 for zipfian).
- use ```-b, --bpk``` to specify a list of bpk to test,
- use ```-r``` to specify #rounds for each testcase,
- use ```-d``` to specify dataset directory.

```plot_N-time_foreach_bpk.py``` will automatically gather results from same ```Z```, ```ZD``` and ```bpk```, and plot ```#files - execution_time``` graph for every file.
- use ```-b, --bpk``` to specify a list of bpk,
- use ```-d``` to specify output directories.


```./build/main``` will be generated to run single testcase for 3 bpk distribution algorithms. Algorithms are implemented in cpp, see ```/src``` and ```/exec``` for details
- use ```-b``` to specify bpk,
- use ```-r``` to specify #rounds for the testcase,
- use ```-f``` to specify testcase name.