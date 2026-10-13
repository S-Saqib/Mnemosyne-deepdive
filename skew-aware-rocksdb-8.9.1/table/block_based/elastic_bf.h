//  ElasticBF (Li et al., USENIX ATC 2019) on top of RocksDB 8.9.1 / Mnemosyne.
//
//  Paper concepts and where they live in this file:
//    * filter unit          -> elastic_bf::BuildUnit / UnitFilter
//    * segment + filter group
//                           -> elastic_bf::SegmentMeta (on-disk index entry)
//                              ElasticBFManager::SegmentState (run-time state)
//    * expiring policy      -> SegmentState::last_access, Expired()
//                              (currentTime = number of Get requests)
//    * E[Extra_IO] = sum f_i * r_i cost model and adjustment
//                           -> ElasticBFManager::MaybePromoteLocked
//    * Multi-Queue Q0..Qn   -> ElasticBFManager::queues_
//    * background loading   -> ElasticBFManager::LoaderLoop
//    * hotness inheritance  -> ElasticBFManager::PrepareInheritance
//
//  Every assumption that is not spelled out in the paper is marked with
//  "ASSUMPTION" and collected in ELASTICBF.md.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rocksdb/comparator.h"
#include "rocksdb/elastic_bf.h"
#include "rocksdb/slice.h"
#include "rocksdb/status.h"
#include "rocksdb/table.h"
#include "table/format.h"

namespace ROCKSDB_NAMESPACE {

class TableFactory;

namespace elastic_bf {

// Name of the meta block holding the segment index of an SST.
extern const char* const kIndexBlockName;

constexpr uint32_t kMaxUnits = 16;

// 64-bit hash of a user key (computed once per key).
uint64_t KeyHash(const Slice& user_key);
// Hash used by unit `unit` derived from the key hash. Every unit uses a
// different, independent hash function (paper Sec 3.2). ASSUMPTION: units are
// made independent by remixing one 64-bit XXH3 key hash with a per-unit seed
// through a 64-bit finalizer instead of hashing the key n times.
uint64_t UnitHash(uint64_t key_hash, uint32_t unit);

// Builds the serialized contents of a filter unit (a cache-local Bloom filter,
// the same FastLocalBloom implementation RocksDB uses for its full filters,
// so that baseline and ElasticBF differ only in allocation).
//   layout: [bloom bytes (multiple of 64)][num_probes (1 byte)]
std::string BuildUnit(const std::vector<uint64_t>& key_hashes, uint32_t unit,
                      double bits_per_key);

struct UnitFilter {
  std::string data;  // bloom bytes only
  int num_probes = 0;
  bool MayMatch(uint64_t key_hash, uint32_t unit) const;
};
Status ParseUnit(std::string&& contents, UnitFilter* out);

// One entry of the per-SST segment index.
struct SegmentMeta {
  std::string smallest;  // smallest user key in the segment
  std::string largest;   // largest user key in the segment
  uint64_t num_keys = 0; // distinct user keys added to the units
  std::vector<BlockHandle> units;  // one handle per filter unit
};

struct SegmentIndex {
  double unit_bits_per_key = 0.0;
  uint32_t num_units = 0;
  std::vector<SegmentMeta> segments;
};
void EncodeIndex(const SegmentIndex& index, std::string* dst);
Status DecodeIndex(const Slice& src, SegmentIndex* index);

// Resolved configuration (from BlockBasedTableOptions).
struct Config {
  double budget_bits_per_key = 0.0;
  double unit_bits_per_key = 0.0;
  uint32_t num_units = 0;
  uint32_t default_units = 0;
  uint64_t segment_size = 0;
  uint64_t life_time = 0;
  bool async_load = true;
  bool inheritance = true;
};
Config ResolveConfig(const BlockBasedTableOptions& opts);

}  // namespace elastic_bf

class ElasticBFManager {
 public:
  // Reads the raw contents of a block (without trailer) into *out.
  using UnitReader = std::function<Status(const BlockHandle&, std::string*)>;

  struct TableState;
  struct SegmentState {
    TableState* table = nullptr;
    const elastic_bf::SegmentMeta* meta = nullptr;
    double unit_bits = 0.0;  // in-memory bits of one unit of this segment
    double budget_bits = 0.0;  // budget contributed by this segment
    // Resident units: always the prefix [0, units.size()) of the group.
    std::vector<std::shared_ptr<const elastic_bf::UnitFilter>> units;
    uint32_t pending = 0;     // units reserved and being loaded
    double freq = 0.0;        // f_i : access frequency
    uint64_t last_access = 0; // expiredTime = last_access + lifeTime
    bool in_queue = false;
    std::list<SegmentState*>::iterator qpos;
    uint32_t enabled() const { return static_cast<uint32_t>(units.size()); }
  };
  struct TableState {
    uint64_t file_number = 0;
    const Comparator* ucmp = nullptr;
    elastic_bf::SegmentIndex index;
    std::vector<std::unique_ptr<SegmentState>> segs;
    UnitReader reader;
    bool alive = true;
    int inflight = 0;
    // While this table is the input of a running compaction (see
    // PrepareInheritance): reserved_bits = budget it lent to other segments
    // (budget - used > 0), which disappears from the free budget when it is
    // deleted; release_bits = memory it uses beyond its budget (used - budget
    // > 0), which the compaction outputs may take over.
    double reserved_bits = 0.0;
    double release_bits = 0.0;
  };

