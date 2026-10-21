# SHaMBa in the Mnemosyne codebase

This repository contains a port of **SHaMBa** (Ju Hyoung Mun, Zichen Zhu,
Aneesh Raman, Manos Athanassoulis, *"LSM-Trees Under (Memory) Pressure"*,
ADMS 2022) into the same modified RocksDB 8.9.1 that carries Mnemosyne /
Mnemosyne+, plus the harness changes needed to run it next to the other
methods on the same workloads.

SHaMBa replaces the Bloom filter of each SST file with **modular Bloom
filters**: several smaller filter modules with the same total size. A lookup
probes the modules one after the other and, based on how useful the filter of
that file has been, may skip the later modules or the whole filter. It is
designed for the case where the filters do not all fit in memory.

The port follows the released code
([BU-DiSC/SHaMBa](https://github.com/BU-DiSC/SHaMBa), a modified RocksDB 6.19
in `rocksdb-mbf` and its benchmark `mbf-benchmark`). Where the port differs
from it, see [Differences and assumptions](#differences-and-assumptions).

## Quick start

```bash
# 1. build (same as before)
cd skew-aware-rocksdb-8.9.1 && make static_lib -j$(nproc) && cd ..
cd skew-aware-bpk-benchmark && make && cd ..

# 2. one run of workload type II with RocksDB, Mnemosyne, Mnemosyne+ and SHaMBa
cd skew-aware-bpk-benchmark
./query_lat_exp -T 4 -E 512 --dd -p ${DB_HOME} \
  --iwp ../workload_generator_scripts/ingestion_workload.txt \
  --qwp ../workload_generator_scripts/Z1.0_ZD1_mixed_update_query_workload.txt \
  -B 32 -P 1024 -b 4 --BCC 524288 -V 1 --dr \
  --shamba --run-stats-op output.txt

# 3. Fig. 12 of the Mnemosyne paper, which includes SHaMBa
cd ../exp-scripts && FAST_DB_HOME=/path/on/ssd/db_working_home ./fig12.sh
```

`--shamba` adds a SHaMBa run after the other methods (without it the program
behaves exactly as before). SHaMBa keeps its own database in
`${DB_HOME}-shamba`. Its results are printed with the same lines as the
others, suffixed with `(shamba)`, e.g. `read bytes (shamba): ...` and
`point query latency (shamba): ...`, followed by
`shamba whole filter skipped times`, the number of file lookups for which the
whole filter was skipped. As for Mnemosyne, the false positive rate line counts
every module probe, so it is not comparable with the one of RocksDB; compare
the read bytes and the accessed data blocks instead.

| flag (query_lat_exp) | default | meaning |
|---|---|---|
| `--shamba` | off | also run SHaMBa |
| `-b` | (required) | bits-per-key of every file, the same `b` given to the other methods |
| `--shamba_modules` | 2 | number of equal-sized filter modules per file |
| `--shamba_skip` | `P` | skipping policy: `none`, `P` or `F` (see below) |
| `--shamba_th1` | 0.01 | utility threshold below which the whole filter is skipped (`F` only) |
| `--shamba_th2` | 0.02 | utility threshold below which the modules after the first one are skipped (`P` and `F`) |

Skipping policies:

| `--shamba_skip` | SHaMBa variant | behaviour |
|---|---|---|
| `none` | SHaMBa-eq | every lookup uses all the modules |
| `P` | SHaMBa-eq-P | a lookup may use only the first module |
| `F` | SHaMBa-eq-F | as `P`, and a lookup may also skip the whole filter |

Scripts:

* `skew-aware-bpk-benchmark/exp_query_lat.sh`: the Fig. 12 sweep
  (Z x ZD x bpk), which passes `--shamba`; SHaMBa knobs via `SHAMBA_MODULES`,
  `SHAMBA_SKIP`, `SHAMBA_TH1` and `SHAMBA_TH2`.
* `skew-aware-bpk-benchmark/tput_exp.sh`: the Fig. 11 sweep (workload type I,
  run by `exp-scripts/fig11.sh`), which passes the same `--shamba*` flags of
  `runtime_tput_exp` with the same knobs; `exp-figures/Fig11-*.pdf` show
  SHaMBa in brown, dash-dotted. Its bits-per-key curve is the size of all the
  modules on disk, so skipped modules still count.
* `exp-scripts/fig12.sh`: generates the workloads, runs the sweep, aggregates
  into `agg_output/` and plots `exp-figures/Fig12-*.pdf` (SHaMBa in brown,
  dash-dotted). For example, `SHAMBA_SKIP=none ./fig12.sh` plots SHaMBa-eq
  instead of SHaMBa-eq-P.
* `output/grep_*_by_bpk.sh` print an extra `SHaMBa` block when SHaMBa results
  exist, so the existing aggregation script works unchanged.

## How SHaMBa maps to the code

SHaMBa is a new filter mode, `BitsPerKeyAllocationType::kShambaBpkAlloc`. It
reuses the filter modules the fork already has for Mnemosyne
(`modular_filters`), so the SST format does not change.

| SHaMBa | Code |
|---|---|
| Every file gets the same bits-per-key (no reallocation across files) | `BitsPerKeyAllocHelper::IfNeedAllocateBitsPerKey` returns the overall bits-per-key; `PrepareBpkAllocation` does nothing |
| The filter is split into modules | `BlockBasedTableBuilder::ResetFilterBitsPerKey`, with `max_modulars` modules of `max_bits_per_key_granularity` bits-per-key each |
| Per-file statistics: point reads and point reads that found the key | `FileMetaData::stats.num_point_reads` and `num_existing_point_reads`, maintained by `PointReadsTrackMethod::kNaiiveTrack` |
| Number of point lookups that reached the SST files | `VersionStorageInfo::shamba_num_point_lookups_`, incremented in `Version::Get` |
| Decision per lookup and per file: no module, only the first one, or all of them | `Version::IsFilterSkippedByShamba`, a port of `BlockBasedTable::GetModularFilterReadType()` |
| Skipped modules are not consulted at all | `GetContext::shamba_module_skipping_`, checked in `BlockBasedTable::Get` |
| The first module is cached with high priority, the others with low priority | `GetContext::filter_second_high_priority_cache_` |
| Options | `BlockBasedTableOptions::shamba_*` in `include/rocksdb/table.h` |

### The skipping decision

For a file, let

* `alpha` = fraction of its point reads that found the key,
* `beta` = its point reads divided by all the point lookups that reached the
  SST files,
* `fpr(x)` = false positive rate of a Bloom filter with `floor(x)`
  bits-per-key and the rounded optimal number of probes (1 below 2
  bits-per-key).

Then a lookup on that file

1. skips the whole filter if the policy is `F` and
   `beta * (1 - alpha) * (1 - fpr(first module)) < shamba_util_threshold_1`;
2. otherwise uses only the first module if the policy is `P` or `F` and
   `beta * (1 - alpha) * (fpr(first module) - fpr(all modules)) < shamba_util_threshold_2`;
3. otherwise uses all the modules.

A file without statistics yet uses all its modules.

Files:

* `skew-aware-rocksdb-8.9.1/include/rocksdb/table.h`: `kShambaBpkAlloc` and
  the `shamba_*` options.
* `db/version_set.{h,cc}`: the lookup counter and the skipping decision.
* `db/bpk_alloc_helper.cc`: no bits-per-key reallocation in this mode.
* `table/get_context.h`, `table/block_based/block_based_table_reader.cc`:
  skipped modules are not probed.
* `table/block_based/block_based_table_factory.cc`, `options/options_helper.cc`:
  option parsing.
* `skew-aware-bpk-benchmark/`: `--shamba*` flags in `query_lat_exp`,
  `configShamba` in `emu_util.cc`, scripts.

## Fairness of the comparison in the harness

* **Same memory.** Every file is built with `b` bits-per-key (`-b`), the same
  as the RocksDB baseline. The modules are filter blocks in the block cache,
  like the filters of RocksDB and Mnemosyne, so the block cache is not
  resized.
* **Separate ingestion.** SHaMBa SSTs store their filter as modules, so the
  SHaMBa database is ingested separately.
* Mnemosyne's bits-per-key reallocation and ElasticBF are disabled in the
  SHaMBa run.

## What to expect in Figure 12

SHaMBa targets the case where the filters do not fit in memory. In Figure 12
the block cache is large enough for all of them, so skipping a module saves no
I/O and only raises the false positive rate. Therefore:

* `none` reads about as much as RocksDB (same bits-per-key, same total number
  of probes);
* `P`, the default, reads more than RocksDB whenever it skips the second
  module;
* `F` reads the most. `beta` is at most the share of lookups that reach one
  file, which is far below the thresholds once the database has many files, so
  at full scale nearly every filter is skipped.

Measured with `QUICK=1 ./fig12.sh` (1/1000 scale, one run, default policy `P`,
numbers only indicative): read bytes of SHaMBa divided by those of RocksDB,
over the 11 bits-per-key values.

| workload | mean | range |
|---|---|---|
| Z0.0 (all lookups find the key), uniform | 1.00 | 0.98 to 1.03 |
| Z0.0, skewed | 0.97 | 0.79 to 1.14 |
| Z0.5, uniform | 1.10 | 0.99 to 1.20 |
| Z0.5, skewed | 1.08 | 0.89 to 1.25 |
| Z1.0 (no lookup finds the key), uniform | 2.90 | 0.99 to 9.89 |
| Z1.0, skewed | 1.29 | 1.00 to 3.09 |

The gap is largest for empty lookups at high bits-per-key (9.89 at 12
bits-per-key): RocksDB's filter is then very accurate, while the first module
alone has half the bits. A file would need to be reached by more than a third
of all lookups for its second module to pass the 0.02 threshold there. With
`--shamba_skip none` on a separate small workload (`-b 6`), SHaMBa read 198 MB
and RocksDB 199 MB.

## Differences and assumptions

1. **Number and size of the modules.** The released code has two modules: a
   first one of `prefetch_bpk` bits-per-key and a second one with the rest.
   The port has `--shamba_modules` modules of `b / modules` bits-per-key
   each. With the default of 2 this is the equal split (SHaMBa-eq).
2. **Small modules.** The fork does not create a module that would leave less
   than one bit-per-key for the next one, so `-b 2` with two modules gives a
   single 2 bits-per-key module and nothing can be skipped but the whole
   filter. As in the released code, a module below 2 bits-per-key counts as
   useless in the utility (`fpr = 1`), which applies to `-b 3` with two
   modules of 1.5 bits-per-key.
3. **False positive rates.** The released code looks them up in a table for 0
   to 10 bits-per-key. The port computes them with the formula above, which
   gives the same values from 2 to 10 and also covers larger bits-per-key
   (Figure 12 goes up to 12).
4. **Thresholds.** The defaults (0.01 and 0.02) are those of the released
   benchmark. The paper states 0.2 and 0.1; use `--shamba_th1` and
   `--shamba_th2` (or `SHAMBA_TH1` and `SHAMBA_TH2`) for other values.
5. **Default policy.** `P`, as in the released benchmark, where neither
   `require_all_modules` nor `allow_whole_filter_skipping` is set.
6. **Statistics.** The released code counts reads and true positives per file
   in its own counters. The port reuses the per-file counters of
   `PointReadsTrackMethod::kNaiiveTrack`, which count every point read. The
   counters of a compaction output start from zero, so a new file uses all its
   modules until it is read.
7. **More than two modules.** With more than two modules, `P` and `F` still
   choose between the first module and all of them, as in the released code.

## Limitations

* No correctness test is shipped for this mode (ElasticBF has
  `elasticbf_test`). Skipping a module can only add false positives; in the
  runs done so far, every policy returned the same found and not-found counts
  as RocksDB.
* Only `query_lat_exp` and `runtime_tput_exp` have the `--shamba` flags; the
  other benchmarks do not run SHaMBa.
* `MultiGet` does not apply SHaMBa's skipping (the harness only issues `Get`).
