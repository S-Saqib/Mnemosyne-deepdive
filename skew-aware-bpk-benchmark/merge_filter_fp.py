#!/usr/bin/env python3
# Average the "filter stats (<method>): ..." lines of exp_filter_fp.sh over the
# runs and write one CSV per bits-per-key:
#   python3 merge_filter_fp.py <runs> <agg_dir> <output_prefix> [tag]
# reads <output_prefix><run>/bpk<bpk>_Z<Z>-output.txt and writes
# <agg_dir>/filter_fp_bpk<bpk>.csv, or <agg_dir>/filter_fp_<tag>_bpk<bpk>.csv
# with a tag (one row per proportion of zero-result lookups and method).
import glob
import os
import re
import sys
from collections import defaultdict

FILE_RE = re.compile(r'bpk([0-9.]+)_Z([0-9.]+)-output\.txt$')
LINE_RE = re.compile(r'^filter stats \(([^)]+)\): (.*)$')
FIELDS = ['filter_misses', 'filter_cache_hits', 'filter_positives',
          'true_positives', 'false_positives', 'filter_negatives']


def main(runs, agg_dir, output_prefix, tag):
    # (bpk, Z, method) -> field -> list of values, one per run
    values = defaultdict(lambda: defaultdict(list))
    for run in range(1, runs + 1):
        for path in glob.glob('%s%d/bpk*_Z*-output.txt' % (output_prefix, run)):
            m = FILE_RE.search(os.path.basename(path))
            bpk, z = m.group(1), float(m.group(2))
            with open(path) as f:
                for line in f:
                    lm = LINE_RE.match(line.strip())
                    if not lm:
                        continue
                    stats = dict(kv.split('=') for kv in lm.group(2).split())
                    for field in FIELDS:
                        values[(bpk, z, lm.group(1))][field].append(int(stats[field]))

    by_bpk = defaultdict(list)
    for (bpk, z, method), fields in values.items():
        by_bpk[bpk].append((z, method, fields))
    for bpk, rows in by_bpk.items():
        out = os.path.join(agg_dir, 'filter_fp_%sbpk%s.csv' % (tag + '_' if tag else '', bpk))
        with open(out, 'w') as f:
            f.write('zero_pct,method,runs,' + ','.join(FIELDS) + '\n')
            for z, method, fields in sorted(rows):
                n = len(fields[FIELDS[0]])
                avgs = [sum(fields[k]) / n for k in FIELDS]
                f.write('%d,%s,%d,%s\n' % (round(z * 100), method, n,
                                           ','.join('%.1f' % a for a in avgs)))
        print('wrote %s' % out)


if __name__ == '__main__':
    main(int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else '')
