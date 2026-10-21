# ElasticBF in the Mnemosyne codebase

This repository now contains an implementation of **ElasticBF** (Yongkun Li,
Chengjin Tian, Fan Guo, Cheng Li, Yinlong Xu, *"ElasticBF: Elastic Bloom Filter
with Hotness Awareness for Boosting Read Performance in Large Key-Value
Stores"*, USENIX ATC 2019) inside the same modified RocksDB 8.9.1 that carries
Mnemosyne / Mnemosyne+, plus the harness changes needed to run it next to
RocksDB, Mnemosyne and Mnemosyne+ on the same workloads.

The authors did not release code, so this is a re-implementation from the
paper. Everything the paper leaves open is marked `ASSUMPTION` in the code and
listed in [Assumptions](#assumptions) below.

## Quick start

```bash
# 1. build (same as before)
cd skew-aware-rocksdb-8.9.1 && make static_lib -j$(nproc) && cd ..
cd skew-aware-bpk-benchmark && make && cd ..

# 2. sanity / correctness test of ElasticBF (no false negatives, memory budget,
#    promotions, inheritance, reopen, sync + async unit loading)
cd skew-aware-bpk-benchmark && ./elasticbf_test /path/on/ssd/ebf_test_db

# 3. one run of workload type II with all four systems
./query_lat_exp -T 4 -E 512 --dd -p ${DB_HOME} \
  --iwp ../workload_generator_scripts/ingestion_workload.txt \
  --qwp ../workload_generator_scripts/Z1.0_ZD1_mixed_update_query_workload.txt \
  -B 32 -P 1024 -b 4 --BCC 524288 -V 1 --dr \
  --ebf --run-stats-op output.txt

# 4. Fig. 12 of the Mnemosyne paper, which includes ElasticBF (and Monkey)
cd ../exp-scripts && FAST_DB_HOME=/path/on/ssd/db_working_home ./fig12.sh
```

`--ebf` adds an ElasticBF run after RocksDB, Mnemosyne and Mnemosyne+ (without
it the programs behave exactly as before). ElasticBF results are printed with
the same lines as the others, suffixed with `(elasticbf)`, e.g.
`read bytes (elasticbf): ...`, `point query latency (elasticbf): ...`, followed
by the ElasticBF statistics (units per queue, promotions, unit-load I/O, ...).

| flag (query_lat_exp, runtime_tput_exp) | default | meaning |
|---|---|---|
| `--ebf` | off | also run ElasticBF |
| `-b` | (required) | memory budget in bits-per-key, the same `b` given to the other systems |
| `--ebf_unit_bpk` | `= -b` | bits-per-key of one filter unit |
| `--ebf_units` | 6 | filter units per segment |
| `--ebf_seg_size` | 4194304 | segment size in bytes |
| `--ebf_lifetime` | 10000 | `lifeTime` of the expiring policy, in Get requests |
| `--ebf_sync_load` | off | load newly enabled units in the foreground Get instead of the background thread |
| `--ebf_no_inherit` | off | disable hotness inheritance on compaction |
| `--ebf_keep_bcc` | off | do not shrink the block cache by the unit budget (see fairness) |

Scripts:

* `skew-aware-bpk-benchmark/exp_query_lat.sh`: the Fig. 12 sweep
  (Z x ZD x bpk), which passes `--ebf`; ElasticBF knobs via `EBF_SEG_SIZE`,
  `EBF_UNITS`, `EBF_UNIT_BPK`, `EBF_LIFETIME`, `EBF_EXTRA_FLAGS`.
* `exp-scripts/fig12.sh`: generates the workloads, runs the sweep, aggregates
  into `agg_output/` and plots `exp-figures/Fig12-*.pdf` (ElasticBF in orange).
  With `QUICK=1` it also scales the segment size and lifeTime down.
* `output/grep_*_by_bpk.sh` print an extra `ElasticBF` block when ElasticBF
  results exist, so the existing aggregation script works unchanged.
* `runtime_tput_exp --ebf` adds ElasticBF to workload type I (Fig. 11); its
  tracked bits-per-key column is the bits-per-key of the units resident in
  memory. The Fig. 11 sweep (`skew-aware-bpk-benchmark/tput_exp.sh`, run by
  `exp-scripts/fig11.sh`) passes it, with the same environment knobs as
  `exp_query_lat.sh`; `exp-figures/Fig11-*.pdf` show ElasticBF in orange.

## How the paper maps to the code

| Paper (Sec.) | Code |
|---|---|
| Fine-grained filter: each SST split into **segments**, each with a **filter group** of `n` independent **filter units** (3.2) | `BlockBasedTableBuilder::ElasticBFAddKey / ElasticBFMaybeCutSegment / WriteElasticBFBlocks`, `elastic_bf::BuildUnit` |
| Units stored on disk, loaded on demand (3.2, 3.5) | one raw block per unit + meta block `rocksdb.elasticbf.index`; `BlockBasedTable::SetupElasticBF / ReadElasticBFBlock` |
| A key is absent as soon as one enabled unit says so (3.2) | `ElasticBFManager::KeyMayMatch` (units probed in order 0..k-1) |
| Expiring policy: `currentTime`, `lastAccessedTime`, `expiredTime = lastAccessedTime + lifeTime` (3.3) | `ElasticBFManager::Tick` (called from `DBImpl::GetImpl`), `SegmentState::last_access`, `ExpiredLocked` |
| `E[Extra_IO] = sum_i f_i * r_i`; enable one more unit for the accessed segment if disabling units of expired segments lowers it (3.4) | `ElasticBFManager::MaybePromoteLocked` |
| Multi-Queue `Q0..Qn`, `Qi` = segments with `i` units, LRU->MRU; donors searched from `Qn` down to `Q1` (3.4) | `ElasticBFManager::queues_` |
| Background thread loads units (3.4/3.5) | `ElasticBFManager::LoaderLoop` |
| Hotness inheritance for compaction outputs (3.3) | `CompactionJob::Run` -> `ElasticBFManager::PrepareInheritance`, applied in `RegisterTable` |
| Same memory as the static configuration | `budget_bits_` / `used_bits_` accounting, `ShrinkToBudgetLocked` |

Files:

* `skew-aware-rocksdb-8.9.1/table/block_based/elastic_bf.{h,cc}` (new): units,
  segment index format, manager (MQ, cost model, loader, inheritance, stats).
* `skew-aware-rocksdb-8.9.1/include/rocksdb/elastic_bf.h` (new): public
  statistics API (`GetElasticBFStatistics`, `ResetElasticBFCounters`).
* `include/rocksdb/table.h`: `BlockBasedTableOptions::elastic_bf*` options.
* `table/block_based/block_based_table_{builder,reader,factory}.{h,cc}`,
  `db/db_impl/db_impl.cc`, `db/compaction/compaction_job.cc`: integration.
* `skew-aware-bpk-benchmark/`: `--ebf*` flags in `query_lat_exp` and
  `runtime_tput_exp`, helpers in `emu_util.cc`, `elasticbf_test.cc`, scripts.

### Data layout

An ElasticBF SST has no regular filter block. While the table is built, the
64-bit hash of every distinct user key is buffered per segment; a segment is
closed at the first data-block boundary after it covers `elastic_bf_segment_size`
bytes of the file, at which point its `n` units are built. In `Finish()` all
units are written as raw blocks in **decreasing unit index** (all unit `n-1`
blocks, ..., all unit 0 blocks), followed by the segment index (per segment:
smallest/largest user key, #keys, `n` block handles). The tail size recorded
for the file excludes the units that are not loaded by default, so the tail
prefetch done when a table is opened brings in exactly the default units
(the trick Mnemosyne already uses for its modular filters); other units are
read individually when they are enabled.

### Run time

* Opening a table registers its segments with the per-DB `ElasticBFManager`
  (one per table factory) and loads the default units (or the inherited number
  of units, see below). Closing/deleting a table unregisters them and frees
  their units.
* Every `Get` advances `currentTime`. For every SST the Get reaches, the key is
  mapped to the segment whose key range covers it (binary search on the
  segments' largest keys); the segment's `f_i` is incremented, its
  `lastAccessedTime` set to `currentTime`, and it moves to the MRU end of its
  queue; then its enabled units are checked.
* Adjustment on the same access: if the segment has fewer than `n` units, the
  gain of one more unit is `f_i (r^k - r^{k+1})`. If free budget exists it is
  used; otherwise expired segments are scanned from `Qn` down to `Q1`, each
  queue from LRU to MRU, and a candidate in `Qq` is taken as a donor if
  `gain > sum of donor losses f_j (r^{q-1} - r^q)`. If no expired segment
  exists, or no donor makes `E[Extra_IO]` smaller, nothing changes. Donors
  lose their last unit immediately; the new unit is loaded by the background
  thread (or in the foreground with `--ebf_sync_load`) and becomes usable when
  the load completes.
* `r` is the per-unit false positive rate `e^{-b' (ln 2)^2}` (= 0.6185^{b'})
  where `b'` is the unit bits-per-key; a segment with `k` units has FPR `r^k`.
  The implementation uses RocksDB's FastLocalBloom for each unit (the same
  filter RocksDB/Mnemosyne use), and `elasticbf_test` checks that `k` units give
  FPR ~ `r^k` (independent hashes).

## Fairness of the comparison in the harness

* **Same memory.** The budget is `b` bits-per-key over all keys of live SSTs,
  the same `b` (`-b`) the other systems get. RocksDB/Mnemosyne keep their
  filters in the block cache (`cache_index_and_filter_blocks=true`), while
  ElasticBF units live in the manager. To keep the total memory equal, the
  ElasticBF run shrinks the block cache by the unit budget (computed after
  ingestion in `query_lat_exp`; estimated as `b x #inserts / 8` bytes in
  `runtime_tput_exp`). `--ebf_keep_bcc` disables this.
* **Same I/O accounting.** Unit loads done by the background thread are not
  visible in the Get thread's `iostats_context`, so their bytes are added to
  `read bytes (elasticbf)`; `elasticbf unit load bytes` reports them alone.
  Units loaded while opening tables come from the tail prefetch, like the
  regular filter blocks of the other systems.
* **Separate ingestion.** ElasticBF SSTs have a different format, so the
  ElasticBF database is ingested separately (Mnemosyne+ reuses Mnemosyne's).
  ElasticBF starts the query phase cold (all segments with their default
  units), like the paper.
* Mnemosyne-specific mechanisms (modular filters, per-file bits-per-key,
  Mnemosyne+ point-read tracking) are disabled in the ElasticBF run.

## Sanity check at small scale

Run on a 2-core VM (not the paper's hardware, numbers are only indicative):
1M inserts of 512 B entries, then 1M zero-result point lookups + 200K updates,
`-b 4`, 32 MB block cache, 2 MB files, direct reads,
`--ebf --ebf_seg_size 1048576 --ebf_lifetime 2000` (smaller segments and
lifeTime because the database has ~800 segments instead of the paper's 25K).

| query distribution | metric | RocksDB | Mnemosyne | Mnemosyne+ | ElasticBF |
|---|---|---|---|---|---|
| skewed (`--ZD 1`, normal) | read bytes (GB) | 3.02 | 3.08 | 2.40 | 0.74 |
| | point query latency (us) | 24.0 | 25.2 | 22.7 | 15.1 |
| | filter false-positive rate | 14.3% | 15.7% | 15.3% | 7.7% |
| uniform (`--ZD 0`) | read bytes (GB) | 5.08 | 3.46 | 3.18 | 4.45 |
| | point query latency (us) | 36.2 | 28.8 | 28.7 | 33.5 |
| | filter false-positive rate | 14.2% | 15.7% | 15.5% | 9.6% |

ElasticBF's resident filter memory was 3.98 and 4.15 bits-per-key (budget 4;
the difference is the cache-line rounding of the small last segment of each
2 MB file, negligible with 16 MB files). As expected from the paper, ElasticBF
gains most under skew; under uniform lookups it still lowers the filter FPR
(per-level access frequencies differ) but Mnemosyne's per-file allocation
reads less. The false-positive rate of Mnemosyne / Mnemosyne+ is per filter
module probe, so it is not directly comparable.

## Assumptions

Where the paper is not explicit, the implementation does the following. All of
them are configurable or isolated in `elastic_bf.cc`.

1. **Units loaded by default.** Not stated in the paper. Each segment keeps
   `floor(budget_bpk / unit_bpk)` units by default (1 with the paper's
   4-bpk budget and 4-bpk units).
2. **Unit size relative to the budget `b`.** The paper's setting is a budget
   of 4 bits-per-key, 6 units of 4 bits-per-key each. Since the Mnemosyne
   experiments sweep `b` = 2..7, the default unit size is `b` itself (one unit
   = the whole static filter, up to 6b per hot segment); `--ebf_unit_bpk`
   changes it (e.g. `--ebf_unit_bpk 2 -b 4` gives 2 default units).
3. **Memory budget.** Per segment, the budget is the memory of its default
   units (including the 64-byte rounding of each unit) plus
   `(b - default_units x unit_bpk) x #keys` when `b` is not a multiple of the
   unit size; the total follows the live SSTs. The paper only says that memory
   stays unchanged, so the following rules keep it at the budget:
   * flush outputs and tables opened at start-up get their default units;
   * compaction outputs are memory-conserving: together they may use what the
     compaction inputs used, plus the change of budget (number of keys), see 9;
   * while compaction inputs wait to be deleted, the budget they lent to other
     segments (budget - used) is reserved, i.e. promotions cannot spend it,
     and the memory they hold beyond their budget is attributed to their
     outputs;
   * if the total is still above the budget once the pending inputs are
     accounted for (rounding, inheritance disabled, drift), units are
     disabled one at a time, the one whose removal increases `E[Extra_IO]`
     the least per bit, taken from expired segments first, then from
     non-expired segments that have been accessed, then from any segment
     (`budget_demotions`).
   The harness reports the actual resident bits-per-key
   (`elasticbf effective bits-per-key`); it stays at `b` up to the cache-line
   rounding of small units.
4. **`currentTime`** counts user `Get` requests (`DBImpl::GetImpl`), including
   Gets served by the memtable. `MultiGet` is not instrumented and does not use
   ElasticBF filtering (the harness only issues `Get`).
5. **Segment access.** A segment is accessed when a Get reaches its SST (the key
   is within the file's range as decided by RocksDB's `FilePicker`) and the key
   falls in the segment's range, for both existing and non-existing keys. The
   paper only says "each time a segment is accessed".
6. **`f_i`** is the cumulative number of accesses (no decay), as the paper's
   cost formula uses an access frequency and says the frequency of an expired
   segment does not change. Recency only enters through expiry.
7. **New segments** have `f_i = 0` and `lastAccessedTime = currentTime` at
   creation (flush or DB open), i.e. they get one `lifeTime` before they can be
   used as donors. Not specified in the paper.
8. **Donor search.** One unit is added per access. Free budget is used first.
   Donors must be expired, are taken from `Qn` down to `Q1`, LRU to MRU, and
   several donors can fund one unit when unit sizes differ (last segment of an
   SST is smaller). At most 256 expired candidates are inspected per
   adjustment to bound the cost of a Get (implementation bound, not in the
   paper). Demoted segments keep their recency order in the lower queue.
9. **Hotness inheritance.** When a compaction finishes writing its outputs
   (before RocksDB opens them for verification), the hotness of the input
   segments is captured. A new segment overlapping (by user-key range) a set of
   old segments takes the **mean** `f_i` and the **latest** `lastAccessedTime`
   of those segments; its target number of units is **round(mean number of
   units they had)** ("enable units according to the hotness"; the paper does
   not give the mapping). Units are then handed out from the memory released
   by the inputs (memory-conserving, see 3), hottest segments first: first up
   to min(target, default), then up to the target. Consequently, outputs of
   inputs that had donated their units to hot segments elsewhere can start
   with fewer than the default units. Flush outputs do not inherit (new
   data); trivial moves keep the same file and therefore its state.
10. **Hash functions.** Units use independent hash functions: one 64-bit XXH3
    hash of the key is remixed with a per-unit seed by a 64-bit finalizer
    (measured FPRs match `r^k`).
11. **Bloom filter variant.** RocksDB's FastLocalBloom (cache-line local),
    i.e. the same implementation as the baseline full filters, rather than
    LevelDB's Bloom filter used by the authors.
12. **Segment boundaries.** Segments consist of whole data blocks; a segment is
    closed at the first data-block boundary after `segment_size` bytes of the
    file (compressed size, as stored). The last segment of an SST is smaller.
13. **Unit loading.** By default asynchronous (paper): the Get that triggers
    the promotion uses the units it had; the new unit is usable once loaded.
    Unit blocks are read with checksum verification, bypassing the block cache.
14. **Metadata memory** (segment keys, counters, queue nodes) is not charged
    against the filter budget (the paper reports ~4 bytes per segment; this
    implementation also keeps the segment's smallest/largest keys).
15. **Block cache in the harness** is shrunk by the unit budget (see fairness).

## Limitations

* Whole-key filtering only (no prefix extractor support), no user-defined
  timestamps, no partitioned filters.
* Requires the default single-threaded table building
  (`compression_opts.parallel_threads = 1`) and no buffered compression
  dictionary (`max_dict_bytes = 0`); otherwise keys are not added to units.
* `MultiGet` does not consult ElasticBF (falls back to reading the table).
* Hotness is kept in memory only: it is lost when the DB is closed or a table
  reader is evicted from the table cache (`max_open_files != -1`).
