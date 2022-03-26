#ifndef STATS_H
#define STATS_H

#include "Common.H"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <unordered_map>

struct varData {
private:
public:
  uint32_t num_reads = 0;
  uint32_t num_writes = 0;
  std::set<size_t> tasks_reading;
  std::set<size_t> tasks_writing;
};

struct taskData {
private:
public:
  uint32_t num_reads = 0;
  uint32_t num_writes = 0;
  std::set<void*> vars_read;
  std::set<void*> vars_written;
  std::set<uint32_t> locks_acq;
  std::set<uint32_t> locks_rel;
};

// This class accumulates statistics across all threads and variables. We should try to compute the
// final results only at task termination and at application termination, to avoid the overheads of
// frequent locking.
class GlobalStats {
private:
  std::unordered_map<void*, varData> varDataMap;
  std::unordered_map<size_t, taskData> taskDataMap;

public:
  my_lock gs_lock = 0;

  uint64_t total_num_rds = 0;
  uint64_t total_num_wrs = 0;
  uint32_t total_num_lock_acqs = 0;
  uint32_t total_num_lock_rels = 0;
  uint32_t total_num_viol = 0;

  uint64_t total_num_recordmems = 0;
  uint64_t total_num_recordaccess = 0;

  // // Total number of LCA steps
  // uint64_t total_num_lcasteps = 0;
  // uint64_t lca_time = 0;
  // uint64_t parallel_time = 0;
  // Cumulative of per-variable stats

  // Max number of unique reader tasks per variable across all variables
  uint32_t pv_max_rds_tasks = 0;
  // Max number of unique writer tasks per variable across all variables
  uint32_t pv_max_wrs_tasks = 0;

  // TODO: This is not useful unless we store the address of the instance variable.
  // Max number of reads across all tasks across all variables
  uint32_t pv_max_rds = 0;
  // Max number of writes across all tasks across all variables
  uint32_t pv_max_wrs = 0;

  // Cumulative of per-task stats
  uint32_t pt_max_rds = 0, pt_min_rds = UINT32_MAX, pt_max_wrs = 0, pt_min_wrs = UINT32_MAX;
  uint32_t pt_max_vars_read = 0, pt_max_vars_written = 0;
  uint32_t pt_max_acqs = 0, pt_max_rels = 0;
  uint32_t max_task_depth = 0;

  void track_read(size_t taskId, void* addr) {
    // var data updatation.
    auto count = varDataMap.find(addr);
    if (count == varDataMap.end()) { // First read to the variable
      struct varData var_data;
      var_data.num_reads = 1;
      var_data.tasks_reading.insert(taskId);
      varDataMap.insert(std::make_pair(addr, var_data));
    } else {
      //assert(count->second < UINT64_MAX - 1);
      count->second.num_reads++;
      count->second.tasks_reading.insert(taskId);
      if (pv_max_rds_tasks < count->second.tasks_reading.size())
        pv_max_rds_tasks = count->second.tasks_reading.size();
      if (pv_max_rds < count->second.num_reads)
        pv_max_rds = count->second.num_reads;
    }

    // task data updation
    auto count1 = taskDataMap.find(taskId);
    if (count1 == taskDataMap.end()) { // First read by the task
      struct taskData task_data;
      task_data.num_reads = 1;
      task_data.vars_read.insert(addr);
      taskDataMap.insert(std::make_pair(taskId, task_data));
    } else {
      //assert(count->second < UINT64_MAX - 1);
      count1->second.num_reads++;
      count1->second.vars_read.insert(addr);
      if (pt_max_vars_read < count1->second.vars_read.size())
        pt_max_vars_read = count1->second.vars_read.size();
      if (pt_max_rds < count1->second.num_reads)
        pt_max_rds = count1->second.num_reads;
    }
  }

