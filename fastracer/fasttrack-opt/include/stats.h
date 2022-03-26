#ifndef STATS_H
#define STATS_H

#include "Common.H"
#include <cassert>
#include <iostream>
#include<chrono>

// PROSPAR: Support timers
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
extern std::ostream& operator<<(std::ostream& out, const FT_READ_TYPE value);
extern std::ostream& operator<<(std::ostream& out, const FT_WRITE_TYPE value);

#if STATS
extern uint64_t numrds;
extern uint64_t numwrs;
#endif

#if DEBUG
extern my_lock printLock;
#endif

// Should not require synchronization, other tasks should not access
class PerTaskStats {
private:
  std::map<uint32_t, uint64_t> rd_map;
  std::map<uint32_t, uint64_t> wr_map;
  std::map<uint32_t, uint64_t> acq_map;
  std::map<uint32_t, uint64_t> rel_map;

public:
  uint64_t num_rds = 0; // All reads by task
  uint64_t num_wrs = 0; // All writes by task
  uint64_t num_acqs = 0;
  uint64_t num_rels = 0;
  uint64_t depth = 0; // inheritance tree

  uint64_t lock_nesting_depth = 0; // lock nesting depth
  uint64_t max_lock_nesting_depth = 0;

  uint64_t num_rd_sameepoch = 0;
  uint64_t num_rd_shared_sameepoch = 0;
  uint64_t num_rd_shared = 0;
  uint64_t num_rd_exclusive = 0;
  uint64_t num_rd_share = 0;

  uint64_t num_wr_sameepoch = 0;
  uint64_t num_wr_exclusive = 0;
  uint64_t num_wr_shared = 0;

  uint64_t num_racy_rds = 0;
  uint64_t num_racy_wrs = 0;

  void track_read(uint32_t addr, FT_READ_TYPE type) {
    auto count = rd_map.find(addr);
    if (count == rd_map.end()) { // First read to the variable
      rd_map.insert(std::make_pair(addr, 1));
    } else {
      count->second++;
    }
    num_rds++;
    switch (type) {
      case RD_SAME_EPOCH: {
        num_rd_sameepoch++;
        break;
      }
      case RD_SHARED_SAME_EPOCH: {
        num_rd_shared_sameepoch++;
        break;
      }
      case RD_EXCLUSIVE: {
        num_rd_exclusive++;
        break;
      }
      case RD_SHARE: {
        num_rd_share++;
        break;
      }
      case RD_SHARED: {
        num_rd_shared++;
        break;
      }
      default: {
        assert(false && "Unknown read type!");
      }
    }
    if (num_rds != (num_rd_exclusive + num_rd_sameepoch + num_rd_shared_sameepoch + num_rd_share +
                    num_rd_shared)) {
      std::cout << "Num read exclusive: " << num_rd_exclusive
                << " Num read same epoch: " << num_rd_sameepoch
                << " Num read shared same epoch: " << num_rd_shared_sameepoch
                << " Num read share: " << num_rd_share << " Num read shared: " << num_rd_shared
                << " Num reads: " << num_rds << " Sum :"
                << (num_rd_exclusive + num_rd_sameepoch + num_rd_shared_sameepoch + num_rd_share +
                    num_rd_shared)
                << " Num racy reads: " << num_racy_rds << std::endl;
    }
    assert(num_rds == (num_rd_exclusive + num_rd_sameepoch + num_rd_shared_sameepoch +
                       num_rd_share + num_rd_shared));
  }

  uint64_t get_num_vars_read_by_task() const { return rd_map.size(); }

  uint64_t get_num_vars_written_by_task() const { return wr_map.size(); }

