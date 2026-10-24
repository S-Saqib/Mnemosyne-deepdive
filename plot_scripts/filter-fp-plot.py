import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.font_manager as font_manager
import pandas as pd
import argparse

prop = font_manager.FontProperties(fname="../fonts/LinLibertine_Mah.ttf")
mpl.rcParams['font.family'] = prop.get_name()
mpl.rcParams['text.usetex'] = True
fontsize = 28
mpl.rcParams.update({'font.size': fontsize})

# method in query_lat_exp's "filter stats (<method>)" lines, color, line style, marker, label
methods = [
    ['rocksdb', 'black', 'solid', 'D', 'RocksDB'],
    ['monkey-bottom-up', 'tab:green', 'dashed', '^', 'Monkey (Bottom-Up)'],
    ['monkey-top-down', 'purple', 'dotted', 'v', 'Monkey (Top-Down)'],
    ['mnemosyne', 'blue', 'solid', 'p', 'Mnemosyne'],
    ['mnemosyne-plus', 'red', 'solid', 'x', 'Mnemosyne$^+$'],
    ['elasticbf', 'tab:orange', 'solid', 'o', 'ElasticBF'],
    ['shamba', 'tab:brown', 'dashdot', 's', 'SHaMBa']
]
# column of merge_filter_fp.py's CSV, y-axis label
panels = [
    ['filter_misses', r'\#filter misses'],
    ['false_positives', r'\#false positives']
]


def main(args):
    df = pd.read_csv(args.path)
    fig, axes = plt.subplots(1, len(panels), figsize=(8 * len(panels), 6))
    for ax, (column, ylabel) in zip(axes, panels):
        for name, color, linestyle, marker, label in methods:
            rows = df[df['method'] == name].sort_values('zero_pct')
            if rows.empty:
                continue
            ax.plot(rows['zero_pct'], rows[column], color=color, linestyle=linestyle,
                    marker=marker, markersize=12, fillstyle='none', label=label)
        ax.set_xticks(sorted(df['zero_pct'].unique()))
        ax.set_xlabel(r'Zero-result lookups (\%)')
        ax.set_ylabel(ylabel)
        if args.logY:
            ax.set_yscale('symlog', linthresh=1)
        else:
            ax.set_ylim(bottom=0)
    fig.tight_layout()
    handles, labels = axes[0].get_legend_handles_labels()
    # the title sits on top of the legend
    fig.legend(handles, labels, loc='lower center', ncol=4, fontsize=fontsize - 8,
               frameon=False, bbox_to_anchor=(0.5, 1.0), title=args.title or None,
               title_fontsize=fontsize)
    fig.savefig(args.name, bbox_inches='tight', pad_inches=0.02)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--path', required=True, help='CSV written by merge_filter_fp.py')
    parser.add_argument('--name', required=True, help='output figure')
    parser.add_argument('--title', default='', help='figure title')
    parser.add_argument('--logY', action='store_true', help='symmetric log scale for the y-axes')
    main(parser.parse_args())
