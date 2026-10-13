// Correctness and sanity checks for the ElasticBF implementation
// (table/block_based/elastic_bf.*). Build with `make elasticbf_test` and run
// `./elasticbf_test [db_path]`. Exits non-zero on failure.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "rocksdb/cache.h"
#include "rocksdb/db.h"
#include "rocksdb/elastic_bf.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/iostats_context.h"
#include "rocksdb/options.h"
#include "rocksdb/perf_context.h"
#include "rocksdb/statistics.h"
#include "rocksdb/table.h"
#include "table/block_based/elastic_bf.h"

using namespace ROCKSDB_NAMESPACE;

static int failures = 0;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::cerr << "FAILED: " << msg << " (" #cond ")" << std::endl; \
      failures++;                                                     \
    }                                                                 \
  } while (0)

static std::string Key(uint64_t i) {
  char buf[32];
  snprintf(buf, sizeof(buf), "key%016llu", (unsigned long long)i);
  return buf;
}

// 1) A single unit has no false negatives and the expected FPR; k units
//    multiply the FPR (independent hash functions).
static void TestUnits() {
  const double bpk = 4.0;
  const uint32_t n = 6;
  std::vector<uint64_t> hashes;
  for (uint64_t i = 0; i < 20000; i++) {
    hashes.push_back(elastic_bf::KeyHash(Key(i * 2)));  // even keys
  }
  std::vector<elastic_bf::UnitFilter> units(n);
  for (uint32_t u = 0; u < n; u++) {
    Status s = elastic_bf::ParseUnit(elastic_bf::BuildUnit(hashes, u, bpk),
                                     &units[u]);
    CHECK(s.ok(), "parse unit");
  }
  for (uint64_t i = 0; i < 20000; i++) {
    uint64_t h = elastic_bf::KeyHash(Key(i * 2));
    for (uint32_t u = 0; u < n; u++) {
      CHECK(units[u].MayMatch(h, u), "false negative in unit");
    }
  }
  const uint64_t probes = 200000;
  std::vector<uint64_t> fp(n + 1, 0);  // fp[k]: false positives with k units
  for (uint64_t i = 0; i < probes; i++) {
    uint64_t h = elastic_bf::KeyHash(Key(i * 2 + 1));  // odd keys: absent
    uint32_t k = 0;
    while (k < n && units[k].MayMatch(h, k)) k++;
    for (uint32_t j = 1; j <= k; j++) fp[j]++;
  }
  const double r = std::exp(-bpk * std::log(2.0) * std::log(2.0));
  for (uint32_t k = 1; k <= 3; k++) {
    double measured = fp[k] * 1.0 / probes;
    double expected = std::pow(r, k);
    std::cout << "  units=" << k << " measured FPR=" << measured
              << " theoretical=" << expected << std::endl;
    CHECK(measured < expected * 1.6 + 1e-4, "unit FPR too high");
  }
}

static bool g_async = false;
static Options MakeOptions(bool elastic, double bpk,
                           std::shared_ptr<Statistics> stats) {
  Options options;
  options.create_if_missing = true;
  options.write_buffer_size = 1 << 20;  // 1 MB
  options.target_file_size_base = 1 << 20;
  options.max_bytes_for_level_base = 4 << 20;
  options.max_bytes_for_level_multiplier = 4;
  options.level0_file_num_compaction_trigger = 2;
  options.statistics = stats;
  options.compression = kNoCompression;
  BlockBasedTableOptions t;
  t.block_size = 4096;
  t.no_block_cache = true;  // every data-block access is an I/O
  if (elastic) {
    t.elastic_bf = true;
    t.elastic_bf_bits_per_key = bpk;
    t.elastic_bf_unit_bits_per_key = bpk;
    t.elastic_bf_num_units = 6;
    t.elastic_bf_segment_size = 128 << 10;  // 128 KB segments (~200 total)
    // paper: lifeTime in the order of the number of segments
    t.elastic_bf_life_time = 400;
    t.elastic_bf_async_load = g_async;
  } else {
    t.filter_policy.reset(NewBloomFilterPolicy(bpk, false));
  }
  options.table_factory.reset(NewBlockBasedTableFactory(t));
  return options;
}

struct RunResult {
  uint64_t false_positives = 0;
  uint64_t filter_negatives = 0;
};