  void track_write(uint32_t addr, FT_WRITE_TYPE type) {
    auto count = wr_map.find(addr);
    if (count == wr_map.end()) { // First write to the variable
      wr_map.insert(std::make_pair(addr, 1));
    } else {
      count->second++;
    }
    num_wrs++;
    switch (type) {
      case WR_EXCLUSIVE: {
        num_wr_exclusive++;
        break;
      }
      case WR_SAME_EPOCH: {
        num_wr_sameepoch++;
        break;
      }
      case WR_SHARED: {
        num_wr_shared++;
        break;
      }
      default: {
        assert(false && "Unknown write type!");
      }
    }
  }

  void track_acq(uint32_t addr) {
    auto count = acq_map.find(addr);
    if (count == acq_map.end()) { // First acquire
      acq_map.insert(std::make_pair(addr, 1));
    } else {
      count->second++;
    }
    num_acqs++;
  }

  void track_rel(uint32_t addr) { // Second acquire
    auto count = rel_map.find(addr);
    if (count == rel_map.end()) {
      rel_map.insert(std::make_pair(addr, 1));
    } else {
      count->second++;
    }
    num_rels++;
  }
};

// This requires synchronization since multiple tasks/threads may access the variable concurrently
class PerSharedVariableStat {
private:
  // Number of reads by each task, map from task id -> count
  std::map<uint32_t, uint64_t> rd_access;
  // Number of writes by each task, map from task id -> count
  std::map<uint32_t, uint64_t> wr_access;
  uint64_t num_rds = 0;
  uint64_t num_wrs = 0;

public:
  void track_read(uint16_t tid) {
    auto count = rd_access.find(tid);
    if (count == rd_access.end()) { // First access by tid
      rd_access.insert(std::make_pair(tid, 1));
    } else {
      count->second++;
    }
    num_rds++;
  }

  inline uint64_t get_num_rd_tasks() const { return rd_access.size(); }

  inline uint64_t get_num_rds() const { return num_rds; }

  inline uint64_t get_num_wrs() const { return num_wrs; }

  inline uint64_t get_num_wr_tasks() const { return wr_access.size(); }

  void track_write(uint16_t tid) {
    auto count = wr_access.find(tid);
    if (count == wr_access.end()) { // First access by tid
      wr_access.insert(std::make_pair(tid, 1));
    } else {
      count->second++;
    }
    num_wrs++;
  }
};

#if 0
class PerLockStats {
public:
  // std::unordered_set<size_t> tid_set;
};
#endif

// Accumulate statistics across all threads and variables. We should compute the
// final results only at termination and at application termination, to avoid the overheads of
// frequent locking.
class GlobalStats {
public:
  my_lock gs_lock = 0;

  uint64_t gs_num_recordmems = 0;
  uint64_t gs_num_recordaccess = 0;

  uint64_t gs_tot_wait_calls = 0;

  uint64_t gs_num_rds = 0;
  uint64_t gs_num_wrs = 0;
  uint64_t gs_num_lock_acqs = 0;
  uint64_t gs_num_lock_rels = 0;
  uint64_t gs_num_racy_rds = 0;
  uint64_t gs_num_racy_wrs = 0;

  uint64_t gs_num_rd_sameepoch = 0;
  uint64_t gs_num_rd_shared_sameepoch = 0;
  uint64_t gs_num_rd_exclusive = 0;
  uint64_t gs_num_rd_share = 0;
  uint64_t gs_num_rd_shared = 0;

  uint64_t gs_num_wr_sameepoch = 0;
  uint64_t gs_num_wr_exclusive = 0;
  uint64_t gs_num_wr_shared = 0;

  uint64_t gs_totTaskvcSize = 0;// Total Size of Vc allocated across all tasks

  // Cumulative of per-variable stats

  // Max number of reader tasks per variable across all variables
  uint64_t pv_max_rd_tasks = 0;
  // Max number of writer tasks per variable across all variables
  uint64_t pv_max_wr_tasks = 0;

  // Max number of reads across all variables
  uint64_t pv_max_num_rds = 0;
  // Max number of writes across all variables
  uint64_t pv_max_num_wrs = 0;

  // Cumulative of per-task stats

