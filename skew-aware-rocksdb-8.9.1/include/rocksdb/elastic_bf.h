//  ElasticBF: Elastic Bloom Filter with Hotness Awareness (USENIX ATC'19),
//  re-implemented on top of RocksDB 8.9.1 / Mnemosyne. See ELASTICBF.md.
//
//  This header exposes run-time statistics of the ElasticBF manager so that
//  benchmarks can report them next to RocksDB / Mnemosyne numbers.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rocksdb/rocksdb_namespace.h"
#include "rocksdb/status.h"

namespace ROCKSDB_NAMESPACE {

class TableFactory;

struct ElasticBFStatistics {
  // ---- configuration actually in use ----
  double budget_bits_per_key = 0.0;
  double unit_bits_per_key = 0.0;
  uint32_t num_units = 0;
  uint32_t default_units = 0;
  uint64_t segment_size = 0;
  uint64_t life_time = 0;

  // ---- state (not reset by ResetElasticBFCounters) ----
  uint64_t current_time = 0;  // logical clock = number of Get requests
  uint64_t num_tables = 0;
  uint64_t num_segments = 0;
  uint64_t num_keys = 0;        // keys covered by registered segments
  double budget_bits = 0.0;     // budget_bits_per_key * num_keys
  double used_bits = 0.0;       // bits of resident (+ being loaded) units
  uint64_t num_expired_segments = 0;
  // segments_per_queue[i] = number of segments with exactly i resident units
  std::vector<uint64_t> segments_per_queue;

  // ---- counters (reset by ResetElasticBFCounters) ----
  uint64_t segment_probes = 0;     // segment filter-group checks
  uint64_t probe_negatives = 0;    // checks answered "definitely absent"
  uint64_t probe_positives = 0;    // checks answered "may exist"
  uint64_t unit_checks = 0;        // individual unit probes performed
  uint64_t promotions = 0;         // units enabled by the adjustment
  uint64_t demotions = 0;          // units disabled to fund promotions
  uint64_t budget_demotions = 0;   // units disabled to restore the budget
  uint64_t adjust_not_beneficial = 0;  // E[Extra_IO] would not decrease
  uint64_t adjust_no_expired = 0;      // no expired segment could donate
  uint64_t unit_loads = 0;         // on-demand unit loads (after open)
  uint64_t unit_load_bytes = 0;    // bytes read by on-demand unit loads
  uint64_t open_unit_loads = 0;    // units loaded while opening tables
  uint64_t open_unit_bytes = 0;    // bytes of those units
  uint64_t inherited_segments = 0; // segments that inherited hotness
  uint64_t unit_load_errors = 0;
  // probes_by_units[k] = segment checks done with exactly k resident units
  std::vector<uint64_t> probes_by_units;

  double EffectiveBitsPerKey() const {
    return num_keys == 0 ? 0.0 : used_bits / static_cast<double>(num_keys);
  }
  std::string ToString() const;
};

// Returns NotSupported if `factory` is not a block-based table factory with
// ElasticBF enabled.
Status GetElasticBFStatistics(const TableFactory* factory,
                              ElasticBFStatistics* stats);

// Resets the counters (not the hotness / resident-unit state).
Status ResetElasticBFCounters(const TableFactory* factory);

}  // namespace ROCKSDB_NAMESPACE