static RunResult RunDB(const std::string& path, bool elastic,
                       ElasticBFStatistics* out_stats) {
  const double bpk = 4.0;
  const uint64_t num_keys = 120000;
  auto stats = CreateDBStatistics();
  Options options = MakeOptions(elastic, bpk, stats);
  DestroyDB(path, options);
  DB* db = nullptr;
  Status s = DB::Open(options, path, &db);
  CHECK(s.ok(), "open " + s.ToString());
  std::string value(200, 'v');
  // insert even keys in random order to create several levels
  std::vector<uint64_t> ids;
  for (uint64_t i = 0; i < num_keys; i++) ids.push_back(i * 2);
  std::mt19937_64 rng(42);
  std::shuffle(ids.begin(), ids.end(), rng);
  for (uint64_t id : ids) {
    s = db->Put(WriteOptions(), Key(id), value);
    CHECK(s.ok(), "put");
  }
  db->Flush(FlushOptions());
  db->WaitForCompact(WaitForCompactOptions());

  // every existing key must be found (no false negatives)
  std::string v;
  uint64_t missing = 0;
  for (uint64_t i = 0; i < num_keys; i += 7) {
    if (!db->Get(ReadOptions(), Key(i * 2), &v).ok()) missing++;
  }
  CHECK(missing == 0, "existing keys not found: " + std::to_string(missing));

  {
    std::string lv;
    db->GetProperty("rocksdb.levelstats", &lv);
    std::cout << lv;
  }
  // skewed zero-result lookups: 90% of them hit 5% of the key space
  get_perf_context()->Reset();
  get_perf_context()->EnablePerLevelPerfContext();
  SetPerfLevel(PerfLevel::kEnableCount);
  std::uniform_int_distribution<uint64_t> all(0, num_keys - 1);
  std::uniform_int_distribution<uint64_t> hot(0, num_keys / 20);
  std::uniform_real_distribution<double> coin(0, 1);
  for (int i = 0; i < 150000; i++) {
    uint64_t id = coin(rng) < 0.9 ? hot(rng) : all(rng);
    db->Get(ReadOptions(), Key(id * 2 + 1), &v);  // odd keys: absent
  }
  {
    uint64_t useful = 0, positive = 0;
    if (get_perf_context()->level_to_perf_context) {
      for (auto& kv : *get_perf_context()->level_to_perf_context) {
        useful += kv.second.bloom_filter_useful;
        positive += kv.second.bloom_filter_full_positive;
      }
    }
    std::cout << "  table probes: useful=" << useful << " positive=" << positive
              << std::endl;
  }
  RunResult res;
  res.false_positives = get_perf_context()->bloom_sst_hit_count;
  res.filter_negatives = get_perf_context()->bloom_sst_miss_count;
  SetPerfLevel(PerfLevel::kDisable);

  if (elastic) {
    ElasticBFStatistics st;
    s = GetElasticBFStatistics(options.table_factory.get(), &st);
    CHECK(s.ok(), "stats");
    std::cout << st.ToString();
    CHECK(st.num_segments > 0, "segments registered");
    CHECK(st.promotions > 0, "hot segments got promoted");
    CHECK(st.demotions > 0, "expired segments donated units");
    // memory budget: resident units never exceed the budget
    CHECK(st.used_bits <= st.budget_bits + 1e-6, "memory budget exceeded");
    *out_stats = st;
  }

  // write more data so that compactions run while hotness is known
  for (uint64_t i = 0; i < num_keys / 2; i++) {
    db->Put(WriteOptions(), Key(num_keys * 2 + i * 2), value);
  }
  db->Flush(FlushOptions());
  db->WaitForCompact(WaitForCompactOptions());
  if (elastic) {
    ElasticBFStatistics st;
    GetElasticBFStatistics(options.table_factory.get(), &st);
    std::cout << "  after more compactions: inherited_segments="
              << st.inherited_segments << " used/budget=" << st.used_bits
              << "/" << st.budget_bits << std::endl;
    CHECK(st.inherited_segments > 0, "compaction outputs inherited hotness");
    CHECK(st.used_bits <= st.budget_bits + 1e-6,
          "memory budget exceeded after compactions");
  }
  missing = 0;
  for (uint64_t i = 0; i < num_keys + num_keys / 2; i += 5) {
    if (!db->Get(ReadOptions(), Key(i * 2), &v).ok()) missing++;
  }
  CHECK(missing == 0, "existing keys not found after compactions: " +
                          std::to_string(missing));
  delete db;

  // reopen: tables re-register with the default number of units
  s = DB::Open(options, path, &db);
  CHECK(s.ok(), "reopen");
  missing = 0;
  for (uint64_t i = 0; i < num_keys; i += 11) {
    if (!db->Get(ReadOptions(), Key(i * 2), &v).ok()) missing++;
  }
  CHECK(missing == 0, "existing keys not found after reopen");
  delete db;
  DestroyDB(path, options);
  return res;
}

int main(int argc, char** argv) {
  std::string path = argc > 1 ? argv[1] : "./elasticbf_test_db";
  std::cout << "[1] filter units" << std::endl;
  TestUnits();
  std::cout << "[2] RocksDB baseline (4 bits-per-key)" << std::endl;
  ElasticBFStatistics st;
  RunResult base = RunDB(path + "-rocksdb", false, &st);
  std::cout << "  false positives=" << base.false_positives
            << " filtered=" << base.filter_negatives << std::endl;
  std::cout << "[3] ElasticBF (4 bits-per-key budget, 6 x 4-bpk units)"
            << std::endl;
  RunResult ebf = RunDB(path + "-elasticbf", true, &st);
  std::cout << "  false positives=" << ebf.false_positives
            << " filtered=" << ebf.filter_negatives << std::endl;
  // The number of table probes depends on the LSM layout produced by
  // background compactions, so compare false-positive *rates*.
  double base_fpr = base.false_positives * 1.0 /
                    (base.false_positives + base.filter_negatives);
  double ebf_fpr =
      ebf.false_positives * 1.0 / (ebf.false_positives + ebf.filter_negatives);
  std::cout << "false-positive rate: RocksDB=" << base_fpr
            << " ElasticBF=" << ebf_fpr << std::endl;
  CHECK(ebf_fpr < base_fpr,
        "ElasticBF should reduce the false-positive rate on a skewed workload");
  std::cout << "[4] ElasticBF with background (asynchronous) unit loading"
            << std::endl;
  g_async = true;
  RunResult ebf_async = RunDB(path + "-elasticbf-async", true, &st);
  double async_fpr = ebf_async.false_positives * 1.0 /
                     (ebf_async.false_positives + ebf_async.filter_negatives);
  std::cout << "false-positive rate: ElasticBF(async)=" << async_fpr
            << std::endl;
  CHECK(async_fpr < base_fpr, "async ElasticBF should reduce the FPR");
  if (failures == 0) {
    std::cout << "ALL ELASTICBF TESTS PASSED" << std::endl;
    return 0;
  }
  std::cout << failures << " FAILURE(S)" << std::endl;
  return 1;
}