  explicit ElasticBFManager(const BlockBasedTableOptions& opts);
  ~ElasticBFManager();

  ElasticBFManager(const ElasticBFManager&) = delete;
  ElasticBFManager& operator=(const ElasticBFManager&) = delete;

  const elastic_bf::Config& config() const { return cfg_; }

  // currentTime++ : called once per user Get request.
  void Tick() { current_time_.fetch_add(1, std::memory_order_relaxed); }

  // Registers an opened SST. `open_reader` is used (synchronously, during this
  // call) to load the initial units, typically from the tail prefetch buffer.
  // `reader` must stay valid until UnregisterTable returns.
  TableState* RegisterTable(uint64_t file_number, const Comparator* ucmp,
                            elastic_bf::SegmentIndex&& index,
                            const UnitReader& open_reader, UnitReader reader);
  void UnregisterTable(TableState* ts);

  // Filter check for `user_key` in table `ts`; also updates hotness and may
  // trigger a unit adjustment.
  bool KeyMayMatch(TableState* ts, const Slice& user_key);

  // Called by a compaction once its output files are written and before they
  // are opened: remembers the hotness of the input segments so that the
  // output segments can inherit it when they are registered.
  void PrepareInheritance(const std::vector<uint64_t>& input_file_numbers,
                          const std::vector<uint64_t>& output_file_numbers);

  void GetStatistics(ElasticBFStatistics* stats);
  void ResetCounters();

 private:
  struct LoadTask {
    TableState* ts = nullptr;
    SegmentState* seg = nullptr;
    uint32_t unit = 0;
  };
  struct InheritedSegment {
    std::string smallest, largest;
    double freq = 0.0;
    uint64_t last_access = 0;
    uint32_t enabled = 0;
  };
  struct InheritanceSource {
    std::vector<InheritedSegment> segs;
    // Memory the outputs of the compaction may still use beyond their own
    // budget: (bits used by the inputs) - (budget of the inputs) - (bits
    // allocated to registered outputs) + (budget of registered outputs).
    double available_bits = 0.0;
  };

  bool ExpiredLocked(const SegmentState* s, uint64_t now) const {
    return now > s->last_access + cfg_.life_time;
  }
  void InsertSortedFromFrontLocked(SegmentState* s);
  void InsertSortedFromBackLocked(SegmentState* s);
  void RemoveFromQueueLocked(SegmentState* s);
  bool MaybePromoteLocked(TableState* ts, SegmentState* seg, uint64_t now,
                          LoadTask* task);
  void DemoteLocked(SegmentState* s);
  void ShrinkToBudgetLocked(uint64_t now);
  void DoLoad(const LoadTask& task);  // REQUIRES: task.ts->inflight was ++'d
  void LoaderLoop();
  void ApplyInheritanceLocked(TableState* ts, const InheritanceSource& src,
                              std::vector<uint32_t>* targets);

  const elastic_bf::Config cfg_;
  double r_pow_[elastic_bf::kMaxUnits + 1];  // r^i, r = per-unit FPR

  std::atomic<uint64_t> current_time_{0};

  std::mutex mu_;
  std::condition_variable cv_;
  std::unordered_set<TableState*> tables_;
  std::unordered_map<uint64_t, TableState*> tables_by_number_;
  std::unordered_map<uint64_t, std::shared_ptr<InheritanceSource>>
      pending_inheritance_;
  std::vector<std::list<SegmentState*>> queues_;  // Q0..Qn, front = LRU
  double budget_bits_ = 0.0;
  double used_bits_ = 0.0;
  double reserved_bits_ = 0.0;
  double release_bits_ = 0.0;
  // used - budget once all current compaction inputs are deleted
  double ProjectedOverLocked() const {
    return used_bits_ - budget_bits_ - release_bits_ + reserved_bits_;
  }
  uint64_t num_keys_ = 0;
  uint64_t num_segments_ = 0;

  std::deque<LoadTask> tasks_;
  bool stop_ = false;
  std::thread loader_;

  // counters
  std::atomic<uint64_t> segment_probes_{0}, probe_negatives_{0},
      probe_positives_{0}, unit_checks_{0}, promotions_{0}, demotions_{0},
      adjust_not_beneficial_{0}, adjust_no_expired_{0}, unit_loads_{0},
      unit_load_bytes_{0}, open_unit_loads_{0}, open_unit_bytes_{0},
      inherited_segments_{0}, unit_load_errors_{0}, budget_demotions_{0};
  std::atomic<uint64_t> probes_by_units_[elastic_bf::kMaxUnits + 1] = {};
};

// RAII handle owned by a BlockBasedTable.
class ElasticBFTable {
 public:
  ElasticBFTable(std::shared_ptr<ElasticBFManager> mgr,
                 ElasticBFManager::TableState* ts)
      : mgr_(std::move(mgr)), ts_(ts) {}
  ~ElasticBFTable() {
    if (mgr_ && ts_) mgr_->UnregisterTable(ts_);
  }
  bool KeyMayMatch(const Slice& user_key) {
    return mgr_->KeyMayMatch(ts_, user_key);
  }

 private:
  std::shared_ptr<ElasticBFManager> mgr_;
  ElasticBFManager::TableState* ts_;
};

// Returns the ElasticBF manager of a block-based table factory, or nullptr.
ElasticBFManager* GetElasticBFManager(const TableFactory* factory);

}  // namespace ROCKSDB_NAMESPACE
