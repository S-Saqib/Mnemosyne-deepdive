#ifndef STATS_HH
#define STATS_HH
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include "envir/env.hh"
#include <cmath>


const double log_2_squared = std::pow(std::log(2), 2);
double evaluate_fpr(const double bpk);

inline double evaluate_fpr(const double bpk) {
  return std::exp( - (bpk) * log_2_squared);
}

struct DbStats {
  uint32_t fst_level_with_entries = 0;
  uint32_t num_levels = 0;
  uint32_t num_entries = 0;
  uint32_t num_files = 0;
  uint64_t num_total_fp_queries = 0;
  std::vector<uint64_t> level2entries;
  std::vector<uint64_t> entries_in_level0;
  std::unordered_map<uint64_t, uint64_t> fileID2fp_queries; // number of false positive queries for a fileID
  std::unordered_map<uint64_t, uint64_t> fileID2entries;    // number of entries for a fileID
  double evaluate_total_fpr(const std::unordered_map<uint64_t, double> &fileID2bpk) {
    double fpr = 0.0;
    for(auto iter = fileID2fp_queries.begin(); iter!=fileID2fp_queries.end(); iter ++) {
      fpr += iter->second * evaluate_fpr(fileID2bpk.at(iter->first));
    }
    if(num_total_fp_queries == 0.0f) return 1.0f;
    return fpr / num_total_fp_queries;
  }
};

class Get_BPK {
public:
  Get_BPK(Env *_env) : _env(_env) {}
  virtual std::unordered_map<uint64_t, double> GetOptBPK(DbStats db_stats) = 0;
  Env *_env;
};

class Scan_To_Get_OPTBPK: public Get_BPK {
public:
  Scan_To_Get_OPTBPK(Env *_env) : Get_BPK(_env) {}
  std::unordered_map<uint64_t, double> GetOptBPK(DbStats db_stats);
};

class Scan_To_Get_OPTBPK_No_Normalize: public Get_BPK {
public:
  Scan_To_Get_OPTBPK_No_Normalize(Env *_env) : Get_BPK(_env) {}
  std::unordered_map<uint64_t, double> GetOptBPK(DbStats db_stats);
};

class Method_Of_Monkey: public Get_BPK {
  public:
  Method_Of_Monkey(Env *_env) : Get_BPK(_env) {}
  std::unordered_map<uint64_t, double> GetOptBPK(DbStats db_stats);
};

class Binary_Search_Method: public Get_BPK {
  public:
  Binary_Search_Method(Env *_env) : Get_BPK(_env) {}
  std::unordered_map<uint64_t, double> GetOptBPK(DbStats db_stats);
};

class Scan_To_Get_OPTBPK_Debug: public Get_BPK {
public:
  Scan_To_Get_OPTBPK_Debug(Env *_env) : Get_BPK(_env) {}
  std::unordered_map<uint64_t, double> GetOptBPK(DbStats db_stats);
};

#endif