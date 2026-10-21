from os.path import expanduser
import matplotlib as mpl
import matplotlib.pyplot as plt
import matplotlib.font_manager as font_manager
import pandas as pd
import numpy as np
import os, sys, argparse, copy, time
import math

prop = font_manager.FontProperties(fname="../fonts/LinLibertine_Mah.ttf")
mpl.rcParams['font.family'] = prop.get_name()
mpl.rcParams['text.usetex'] = True
fontsize = 48
mpl.rcParams.update({'font.size': fontsize})

# unit of the x-axis (number of operations) -> divisor
ops_units = {'M': 1000000, 'K': 1000}

def main(args):
    
    df = pd.read_csv(args.tput_path, sep=",")
    methods = [
        ['uniform', 'black' , 'solid', 'D', 'RocksDB'],
        ['monkey-bottom-up', 'tab:blue', 'dashed', 'p', 'Monkey (Bottom-Up)'],
        ['monkey-top-down', 'purple', 'dotted', 'p', 'Monkey (Top-Down)'],
        ['mnemosyne', 'red', 'dashdot', 'x', 'Mnemosyne'],
        ['elasticbf', 'tab:orange', 'solid', 'o', 'ElasticBF'],
        ['shamba', 'tab:brown', 'dashdot', 's', 'SHaMBa']
    ]

    fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(args.width,12))

    max_tput = 0
    for method in methods:
        key = "tput-" + method[0]
        if key in df:
            tmp = df[key]*100000
            max_tput = max(max_tput, tmp.max())
            ax1.plot(tmp[1:], color=method[1], linestyle=method[2], fillstyle='none', label=method[4])
    
    #ax.set_xticks(x, xlabels)
    #ax.set_xlabel('Operations ($M$)', fontsize=fontsize-6)
    
    
    ax1.set_ylabel('Tput (10Kops/s)', fontsize=fontsize-10)

    if args.tput_ymax > 0:
        ymax = args.tput_ymax
    else:
        ymax = max_tput
    #print(ymax)
    if args.autoY:
        ax1.set_ylim(bottom=0.0)
    else:
        ax1.set_ylim(bottom=0.0, top=ymax)
        ax1.set_yticks([i*args.tput_yinterval for i in range(math.ceil(ymax/args.tput_yinterval+0.5))])
    ax1.tick_params(axis='y', labelsize=fontsize-12)
    if args.showLegend:
        ax1.legend(loc=args.legendLoc, ncol=2, columnspacing=0.2, labelspacing=0.1,handlelength=1.2,fontsize=fontsize-12,handletextpad=0.2)

    df2 = pd.read_csv(args.bpk_path, sep=",")

    max_bpk = 0.0
    for method in methods:
        key = "bpk-" + method[0]
        if key in df2:
            tmp = df2[key][1:]
            max_bpk = max(max_bpk, tmp.max())
            # plotting from the second line since the first line is 0 for 0 operation
            ax2.plot(tmp, color=method[1], linestyle=method[2], fillstyle='none', label=method[4])
    
    ops_divisor = ops_units[args.opsUnit]
    x = []
    xlabels = []
    for i,y in enumerate(df['ops']):
        if i%50 == 0:
            x.append(i)
            xlabels.append(str(int(y//ops_divisor)))

    ax2.set_xticks(x, xlabels)
    ax2.set_xlabel('Operations ($' + args.opsUnit + '$)', fontsize=fontsize-10)
    
    ax2.tick_params(axis='y', labelsize=fontsize-12)
    ax2.tick_params(axis='x', labelsize=fontsize-12)
    ax2.set_ylabel('Actual Avg Bpk', fontsize=fontsize-10)
    ymax = max_bpk
    if args.bpk_ymax > 0:
        ymax = args.bpk_ymax

    if args.autoY:
        ax2.set_ylim(bottom=0.0)
    else:
        ax2.set_ylim(bottom=0.0, top=ymax)
        ax2.set_yticks([i*args.bpk_yinterval for i in range(math.ceil(max_bpk/args.bpk_yinterval))])
    #ax.set_yticklabels(['%.1f' % (i*2) for i in range(math.ceil(max_bpk/2))])
    #ax.margins(y=0.5)
        
    plt.tight_layout()
    plt.subplots_adjust(hspace=0.1)
    plt.savefig(args.name, bbox_inches = "tight", dpi=900)
    #plt.show()
    


if __name__ == "__main__":
    parser = argparse.ArgumentParser('monkey-vs-mnemosyne-tput-benchmark-plot')
    parser.add_argument('--tput_path',help='suffix to be plot',type=str, default='../skew-aware-bpk-benchmark/exp-throughputs-and-bpk-for-mixed-empty-workloads/workload_type_I-bpk-2.0_throughputs.txt')
    parser.add_argument('--bpk_path',help='suffix to be plot',type=str, default='../skew-aware-bpk-benchmark/exp-throughputs-and-bpk-for-mixed-empty-workloads/workload_type_I-bpk-2.0_tracked_avg_bpk.txt')
    parser.add_argument('--name',help='name of the plot', default='../exp-figures/static-monkey-vs-rocksdb-tput-and-bpk-workload_type_I-bpk-2.0.pdf')
    parser.add_argument('--legendLoc',help='the location of the legend', default='lower center')
    parser.add_argument('--showLegend', action='store_true', help='show the legend')
    parser.add_argument('--tput_ymax',help='the maximum throughput', type=float, default=-1000)
    parser.add_argument('--tput_yinterval',help='the interval of ticks for throughput', type=float, default=3)
    parser.add_argument('--bpk_ymax',help='the maximum throughput', type=float, default=-1000)
    parser.add_argument('--bpk_yinterval',help='the interval of ticks for bpk', type=float, default=2)
    parser.add_argument('--width',help='the width of the figure', type=float, default=16)
    parser.add_argument('--opsUnit',help='the unit of the number of operations on the x-axis', choices=list(ops_units.keys()), default='M')
    parser.add_argument('--autoY', action='store_true', help='let matplotlib choose the y-axis ranges and ticks instead of --tput_yinterval and --bpk_yinterval')

    args = parser.parse_args()
    main(args)
