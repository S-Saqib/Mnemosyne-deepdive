#include <algorithm>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include "stats/stats.hh"
#include "envir/env.hh"
#include <cmath>

using namespace std;


unordered_map<uint64_t, double> Scan_To_Get_OPTBPK_Debug::GetOptBPK(DbStats db_stats) {
  if (db_stats.num_levels <= 1) return {}; // do nothing when L <= 1
  uint64_t total_filter_memory = db_stats.num_entries * _env->bits_per_key;
  // solve \sum - ln p_i / (ln 2)^2 * n_i = M where n_i is the number of entries per file
  // plugged with Lagrange multiplier we have p_i * z_i / n_i should be a constant C where z_i is the number of non-existing
  // queries per file. Let C = p_i * z_i / n_i, we try to solve C first and then calculate p_i per file
  // std:: cout << "Total memory: " << total_filter_memory << std::endl;
  // let S = \sum (ln n_i / z_i) * n_i, we have - (\sum z_i) * C - S = M * (ln 2)^2

  // p_i refers to the false positive rate of the i-th file, fp_queries represent the number of empty queries
  double S = 0;
  uint64_t fileID = 0;
  uint64_t fp_queries = 0;
  uint64_t num_entries_with_fp_queries = 0;
  std::priority_queue<pair<double, uint64_t> > entries_over_fp_with_fileID;
  double tmp;
  // fileID2entries represents the map between fileID and the number of entries of the associated file
  // fileID2fp_enrties represents the number of empty queries of the associated file
  for(auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
    if (db_stats.fileID2fp_queries.find(iter->first) != db_stats.fileID2fp_queries.end()) {
      fp_queries = db_stats.fileID2fp_queries.at(iter->first);
      if (fp_queries != 0 && iter->second != 0) {
	      tmp = iter->second * 1.0 / db_stats.fileID2fp_queries.at(iter->first);
        // NOTE: tmp * db_stats.num_total_fp_queries = n_i / z_i
        // std::cout << " fileID: " << iter->first << " tmp: " << tmp << std::endl;
        S += std::log(tmp * db_stats.num_total_fp_queries * 1.0) * iter->second;
	      num_entries_with_fp_queries += iter->second;
	      entries_over_fp_with_fileID.push(make_pair(tmp, iter->first));
      }      
    }
  }
  double num_total_fp_queries = db_stats.num_total_fp_queries;
  double C = -(total_filter_memory * log_2_squared + S) * 1.0 / num_entries_with_fp_queries; 
  // C = ln(lambda), require: lambda * z_i / n_i > 0.0

  // final bpk assignments are stored in fileID2bpk
  unordered_map<uint64_t, double> fileID2bpk;
  // fileID with no bpk will also be stored in fileIDwithNobpk
  unordered_set<uint64_t> fileIDwithNobpk;

  if(entries_over_fp_with_fileID.empty()) {
    for (auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
      fileID2bpk.emplace(iter->first, _env->bits_per_key);
    }
    std::cerr << "loop_cnt:              " << 0 << std::endl;
    return fileID2bpk;
  }
  // Calculating S = \sum (ln z_i / n_i) * z_i
  int __debug_cnt = 0; // for DEBUG
  while (
      !entries_over_fp_with_fileID.empty() && 
      std::log(entries_over_fp_with_fileID.top().first * num_total_fp_queries) + C > 0.0 // this implies current ln(z_i / n_i * lambda) > 0.0
    ) {
    ++ __debug_cnt;
    uint64_t fileID = entries_over_fp_with_fileID.top().second;
    S -= std::log(entries_over_fp_with_fileID.top().first * num_total_fp_queries) * db_stats.fileID2entries.at(fileID);
    num_entries_with_fp_queries -= db_stats.fileID2entries.at(fileID);

    /* normalize z_i */
    S += num_entries_with_fp_queries * std::log((num_total_fp_queries - db_stats.fileID2fp_queries.at(fileID)) * 1.0 / num_total_fp_queries);
    num_total_fp_queries -= db_stats.fileID2fp_queries.at(fileID);

    fileIDwithNobpk.insert(fileID);
    fileID2bpk.emplace(fileID, 0.0);
    C = -(total_filter_memory * log_2_squared + S) / num_entries_with_fp_queries; // updated C
    entries_over_fp_with_fileID.pop();
    // std::cerr << "log(entries_over_fp_with_fileID.top().first) + C : " << std::log(entries_over_fp_with_fileID.top().first) + C << std::endl;
  }
  double bpk = 0.0;
  double final_total_memory = 0.0;
  for (auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
    if (db_stats.fileID2fp_queries.find(iter->first) != db_stats.fileID2fp_queries.end()) {
      fp_queries = db_stats.fileID2fp_queries.at(iter->first);
      if (fp_queries != 0 && fileIDwithNobpk.find(iter->first) == fileIDwithNobpk.end()) {
        bpk = -(std::log(iter->second * num_total_fp_queries * 1.0 / fp_queries) + C) / log_2_squared;
        fileID2bpk.emplace(iter->first, bpk);
        final_total_memory += bpk * iter->second;
      } else {
        fileID2bpk.emplace(iter->first, 0.0);
      }
    }
  }
  std::cerr << "loop_cnt:              " << __debug_cnt << std::endl;
  return fileID2bpk;
}

