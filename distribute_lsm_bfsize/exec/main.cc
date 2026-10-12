#include <iostream>
#include "stats/stats.hh"
#include "envir/env.hh"
#include <random>
#include "cmdline.hh"
#include <chrono>
Env env(1);

DbStats stats;

void print_bpk_distribution(const std::unordered_map<uint64_t, double> &di, std::string info)  {
  std::cout << info << std::endl;
  std::cout << "bpk_distribution: [" << std::endl;
  int cnt = 0;
  for(auto iter = di.begin(); iter != di.end(); iter ++) {
    cnt ++;
    if(cnt == 5) {
      fprintf(stdout, "...\n");
      break;
    }
    fprintf(stdout, "fid: %3d, bpk %2.5f, entries: %4d, total_bits: %10.3f\n", iter->first, iter->second, stats.fileID2entries[iter->first], stats.fileID2entries[iter->first] * iter->second);
    // std::cerr << "  fid: " << iter->first << ", bpk: " << iter->second << ", entries " << stats.fileID2entries[iter->first] << ", total_bits: " << stats.fileID2entries[iter->first] * iter->second << std::endl;
  }
  std::cout << "]" << std::endl;
  std::cout << "fpr: " << stats.evaluate_total_fpr(di) << std::endl;
  std::cout << std::endl;
}

inline void read_and_inititate(FILE *fp, Env &env, DbStats &db_stats) {
  fscanf(fp, "%d%d%d", &db_stats.num_entries, &env.bits_per_key, &db_stats.num_files);
  for(int i = 0; i < db_stats.num_files; i ++) {
    int fid, entries, fp_queries;
    fscanf(fp, "%d%d%d", &fid, &entries, &fp_queries);
    db_stats.fileID2entries[fid] = entries;
    db_stats.fileID2fp_queries[fid] = fp_queries;
    db_stats.num_total_fp_queries += fp_queries;
    // db_stats.num_entries += entries;
  }
  db_stats.num_levels = 2;
}

int main(int argc, char **argv) {
  cmdline::parser psr = cmdline::parser();
  psr.add<std::string>("fname", 'f', "fname", true, "");
  psr.add<int>("rounds", 'r', "rounds", false, 10);
  psr.add<int>("bytesperkey", 'b', "bytesperkey", false, -1);
  bool ok_ = psr.parse(argc, argv);
  if(!ok_) {
    std::cerr << "INVALID ARGS" << std::endl;
    return 0;
  }
  std::string fname = psr.get<std::string>("fname");
  FILE* Fp = stdin;
  if(fname != "") Fp = fopen(fname.c_str(), "r");
  read_and_inititate(Fp, env, stats);
  int rounds = psr.get<int>("rounds");
  int __bpk__ = psr.get<int>("bytesperkey");
  if(__bpk__ != -1) {
    env.bits_per_key = __bpk__;
  }
  // std::cerr << "start" << std::endl;
  Get_BPK *p = static_cast<Get_BPK*> (new Method_Of_Monkey(&env));
  Get_BPK *p1 = static_cast<Get_BPK*> (new Binary_Search_Method(&env));
  Get_BPK *p2 = static_cast<Get_BPK*> (new Scan_To_Get_OPTBPK(&env));
  // std::cerr << "using_p" << std::endl;
  double total_fpr_p = 0, total_fpr_p1 = 0, total_fpr_p2 = 0;
  double total_time_p = 0, total_time_p1 = 0, total_time_p2 = 0;
  for(int i = 0; i < rounds; i ++) {
    auto start = std::chrono::high_resolution_clock::now();
    
    auto x = p->GetOptBPK(stats);
    total_fpr_p += stats.evaluate_total_fpr(x);
    // print_bpk_distribution(x, "monkey");
    
    auto end1 = std::chrono::high_resolution_clock::now();
    
    auto x1 = p1->GetOptBPK(stats);
    total_fpr_p1 += stats.evaluate_total_fpr(x1);
    
    auto end2 = std::chrono::high_resolution_clock::now();

    auto x2 = p2->GetOptBPK(stats);
    total_fpr_p2 += stats.evaluate_total_fpr(x2);
    
    auto end3 = std::chrono::high_resolution_clock::now();

    total_time_p += std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start).count() / 1e9;
    total_time_p1 += std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - end1).count() / 1e9;
    total_time_p2 += std::chrono::duration_cast<std::chrono::nanoseconds>(end3 - end2).count() / 1e9;
    
    // print_bpk_distribution(x, "monkey");
    // print_bpk_distribution(x1, "binary_search");
    // print_bpk_distribution(x2, "scan");
  }
  std::cerr << "avgFpr Monkey:         " << total_fpr_p / rounds << std::endl;
  std::cerr << "avgFpr BinarySearch:   " << total_fpr_p1 / rounds << std::endl;
  std::cerr << "avgFpr ScanNormalized: " << total_fpr_p2 / rounds << std::endl;
  std::cerr << "avgTime Monkey:        " << total_time_p / rounds << std::endl;
  std::cerr << "avgTime BinarySearch:  " << total_time_p1 / rounds << std::endl;
  std::cerr << "avgTime ScanNormalized:" << total_time_p2 / rounds << std::endl;
  Get_BPK *px = static_cast<Get_BPK*> (new Scan_To_Get_OPTBPK_Debug(&env));
  px->GetOptBPK(stats);
  std::cerr << "number of Files:       " << stats.num_files << std::endl;
}