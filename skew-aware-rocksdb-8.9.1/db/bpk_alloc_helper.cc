#include "bpk_alloc_helper.h"

#include <cmath>
#include <memory>
#include <unordered_map>


namespace ROCKSDB_NAMESPACE {

const double log_2_squared = std::pow(std::log(2), 2);

void BitsPerKeyAllocHelper::PrepareBpkAllocation(const Compaction* compaction) {
  if (bpk_alloc_type_ == BitsPerKeyAllocationType::kDefaultBpkAlloc) return;
  if (bpk_alloc_type_ == BitsPerKeyAllocationType::kNaiveMonkeyBpkAlloc) {
    if (naive_monkey_bpk_list.size() == 0) return;
    int output_level = 0;
    if (compaction != NULL) {
      output_level = compaction->output_level();
    }
    if (output_level < (int)naive_monkey_bpk_list.size()) {
      naive_monkey_bpk = naive_monkey_bpk_list[output_level];
    } else {
      naive_monkey_bpk = overall_bits_per_key_;
    }
    return;
  }
  if (bpk_alloc_type_ == BitsPerKeyAllocationType::kMnemosyneBpkAlloc ||
      (bpk_alloc_type_ == BitsPerKeyAllocationType::kMnemosynePlusBpkAlloc &&
       vstorage_->GetAccumulatedNumPointReads() == 0)) {
    bpk_alloc_type_ = BitsPerKeyAllocationType::kMnemosyneBpkAlloc;
    if (mnemosyne_bpk_optimization_prepared_flag_) return;
    PrepareForMnemosyne(compaction);

  } else if (bpk_alloc_type_ ==
             BitsPerKeyAllocationType::kMnemosynePlusBpkAlloc) {
    if (mnemosyne_plus_bpk_optimization_prepared_flag_) return;
    PrepareForMnemosynePlus(compaction);
  }
  total_num_entries_ = vstorage_->GetCurrentTotalNumEntries();
  vstorage_->UpdateNumEmptyPointReads(mnemosyne_plus_total_empty_queries_);
  vstorage_->UpdateCurrentTotalFilterSize(agg_filter_size_);
}

// PrepareForMnemosyne: set up the workload-agnostic ("Monkey-style")
// bits-per-key (bpk) optimization used by kMnemosyneBpkAlloc.
//
// Goal. A point lookup probes one Bloom filter per L0 file and at most one per
// level L >= 1. For a key that doesn't exist, the expected number of wasted
// data-block reads is the sum of those filters' false-positive rates (FPRs).
// With a fixed memory budget M = N * overall_bits_per_key_, pick b_i (the bpk
// of filter group i, which holds n_i keys) to
//     minimize  sum_i exp(-b_i * ln(2)^2)     (FPR of an optimal Bloom filter)
//     s.t.      sum_i n_i * b_i = M
// Using Lagrange multipliers gives the closed form
//     b_i = -(ln(n_i) + C) / ln(2)^2
//     C   = -(M * ln(2)^2 + sum_i n_i * ln(n_i)) / N,   N = sum_i n_i
// so larger groups get fewer bits per key. When a group's b_i comes out below
// 1, its filter isn't worth building: the group is dropped (b_i = 0) and C is
// recomputed for the remaining groups with the SAME budget M, which moves the
// dropped group's bits to everyone else.
//
// This function builds the groups {n_i} as they will look AFTER the current
// flush/compaction, runs the drop-and-recompute loop, and leaves C in
// mnemosyne_common_constant_in_bpk_optimization_. IfNeedAllocateBitsPerKey()
// later plugs each new file's group size into the b_i formula.
//
// Groups:
//   * L0: one group per file. L0 files overlap, so a lookup may probe every
//     one of them, and each contributes its own FPR term.
//   * L >= 1: one group per level. Files in a level don't overlap, so a lookup
//     probes at most one of them, and the whole level shares one b.
//
// Callers:
//   * Compaction: CompactionJob::Run() -> PrepareBpkAllocation(compaction) on
//     a fresh helper, before any output file is written.
//   * Flush: BuildTable() -> IfNeedAllocateBitsPerKey(), which first pushes the
//     new L0 file into level_states_pq_, mnemosyne_num_entries_ and
//     mnemosyne_temp_sum_in_bpk_optimization_, and then gets here with
//     compaction == nullptr. That is why none of those members are reset below.
//
// Per-file inputs (FileMetaData, db/version_edit.h):
//   * num_entries: every entry in the SST, including point deletes, merges and
//     range tombstones (mirrors TableProperties::num_entries).
//   * num_range_deletions: number of range tombstones (DeleteRange). They go
//     into the range-deletion block, not the Bloom filter
//     (BlockBasedTableBuilder::Add), so num_entries - num_range_deletions is
//     the number of keys hashed into the file's filter.
//   * filter_size: bytes in the file's filter block
//     (TableProperties::filter_size, also persisted in the MANIFEST).
//   * bpk: bpk this fork chose when it built the file. -1 = unknown/default
//     (bpk is not written to the MANIFEST, so files loaded at DB open have
//     -1). 0 = no filter, skipped on reads (Version::IsFilterSkipped).
void BitsPerKeyAllocHelper::PrepareForMnemosyne(const Compaction* compaction) {
  // Levels >= 1 whose whole-level group gets dropped (b < 1). Handed to
  // VersionStorageInfo at the end; Version::IsFilterSkipped() then skips the
  // filters of these levels on reads.
  std::unordered_set<size_t> levelIDs_with_bpk0_in_mnemosyne;
  // Number of filter keys each level loses to this compaction. LevelState's
  // hash and operator== only look at (level, file_number), so:
  //   * LevelState(L, *, 0) with L >= 1 is the key for "all of level L";
  //   * LevelState(0, *, fd) is the key for one particular L0 input file.
  // The mapped value holds the count; the key's num_entries field is ignored.
  std::unordered_map<LevelState, uint64_t, LevelState::HashFunction>
      level2removed_num_entries;
  // Filter keys that move from the upper input level(s) into max_level.
  uint64_t added_entries_in_max_level = 0;
  // Filter keys across all compaction inputs. It's a member because
  // IfNeedAllocateBitsPerKey() also uses it, to leave these keys out of the
  // "old" totals when capping the average bpk.
  num_entries_in_compaction_ = 0;
  // Deepest level among the compaction inputs. This is the output level only
  // if some output-level files overlap the compaction: the leveled picker
  // appends output-level inputs only when there are some
  // (compaction_picker_level.cc). Otherwise max_level is the start level.
  int max_level = -1;

  // ---- Step 1: work out what this compaction removes and adds ------------
  // Skipped for flushes (compaction == nullptr); the new flushed file was
  // already added by the caller.
  if (compaction != nullptr) {
    // One CompactionInputFiles {level, files} per input level. For a leveled
    // compaction: [start_level files, overlapping output_level files].
    const std::vector<CompactionInputFiles>* input_files = compaction->inputs();
    for (size_t i = 0; i < input_files->size(); i++) {
      int level = input_files->at(i).level;
      if (level > max_level) {
        max_level = level;
      }
      if (level != 0) {
        // Input level L >= 1: add up the filter keys removed from level L as
        // one number, because level L is a single group.
        uint64_t num_entries = 0;
        if (level2removed_num_entries.find(LevelState(level, 0, 0, 0)) !=
            level2removed_num_entries.end()) {
          num_entries = level2removed_num_entries[LevelState(level, 0, 0, 0)];
        }
        uint64_t num_entries_in_bf = 0;
        for (const FileMetaData* meta : input_files->at(i).files) {
          // Keys in this file's Bloom filter (range tombstones excluded).
          num_entries_in_bf = meta->num_entries - meta->num_range_deletions;
          num_entries += num_entries_in_bf;
          num_entries_in_compaction_ += num_entries_in_bf;
          // Filter bits that go away when this input file is deleted. Use the
          // real filter size if this fork built the file (bpk known);
          // otherwise estimate it with the default bpk.
          if (meta->bpk != -1) {
            num_bits_for_filter_to_be_removed_ += meta->filter_size * 8;
          } else {
            num_bits_for_filter_to_be_removed_ +=
                num_entries_in_bf * overall_bits_per_key_;
          }
        }
        // Record the level's total. (emplace doesn't overwrite an existing
        // key, but each level shows up only once in inputs().)
        level2removed_num_entries.emplace(LevelState(level, num_entries, 0),
                                          num_entries);
      } else {
        // Input level 0: every L0 file is its own group, so record each file
        // separately, keyed by its file number. L0 inputs are always
        // inputs()[0] (the start level), hence at(0) instead of at(i).
        uint64_t num_entries_in_bf = 0;
        for (const FileMetaData* meta : input_files->at(0).files) {
          num_entries_in_bf = meta->num_entries - meta->num_range_deletions;
          level2removed_num_entries.emplace(
              LevelState(0, num_entries_in_bf, meta->fd.GetNumber()),
              num_entries_in_bf);
          num_entries_in_compaction_ += num_entries_in_bf;
          // Same removed-bits accounting as above, but estimated as
          // keys * bpk rather than from filter_size.
          if (meta->bpk != -1) {
            num_bits_for_filter_to_be_removed_ += num_entries_in_bf * meta->bpk;
          } else {
            num_bits_for_filter_to_be_removed_ +=
                num_entries_in_bf * overall_bits_per_key_;
          }
        }
      }
    }
    if (max_level > 0) {
      // Compaction into level max_level >= 1. Keys arriving there = all input
      // keys minus those already in max_level. This is an upper bound,
      // because merging can drop duplicate/deleted keys.
      added_entries_in_max_level =
          num_entries_in_compaction_ -
          level2removed_num_entries[LevelState(max_level, 0, 0)];
      // max_level's own input files get rewritten into the same level, so its
      // count must not be subtracted in Step 2. Remove it from the map.
      level2removed_num_entries.erase(LevelState(max_level, 0, 0));
    } else if (max_level == 0 &&
               !no_filter_optimize_for_level0_) {  // intra L0 compaction
      // L0 -> L0: every input key ends up in one new L0 file, which becomes
      // its own group (pushed in Step 3).
      added_entries_in_max_level = num_entries_in_compaction_;
    }
  }

  // ---- Step 2: build the groups from the current LSM tree -----------------
  // vstorage_ is the VersionStorageInfo the helper was built with: the
  // compaction's input version, or the version current at flush time. It
  // still lists the compaction's input files, so Step 1's numbers are used to
  // adjust for them.
  uint64_t num_entries_in_filter_by_level = 0;
  uint64_t max_fd_number = 0;
  uint64_t tmp_fd_number = 0;
  // With BlockBasedTableOptions::no_filter_optimize_for_level0, L0 keeps the
  // default bpk and stays out of the optimization altogether.
  int start_level = 0;
  if (no_filter_optimize_for_level0_) {
    start_level = 1;
  }
  // Total filter bytes on disk right now, including files that are compaction
  // inputs. IfNeedAllocateBitsPerKey() subtracts
  // num_bits_for_filter_to_be_removed_ from this to get the "old" bits that
  // will survive, and PrepareBpkAllocation() publishes it to vstorage_.
  agg_filter_size_ = 0;
  uint64_t tmp_agg_filter_size = 0;
  uint64_t tmp_num_entries_in_filter_by_file = 0;
  for (int level = start_level; level < vstorage_->num_levels(); ++level) {
    num_entries_in_filter_by_level = 0;
    tmp_agg_filter_size = 0;
    for (auto* file_meta : vstorage_->LevelFiles(level)) {
      // Keys in this file's Bloom filter (range tombstones excluded).
      tmp_num_entries_in_filter_by_file =
          file_meta->num_entries - file_meta->num_range_deletions;
      // SST file numbers come from VersionSet::NewFileNumber() and only grow,
      // so the maximum is used below to make up a number for a new file.
      tmp_fd_number = file_meta->fd.GetNumber();
      max_fd_number = std::max(max_fd_number, tmp_fd_number);
      // Count filter bytes before any `continue`, so every file on disk is
      // included.
      tmp_agg_filter_size += file_meta->filter_size;
      if (tmp_num_entries_in_filter_by_file == 0) continue;
      if (level == 0) {
        if (compaction != nullptr && !level2removed_num_entries.empty()) {
          if (level2removed_num_entries.find(LevelState(0, 0, tmp_fd_number)) !=
              level2removed_num_entries.end()) {
            // This file in level 0 is being picked to compact, so we shall
            // not consider this file as an input file in the optimization
            // procedure
            continue;
          }
        }
        // A surviving L0 file is a group of its own: push it and add
        // n * ln(n) to the running sum used for C.
        level_states_pq_.push(LevelState(0, tmp_num_entries_in_filter_by_file,
                                         file_meta->fd.GetNumber()));
        mnemosyne_temp_sum_in_bpk_optimization_ +=
            std::log(tmp_num_entries_in_filter_by_file) *
            tmp_num_entries_in_filter_by_file;
      }
      // Levels >= 1 build their group size here. For L0 this only feeds N.
      num_entries_in_filter_by_level += tmp_num_entries_in_filter_by_file;
    }
    if (max_level == level) {
      // adjust the number of entries by adding the compacted number of
      // entries from upper levels
      num_entries_in_filter_by_level += added_entries_in_max_level;
    }
    if (level != 0) {
      // Take out the keys this compaction moves out of the level (the upper
      // input level). std::min stops unsigned underflow.
      if (compaction != nullptr && !level2removed_num_entries.empty()) {
        if (level2removed_num_entries.find(LevelState(level, 0, 0)) !=
            level2removed_num_entries.end()) {
          num_entries_in_filter_by_level -=
              std::min(num_entries_in_filter_by_level,
                       level2removed_num_entries[LevelState(level, 0, 0)]);
        }
      }

      // skip the current level if the current level has no entries
      // (Note: this also skips adding the level's filter bytes to
      // agg_filter_size_ below, even though those bytes were already counted
      // in num_bits_for_filter_to_be_removed_ in Step 1.)
      if (num_entries_in_filter_by_level == 0) continue;

      // The whole level is one group: push it and add n * ln(n) to the sum.
      level_states_pq_.push(LevelState(level, num_entries_in_filter_by_level, 0,
                                       tmp_agg_filter_size));
      mnemosyne_temp_sum_in_bpk_optimization_ +=
          std::log(num_entries_in_filter_by_level) *
          num_entries_in_filter_by_level;
    }
    agg_filter_size_ += tmp_agg_filter_size;
    // N: total keys taking part in the optimization.
    mnemosyne_num_entries_ += num_entries_in_filter_by_level;
  }

  // ---- Step 3: compute the Lagrange constant C ----------------------------
  // Nothing to optimize. Note that the prepared flag stays false here.
  if (mnemosyne_num_entries_ == 0) return;
  if (added_entries_in_max_level > 0 && max_level == 0 &&
      !no_filter_optimize_for_level0_) {
    // Intra-L0 compaction: the output L0 file doesn't exist yet, so add a
    // placeholder group for it. Its keys are already in N (added to level 0
    // at "max_level == level" above). max_fd_number + 11 is just a file
    // number no existing file uses; the real one isn't assigned yet.
    level_states_pq_.push(
        LevelState(0, added_entries_in_max_level, max_fd_number + 11));
    mnemosyne_temp_sum_in_bpk_optimization_ +=
        std::log(added_entries_in_max_level) * added_entries_in_max_level;
  }
  // M: memory budget in bits = N * the user's bits_per_key (the value the
  // filter policy was configured with, e.g. NewBloomFilterPolicy(10)). Stays
  // fixed while groups get dropped below.
  total_bits_for_filter_ = mnemosyne_num_entries_ * overall_bits_per_key_;
  // C = -(M * ln(2)^2 + sum n_i * ln(n_i)) / N
  mnemosyne_common_constant_in_bpk_optimization_ =
      -(total_bits_for_filter_ * log_2_squared +
        mnemosyne_temp_sum_in_bpk_optimization_) /
      mnemosyne_num_entries_;

  // ---- Step 4: drop groups that would get less than 1 bit per key ---------
  // b_i = -(ln(n_i) + C) / ln(2)^2 < 1  <=>  ln(n_i) + C > -ln(2)^2.
  // b_i shrinks as n_i grows, so the largest group is checked first (the heap
  // is meant to be a max-heap on num_entries; see LevelState::operator< in
  // bpk_alloc_helper.h). Every time a group is dropped, C is recomputed over
  // the remaining groups with the same M. That raises everyone's b_i, and
  // the loop stops at the first group whose b_i >= 1.
  while (!level_states_pq_.empty() &&
         std::log(level_states_pq_.top().num_entries) +
                 mnemosyne_common_constant_in_bpk_optimization_ >
             -log_2_squared) {
    if (level_states_pq_.top().level == 0) {
      // Dropped L0 file: mark it directly. Version::IsFilterSkipped() sees
      // bpk == 0 and doesn't probe this file's filter on Get().
      for (FileMetaData* meta : vstorage_->LevelFiles(0)) {
        if (meta->fd.GetNumber() == level_states_pq_.top().file_number) {
          meta->bpk = 0.0;
        }
      }
    } else {
      // Dropped level: remember the level ID (published after the loop).
      levelIDs_with_bpk0_in_mnemosyne.insert(level_states_pq_.top().level);
    }
    // Take the group out of N and sum n * ln(n), then recompute C.
    tmp_num_entries_in_filter_by_file = level_states_pq_.top().num_entries;
    mnemosyne_temp_sum_in_bpk_optimization_ -=
        std::log(tmp_num_entries_in_filter_by_file) *
        tmp_num_entries_in_filter_by_file;
    mnemosyne_num_entries_ -= tmp_num_entries_in_filter_by_file;
    mnemosyne_common_constant_in_bpk_optimization_ =
        -(total_bits_for_filter_ * log_2_squared +
          mnemosyne_temp_sum_in_bpk_optimization_) /
        mnemosyne_num_entries_;
    level_states_pq_.pop();
  }

  // ---- Step 5: save the results ------------------------------------------
  // Largest group that still gets a filter. The threshold check that would
  // use it in IfNeedAllocateBitsPerKey() is commented out; it now evaluates
  // the b_i formula directly against C.
  if (!level_states_pq_.empty()) {
    mnemosyne_bpk_num_entries_threshold_ = level_states_pq_.top().num_entries;
  }

  // Share the dropped levels with the read path
  // (VersionStorageInfo::IsFilterSkippedWithEmptyBpkInMnemosyne).
  vstorage_->SetLevelIDsWithEmptyBpkInMnemosyne(
      levelIDs_with_bpk0_in_mnemosyne);
  // Later PrepareBpkAllocation() calls on this helper return right away.
  mnemosyne_bpk_optimization_prepared_flag_ = true;
}

void BitsPerKeyAllocHelper::PrepareForMnemosynePlus(
    const Compaction* compaction) {
  uint64_t num_point_reads;
  uint64_t num_existing_point_reads;
  uint64_t num_empty_point_reads;

  std::unordered_set<uint64_t> fileID_in_compaction;
  num_entries_in_compaction_ = 0;
  uint64_t num_empty_queries_in_compaction = 0;
  uint64_t num_files_in_compaction = 0;
  int max_level = -1;
  agg_filter_size_ = 0;
  if (compaction != nullptr) {
    const std::vector<CompactionInputFiles>* input_files = compaction->inputs();
    std::unordered_map<int, uint64_t> level2num_point_reads;
    uint64_t tmp_num_point_reads = 0;
    uint64_t num_existing_point_reads_in_compaction = 0;
    for (size_t i = 0; i < input_files->size(); i++) {
      int level = input_files->at(i).level;
      if (level > max_level) {
        max_level = level;
      }
      num_files_in_compaction += input_files->at(i).size();
      tmp_num_point_reads = 0;
      if (level2num_point_reads.find(level) != level2num_point_reads.end()) {
        tmp_num_point_reads = level2num_point_reads[level];
      }
      uint64_t num_entries_in_bf;
      uint64_t min_num_point_reads = 0;
      for (const FileMetaData* meta : input_files->at(i).files) {
        if (i == 0) {
          min_num_point_reads =
              round(meta->stats.start_global_point_read_number *
                    vstorage_->GetAvgNumPointReadsPerLvl0File());
        }
        std::pair<uint64_t, uint64_t> est_num_point_reads =
            meta->stats.GetEstimatedNumPointReads(
                vstorage_->GetAccumulatedNumPointReads(),
                ioptions_->point_read_learning_rate, -1, min_num_point_reads);
        fileID_in_compaction.insert(meta->fd.GetNumber());
        num_entries_in_bf = meta->num_entries - meta->num_range_deletions;
        num_entries_in_compaction_ += num_entries_in_bf;

        if (est_num_point_reads.first > est_num_point_reads.second ||
            est_num_point_reads.first == 0) {
          if (meta->bpk != -1) {
            num_bits_for_filter_to_be_removed_ += meta->filter_size * 8;
          } else {
            num_bits_for_filter_to_be_removed_ +=
                num_entries_in_bf * overall_bits_per_key_;
          }
        }

        if (est_num_point_reads.first == 0) continue;

        if (level == 0) {
          tmp_num_point_reads =
              std::max(tmp_num_point_reads, est_num_point_reads.first);
        } else {
          tmp_num_point_reads += est_num_point_reads.first;
        }
        num_existing_point_reads_in_compaction += est_num_point_reads.second;
      }
      level2num_point_reads.emplace(level, tmp_num_point_reads);
    }
    if (max_level > -1) {
      num_empty_queries_in_compaction =
          std::max(level2num_point_reads[max_level],
                   num_existing_point_reads_in_compaction) -
          num_existing_point_reads_in_compaction;
      if (max_level == 0 && no_filter_optimize_for_level0_) {
        num_empty_queries_in_compaction = 0;
        num_entries_in_compaction_ = 0;
      }
    }
  }

  int start_level = 0;
  if (no_filter_optimize_for_level0_) {
    start_level = 1;
  }
  uint64_t tmp_num_entries_in_filter_by_file = 0;
  for (int level = start_level; level < vstorage_->num_levels(); ++level) {
    for (auto* file_meta : vstorage_->LevelFiles(level)) {
      tmp_num_entries_in_filter_by_file =
          file_meta->num_entries - file_meta->num_range_deletions;
      agg_filter_size_ += file_meta->filter_size;
      if (tmp_num_entries_in_filter_by_file == 0) continue;
      if (compaction != nullptr &&
          fileID_in_compaction.find(file_meta->fd.GetNumber()) !=
              fileID_in_compaction.end())
        continue;
      uint64_t min_num_point_reads = 0;
      if (level == 0) {
        min_num_point_reads =
            round(file_meta->stats.start_global_point_read_number *
                  vstorage_->GetAvgNumPointReadsPerLvl0File());
      }
      std::pair<uint64_t, uint64_t> est_num_point_reads =
          file_meta->stats.GetEstimatedNumPointReads(
              vstorage_->GetAccumulatedNumPointReads(),
              ioptions_->point_read_learning_rate, -1, min_num_point_reads);
      num_point_reads = est_num_point_reads.first;
      num_existing_point_reads = est_num_point_reads.second;
      mnemosyne_plus_num_entries_ += tmp_num_entries_in_filter_by_file;
      // if (num_point_reads == 0) continue;
      if (num_existing_point_reads < num_point_reads) {
        num_empty_point_reads = num_point_reads - num_existing_point_reads;
        mnemosyne_plus_total_empty_queries_ += num_empty_point_reads;
        mnemosyne_plus_temp_sum_in_bpk_optimization_ +=
            std::log(tmp_num_entries_in_filter_by_file * 1.0 /
                     num_empty_point_reads) *
            tmp_num_entries_in_filter_by_file;
        mnemosyne_plus_num_entries_with_empty_queries_ +=
            tmp_num_entries_in_filter_by_file;
        if (file_meta->filter_size != 0) {
          file_meta->bpk =
              file_meta->filter_size * 8 /
              (file_meta->num_entries - file_meta->num_range_deletions);
        } else {
          file_meta->bpk = 0;
        }
        file_workload_state_pq_.push(
            FileWorkloadState(tmp_num_entries_in_filter_by_file,
                              num_empty_point_reads, file_meta));
      } else {
        file_meta->bpk = 0.0;
        if (num_point_reads != 0)
          agg_filter_size_ -=
              std::min(file_meta->filter_size, agg_filter_size_);
        // skipped_filter_size += file_meta->filter_size;
      }
    }
  }

  // Adjust according to statistics in compaction
  mnemosyne_plus_num_entries_ += num_entries_in_compaction_;
  mnemosyne_plus_total_empty_queries_ += num_empty_queries_in_compaction;
  if (num_entries_in_compaction_ > 0 && num_empty_queries_in_compaction > 0) {
    // assuming that the number of zero-result queries distributes uniformly
    // in the newly generated files
    mnemosyne_plus_num_entries_with_empty_queries_ +=
        num_entries_in_compaction_;
    mnemosyne_plus_temp_sum_in_bpk_optimization_ +=
        num_entries_in_compaction_ * std::log(num_entries_in_compaction_ * 1.0 /
                                              num_empty_queries_in_compaction);
    uint64_t avg_num_entries_in_compaction =
        num_entries_in_compaction_ * 1.0 / num_files_in_compaction;
    uint64_t avg_num_empty_queries_in_compaction =
        num_empty_queries_in_compaction * 1.0 / num_files_in_compaction;
    if (avg_num_entries_in_compaction < 1.0) {
      file_workload_state_pq_.push(
          FileWorkloadState(num_entries_in_compaction_,
                            num_empty_queries_in_compaction, nullptr));
    } else {
      for (size_t i = 0; i < num_files_in_compaction; i++) {
        file_workload_state_pq_.push(
            FileWorkloadState(avg_num_entries_in_compaction,
                              avg_num_empty_queries_in_compaction, nullptr));
      }
    }
  }

  total_bits_for_filter_ = mnemosyne_plus_num_entries_ * overall_bits_per_key_;
  if (mnemosyne_plus_total_empty_queries_ == 0 ||
      file_workload_state_pq_.empty()) {
    mnemosyne_plus_bpk_optimization_prepared_flag_ = true;
    return;
  }
  mnemosyne_plus_temp_sum_in_bpk_optimization_ +=
      std::log(mnemosyne_plus_total_empty_queries_) *
      mnemosyne_plus_num_entries_with_empty_queries_;
  mnemosyne_plus_common_constant_in_bpk_optimization_ =
      -(total_bits_for_filter_ * log_2_squared +
        mnemosyne_plus_temp_sum_in_bpk_optimization_) /
      mnemosyne_plus_num_entries_with_empty_queries_;
  double weight = 0.0;
  while (!file_workload_state_pq_.empty() &&
         std::log(file_workload_state_pq_.top().weight *
                  mnemosyne_plus_total_empty_queries_) +
                 mnemosyne_plus_common_constant_in_bpk_optimization_ >
             -log_2_squared) {
    if (file_workload_state_pq_.top().meta != nullptr) {
      file_workload_state_pq_.top().meta->bpk = 0.0;
      agg_filter_size_ -= std::min(
          agg_filter_size_, file_workload_state_pq_.top().meta->filter_size);
    }
    weight = file_workload_state_pq_.top().weight;
    mnemosyne_plus_temp_sum_in_bpk_optimization_ -=
        std::log(weight * mnemosyne_plus_total_empty_queries_) *
        file_workload_state_pq_.top().num_entries;
    mnemosyne_plus_num_entries_with_empty_queries_ -=
        file_workload_state_pq_.top().num_entries;
    mnemosyne_plus_temp_sum_in_bpk_optimization_ +=
        mnemosyne_plus_num_entries_with_empty_queries_ *
        std::log((mnemosyne_plus_total_empty_queries_ -
                  file_workload_state_pq_.top().num_empty_point_reads) *
                 1.0 / mnemosyne_plus_total_empty_queries_);
    mnemosyne_plus_total_empty_queries_ -=
        file_workload_state_pq_.top().num_empty_point_reads;
    mnemosyne_plus_common_constant_in_bpk_optimization_ =
        -(total_bits_for_filter_ * log_2_squared +
          mnemosyne_plus_temp_sum_in_bpk_optimization_) /
        mnemosyne_plus_num_entries_with_empty_queries_;
    file_workload_state_pq_.pop();
  }

  if (!file_workload_state_pq_.empty()) {
    mnemosyne_plus_bpk_weight_threshold_ = file_workload_state_pq_.top().weight;
  }

  if (mnemosyne_plus_common_constant_in_bpk_optimization_scaled_down_ > 0) {
    // if (vstorage_->GetBitsPerKeyCommonConstant() < 0 &&
    //  		  mnemosyne_plus_common_constant_in_bpk_optimization_ <
    //  vstorage_->GetBitsPerKeyCommonConstant()) {

    /*   mnemosyne_plus_common_constant_in_bpk_optimization_ =
           ioptions_->point_read_learning_rate *
               vstorage_->GetBitsPerKeyCommonConstant() +
           (1.0 - ioptions_->point_read_learning_rate) *
               mnemosyne_plus_common_constant_in_bpk_optimization_;*/
    mnemosyne_plus_common_constant_in_bpk_optimization_scaled_down_--;
  }

  mnemosyne_plus_bpk_optimization_prepared_flag_ = true;
}

bool BitsPerKeyAllocHelper::IfNeedAllocateBitsPerKey(
    const FileMetaData& meta, uint64_t num_entries_in_output_level,
    double* bits_per_key, bool is_last_level, const Compaction* compaction) {
  if (bpk_alloc_type_ == BitsPerKeyAllocationType::kDefaultBpkAlloc)
    return false;
  assert(bits_per_key);
  if (bpk_alloc_type_ == BitsPerKeyAllocationType::kNaiveMonkeyBpkAlloc) {
    *bits_per_key = naive_monkey_bpk;
    return true;
  }
  uint64_t num_entries = meta.num_entries - meta.num_range_deletions;
  double tmp_bits_per_key = overall_bits_per_key_;
  double weight = 0.0;
  if (bpk_alloc_type_ == BitsPerKeyAllocationType::kMnemosyneBpkAlloc ||
      (bpk_alloc_type_ == BitsPerKeyAllocationType::kMnemosynePlusBpkAlloc &&
       vstorage_->GetAccumulatedNumPointReads() == 0)) {
    if (!mnemosyne_bpk_optimization_prepared_flag_) {
      flush_flag_ = true;
      mnemosyne_temp_sum_in_bpk_optimization_ +=
          num_entries_in_output_level * std::log(num_entries_in_output_level);
      mnemosyne_num_entries_ += num_entries_in_output_level;
      level_states_pq_.push(
          LevelState(0, num_entries_in_output_level, meta.fd.GetNumber()));
      PrepareBpkAllocation(compaction);
    }

    // for bits-per-key < 1, give it 1 if it is larger than or equal to 0.5
    if (/*num_entries_in_output_level >
           mnemosyne_bpk_num_entries_threshold_ ||*/
        std::log(num_entries_in_output_level) +
            mnemosyne_common_constant_in_bpk_optimization_ >
        -log_2_squared * 0.5) {
      tmp_bits_per_key = 0;
    } else {
      tmp_bits_per_key =
          std::max(-(std::log(num_entries_in_output_level) +
                     mnemosyne_common_constant_in_bpk_optimization_) /
                       log_2_squared,
                   1.0);
    }
  } else if (bpk_alloc_type_ ==
             BitsPerKeyAllocationType::kMnemosynePlusBpkAlloc) {
    std::pair<uint64_t, uint64_t> est_num_point_reads =
        meta.stats.GetEstimatedNumPointReads(
            vstorage_->GetAccumulatedNumPointReads(),
            ioptions_->point_read_learning_rate);

    uint64_t num_point_reads = est_num_point_reads.first;
    if (num_point_reads == 0) {
      if (is_last_level) {
        *bits_per_key = 0;
        return true;
      } else {
        if (!compaction) return false;
        // using Mnemosyne to assign bpk if num_point_reads == 0 for files not
        // in the bottommest level
        PrepareForMnemosyne(compaction);
        bpk_alloc_type_ = BitsPerKeyAllocationType::kMnemosyneBpkAlloc;
        bool result =
            IfNeedAllocateBitsPerKey(meta, num_entries_in_output_level,
                                     bits_per_key, is_last_level, compaction);
        bpk_alloc_type_ = BitsPerKeyAllocationType::kMnemosynePlusBpkAlloc;
        // do not assign large bpk if num_point_reads == 0
        return result && bits_per_key != NULL &&
               *bits_per_key < overall_bits_per_key_;
      }
    }
    uint64_t num_existing_point_reads = est_num_point_reads.second;

    if (!mnemosyne_plus_bpk_optimization_prepared_flag_) {
      flush_flag_ = true;
      if (num_point_reads > num_existing_point_reads) {
        mnemosyne_plus_total_empty_queries_ =
            num_point_reads - num_existing_point_reads;
      }
      mnemosyne_plus_temp_sum_in_bpk_optimization_ +=
          num_entries *
          std::log(num_entries * 1.0 / mnemosyne_plus_total_empty_queries_);
      mnemosyne_plus_num_entries_ += num_entries;
      mnemosyne_plus_num_entries_with_empty_queries_ += num_entries;
      PrepareBpkAllocation();
    }

    if (mnemosyne_plus_total_empty_queries_ == 0 ||
        file_workload_state_pq_.empty())
      return false;

    if (num_existing_point_reads >= num_point_reads &&
        num_existing_point_reads > 0) {
      *bits_per_key = 0;
      return true;
    }

    weight = num_entries * 1.0 / (num_point_reads - num_existing_point_reads);
    // for bits-per-key < 1, give it 1 if it is larger than or equal to 0.5
    if (/*weight > mnemosyne_plus_bpk_weight_threshold_ ||*/
        std::log(weight * mnemosyne_plus_total_empty_queries_) +
            mnemosyne_plus_common_constant_in_bpk_optimization_ >
        -log_2_squared * 0.5) {
      tmp_bits_per_key = 0;
    } else {
      tmp_bits_per_key =
          std::max(-(std::log(weight * mnemosyne_plus_total_empty_queries_) +
                     mnemosyne_plus_common_constant_in_bpk_optimization_) /
                       log_2_squared,
                   1.0);
    }
  }

  // tmp_bits_per_key = std::min(tmp_bits_per_key, max_bits_per_key_);
  if (tmp_bits_per_key > max_bits_per_key_) {
    tmp_bits_per_key = max_bits_per_key_;
    if (bpk_alloc_type_ == BitsPerKeyAllocationType::kMnemosynePlusBpkAlloc) {
      /*double temp_mnemosyne_plus_common_constant_in_bpk_optimization_ =
          -(tmp_bits_per_key * log_2_squared) -
          std::log(weight * mnemosyne_plus_total_empty_queries_);
      mnemosyne_plus_common_constant_in_bpk_optimization_ =
          ioptions_->point_read_learning_rate *
              temp_mnemosyne_plus_common_constant_in_bpk_optimization_ +
          (1.0 - ioptions_->point_read_learning_rate) *
              mnemosyne_plus_common_constant_in_bpk_optimization_;*/
      mnemosyne_plus_common_constant_in_bpk_optimization_scaled_down_++;
    }
  }

  uint64_t old_total_bits =
      agg_filter_size_ * 8 - num_bits_for_filter_to_be_removed_;
  // if (old_total_bits > vstorage_->GetSkippedFilterSize() * 8) {
  //   old_total_bits -= vstorage_->GetSkippedFilterSize() * 8;
  // }
  uint64_t old_total_entries = total_num_entries_ - num_entries_in_compaction_;
  const double overused_bits_per_key = overall_bits_per_key_ + 1.0;
  // const double overused_bits_per_key = overall_bits_per_key_ * (1.0 + 0.2);
  if (old_total_entries == 0 ||
      old_total_bits > old_total_entries * overused_bits_per_key) {
    // if the old bits-per-key exceeds the overall bits-per-key (this should
    // rarely happen), we only care about if the assigned bits-per-key is larger
    // than
    if (tmp_bits_per_key > overused_bits_per_key) {
      return false;
    }
  } else if ((old_total_bits + tmp_bits_per_key * num_entries) * 1.0 /
                 (old_total_entries + num_entries) >
             overused_bits_per_key) {  // if the bits-per-key is overused

    tmp_bits_per_key =
        ((overused_bits_per_key) * (old_total_entries + num_entries) -
         old_total_bits) /
        num_entries;
  }
  *bits_per_key = tmp_bits_per_key;
  avg_curr_bits_per_key = (old_total_bits + tmp_bits_per_key * num_entries) *
                          1.0 / (old_total_entries + num_entries);
  return true;
}

void BitsPerKeyAllocHelper::UpdateAggStatistics(FileMetaData* file_meta) {
  if (bpk_alloc_type_ == BitsPerKeyAllocationType::kDefaultBpkAlloc ||
      bpk_alloc_type_ == BitsPerKeyAllocationType::kNaiveMonkeyBpkAlloc) {
    agg_filter_size_ = 0;
    total_num_entries_ = 0;
    for (int level = 0; level < vstorage_->num_levels(); ++level) {
      for (auto* meta : vstorage_->LevelFiles(level)) {
        agg_filter_size_ += meta->filter_size;
        total_num_entries_ += meta->num_entries - meta->num_range_deletions;
      }
    }
  }

  if (file_meta != NULL) {
    agg_filter_size_ += file_meta->filter_size;
    total_num_entries_ +=
        file_meta->num_entries - file_meta->num_range_deletions;
    if (total_num_entries_ > num_entries_in_compaction_) {
      avg_curr_bits_per_key =
          (agg_filter_size_ * 8 - num_bits_for_filter_to_be_removed_) * 1.0 /
          (total_num_entries_ - num_entries_in_compaction_);
    } else {
      avg_curr_bits_per_key =
          agg_filter_size_ * 8.0 /
          (file_meta->num_entries - file_meta->num_range_deletions);
    }
  }

  vstorage_->UpdateCurrentTotalFilterSize(agg_filter_size_);
}

};  // namespace ROCKSDB_NAMESPACE