  uint64_t pt_max_num_rds = 0, pt_min_num_rds = UINT64_MAX, pt_max_num_wrs = 0,
           pt_min_num_wrs = UINT64_MAX;
  uint64_t pt_max_vars_read = 0, pt_max_vars_written = 0;
  uint64_t pt_max_num_acqs = 0, pt_max_num_rels = 0;
  uint64_t pt_max_lock_nesting_depth = 0;

  uint64_t num_active_tasks = 0, max_num_active_tasks = 0;
  uint64_t max_task_depth = 0; // Max task depth in the inheritance tree across all tasks

  double time_element = 0; 
  double time_RecordMem = 0;
  double time_taskid_map = 0;

  // FIXME: Check all variables are tracked and printed.
  void dump() {
    std::cout << "\n\n***********START STATISTICS DUMP***********\n" << std::dec;

    std::cout << "Total RecordMem Time in milliseconds " << time_RecordMem << "\n";
    std::cout << "Total time in element call in milliseconds " << time_element << "\n";
    std::cout << "Total time in taskid_map find in milliseconds " << time_taskid_map << "\n";

    std::cout << "Total number of tasks spawned: " << task_id_ctr << "\n"
              << "Max depth of a task in the corresponding task graph across all tasks: "
              << max_task_depth << "\n";

    std::cout << "Total Size of TaskVc allocated across all tasks " << gs_totTaskvcSize  << "\n";

    // Per variable
    assert(pv_max_rd_tasks <= task_id_ctr);
    std::cout << "Max number of reader tasks per variable across all variables: " << pv_max_rd_tasks
              << "\n";
    assert(pv_max_wr_tasks <= task_id_ctr);
    std::cout << "Max number of writer tasks per variable across all variables: " << pv_max_wr_tasks
              << "\n";

    std::cout << "Max number of reads across all variables: " << pv_max_num_rds << "\n";
    std::cout << "Max number of writes across all variables: " << pv_max_num_wrs << "\n";

    // Per task
    std::cout << "Max number of reads by a task across all tasks: " << pt_max_num_rds << "\n"
              << "Max number of writes by a task across all tasks: " << pt_max_num_wrs << "\n"
              << "Min number of reads by a task across all tasks: " << pt_min_num_rds << "\n"
              << "Min number of writes by a task across all tasks: " << pt_min_num_wrs << "\n"
              << "Max number of variables read by a task across all tasks: " << pt_max_vars_read
              << "\n"
              << "Max number of variables written by a task across all tasks: "
              << pt_max_vars_written << "\n"
              << "Max number of locks acquired by a task across all tasks: " << pt_max_num_acqs
              << "\n"
              << "Max number of locks released by a task across all tasks: " << pt_max_num_rels
              << "\n";

    // Global
    std::cout << "Total number of RecordMem calls (includes calls from RecordAccess): "
              << gs_num_recordmems << "\n"
              << "Total number of direct RecordMem calls (excluding calls from RecordAccess): "
              << (gs_num_recordmems - gs_num_recordaccess) << "\n"
              << "Total number of RecordAccess calls: " << gs_num_recordaccess << "\n";

    std::cout << "Max number of locks acquired (i.e., nesting depth) by a task at a given time: "
              << pt_max_lock_nesting_depth << "\n";

    assert(gs_num_rds == (gs_num_rd_sameepoch + gs_num_rd_shared_sameepoch + gs_num_rd_exclusive +
                          gs_num_rd_shared + gs_num_rd_share));
    std::cout << "Total number of reads: " << gs_num_rds << "\n";
    std::cout << " Read same epoch: " << gs_num_rd_sameepoch << " ("
              << ((gs_num_rds > 0) ? (gs_num_rd_sameepoch * 100.0 / gs_num_rds) : 0) << "%)\n"
              << " Read shared same epoch: " << gs_num_rd_shared_sameepoch << " ("
              << ((gs_num_rds > 0) ? (gs_num_rd_shared_sameepoch * 100.0 / gs_num_rds) : 0)
              << "%)\n"
              << " Read exclusive: " << gs_num_rd_exclusive << " ("
              << ((gs_num_rds > 0) ? (gs_num_rd_exclusive * 100.0 / gs_num_rds) : 0) << "%)\n"
              << " Read shared: " << gs_num_rd_shared << " ("
              << ((gs_num_rds > 0) ? (gs_num_rd_shared * 100.0 / gs_num_rds) : 0) << "%)\n"
              << " Read share: " << gs_num_rd_share << " ("
              << ((gs_num_rds > 0) ? (gs_num_rd_share * 100.0 / gs_num_rds) : 0) << "%)\n";

    assert(gs_num_wrs == (gs_num_wr_shared + gs_num_wr_exclusive + gs_num_wr_sameepoch));
    std::cout << "Total number of writes: " << gs_num_wrs << "\n";
    std::cout << " Write same epoch: " << gs_num_wr_sameepoch << " ("
              << ((gs_num_wrs > 0) ? (gs_num_wr_sameepoch * 100.0 / gs_num_wrs) : 0) << "%)\n"
              << " Write exclusive: " << gs_num_wr_exclusive << " ("
              << ((gs_num_wrs > 0) ? (gs_num_wr_exclusive * 100.0 / gs_num_wrs) : 0) << "%)\n"
              << " Write shared: " << gs_num_wr_shared << " ("
              << ((gs_num_wrs > 0) ? (gs_num_wr_shared * 100.0 / gs_num_wrs) : 0) << "%)\n";

    std::cout << "Number of racy reads: " << gs_num_racy_rds << "\n"
              << "Number of racy writes: " << gs_num_racy_wrs << "\n";

#if STATS
    std::cout << "New reads: " << numrds << " New writes: " << numwrs << std::endl;
    if (numrds != (gs_num_rds + gs_num_racy_rds)) {
      std::cout << "Total num reads: " << gs_num_rds << " Total num racy reads: " << gs_num_racy_rds
                << " New reads: " << numrds << std::endl;
    }
    assert(numrds == gs_num_rds + gs_num_racy_rds);
    if (numwrs != (gs_num_wrs + gs_num_racy_wrs)) {
      std::cout << "Total num writes: " << gs_num_wrs
                << " Total num racy writes: " << gs_num_racy_wrs << " New writes: " << numwrs
                << std::endl;
    }
    assert(numwrs == gs_num_wrs + gs_num_racy_wrs);
    assert(numrds + numwrs == gs_num_recordmems);
#endif

    if ((gs_num_rds + gs_num_wrs + gs_num_racy_rds + gs_num_racy_wrs) != gs_num_recordmems) {
      std::cout << "Total num reads: " << gs_num_rds << " Total num writes: " << gs_num_wrs
                << std::endl;
    }
    assert((gs_num_rds + gs_num_wrs + gs_num_racy_rds + gs_num_racy_wrs) == gs_num_recordmems);

    std::cout << "Total num of wait_for_all_calls: " << gs_tot_wait_calls << std::endl;

    std::cout << "Maximum number of active concurrent tasks during the execution: "
          << max_num_active_tasks << "\n";

    std::cout << "***********END STATISTICS DUMP***********\n\n";
  }

