#!/usr/bin/env python3
# Compute Monkey's bits-per-key of each level for one YCSB workload and write
# it into the naive_monkey_bpk_list line of a RocksDB options file.
#
# Monkey knows the workload: it is given the number of entries the LSM-tree
# holds at the end of the workload (records + inserts, plus the obsolete
# versions left by updates, as in runtime_tput_exp.cc), and the per-level
# bits-per-key are computed once from it with getNaiveMonkeyBitsPerKey
# (skew-aware-bpk-benchmark/emu_util.cc), ported line by line below.
#
# Usage: monkey_bpk.py <workload file> <options.ini>

import math
import re
import sys


def read_properties(path):
    # YCSB properties: key=value, '#' comments, the last occurrence wins
    props = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#') or '=' not in line:
                continue
            key, value = line.split('=', 1)
            props[key.strip()] = value.strip()
    return props


def read_cf_and_table_options(path):
    opts = {}
    with open(path) as f:
        for line in f:
            m = re.match(r'\s*([A-Za-z0-9_]+)=(.*)$', line)
            if m:
                opts[m.group(1)] = m.group(2).strip()
    return opts


def avg_digits_uniform_u64():
    # average length of std::to_string(utils::Hash(key_num)), a 64-bit hash
    total = 2.0 ** 64
    avg = 0.0
    for d in range(1, 21):
        lo = 0 if d == 1 else 10 ** (d - 1)
        hi = min(10 ** d, total)
        if hi > lo:
            avg += d * (hi - lo) / total
    return avg


def mean_field_length(dist, field_len):
    if dist == 'constant':
        return float(field_len)
    if dist == 'uniform':
        return (1 + field_len) / 2.0
    if dist == 'zipfian':
        # ZipfianGenerator(1, field_len): value 1 + i with weight 1/(i+1)^0.99
        w = [1.0 / (i + 1) ** 0.99 for i in range(field_len)]
        return sum((1 + i) * w[i] for i in range(field_len)) / sum(w)
    raise ValueError('unknown field_len_dist ' + dist)


def entry_size(props):
    # key: "user" + the (hashed) key number, zero-padded to `zeropadding`
    records = int(props.get('recordcount', '1000'))
    zero_padding = int(props.get('zeropadding', '1'))
    if props.get('insertorder', 'hashed') == 'hashed':
        digits = avg_digits_uniform_u64()
    else:
        digits = len(str(records))
    key = 4 + max(zero_padding, digits)
    # value: RocksdbDB::SerializeRow, i.e. per field a 4-byte length, the field
    # name ("field" + index), a 4-byte length and the field value
    field_count = int(props.get('fieldcount', '10'))
    field_len = int(props.get('fieldlength', '100'))
    dist = props.get('field_len_dist', 'constant')
    names = sum(len('field') + len(str(i)) for i in range(field_count))
    value = field_count * (8 + mean_field_length(dist, field_len)) + names
    return key + value


def num_final_entries(props, size_ratio):
    records = int(props.get('recordcount', '1000'))
    ops = int(props.get('operationcount', '1000'))
    inserts = int(round(float(props.get('insertproportion', '0')) * ops))
    writes = int(round((float(props.get('updateproportion', '0')) +
                        float(props.get('readmodifywriteproportion', '0'))) * ops))
    unique = records + inserts
    space_amp = min(writes, int(unique * (1.0 - 1.0 / size_ratio)))
    return unique + space_amp


