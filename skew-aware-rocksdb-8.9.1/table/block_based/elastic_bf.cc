//  ElasticBF (Li et al., USENIX ATC 2019) on top of RocksDB 8.9.1 / Mnemosyne.
//  See elastic_bf.h and ELASTICBF.md.

#include "table/block_based/elastic_bf.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <sstream>

#include "table/block_based/block_based_table_factory.h"
#include "table/block_based/block_based_table_reader.h"
#include "table/block_based/filter_policy_internal.h"
#include "util/bloom_impl.h"
#include "util/coding.h"
#include "util/hash.h"

namespace ROCKSDB_NAMESPACE {

namespace elastic_bf {

const char* const kIndexBlockName = "rocksdb.elasticbf.index";

namespace {
constexpr uint32_t kIndexFormatVersion = 1;
constexpr uint64_t kKeyHashSeed = 0x5eed0e1a571cbfULL;

inline uint64_t Fmix64(uint64_t k) {
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccdULL;
  k ^= k >> 33;
  k *= 0xc4ceb9fe1a85ec53ULL;
  k ^= k >> 33;
  return k;
}

inline uint32_t UnitLenBytes(uint64_t num_keys, double bits_per_key) {
  // FastLocalBloom works on 64-byte cache lines. Round to the *nearest*
  // number of cache lines (not up) so that, on average, a unit uses exactly
  // bits_per_key bits per key and ElasticBF gets the same memory as the
  // static filters it is compared with.
  double lines = static_cast<double>(num_keys) * bits_per_key / 512.0;
  uint64_t n = static_cast<uint64_t>(std::llround(lines));
  if (n < 1) n = 1;
  return static_cast<uint32_t>(n * 64);
}

inline int UnitNumProbes(double bits_per_key) {
  int millibits = static_cast<int>(std::lround(bits_per_key * 1000.0));
  return FastLocalBloomImpl::ChooseNumProbes(std::max(millibits, 1));
}
}  // namespace

uint64_t KeyHash(const Slice& user_key) {
  return Hash64(user_key.data(), user_key.size(), kKeyHashSeed);
}

uint64_t UnitHash(uint64_t key_hash, uint32_t unit) {
  return Fmix64(key_hash +
                0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(unit) + 1));
}

std::string BuildUnit(const std::vector<uint64_t>& key_hashes, uint32_t unit,
                      double bits_per_key) {
  uint32_t len_bytes = UnitLenBytes(key_hashes.size(), bits_per_key);
  int num_probes = UnitNumProbes(bits_per_key);
  std::string out(len_bytes, '\0');
  char* data = &out[0];
  for (uint64_t h : key_hashes) {
    uint64_t uh = UnitHash(h, unit);
    FastLocalBloomImpl::AddHash(Lower32of64(uh), Upper32of64(uh), len_bytes,
                                num_probes, data);
  }
  out.push_back(static_cast<char>(num_probes));
  return out;
}

bool UnitFilter::MayMatch(uint64_t key_hash, uint32_t unit) const {
  uint64_t uh = UnitHash(key_hash, unit);
  return FastLocalBloomImpl::HashMayMatch(
      Lower32of64(uh), Upper32of64(uh), static_cast<uint32_t>(data.size()),
      num_probes, data.data());
}

Status ParseUnit(std::string&& contents, UnitFilter* out) {
  if (contents.size() < 65 || (contents.size() - 1) % 64 != 0) {
    return Status::Corruption("ElasticBF: bad filter unit size");
  }
  out->num_probes = static_cast<unsigned char>(contents.back());
  contents.pop_back();
  if (out->num_probes <= 0 || out->num_probes > 30) {
    return Status::Corruption("ElasticBF: bad filter unit probes");
  }
  out->data = std::move(contents);
  return Status::OK();
}

void EncodeIndex(const SegmentIndex& index, std::string* dst) {
  PutFixed32(dst, kIndexFormatVersion);
  PutVarint32(dst, index.num_units);
  uint64_t bpk_bits;
  static_assert(sizeof(bpk_bits) == sizeof(index.unit_bits_per_key), "");
  memcpy(&bpk_bits, &index.unit_bits_per_key, sizeof(bpk_bits));
  PutFixed64(dst, bpk_bits);
  PutVarint32(dst, static_cast<uint32_t>(index.segments.size()));
  for (const auto& seg : index.segments) {
    PutLengthPrefixedSlice(dst, seg.smallest);
    PutLengthPrefixedSlice(dst, seg.largest);
    PutVarint64(dst, seg.num_keys);
    assert(seg.units.size() == index.num_units);
    for (const auto& h : seg.units) {
      h.EncodeTo(dst);
    }
  }
}

Status DecodeIndex(const Slice& src, SegmentIndex* index) {
  Slice in = src;
  uint32_t version = 0;
  if (!GetFixed32(&in, &version) || version != kIndexFormatVersion) {
    return Status::Corruption("ElasticBF: bad index version");
  }
  uint32_t num_units = 0;
  uint64_t bpk_bits = 0;
  uint32_t num_segs = 0;
  if (!GetVarint32(&in, &num_units) || !GetFixed64(&in, &bpk_bits) ||
      !GetVarint32(&in, &num_segs) || num_units == 0 ||
      num_units > kMaxUnits) {
    return Status::Corruption("ElasticBF: bad index header");
  }
  index->num_units = num_units;
  memcpy(&index->unit_bits_per_key, &bpk_bits, sizeof(bpk_bits));
  index->segments.clear();
  index->segments.resize(num_segs);
  for (uint32_t i = 0; i < num_segs; i++) {
    SegmentMeta& seg = index->segments[i];
    Slice smallest, largest;
    if (!GetLengthPrefixedSlice(&in, &smallest) ||
        !GetLengthPrefixedSlice(&in, &largest) ||
        !GetVarint64(&in, &seg.num_keys)) {
      return Status::Corruption("ElasticBF: bad segment entry");
    }
    seg.smallest = smallest.ToString();
    seg.largest = largest.ToString();
    seg.units.resize(num_units);
    for (uint32_t u = 0; u < num_units; u++) {
      Status s = seg.units[u].DecodeFrom(&in);
      if (!s.ok()) return s;
    }
  }
  return Status::OK();
}

Config ResolveConfig(const BlockBasedTableOptions& opts) {
  Config c;
  c.budget_bits_per_key = opts.elastic_bf_bits_per_key;
  if (c.budget_bits_per_key <= 0.0 && opts.filter_policy != nullptr) {
    const char* name = opts.filter_policy->Name();
    if (strcmp(name, BloomFilterPolicy::kClassName()) == 0 ||
        strcmp(name, RibbonFilterPolicy::kClassName()) == 0) {
      c.budget_bits_per_key =
          static_cast<const BloomLikeFilterPolicy*>(opts.filter_policy.get())
              ->GetBitsPerKey();
    }
  }
  if (c.budget_bits_per_key <= 0.0) {
    // ASSUMPTION: RocksDB's default bits-per-key when nothing is given.
    c.budget_bits_per_key = 10.0;
  }
  c.unit_bits_per_key = opts.elastic_bf_unit_bits_per_key > 0.0
                            ? opts.elastic_bf_unit_bits_per_key
                            : c.budget_bits_per_key;
  c.num_units = std::max<uint32_t>(
      1, std::min<uint32_t>(opts.elastic_bf_num_units, kMaxUnits));
  // ASSUMPTION: the paper does not state how many units are loaded by default;
  // we load as many as fit in the per-key budget (1 with the paper's setting
  // of a 4 bits-per-key budget and 4 bits-per-key units).
  double d = std::floor(c.budget_bits_per_key / c.unit_bits_per_key + 1e-9);
  c.default_units = static_cast<uint32_t>(
      std::max(0.0, std::min<double>(d, c.num_units)));
  c.segment_size = std::max<uint64_t>(opts.elastic_bf_segment_size, 1);
  c.life_time = opts.elastic_bf_life_time;
  c.async_load = opts.elastic_bf_async_load;
  c.inheritance = opts.elastic_bf_hotness_inheritance;
  return c;
}

}  // namespace elastic_bf

