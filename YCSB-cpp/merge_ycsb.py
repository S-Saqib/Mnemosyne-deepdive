import sys

runs = int(sys.argv[1])
exp_dir = sys.argv[2]
workloads = ["load", "a", "b", "c", "d", "f"]
methods = ["mnemosyne-plus", "mnemosyne", "default"]


def aggregate(filename, result, workload_index):
    infile = open(filename, "r")
    data = infile.readlines()
    infile.close()
    for i in range(len(data)):
        line = data[i].strip()
        if line.startswith("Load throughput"):
            result[0] += float(line.split(':')[-1].strip())
        elif line.startswith("Run throughput"):
            result[workload_index] += float(line.split(':')[-1].strip())
            break

def output(filename, result):
    outfile = open(filename, "w")
    header = "workload"
    for m in methods:
        header += "," + m
    header += "\n"
    outfile.write(header)
    for i in range(len(workloads)):
        line = workloads[i]
        for m in methods:
            line += "," + str(round(result[m][i],2))
        line += "\n"
        outfile.write(line)
    outfile.close()


total_results = dict()
for i in range(len(methods)): 
    total_results[methods[i]] = [0 for _ in workloads]
    for j in range(1, len(workloads)):
        for k in range(1, runs+1):
            aggregate(exp_dir + "/run" + str(k) + "/" + methods[i] + "_workload" + workloads[j] + "_ycsb_result.txt", total_results[methods[i]], j)
    total_results[methods[i]][0] /= runs*len(workloads)*1.0
    for j in range(1, len(workloads)):
        total_results[methods[i]][j] /= runs*1.0

output(exp_dir + "/ycsb_agg_exp.txt", total_results)