def naive_monkey_bpk(num_entries, num_entries_per_table, size_ratio,
                     max_files_in_L0, overall_bits_per_key, list_size,
                     dynamic_cmpct, optimize_L0_files=False):
    # port of getNaiveMonkeyBitsPerKey (emu_util.cc); size_t arithmetic kept
    bpk = [overall_bits_per_key] * list_size
    log_2_squared = math.log(2) ** 2
    if not optimize_L0_files:
        num_entries -= num_entries_per_table * max_files_in_L0
    if num_entries <= 0:
        # everything fits in L0 (the C++ code would underflow here)
        return bpk, 0, 0
    num_files = (num_entries + num_entries_per_table - 1) // num_entries_per_table
    total_filter_memory = int(overall_bits_per_key * num_entries)
    if num_files <= max_files_in_L0 or size_ratio == 1.0:
        # same bits-per-key if the workload fits in L0
        return bpk, 0, num_files
    num_levels = min(int(math.ceil(math.log((num_files - max_files_in_L0) *
                                            (size_ratio - 1.0) / size_ratio + 1) /
                                   math.log(size_ratio))) + 1, list_size) - 1

    S1 = 0.0
    S2 = 0.0
    if optimize_L0_files:
        S1 = num_entries_per_table / log_2_squared
        S2 = max_files_in_L0 * S1 * math.log(S1)
    base = size_ratio * num_entries_per_table / log_2_squared
    for _ in range(num_levels):
        S1 += base
        S2 += base * math.log(base)
        base *= size_ratio
    log_lambda = -(total_filter_memory + S2) / S1
    max_level_with_filter = num_levels
    while (log_lambda + max_level_with_filter * math.log(size_ratio) +
           math.log(num_entries_per_table) > -log_2_squared and
           max_level_with_filter >= 1):
        S2 -= base * math.log(base)
        S1 -= base
        max_level_with_filter -= 1
        log_lambda = -(total_filter_memory + S2) / S1
    if optimize_L0_files:
        bpk[0] = -(log_lambda + math.log(num_entries_per_table / log_2_squared)) / log_2_squared
    for i in range(max_level_with_filter, list_size - 1):
        bpk[i + 1] = 0.0
    base = size_ratio * num_entries_per_table
    for i in range(1, max_level_with_filter + 1):
        bpk[i] = -(log_lambda + math.log(base / log_2_squared)) / log_2_squared
        if bpk[i] < 1.0:
            bpk[i] = 0.0
        base *= size_ratio
    if dynamic_cmpct:
        # bottom-up construction: the last level is the last level of the tree
        movement = list_size - 1 - num_levels
        if movement > 0:
            for i in range(list_size - 1, movement, -1):
                bpk[i] = bpk[i - movement]
            if max_level_with_filter >= 1:
                for i in range(2, min(movement, list_size - 1) + 1):
                    bpk[i] = bpk[1]
    if not optimize_L0_files:
        bpk[0] = overall_bits_per_key
    return bpk, num_levels, num_files


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: monkey_bpk.py <workload file> <options.ini>')
    workload_path, options_path = sys.argv[1], sys.argv[2]
    props = read_properties(workload_path)
    opts = read_cf_and_table_options(options_path)

    m = re.match(r'bloomfilter:([0-9.]+):', opts['filter_policy'])
    bits_per_key = float(m.group(1))
    write_buffer_size = int(opts['write_buffer_size'])
    size_ratio = float(opts['max_bytes_for_level_multiplier'])
    max_files_in_L0 = int(opts['level0_file_num_compaction_trigger'])
    num_levels = int(opts['num_levels'])
    dynamic_cmpct = opts['level_compaction_dynamic_level_bytes'] == 'true'

    e = entry_size(props)
    entries_per_table = int(math.floor(write_buffer_size / e))
    n = num_final_entries(props, size_ratio)
    bpk, tree_levels, num_files = naive_monkey_bpk(
        n, entries_per_table, size_ratio, max_files_in_L0, bits_per_key,
        num_levels, dynamic_cmpct)
    bpk_str = ':'.join('%.6g' % b for b in bpk)

    with open(options_path) as f:
        content = f.read()
    content, count = re.subn(r'(?m)^(\s*naive_monkey_bpk_list=).*$',
                             lambda mo: mo.group(1) + bpk_str, content)
    if count != 1:
        sys.exit('expected one naive_monkey_bpk_list line in ' + options_path)
    with open(options_path, 'w') as f:
        f.write(content)

    print('Monkey (%s): entry size %.1f B, %d entries per memtable, %d entries '
          'at the end of the workload, %d files, %d levels below L0, '
          'bits-per-key of L0..L%d: %s' %
          ('bottom-up' if dynamic_cmpct else 'top-down', e, entries_per_table,
           n, num_files, tree_levels, num_levels - 1, bpk_str))


if __name__ == '__main__':
    main()