using elastic_bf::kMaxUnits;

namespace {
// ASSUMPTION (implementation bound): the paper scans expired segments from
// Qn down to Q1, each queue from LRU to MRU. To keep the per-Get cost bounded
// we inspect at most this many expired candidates per adjustment.
constexpr size_t kMaxDonorScan = 256;
}  // namespace

ElasticBFManager::ElasticBFManager(const BlockBasedTableOptions& opts)
    : cfg_(elastic_bf::ResolveConfig(opts)) {
  // Per-unit false positive rate, r = e^{-b' (ln 2)^2} ~= 0.6185^{b'}
  // (paper Sec 3.2). The group FPR with i units enabled is r^i.
  const double r = std::exp(-cfg_.unit_bits_per_key * std::log(2.0) *
                            std::log(2.0));
  r_pow_[0] = 1.0;
  for (uint32_t i = 1; i <= kMaxUnits; i++) r_pow_[i] = r_pow_[i - 1] * r;
  queues_.resize(cfg_.num_units + 1);
  if (cfg_.async_load) {
    loader_ = std::thread(&ElasticBFManager::LoaderLoop, this);
  }
}

ElasticBFManager::~ElasticBFManager() {
  {
    std::lock_guard<std::mutex> l(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (loader_.joinable()) loader_.join();
}

void ElasticBFManager::RemoveFromQueueLocked(SegmentState* s) {
  if (!s->in_queue) return;
  queues_[s->enabled()].erase(s->qpos);
  s->in_queue = false;
}

// Queues are ordered by last_access (front = LRU, back = MRU).
void ElasticBFManager::InsertSortedFromFrontLocked(SegmentState* s) {
  auto& q = queues_[s->enabled()];
  auto it = q.begin();
  while (it != q.end() && (*it)->last_access <= s->last_access) ++it;
  s->qpos = q.insert(it, s);
  s->in_queue = true;
}

void ElasticBFManager::InsertSortedFromBackLocked(SegmentState* s) {
  auto& q = queues_[s->enabled()];
  auto it = q.end();
  while (it != q.begin()) {
    auto prev = std::prev(it);
    if ((*prev)->last_access <= s->last_access) break;
    it = prev;
  }
  s->qpos = q.insert(it, s);
  s->in_queue = true;
}

ElasticBFManager::TableState* ElasticBFManager::RegisterTable(
    uint64_t file_number, const Comparator* ucmp,
    elastic_bf::SegmentIndex&& index, const UnitReader& open_reader,
    UnitReader reader) {
  auto* ts = new TableState();
  ts->file_number = file_number;
  ts->ucmp = ucmp;
  ts->index = std::move(index);
  ts->reader = std::move(reader);
  const uint32_t n = std::min(ts->index.num_units, cfg_.num_units);
  ts->segs.reserve(ts->index.segments.size());
  for (const auto& meta : ts->index.segments) {
    auto seg = std::make_unique<SegmentState>();
    seg->table = ts;
    seg->meta = &meta;
    // bits that one unit occupies in memory (bloom bytes, excl. probes byte)
    seg->unit_bits =
        meta.units.empty()
            ? 0.0
            : static_cast<double>(meta.units[0].size() - 1) * 8.0;
    // Budget of the segment = memory of the equivalent static configuration:
    // its default units (with their cache-line rounding) plus the per-key
    // budget left over when the budget is not a multiple of the unit size.
    const uint32_t d = std::min(cfg_.default_units, ts->index.num_units);
    seg->budget_bits =
        d * seg->unit_bits +
        std::max(0.0, cfg_.budget_bits_per_key - d * cfg_.unit_bits_per_key) *
            static_cast<double>(meta.num_keys);
    ts->segs.push_back(std::move(seg));
  }

  // ---- plan the initial number of units of every segment ----
  std::vector<uint32_t> targets(ts->segs.size(),
                                std::min(cfg_.default_units, n));
  {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& seg : ts->segs) {
      budget_bits_ += seg->budget_bits;
      num_keys_ += seg->meta->num_keys;
      num_segments_++;
    }
    std::shared_ptr<InheritanceSource> src;
    auto it = pending_inheritance_.find(file_number);
    if (it != pending_inheritance_.end()) {
      src = it->second;
      pending_inheritance_.erase(it);
    }
    // ASSUMPTION: a new segment has f = 0 and lastAccessedTime = currentTime
    // at creation, i.e. it gets one lifeTime to prove it is hot before it can
    // donate its units (unless it inherits hotness below).
    const uint64_t now = current_time_.load(std::memory_order_relaxed);
    for (auto& seg : ts->segs) seg->last_access = now;
    if (src && cfg_.inheritance) {
      ApplyInheritanceLocked(ts, *src, &targets);
    }
    const uint32_t d = std::min(cfg_.default_units, n);
    if (src && cfg_.inheritance) {
      // Memory-conserving allocation for compaction outputs: the outputs of
      // a compaction may use the memory that its inputs used, adjusted by the
      // change of budget (number of keys). Units go to the hottest (inherited
      // f_i) segments first: first up to the default, then up to the
      // inherited target. ASSUMPTION (see ELASTICBF.md).
      double avail = src->available_bits;
      for (auto& seg : ts->segs) avail += seg->budget_bits;
      std::vector<size_t> order(ts->segs.size());
      for (size_t i = 0; i < order.size(); i++) order[i] = i;
      std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return ts->segs[a]->freq > ts->segs[b]->freq;
      });
      std::vector<uint32_t> alloc(ts->segs.size(), 0);
      for (int pass = 0; pass < 2; pass++) {
        for (size_t i : order) {
          SegmentState* seg = ts->segs[i].get();
          const uint32_t cap =
              pass == 0 ? std::min(targets[i], d) : std::min(targets[i], n);
          while (alloc[i] < cap && avail >= seg->unit_bits) {
            alloc[i]++;
            avail -= seg->unit_bits;
          }
        }
      }
      // = previous available + budget of this table - units given to it
      src->available_bits = avail;
      targets = std::move(alloc);
    } else {
      // flush outputs / tables opened at start-up: default units
      for (auto& t : targets) t = std::min(t, d);
    }
    for (size_t i = 0; i < ts->segs.size(); i++) {
      SegmentState* seg = ts->segs[i].get();
      seg->pending = targets[i];
      used_bits_ += targets[i] * seg->unit_bits;
    }
  }

  // ---- load them (normally served from the tail prefetch buffer) ----
  std::vector<std::vector<std::shared_ptr<const elastic_bf::UnitFilter>>>
      loaded(ts->segs.size());
  uint64_t loads = 0, bytes = 0;
  for (size_t i = 0; i < ts->segs.size(); i++) {
    for (uint32_t u = 0; u < targets[i]; u++) {
      const BlockHandle& h = ts->segs[i]->meta->units[u];
      std::string contents;
      Status s = open_reader(h, &contents);
      auto unit = std::make_shared<elastic_bf::UnitFilter>();
      if (s.ok()) s = elastic_bf::ParseUnit(std::move(contents), unit.get());
      if (!s.ok()) {
        unit_load_errors_.fetch_add(1, std::memory_order_relaxed);
        break;
      }
      loads++;
      bytes += h.size() + BlockBasedTable::kBlockTrailerSize;
      loaded[i].push_back(std::move(unit));
    }
  }

  // ---- install ----
  {
    std::lock_guard<std::mutex> l(mu_);
    for (size_t i = 0; i < ts->segs.size(); i++) {
      SegmentState* seg = ts->segs[i].get();
      used_bits_ -= (seg->pending - loaded[i].size()) * seg->unit_bits;
      seg->pending = 0;
      seg->units = std::move(loaded[i]);
      InsertSortedFromFrontLocked(seg);
    }
    tables_.insert(ts);
    tables_by_number_[file_number] = ts;
    ShrinkToBudgetLocked(current_time_.load(std::memory_order_relaxed));
  }
  open_unit_loads_.fetch_add(loads, std::memory_order_relaxed);
  open_unit_bytes_.fetch_add(bytes, std::memory_order_relaxed);
  return ts;
}