unordered_map<uint64_t, double> Scan_To_Get_OPTBPK::GetOptBPK(DbStats db_stats) {
  
  if (db_stats.num_levels <= 1) return {}; // do nothing when L <= 1
  uint64_t total_filter_memory = db_stats.num_entries * _env->bits_per_key;
  // solve \sum - ln p_i / (ln 2)^2 * n_i = M where n_i is the number of entries per file
  // plugged with Lagrange multiplier we have p_i * z_i / n_i should be a constant C where z_i is the number of non-existing
  // queries per file. Let C = p_i * z_i / n_i, we try to solve C first and then calculate p_i per file
  // std:: cout << "Total memory: " << total_filter_memory << std::endl;
  // let S = \sum (ln n_i / z_i) * n_i, we have - (\sum z_i) * C - S = M * (ln 2)^2

  // p_i refers to the false positive rate of the i-th file, fp_queries represent the number of empty queries
  double S = 0;
  uint64_t fileID = 0;
  uint64_t fp_queries = 0;
  uint64_t num_entries_with_fp_queries = 0;
  std::priority_queue<pair<double, uint64_t> > entries_over_fp_with_fileID;
  double tmp;
  // fileID2entries represents the map between fileID and the number of entries of the associated file
  // fileID2fp_enrties represents the number of empty queries of the associated file
  for(auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
    if (db_stats.fileID2fp_queries.find(iter->first) != db_stats.fileID2fp_queries.end()) {
      fp_queries = db_stats.fileID2fp_queries.at(iter->first);
      if (fp_queries != 0 && iter->second != 0) {
	      tmp = iter->second * 1.0 / db_stats.fileID2fp_queries.at(iter->first);
        // NOTE: tmp * db_stats.num_total_fp_queries = n_i / z_i
        // std::cout << " fileID: " << iter->first << " tmp: " << tmp << std::endl;
        S += std::log(tmp * db_stats.num_total_fp_queries * 1.0) * iter->second;
	      num_entries_with_fp_queries += iter->second;
	      entries_over_fp_with_fileID.push(make_pair(tmp, iter->first));
      }      
    }
  }
  double num_total_fp_queries = db_stats.num_total_fp_queries;
  double C = -(total_filter_memory * log_2_squared + S) * 1.0 / num_entries_with_fp_queries; 
  // C = ln(lambda), require: lambda * z_i / n_i > 0.0

  // final bpk assignments are stored in fileID2bpk
  unordered_map<uint64_t, double> fileID2bpk;
  // fileID with no bpk will also be stored in fileIDwithNobpk
  unordered_set<uint64_t> fileIDwithNobpk;

  if(entries_over_fp_with_fileID.empty()) {
    for (auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
      fileID2bpk.emplace(iter->first, _env->bits_per_key);
    }
    return fileID2bpk;
  }
  // Calculating S = \sum (ln z_i / n_i) * z_i
  while (
      !entries_over_fp_with_fileID.empty() && 
      std::log(entries_over_fp_with_fileID.top().first * num_total_fp_queries) + C > 0.0 // this implies current ln(z_i / n_i * lambda) > 0.0
    ) {
    uint64_t fileID = entries_over_fp_with_fileID.top().second;
    S -= std::log(entries_over_fp_with_fileID.top().first * num_total_fp_queries) * db_stats.fileID2entries.at(fileID);
    num_entries_with_fp_queries -= db_stats.fileID2entries.at(fileID);

    /* normalize z_i */
    S += num_entries_with_fp_queries * std::log((num_total_fp_queries - db_stats.fileID2fp_queries.at(fileID)) * 1.0 / num_total_fp_queries);
    num_total_fp_queries -= db_stats.fileID2fp_queries.at(fileID);

    fileIDwithNobpk.insert(fileID);
    fileID2bpk.emplace(fileID, 0.0);
    C = -(total_filter_memory * log_2_squared + S) / num_entries_with_fp_queries; // updated C
    entries_over_fp_with_fileID.pop();
    // std::cerr << "log(entries_over_fp_with_fileID.top().first) + C : " << std::log(entries_over_fp_with_fileID.top().first) + C << std::endl;
  }
  double bpk = 0.0;
  double final_total_memory = 0.0;
  for (auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
    if (db_stats.fileID2fp_queries.find(iter->first) != db_stats.fileID2fp_queries.end()) {
      fp_queries = db_stats.fileID2fp_queries.at(iter->first);
      if (fp_queries != 0 && fileIDwithNobpk.find(iter->first) == fileIDwithNobpk.end()) {
        bpk = -(std::log(iter->second * num_total_fp_queries * 1.0 / fp_queries) + C) / log_2_squared;
        fileID2bpk.emplace(iter->first, bpk);
        final_total_memory += bpk * iter->second;
      } else {
        fileID2bpk.emplace(iter->first, 0.0);
      }
    }
  }
  
  return fileID2bpk;
}


