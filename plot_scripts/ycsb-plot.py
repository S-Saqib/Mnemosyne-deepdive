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
fontsize = 32
mpl.rcParams.update({'font.size': fontsize})

def main(args):
    df = pd.read_csv(args.path, sep=",")
    methods1 = [
        ['default', 'black' , 'RocksDB', -0.2],
        ['monkey-bottom-up', 'tab:green', 'Monkey (Bottom-Up)', 0.0],
        ['monkey-top-down', 'purple', 'Monkey (Top-Down)', 0.0],
        ['mnemosyne', 'blue', 'Mnemosyne', 0.0],
        ['mnemosyne-plus', 'red', 'Mnemosyne$^+$', 0.2]
    ]

    methods2 = [
        ['default', 'black' , 'RocksDB', -0.1],
        ['mnemosyne', 'red', 'Mnemosyne', 0.1],
    ]

    fig, ax = plt.subplots(figsize=(8, 6))

    #adjusted_workload = pd.concat([df['workload'].iloc[1:], df['workload'].iloc[[0]]], ignore_index=True)
    adjusted_workload = df['workload']

    X_axis = np.arange(adjusted_workload.size) 
    
    width = 0.2
    if args.onlyMnemosyne:
        methods = methods2
    else:
        # the methods in the results (older results have no Monkey), side by side
        methods = [m for m in methods1 if m[0] in df.columns]
        width = 0.8/len(methods)
        for i, method in enumerate(methods):
            method[3] = (i - (len(methods) - 1)/2.0)*width
    for method in methods:
        df[method[0]] = df[method[0]]/10000
        #adjusted_data = pd.concat([df[method[0]].iloc[1:], df[method[0]].iloc[[0]]], ignore_index=True)
        adjusted_data = df[method[0]]
        plt.bar(X_axis + method[3], adjusted_data, color=method[1], label=method[2], width=width)


    ax.set_xticks(X_axis)
    ax.set_xticklabels([x[0].capitalize() + x[1:] if len(x) > 0 else x.capitalize()  for x in adjusted_workload])
    ax.set_xlabel('Workload', fontsize=fontsize-2)
    
    
    ax.set_ylabel('Tput (10$K$ops/s)', fontsize=fontsize-2)
    if args.autoY:
        # room above the bars for the legend
        ax.set_ylim(bottom=0.0, top=max(df[m[0]].max() for m in methods)*(1.6 if args.showLegend and len(methods) <= 3 else 1.1))
    elif args.ymax != -1000:
        ax.set_ylim(bottom=0.0, top=args.ymax)
        ax.set_yticks([i*5 for i in range(int(math.ceil(args.ymax/5) + 1))])
    else:
        ax.set_ylim(bottom=0.0, top=21)
        ax.set_yticks([0,5,10,15,20])
   
        
    if args.showLegend:
        if len(methods) <= 3:
            plt.legend(loc=args.legendLoc, ncol=1, columnspacing=0.4, labelspacing=0.1,borderaxespad=0.1,handletextpad=0.2,handlelength=2,fontsize=fontsize-2)
        else:
            # too many entries to fit inside the axes: put them above
            plt.legend(loc='lower center', bbox_to_anchor=(0.5, 1.02), ncol=2, columnspacing=0.6, labelspacing=0.1,borderaxespad=0.1,handletextpad=0.2,handlelength=1.5,fontsize=fontsize-10, frameon=False)
    plt.tight_layout()
    ax.figure.savefig(args.name, bbox_inches = "tight", dpi=900)


if __name__ == "__main__":
    parser = argparse.ArgumentParser('ycsb-plot')
    parser.add_argument('--path',help='suffix to be plot',type=str, default='../YCSB-cpp/exp-th1_dynamic_cmpact/ycsb_agg_exp.txt')
    parser.add_argument('--name',help='name of the plot', default='../exp-figures/ycsb-th1.pdf')
    parser.add_argument('--onlyMnemosyne',help='if only plot Mnemosyne', action='store_true')
    parser.add_argument('--showLegend', action='store_true', help='show the legend')
    parser.add_argument('--legendLoc',help='the location of the legend', default='upper left')
    parser.add_argument('--ymax',help='the maximum y value', type=float, default=-1000)
    parser.add_argument('--autoY', action='store_true', help='fit the y-axis to the data instead of 0-21 or --ymax')
    
    args = parser.parse_args()
    main(args)