void ElasticBFManager::UnregisterTable(TableState* ts) {
  std::unique_lock<std::mutex> l(mu_);
  ts->alive = false;
  // drop queued (not started) loads of this table
  for (auto it = tasks_.begin(); it != tasks_.end();) {
    if (it->ts == ts) {
      it = tasks_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto& seg : ts->segs) {
    RemoveFromQueueLocked(seg.get());
    used_bits_ -= (seg->enabled() + seg->pending) * seg->unit_bits;
    budget_bits_ -= seg->budget_bits;
    num_keys_ -= seg->meta->num_keys;
    num_segments_--;
  }
  reserved_bits_ -= ts->reserved_bits;
  release_bits_ -= ts->release_bits;
  ts->reserved_bits = 0.0;
  ts->release_bits = 0.0;
  tables_.erase(ts);
  auto it = tables_by_number_.find(ts->file_number);
  if (it != tables_by_number_.end() && it->second == ts) {
    tables_by_number_.erase(it);
  }
  cv_.wait(l, [ts] { return ts->inflight == 0; });
  ShrinkToBudgetLocked(current_time_.load(std::memory_order_relaxed));
  l.unlock();
  delete ts;
}

void ElasticBFManager::DemoteLocked(SegmentState* s) {
  assert(s->enabled() > 0);
  RemoveFromQueueLocked(s);
  s->units.pop_back();
  used_bits_ -= s->unit_bits;
  // keep the queue ordered by last access (demoted segments are usually
  // expired, i.e. near the LRU end)
  InsertSortedFromFrontLocked(s);
  demotions_.fetch_add(1, std::memory_order_relaxed);
}

// The paper keeps the memory of resident units fixed. Registering compaction
// outputs and dropping their inputs can leave the manager above budget: e.g.
// units that cold segments donated to hot ones are not given back when the
// cold segments are compacted away, inherited unit counts are rounded, and
// updates shrink the number of keys (hence the budget). The budget is
// restored right away by disabling, one at a time, the unit whose removal
// increases E[Extra_IO] = sum f_i r_i the least per bit, searching
//   1. expired segments (the only donors the paper allows), then
//   2. non-expired segments that have been accessed (f_i > 0), then
//   3. any segment.
// Step 2 skips brand-new segments (f_i = 0, not expired) because their
// hotness is unknown rather than zero. ASSUMPTION: the paper does not
// describe this step.
void ElasticBFManager::ShrinkToBudgetLocked(uint64_t now) {
  // The budget is checked against the state after the inputs of running
  // compactions are deleted, so that the units their outputs took over from
  // them are not counted twice.
  while (ProjectedOverLocked() > 0.5) {
    SegmentState* best = nullptr;
    double best_cost = 0.0;
    for (int pass = 0; pass < 3 && best == nullptr; pass++) {
      for (uint32_t q = 1; q < queues_.size(); q++) {
        for (SegmentState* s : queues_[q]) {
          const bool expired = ExpiredLocked(s, now);
          if (pass == 0 && !expired) break;  // queues ordered by last access
          if (s->pending > 0 || s->unit_bits <= 0.0) continue;
          if (pass == 1 && (expired || s->freq <= 0.0)) continue;
          const double cost =
              s->freq * (r_pow_[q - 1] - r_pow_[q]) / s->unit_bits;
          if (best == nullptr || cost < best_cost) {
            best = s;
            best_cost = cost;
          }
        }
      }
    }
    if (best == nullptr) break;
    DemoteLocked(best);
    demotions_.fetch_sub(1, std::memory_order_relaxed);
    budget_demotions_.fetch_add(1, std::memory_order_relaxed);
  }
}

// Paper Sec 3.4: on an access, check whether enabling one more unit for the
// accessed segment while disabling units of expired segments (searched from
// Qn down to Q1, LRU to MRU) reduces E[Extra_IO] = sum_i f_i * r_i.
bool ElasticBFManager::MaybePromoteLocked(TableState* ts, SegmentState* seg,
                                          uint64_t now, LoadTask* task) {
  const uint32_t n = std::min(ts->index.num_units, cfg_.num_units);
  const uint32_t k = seg->enabled();
  if (seg->pending > 0 || k >= n || seg->unit_bits <= 0.0) return false;
  const double need = seg->unit_bits;
  const double gain = seg->freq * (r_pow_[k] - r_pow_[k + 1]);
  double free_bits = budget_bits_ - used_bits_ - reserved_bits_;

  SegmentState* victims[kMaxUnits * 4];
  size_t num_victims = 0;
  double loss = 0.0;
  bool saw_expired = false;
  size_t scanned = 0;
  if (free_bits < need) {
    for (uint32_t q = static_cast<uint32_t>(queues_.size()) - 1;
         q >= 1 && free_bits < need && scanned < kMaxDonorScan; q--) {
      for (SegmentState* s : queues_[q]) {
        if (!ExpiredLocked(s, now)) break;  // ordered by last_access
        if (++scanned > kMaxDonorScan) break;
        if (s == seg || s->pending > 0) continue;
        saw_expired = true;
        const double delta = s->freq * (r_pow_[q - 1] - r_pow_[q]);
        if (gain > loss + delta &&
            num_victims < sizeof(victims) / sizeof(victims[0])) {
          victims[num_victims++] = s;
          loss += delta;
          free_bits += s->unit_bits;
          if (free_bits >= need) break;
        }
      }
    }
    if (free_bits < need) {
      if (saw_expired) {
        adjust_not_beneficial_.fetch_add(1, std::memory_order_relaxed);
      } else {
        adjust_no_expired_.fetch_add(1, std::memory_order_relaxed);
      }
      return false;
    }
  }
  for (size_t i = 0; i < num_victims; i++) DemoteLocked(victims[i]);
  seg->pending = 1;
  used_bits_ += need;
  promotions_.fetch_add(1, std::memory_order_relaxed);
  task->ts = ts;
  task->seg = seg;
  task->unit = k;
  return true;
}

bool ElasticBFManager::KeyMayMatch(TableState* ts, const Slice& user_key) {
  const auto& segs = ts->segs;
  if (segs.empty()) return true;
  // segment whose key range covers the key: first with largest >= key
  size_t lo = 0, hi = segs.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (ts->ucmp->Compare(Slice(segs[mid]->meta->largest), user_key) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  segment_probes_.fetch_add(1, std::memory_order_relaxed);
  if (lo == segs.size()) {
    // larger than every point key of the table
    probe_negatives_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  SegmentState* seg = segs[lo].get();

  std::shared_ptr<const elastic_bf::UnitFilter> units[kMaxUnits];
  uint32_t k = 0;
  LoadTask task;
  bool sync_task = false;
  {
    std::lock_guard<std::mutex> l(mu_);
    const uint64_t now = current_time_.load(std::memory_order_relaxed);
    // expiring policy: lastAccessedTime = currentTime,
    // expiredTime = lastAccessedTime + lifeTime
    seg->freq += 1.0;
    seg->last_access = now;
    RemoveFromQueueLocked(seg);
    seg->qpos = queues_[seg->enabled()].insert(queues_[seg->enabled()].end(),
                                               seg);  // MRU
    seg->in_queue = true;
    k = seg->enabled();
    for (uint32_t u = 0; u < k; u++) units[u] = seg->units[u];
    if (MaybePromoteLocked(ts, seg, now, &task)) {
      if (cfg_.async_load) {
        tasks_.push_back(task);
        cv_.notify_all();
      } else {
        ts->inflight++;
        sync_task = true;
      }
    }
  }

  probes_by_units_[k].fetch_add(1, std::memory_order_relaxed);
  const uint64_t h = elastic_bf::KeyHash(user_key);
  bool may_match = true;
  uint32_t checks = 0;
  for (uint32_t u = 0; u < k; u++) {
    checks++;
    if (!units[u]->MayMatch(h, u)) {
      may_match = false;
      break;
    }
  }
  unit_checks_.fetch_add(checks, std::memory_order_relaxed);
  if (may_match) {
    probe_positives_.fetch_add(1, std::memory_order_relaxed);
  } else {
    probe_negatives_.fetch_add(1, std::memory_order_relaxed);
  }
  if (sync_task) DoLoad(task);
  return may_match;
}

void ElasticBFManager::DoLoad(const LoadTask& task) {
  const BlockHandle& h = task.seg->meta->units[task.unit];
  std::string contents;
  Status s = task.ts->reader(h, &contents);
  auto unit = std::make_shared<elastic_bf::UnitFilter>();
  if (s.ok()) s = elastic_bf::ParseUnit(std::move(contents), unit.get());
  {
    std::lock_guard<std::mutex> l(mu_);
    task.ts->inflight--;
    if (task.ts->alive) {
      SegmentState* seg = task.seg;
      seg->pending = 0;
      if (s.ok() && seg->enabled() == task.unit) {
        RemoveFromQueueLocked(seg);
        seg->units.push_back(std::move(unit));
        InsertSortedFromBackLocked(seg);
        unit_loads_.fetch_add(1, std::memory_order_relaxed);
        unit_load_bytes_.fetch_add(h.size() + BlockBasedTable::kBlockTrailerSize,
                                   std::memory_order_relaxed);
      } else {
        used_bits_ -= seg->unit_bits;
        unit_load_errors_.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
  cv_.notify_all();
}

void ElasticBFManager::LoaderLoop() {
  std::unique_lock<std::mutex> l(mu_);
  while (true) {
    cv_.wait(l, [this] { return stop_ || !tasks_.empty(); });
    if (tasks_.empty()) {
      if (stop_) break;
      continue;
    }
    LoadTask task = tasks_.front();
    tasks_.pop_front();
    task.ts->inflight++;
    l.unlock();
    DoLoad(task);
    l.lock();
  }
}

// Paper Sec 3.3: a segment produced by compaction takes the mean hotness of
// the old segments whose key ranges overlap with it, and enables units
// according to that hotness.
void ElasticBFManager::ApplyInheritanceLocked(TableState* ts,
                                              const InheritanceSource& src,
                                              std::vector<uint32_t>* targets) {
  const uint32_t n = std::min(ts->index.num_units, cfg_.num_units);
  for (size_t i = 0; i < ts->segs.size(); i++) {
    SegmentState* seg = ts->segs[i].get();
    double sum_freq = 0.0, sum_units = 0.0;
    uint64_t max_last = 0;
    size_t cnt = 0;
    Slice smallest(seg->meta->smallest), largest(seg->meta->largest);
    for (const auto& old : src.segs) {
      if (ts->ucmp->Compare(smallest, Slice(old.largest)) <= 0 &&
          ts->ucmp->Compare(Slice(old.smallest), largest) <= 0) {
        sum_freq += old.freq;
        sum_units += old.enabled;
        max_last = std::max(max_last, old.last_access);
        cnt++;
      }
    }
    if (cnt == 0) continue;
    // ASSUMPTION: hotness = (mean f_i, latest lastAccessedTime); the number
    // of units "according to the hotness" = mean number of units the
    // overlapping old segments had (as tuned by the MQ), rounded.
    const bool was_queued = seg->in_queue;
    if (was_queued) RemoveFromQueueLocked(seg);
    seg->freq = sum_freq / cnt;
    seg->last_access = max_last;
    if (was_queued) InsertSortedFromFrontLocked(seg);
    if (targets != nullptr) {
      (*targets)[i] = static_cast<uint32_t>(
          std::min<double>(n, std::lround(sum_units / cnt)));
    }
    inherited_segments_.fetch_add(1, std::memory_order_relaxed);
  }
}

void ElasticBFManager::PrepareInheritance(
    const std::vector<uint64_t>& input_file_numbers,
    const std::vector<uint64_t>& output_file_numbers) {
  if (!cfg_.inheritance || output_file_numbers.empty()) return;
  auto src = std::make_shared<InheritanceSource>();
  std::lock_guard<std::mutex> l(mu_);
  for (uint64_t fn : input_file_numbers) {
    auto it = tables_by_number_.find(fn);
    if (it == tables_by_number_.end()) continue;
    // Memory these inputs lent to other segments (budget - used) is free
    // budget only until they are deleted: reserve it so that promotions do not
    // spend it in the meantime.
    {
      double tb = 0.0, tu = 0.0;
      for (const auto& seg : it->second->segs) {
        tb += seg->budget_bits;
        tu += (seg->enabled() + seg->pending) * seg->unit_bits;
      }
      reserved_bits_ -= it->second->reserved_bits;
      release_bits_ -= it->second->release_bits;
      it->second->reserved_bits = std::max(0.0, tb - tu);
      it->second->release_bits = std::max(0.0, tu - tb);
      reserved_bits_ += it->second->reserved_bits;
      release_bits_ += it->second->release_bits;
    }
    for (const auto& seg : it->second->segs) {
      src->available_bits +=
          (seg->enabled() + seg->pending) * seg->unit_bits - seg->budget_bits;
      InheritedSegment is;
      is.smallest = seg->meta->smallest;
      is.largest = seg->meta->largest;
      is.freq = seg->freq;
      is.last_access = seg->last_access;
      is.enabled = seg->enabled();
      src->segs.push_back(std::move(is));
    }
  }
  if (src->segs.empty()) return;
  if (pending_inheritance_.size() > 65536) pending_inheritance_.clear();
  for (uint64_t out : output_file_numbers) {
    auto it = tables_by_number_.find(out);
    if (it != tables_by_number_.end()) {
      // already opened: inherit hotness only (units are adjusted later by
      // the regular MQ mechanism)
      ApplyInheritanceLocked(it->second, *src, nullptr);
    } else {
      pending_inheritance_[out] = src;
    }
  }
}

void ElasticBFManager::GetStatistics(ElasticBFStatistics* st) {
  *st = ElasticBFStatistics();
  st->budget_bits_per_key = cfg_.budget_bits_per_key;
  st->unit_bits_per_key = cfg_.unit_bits_per_key;
  st->num_units = cfg_.num_units;
  st->default_units = cfg_.default_units;
  st->segment_size = cfg_.segment_size;
  st->life_time = cfg_.life_time;
  {
    std::lock_guard<std::mutex> l(mu_);
    const uint64_t now = current_time_.load(std::memory_order_relaxed);
    st->current_time = now;
    st->num_tables = tables_.size();
    st->num_segments = num_segments_;
    st->num_keys = num_keys_;
    st->budget_bits = budget_bits_;
    st->used_bits = used_bits_;
    st->segments_per_queue.resize(queues_.size());
    for (size_t q = 0; q < queues_.size(); q++) {
      st->segments_per_queue[q] = queues_[q].size();
      for (SegmentState* s : queues_[q]) {
        if (!ExpiredLocked(s, now)) break;
        st->num_expired_segments++;
      }
    }
  }
  st->segment_probes = segment_probes_.load();
  st->probe_negatives = probe_negatives_.load();
  st->probe_positives = probe_positives_.load();
  st->unit_checks = unit_checks_.load();
  st->promotions = promotions_.load();
  st->demotions = demotions_.load();
  st->budget_demotions = budget_demotions_.load();
  st->probes_by_units.resize(cfg_.num_units + 1);
  for (uint32_t i = 0; i <= cfg_.num_units; i++) {
    st->probes_by_units[i] = probes_by_units_[i].load();
  }
  st->adjust_not_beneficial = adjust_not_beneficial_.load();
  st->adjust_no_expired = adjust_no_expired_.load();
  st->unit_loads = unit_loads_.load();
  st->unit_load_bytes = unit_load_bytes_.load();
  st->open_unit_loads = open_unit_loads_.load();
  st->open_unit_bytes = open_unit_bytes_.load();
  st->inherited_segments = inherited_segments_.load();
  st->unit_load_errors = unit_load_errors_.load();
}

void ElasticBFManager::ResetCounters() {
  for (auto* c :
       {&segment_probes_, &probe_negatives_, &probe_positives_, &unit_checks_,
        &promotions_, &demotions_, &adjust_not_beneficial_, &adjust_no_expired_,
        &unit_loads_, &unit_load_bytes_, &open_unit_loads_, &open_unit_bytes_,
        &inherited_segments_, &unit_load_errors_, &budget_demotions_}) {
    c->store(0);
  }
  for (auto& c : probes_by_units_) c.store(0);
}

ElasticBFManager* GetElasticBFManager(const TableFactory* factory) {
  if (factory == nullptr ||
      strcmp(factory->Name(), TableFactory::kBlockBasedTableName()) != 0) {
    return nullptr;
  }
  return static_cast<const BlockBasedTableFactory*>(factory)
      ->elastic_bf_manager();
}

std::string ElasticBFStatistics::ToString() const {
  std::ostringstream os;
  os << "ElasticBF config: budget_bpk=" << budget_bits_per_key
     << " unit_bpk=" << unit_bits_per_key << " units=" << num_units
     << " default_units=" << default_units << " segment_size=" << segment_size
     << " life_time=" << life_time << "\n";
  os << "ElasticBF state: current_time=" << current_time
     << " tables=" << num_tables << " segments=" << num_segments
     << " keys=" << num_keys << " budget_bits=" << static_cast<uint64_t>(budget_bits)
     << " used_bits=" << static_cast<uint64_t>(used_bits)
     << " effective_bpk=" << EffectiveBitsPerKey()
     << " expired_segments=" << num_expired_segments << "\n";
  os << "ElasticBF segments per #units:";
  for (size_t i = 0; i < segments_per_queue.size(); i++) {
    os << " Q" << i << "=" << segments_per_queue[i];
  }
  os << "\n";
  os << "ElasticBF counters: segment_probes=" << segment_probes
     << " negatives=" << probe_negatives << " positives=" << probe_positives
     << " unit_checks=" << unit_checks << " promotions=" << promotions
     << " demotions=" << demotions << " budget_demotions=" << budget_demotions
     << " adjust_not_beneficial=" << adjust_not_beneficial
     << " adjust_no_expired=" << adjust_no_expired
     << " unit_loads=" << unit_loads << " unit_load_bytes=" << unit_load_bytes
     << " open_unit_loads=" << open_unit_loads
     << " open_unit_bytes=" << open_unit_bytes
     << " inherited_segments=" << inherited_segments
     << " unit_load_errors=" << unit_load_errors << "\n";
  os << "ElasticBF probes per #units:";
  for (size_t i = 0; i < probes_by_units.size(); i++) {
    os << " U" << i << "=" << probes_by_units[i];
  }
  os << "\n";
  return os.str();
}

Status GetElasticBFStatistics(const TableFactory* factory,
                              ElasticBFStatistics* stats) {
  ElasticBFManager* m = GetElasticBFManager(factory);
  if (m == nullptr) return Status::NotSupported("ElasticBF not enabled");
  m->GetStatistics(stats);
  return Status::OK();
}

Status ResetElasticBFCounters(const TableFactory* factory) {
  ElasticBFManager* m = GetElasticBFManager(factory);
  if (m == nullptr) return Status::NotSupported("ElasticBF not enabled");
  m->ResetCounters();
  return Status::OK();
}

}  // namespace ROCKSDB_NAMESPACE