unordered_map<uint64_t, double> Scan_To_Get_OPTBPK_No_Normalize::GetOptBPK(DbStats db_stats) {
  if (db_stats.num_levels <= 1) return {}; // do nothing when L <= 1
  uint64_t total_filter_memory = db_stats.num_entries * _env->bits_per_key;
  // solve \sum - ln p_i / (ln 2)^2 * n_i = M where n_i is the number of entries per file
  // plugged with Lagrange multiplier we have p_i * z_i / n_i should be a constant C where z_i is the number of non-existing
  // queries per file. Let C = p_i * z_i / n_i, we try to solve C first and then calculate p_i per file
  // std:: cout << "Total memory: " << total_filter_memory << std::endl;
  // let S = \sum (ln n_i / z_i) * n_i, we have - (\sum z_i) * C - S = M * (ln 2)^2

  // p_i refers to the false positive rate of the i-th file, fp_queries represent the number of empty queries
  double S = 0;
  uint64_t fileID = 0;
  uint64_t fp_queries = 0;
  uint64_t num_entries_with_fp_queries = 0;
  std::priority_queue<pair<double, uint64_t> > entries_over_fp_with_fileID;
  double tmp;
  // fileID2entries represents the map between fileID and the number of entries of the associated file
  // fileID2fp_enrties represents the number of empty queries of the associated file
  for(auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
    if (db_stats.fileID2fp_queries.find(iter->first) != db_stats.fileID2fp_queries.end()) {
      fp_queries = db_stats.fileID2fp_queries.at(iter->first);
      if (fp_queries != 0 && iter->second != 0) {
	      tmp = iter->second * 1.0 / db_stats.fileID2fp_queries.at(iter->first);
        // NOTE: tmp * db_stats.num_total_fp_queries = n_i / z_i
        // std::cout << " fileID: " << iter->first << " tmp: " << tmp << std::endl;
        S += std::log(tmp * 1.0) * iter->second;
	      num_entries_with_fp_queries += iter->second;
	      entries_over_fp_with_fileID.push(make_pair(tmp, iter->first));
      }      
    }
  }
  double C = -(total_filter_memory * log_2_squared + S) * 1.0 / num_entries_with_fp_queries; // C = ln(lambda), C < 0 required

  // final bpk assignments are stored in fileID2bpk
  unordered_map<uint64_t, double> fileID2bpk;
  // fileID with no bpk will also be stored in fileIDwithNobpk
  unordered_set<uint64_t> fileIDwithNobpk;
  if(entries_over_fp_with_fileID.empty()) {
    for (auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
      fileID2bpk.emplace(iter->first, _env->bits_per_key);
    }
    return fileID2bpk;
  }
  // Calculating S = \sum (ln z_i / n_i) * z_i
  while (
      !entries_over_fp_with_fileID.empty() && 
      std::log(entries_over_fp_with_fileID.top().first) + C > 0.0 // this implies current ln(z_i / n_i * lambda) > 1.0
    ) {
    auto topp = entries_over_fp_with_fileID.top();
    uint64_t fileID = topp.second;
    S -= std::log(entries_over_fp_with_fileID.top().first * 1.0) * db_stats.fileID2entries.at(fileID);
    num_entries_with_fp_queries -= db_stats.fileID2entries.at(fileID);
    fileIDwithNobpk.insert(fileID);
    fileID2bpk.emplace(fileID, 0.0);
    C = -(total_filter_memory * log_2_squared + S) / num_entries_with_fp_queries; // updated C
    entries_over_fp_with_fileID.pop();

    // std::cerr << "log(entries_over_fp_with_fileID.top().first) + C : " << std::log(entries_over_fp_with_fileID.top().first) + C << std::endl;
  }
  double bpk = 0.0;
  double final_total_memory = 0.0;
  for (auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
    if (db_stats.fileID2fp_queries.find(iter->first) != db_stats.fileID2fp_queries.end()) {
      fp_queries = db_stats.fileID2fp_queries.at(iter->first);
      if (fp_queries != 0 && fileIDwithNobpk.find(iter->first) == fileIDwithNobpk.end()) {
        bpk = -(std::log(iter->second * 1.0 / fp_queries) + C) / log_2_squared;
        fileID2bpk.emplace(iter->first, bpk);
        final_total_memory += bpk * iter->second;
      } else {
        fileID2bpk.emplace(iter->first, 0.0);
      }
    }
  }
  
  return fileID2bpk;
}


