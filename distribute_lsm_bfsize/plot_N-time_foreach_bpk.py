import pandas as pd
import os
import numpy as np
import matplotlib as mpl
from matplotlib import pyplot as plt
import matplotlib.font_manager as font_manager
import argparse

parser = argparse.ArgumentParser(description="Arguments for test files")
parser.add_argument(
    "-b", "--bpk", 
    nargs="+",             # one or more arguments
    type=int,              # convert each item to int
    help="A list of tested bits-per-key"
)
parser.add_argument("-d", type=str, help="Directory Outputting results")
args = parser.parse_args()
output_dir = args.d

################################################################################# 

prop = font_manager.FontProperties(fname="./fonts/LinLibertine_Mah.ttf")
mpl.rcParams['font.family'] = prop.get_name()
mpl.rcParams['text.usetex'] = True
mpl.rcParams.update({'font.size': 23})
#plt.rc('font',family='Linux Libertine Mono', size=15)

# BPK = [1, 2, 3, 4, 5, 6, 7, 8]
# Zlist = ["Z0.0_ZD0_query_stats.txt", "Z0.25_ZD0_query_stats.txt", "Z0.5_ZD0_query_stats.txt", "Z0.75_ZD0_query_stats.txt", "Z1.0_ZD0_query_stats.txt"]
Ns = [1, 2, 4, 8, 16, 32]

df = pd.read_csv("output/stats.csv")
# print(df)

algos = ["monkey", "binarysearch", "scan"]
algoname = ["gradient descent", "binary search", "scan"]
lines = ["--", "-.", "-"]
linecolors = ["black", "blue", "red"]
linemarkers = ["o", "s", "x"]

def plot_for_linear_plot(bpk: int, zd = 0):
  allZ = [0.0, 0.5, 1.0]
  length = len(allZ)
  zd_to_distribution = {
    0: 'uniform',
    1: 'normal',
    3: 'zipf'
  }
  ii = 0
  entries_per_file = 262144
  for z,ddf in df[df["bpk"] == bpk][df["ZD"] == zd].sort_values(by="Z").groupby(df["Z"]):
    plt.figure(f'Z{z:.1f}_{zd_to_distribution[zd]}_bpk{bpk}', figsize = (8, 6))
    ii += 1
    _df = df[df["bpk"] == bpk][df["ZD"] == zd][df["Z"] == z]
    for i in range(3):
      algo = algos[i]
      name = algoname[i]
      line = lines[i]
      x = []
      y = []
      for n, dfN in _df.groupby(_df["N"]):
        if(len(_df[_df["N"] == n]) > 0):
          _x = int(round((n * 1000000)/entries_per_file)) 
          _y = np.average(_df[_df["N"] == n][f't_{algo}'])
          _h = np.average(_df[_df["N"] == n][f'n_file'])
          x.append(_h)
          y.append(_y)
      plt.plot(x, y, label = f"{name}", color=linecolors[i], marker=linemarkers[i], markersize=16, fillstyle='none')
      plt.ticklabel_format(axis="x", style="sci", scilimits=(0,6))
      plt.yscale("log")
      plt.ylim(1e-6, 100)
      plt.yticks([1e-6,1e-4,0.01,1,100])
      plt.xlabel("\#SST files ($F$)")
      plt.ylabel("time (seconds)")
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{output_dir}/Z{z:.1f}_{zd_to_distribution[zd]}_bpk{bpk}.pdf", bbox_inches = "tight", dpi=900)

def main():
  BPK = args.bpk
  for bpk in BPK:
    for zd,_temp_df_ in df.groupby(df["ZD"]):
      plot_for_linear_plot(bpk = bpk, zd = zd)

if __name__ == "__main__":
  main()