  void update_elemtime(HRTimer start){
    HRTimer end = HR::now();
    double cur = duration_cast<milliseconds>(end - start).count();
    my_getlock(&gs_lock);
    time_element += cur;
    my_releaselock(&gs_lock);
    return;
  }
  void update_RecordMemtime(HRTimer start){
    HRTimer end = HR::now();
    double cur = duration_cast<milliseconds>(end - start).count();
    my_getlock(&gs_lock);
    time_RecordMem += cur;
    my_releaselock(&gs_lock);
    return;
  }
  void update_taskid_map_time(HRTimer start){
    HRTimer end = HR::now();
    double cur = duration_cast<milliseconds>(end - start).count();
    my_getlock(&gs_lock);
    time_taskid_map += cur;
    my_releaselock(&gs_lock);
    return;
  }
  // This method ensures synchronization
  void accumulate(PerTaskStats ptstats) {
#if DEBUG
    my_getlock(&printLock);
#endif
    if (ptstats.num_rds !=
        (ptstats.num_rd_exclusive + ptstats.num_rd_sameepoch + ptstats.num_rd_shared_sameepoch +
         ptstats.num_rd_share + ptstats.num_rd_shared)) {
      std::cout << "Num read exclusive: " << ptstats.num_rd_exclusive
                << " Num read same epoch: " << ptstats.num_rd_sameepoch
                << " Num read shared same epoch: " << ptstats.num_rd_shared_sameepoch
                << " Num read share: " << ptstats.num_rd_share
                << " Num read shared: " << ptstats.num_rd_shared
                << " Num reads: " << ptstats.num_rds << " Sum :"
                << (ptstats.num_rd_exclusive + ptstats.num_rd_sameepoch +
                    ptstats.num_rd_shared_sameepoch + ptstats.num_rd_share + ptstats.num_rd_shared)
                << "\n"
                << std::endl;
      std::cout << "Num racy reads: " << ptstats.num_racy_rds << std::endl;
    }
    assert(ptstats.num_rds == ptstats.num_rd_exclusive + ptstats.num_rd_sameepoch +
                                  ptstats.num_rd_shared_sameepoch + ptstats.num_rd_share +
                                  ptstats.num_rd_shared);
    assert(ptstats.num_wrs ==
           ptstats.num_wr_exclusive + ptstats.num_wr_sameepoch + ptstats.num_wr_shared);
    assert(ptstats.num_acqs == ptstats.num_rels);
#if DEBUG
    my_releaselock(&printLock);
#endif

    my_getlock(&gs_lock);
    gs_num_rd_sameepoch += ptstats.num_rd_sameepoch;
    gs_num_rd_shared_sameepoch += ptstats.num_rd_shared_sameepoch;
    gs_num_rd_exclusive += ptstats.num_rd_exclusive;
    gs_num_rd_shared += ptstats.num_rd_shared;
    gs_num_rd_share += ptstats.num_rd_share;
    gs_num_rds += ptstats.num_rds;

    gs_num_wr_shared += ptstats.num_wr_shared;
    gs_num_wr_exclusive += ptstats.num_wr_exclusive;
    gs_num_wr_sameepoch += ptstats.num_wr_sameepoch;
    gs_num_wrs += ptstats.num_wrs;

    gs_num_racy_rds += ptstats.num_racy_rds;
    gs_num_racy_wrs += ptstats.num_racy_wrs;

    pt_max_num_rds = std::max(ptstats.num_rds, pt_max_num_rds);
    pt_min_num_rds = std::min(ptstats.num_rds, pt_min_num_rds);

    pt_max_num_wrs = std::max(pt_max_num_wrs, ptstats.num_wrs);
    pt_min_num_wrs = std::min(pt_min_num_wrs, ptstats.num_wrs);

    gs_num_lock_acqs += ptstats.num_acqs;
    gs_num_lock_rels += ptstats.num_rels;

    max_task_depth = std::max(max_task_depth, ptstats.depth);
    pt_max_num_acqs = std::max(pt_max_num_acqs, ptstats.num_acqs);
    pt_max_lock_nesting_depth = std::max(pt_max_lock_nesting_depth, ptstats.max_lock_nesting_depth);

    auto vars_read = ptstats.get_num_vars_read_by_task();
    pt_max_vars_read = std::max(pt_max_vars_read, vars_read);

    auto vars_written = ptstats.get_num_vars_written_by_task();
    pt_max_vars_written = std::max(pt_max_vars_written, vars_written);
    my_releaselock(&gs_lock);
  }
};

#endif
