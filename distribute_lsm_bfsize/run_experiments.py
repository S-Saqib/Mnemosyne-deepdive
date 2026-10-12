import os
import re
import pandas as pd
import platform
import argparse
import sys

__plat__ = platform.system().lower()
if __plat__ == 'windows':
  True
elif __plat__ == 'linux':
  True
else:
  print("Unsupported System")
  exit()


parser = argparse.ArgumentParser(description="Arguments for test files")
parser.add_argument("-d", type=str, help="Directory of the File")
parser.add_argument("-r", type=int, default=3, help="Repeated Time for each test")
parser.add_argument(
    "-b", "--bpk", 
    nargs="+",             # one or more arguments
    type=int,              # convert each item to int
    help="A list of tested bits-per-key"
)
args = parser.parse_args()

root_dir = args.d
BPK = args.bpk
print(f"Test directory: {root_dir}")

dir_lists_1 = os.listdir(root_dir)
# or hardcode the workloads
# print(dir_lists_1)


print(f"tested bits-per-key: {BPK}")
# BPK = [1, 2, 3, 4, 5, 6, 7, 8]

stats = {
  "test": [],
  "fname": [],
  "workload": [],
  "bpk": [],
  "Z": [], 
  "ZD": [],
  "N": [],
  "fpr_monkey": [],
  "fpr_binarysearch": [],
  "fpr_scan": [],
  "t_monkey": [], 
  "t_binarysearch": [],
  "t_scan": [],
  "zeroes": [],
  "n_file": []
}
attributes = [
  "fpr_monkey",
  "fpr_binarysearch",
  "fpr_scan",
  "t_monkey", 
  "t_binarysearch",
  "t_scan",
  "zeroes",
  "n_file"
]

bar = lambda percent: "#" * int(50 * percent) + "." * (50 - int(50 * percent))
cnt_dir_list_1 = 0

tot_file = 0

for file in dir_lists_1:                            ## scale?x
  pattern = re.compile(r'\d+x')
  n = int(pattern.findall(file)[0][:-1])
  workload_name = os.path.join(root_dir, file)
  dir_lists_2 = os.listdir(workload_name)
  for tests in dir_lists_2:                         ## test1 - test3
    test_name = os.path.join(workload_name, tests)
    dir_lists_3 = os.listdir(test_name)
    for nfile in dir_lists_3:                       ## Z?_ZD?_query_stats.txt
      pattern3 = re.compile(r'query_stats')
      if(len(pattern3.findall(nfile)) > 0):
        # print(nfile)
        tot_file = tot_file + 1

print(f">> found {tot_file} files")

current_file = 0

for file in dir_lists_1:                            ## scale?x
  pattern = re.compile(r'\d+x')
  n = int(pattern.findall(file)[0][:-1])
  workload_name = os.path.join(root_dir, file)
  dir_lists_2 = os.listdir(workload_name)
  for tests in dir_lists_2:                         ## test1 - test3
    test_name = os.path.join(workload_name, tests)
    dir_lists_3 = os.listdir(test_name)
    for nfile in dir_lists_3:                       ## Z?_ZD?_query_stats.txt
      pattern1 = re.compile(r'Z\d+.\d+')
      pattern2 = re.compile(r'ZD\d+')
      z = float(pattern1.findall(nfile)[0][1:])
      zd = int(pattern2.findall(nfile)[0][2:])
      pattern3 = re.compile(r'query_stats')
      file_name = os.path.join(test_name, nfile)
      if(len(pattern3.findall(nfile)) > 0):
        current_file = current_file + 1
        sys.stdout.write(f"\r|{bar(current_file / tot_file)}|({current_file / tot_file * 100:3.2f}%)| Processing {file}/{tests}/{nfile}") 
      else:
        continue   
      for bpk in BPK:
        print(f'path = {file_name}, n = {n}*1e6 testing: {nfile} with bpk {bpk}.', file=sys.stderr)
        if __plat__ == 'windows':
          os.system(f".\\build\\Release\\main.exe -f {file_name} -b {bpk} -r {args.r} > result.out 2> temp.out")
        elif __plat__ == 'linux':
          os.system(f"./build/main -f {file_name} -b {bpk} -r {args.r} >result.out 2> temp.out")
        df = pd.read_csv("temp.out", sep=':', header=None)
        for i,key in enumerate(attributes):
          stats[key].append(df[1][i])
        stats['N'].append(n)
        stats['Z'].append(z)
        stats['ZD'].append(zd)
        stats["test"].append(tests)
        stats["fname"].append(nfile)
        stats["workload"].append(file)
        stats["bpk"].append(bpk)
        if abs(stats["fpr_scan"][-1] - stats["fpr_binarysearch"][-1]) > 1e-3: 
          with open("log.txt", "a+") as f:
            f.write(f"[ERROR]! gd:{stats['fpr_monkey'][-1]} binary:{stats['fpr_binarysearch'][-1]} scan:{stats['fpr_scan'][-1]}\n")
            f.write(fname + "\n")
        df = pd.DataFrame(data=stats)
        df.to_csv("output/stats.csv")
  sys.stdout.write(f"\r Finished testing directory: {root_dir}")

df = pd.DataFrame(data=stats)
# print(df)
df.to_csv("output/stats.csv")