  void track_write(size_t taskId, void* addr) {
    // var data updatation.
    auto count = varDataMap.find(addr);
    if (count == varDataMap.end()) { // First write to the variable
      struct varData var_data;
      var_data.num_writes = 1;
      var_data.tasks_writing.insert(taskId);
      varDataMap.insert(std::make_pair(addr, var_data));
    } else {
      //assert(count->second < UINT64_MAX - 1);
      count->second.num_writes++;
      count->second.tasks_writing.insert(taskId);
      if (pv_max_wrs_tasks < count->second.tasks_writing.size())
        pv_max_wrs_tasks = count->second.tasks_writing.size();
      if (pv_max_wrs < count->second.num_writes)
        pv_max_wrs = count->second.num_writes;
    }
    // task data updatation.
    auto count1 = taskDataMap.find(taskId);
    if (count1 == taskDataMap.end()) { // First read by the task
      struct taskData task_data;
      task_data.num_writes = 1;
      task_data.vars_written.insert(addr);
      taskDataMap.insert(std::make_pair(taskId, task_data));
    } else {
      //assert(count->second < UINT64_MAX - 1);
      count1->second.num_writes++;
      count1->second.vars_written.insert(addr);
      if (pt_max_vars_written < count1->second.vars_written.size())
        pt_max_vars_written = count1->second.vars_written.size();
      if (pt_max_wrs < count1->second.num_writes)
        pt_max_wrs = count1->second.num_writes;
    }
  }

  void track_acq(size_t taskId, uint32_t addr) {
    auto count = taskDataMap.find(taskId);
    if (count == taskDataMap.end()) { // First lock acq at address
      struct taskData task_data;
      task_data.locks_acq.insert(addr);
      taskDataMap.insert(std::make_pair(taskId, task_data));
    } else {
      count->second.locks_acq.insert(addr);
      if (pt_max_acqs < count->second.locks_acq.size())
        pt_max_acqs = count->second.locks_acq.size();
    }
  }

  void track_rel(size_t taskId, uint32_t addr) {
    auto count = taskDataMap.find(taskId);
    if (count == taskDataMap.end()) { // First lock rel at address
      struct taskData task_data;
      task_data.locks_rel.insert(addr);
      taskDataMap.insert(std::make_pair(taskId, task_data));
    } else {
      count->second.locks_rel.insert(addr);
      if (pt_max_rels < count->second.locks_rel.size())
        pt_max_rels = count->second.locks_rel.size();
    }
  }

  void calFinalStats() {
    for (auto const& count : taskDataMap) {
      if (count.second.num_reads < pt_min_rds)
        pt_min_rds = count.second.num_reads;
      if (count.second.num_writes < pt_min_wrs)
        pt_min_wrs = count.second.num_writes;
    }
  }

  void dump() {
    std::cout << "\n\n***********START STATISTICS DUMP***********\n";

    // Per variable
    std::cout << "Max number of reader tasks across all variables: " << pv_max_rds_tasks << "\n";
    std::cout << "Max number of writer tasks across all variables: " << pv_max_wrs_tasks << "\n";

    std::cout << "Max number of reads across all variables: " << pv_max_rds << "\n";
    std::cout << "Max number of writes across all variables: " << pv_max_wrs << "\n";

    // Per task
    std::cout << "Max number of reads by a task: " << pt_max_rds << "\n";
    std::cout << "Max number of writes by a task: " << pt_max_wrs << "\n";
    std::cout << "Min number of reads by a task: " << pt_min_rds << "\n";
    std::cout << "Min number of writes by a task: " << pt_min_wrs << "\n";
    std::cout << "Max number of variables read by a task " << pt_max_vars_read << "\n";
    std::cout << "Max number of variables written by a task " << pt_max_vars_written << "\n";
    std::cout << "Max number of locks acquired by a task: " << pt_max_acqs << "\n";
    std::cout << "Max number of locks released by a task: " << pt_max_rels << "\n";
    std::cout << "Max depth of a task in the corresponding taskgraph: " << max_task_depth << "\n";

    std::cout << "Total number of tasks spawned: " << task_id_ctr << "\n";

    // Global
    std::cout << "Total number of reads: " << total_num_rds << "\n";
    std::cout << "Total number of writes: " << total_num_wrs << "\n";

    std::cout << "Total number of RecordMem calls: " << total_num_recordmems << "\n"
              << "Total number of RecordAccess calls: " << total_num_recordaccess << "\n";

    // std::cout << "Average number of LCA steps per RecordMem call"
    //           << 1.0 * total_num_lcasteps / total_num_recordmems << "\n";
    // std::cout << "Total time in LCA function: " << lca_time << "milliseconds\n";
    // std::cout << "total time in Parallel Function: " << parallel_time << "milliseconds\n";

    std::cout << "***********END STATISTICS DUMP***********\n\n";
  }
};

#endif