unordered_map<uint64_t, double> Method_Of_Monkey::GetOptBPK(DbStats db_stats) {
  if (db_stats.num_levels <= 1) return {}; // do nothing when L <= 1
  double total_filter_memory = db_stats.num_entries * _env->bits_per_key;
  // solve \sum - ln p_i / (ln 2)^2 * n_i = M where n_i is the number of entries per file
  // plugged with Lagrange multiplier we have p_i * z_i / n_i should be a constant C where z_i is the number of non-existing
  // queries per file. Let C = p_i * z_i / n_i, we try to solve C first and then calculate p_i per file
  unordered_map<uint64_t, double> fileID2bpk;
  int files = db_stats.num_files;
  // std:: cout << "Total memory: " << total_filter_memory << std::endl;
  double R = 0;
  for (auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
    if (iter == db_stats.fileID2entries.begin()) {
      fileID2bpk[iter->first] = total_filter_memory / iter->second;
    } else {
      fileID2bpk[iter->first] = 0.0;
    }
    R += db_stats.fileID2fp_queries[iter->first] * evaluate_fpr(fileID2bpk[iter->first]);
  }
  auto tryswitch = [&] (int fid1, int fid2, double delta) -> double {
    double D = 
      db_stats.fileID2fp_queries[fid1] * (evaluate_fpr(fileID2bpk[fid1] + delta / db_stats.fileID2entries[fid1]) - evaluate_fpr(fileID2bpk[fid1])) 
    + db_stats.fileID2fp_queries[fid2] * (evaluate_fpr(fileID2bpk[fid2] - delta / db_stats.fileID2entries[fid2]) - evaluate_fpr(fileID2bpk[fid2]));
    if(D < 0.0 && fileID2bpk[fid2] - delta / db_stats.fileID2entries[fid2] >= 0.0) {
      fileID2bpk[fid1] += delta / db_stats.fileID2entries[fid1];
      fileID2bpk[fid2] -= delta / db_stats.fileID2entries[fid2];
      // std::cerr << "swap!" << std::endl;
      return D;
    }
    return 0.0;
  };
  while(total_filter_memory > 0.5) {
    double R_new = R;
    for (auto iter1 = db_stats.fileID2entries.begin(); iter1 != db_stats.fileID2entries.end(); iter1++) {
      auto iter2 = iter1;
      iter2 ++;
      for (; iter2 != db_stats.fileID2entries.end(); iter2++) {
        R_new += tryswitch(iter1->first, iter2->first, total_filter_memory);
        R_new += tryswitch(iter2->first, iter1->first, total_filter_memory);
      }
    }
    if(R_new == R) {
      total_filter_memory /= 2.0;
    }
    R = R_new;
  }
  // fileID2bpk[db_stats.fileID2entries.begin()->first] += total_filter_memory / db_stats.fileID2entries.begin()->second;
  return fileID2bpk;
}

unordered_map<uint64_t, double> Binary_Search_Method::GetOptBPK(DbStats db_stats) {
  if (db_stats.num_levels <= 1) return {}; // do nothing when L <= 1
  uint64_t total_filter_memory = db_stats.num_entries * _env->bits_per_key;
  // solve \sum - ln p_i / (ln 2)^2 * n_i = M where n_i is the number of entries per file
  // plugged with Lagrange multiplier we have p_i * z_i / n_i should be a constant C where z_i is the number of non-existing
  // queries per file. Let C = p_i * z_i / n_i, we try to solve C first and then calculate p_i per file
  // std:: cout << "Total memory: " << total_filter_memory << std::endl;
  // let S = \sum (ln n_i / z_i) * n_i, we have - (\sum z_i) * C - S = M * (ln 2)^2

  // p_i refers to the false positive rate of the i-th file, fp_queries represent the number of empty queries
  double S = 0;
  uint64_t fileID = 0;
  uint64_t fp_queries = 0;
  uint64_t num_entries_with_fp_queries = 0;
  std::vector<pair<double, uint64_t> > entries_over_fp_with_fileID;
  double tmp;
  // fileID2entries represents the map between fileID and the number of entries of the associated file
  // fileID2fp_enrties represents the number of empty queries of the associated file
  for(auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
    if (db_stats.fileID2fp_queries.find(iter->first) != db_stats.fileID2fp_queries.end()) {
      fp_queries = db_stats.fileID2fp_queries.at(iter->first);
      if (fp_queries != 0 && iter->second != 0) {
	      tmp = iter->second * 1.0 / db_stats.fileID2fp_queries.at(iter->first);
        // NOTE: tmp * db_stats.num_total_fp_queries = n_i / z_i
        // std::cout << " fileID: " << iter->first << " tmp: " << tmp << std::endl;
        // S += std::log(tmp * 1.0) * iter->second;
	      num_entries_with_fp_queries += iter->second;
	      entries_over_fp_with_fileID.push_back(make_pair(tmp, iter->first));
      }      
    }
  }
  // double C = -(total_filter_memory * log_2_squared + S) * 1.0 / num_entries_with_fp_queries; // C = ln(lambda), C < 0 required
  sort(entries_over_fp_with_fileID.begin(), entries_over_fp_with_fileID.end());
  std::vector<double> ss(entries_over_fp_with_fileID.size(), 0.0);
  std::vector<uint64_t> n_entries(entries_over_fp_with_fileID.size(), 0);
  double SS = 0;
  uint64_t entires = 0;
  for(int i = 0; i < entries_over_fp_with_fileID.size(); i ++) {
    int id = entries_over_fp_with_fileID[i].second;
    SS += db_stats.fileID2entries.at(id) * std::log(entries_over_fp_with_fileID[i].first * 1.0);
    ss[i] = SS;
    entires += db_stats.fileID2entries.at(id);
    n_entries[i] = entires;
  }
  int l = 0, r = entries_over_fp_with_fileID.size();
  while(l + 1 < r) {
    int mid = (l + r) / 2;
    int id = entries_over_fp_with_fileID[mid].first;
    double C = - (total_filter_memory * log_2_squared + ss[mid]) / n_entries[mid];
    // double kthis = 
    bool flag = 0;
    if(mid + 1 < entries_over_fp_with_fileID.size()) {
      double knext = std::log(entries_over_fp_with_fileID[mid + 1].first) + C;
      if(knext <= 0.0) {
        l = mid;
        flag = 1;
      } // corner case when mid + 1 is satisfied.
    } 
    if(flag) continue;
    if(std::log(entries_over_fp_with_fileID[mid].first) + C > 0.0) {
      r = mid;
    } else {
      l = mid;
    }
  }
  // final bpk assignments are stored in fileID2bpk
  unordered_map<uint64_t, double> fileID2bpk;
  // fileID with no bpk will also be stored in fileIDwithNobpk
  unordered_set<uint64_t> fileIDwithNobpk;
  for(size_t i = l + 1; i < entries_over_fp_with_fileID.size(); i ++) {
    fileIDwithNobpk.insert(entries_over_fp_with_fileID[i].second);
    // fprintf(stdout, "delete ==", entries_over_fp_with_fileID[i].second);
  }
  if(entries_over_fp_with_fileID.size() == 0) {
    for (auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
      fileID2bpk.emplace(iter->first, _env->bits_per_key);
    }
    return fileID2bpk;
  }

  float C = - (total_filter_memory * log_2_squared + ss[l]) / n_entries[l];
  double bpk = 0.0;
  double final_total_memory = 0.0;
  for (auto iter = db_stats.fileID2entries.begin(); iter != db_stats.fileID2entries.end(); iter++) {
    if (db_stats.fileID2fp_queries.find(iter->first) != db_stats.fileID2fp_queries.end()) {
      fp_queries = db_stats.fileID2fp_queries.at(iter->first);
      if (fp_queries != 0 && fileIDwithNobpk.find(iter->first) == fileIDwithNobpk.end()) {
        bpk = -(std::log(iter->second * 1.0 / fp_queries) + C) / log_2_squared;
        fileID2bpk.emplace(iter->first, bpk);
        final_total_memory += bpk * iter->second;
      } else {
        fileID2bpk.emplace(iter->first, 0.0);
      }
    }
  }
  
  return fileID2bpk; 
}