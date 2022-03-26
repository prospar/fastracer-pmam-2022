#ifndef NCOMMON_H
#define NCOMMON_H

#include "tbb/atomic.h"
#include <iostream>
#include <map>
#include <mutex>
#include <pthread.h>
#include <stack>
#include <utility>

typedef size_t THREADID;
typedef unsigned long ADDRINT;
typedef pthread_mutex_t PIN_LOCK;
typedef size_t epoch;
typedef tbb::atomic<size_t> my_lock;

extern tbb::atomic<size_t> Ntask_id_ctr;

extern tbb::atomic<size_t> Nlock_ticker;
#ifndef myenum
#define myenum
enum AccessType { READ = 0, WRITE = 1 };
#endif

inline void NPIN_GetLock(PIN_LOCK* lock, int tid) { pthread_mutex_lock(lock); }

inline void NPIN_ReleaseLock(PIN_LOCK* lock) { pthread_mutex_unlock(lock); }

class NPerSharedVariableStat {
private:
  std::map<uint16_t, uint32_t> rd_access; // Number of reads by each task
  std::map<uint16_t, uint32_t> wr_access; // Number of writes by each task
public:
  uint32_t rd_tasks = 0; //No of tasks trying to read the variable
  uint32_t wr_tasks = 0;
  void track_read(uint16_t tid) {
    auto count = rd_access.find(tid);
    if (count == rd_access.end()) { // First access by tid
      rd_access.insert(std::make_pair(tid, 1));
      rd_tasks++;
    } else {
      count->second++;
    }
  }

  void track_write(uint16_t tid) {
    auto count = wr_access.find(tid);
    if (count == wr_access.end()) { // First access by tid
      wr_access.insert(std::make_pair(tid, 1));
      wr_tasks++;
    } else {
      count->second++;
    }
  }
};

// Should not require synchronization
class NPerTaskStats {
private:
  std::map<uint32_t, uint32_t> rd_map;
  std::map<uint32_t, uint32_t> wr_map;
  std::map<uint32_t, uint32_t> acq_map;
  std::map<uint32_t, uint32_t> rel_map;

public:
  uint64_t num_rds = 0;
  uint64_t num_wrs = 0;
  uint64_t num_acqs = 0;
  uint64_t num_rels = 0;
  uint64_t depth = 0;

  uint64_t num_rd_sameepoch = 0;
  uint64_t num_rd_shared = 0;
  uint64_t num_rd_exclusive = 0;
  uint64_t num_rd_share = 0;
  uint64_t num_wr_sameepoch = 0;
  uint64_t num_wr_exclusive = 0;
  uint64_t num_wr_shared = 0;

  void track_read(uint32_t addr) {
    auto count = rd_map.find(addr);
    if (count == rd_map.end()) { // First read to the variable
      rd_map.insert(std::make_pair(addr, 1));
      num_rds++;
    } else {
      count->second++;
    }
  }
  void track_write(uint32_t addr) {
    auto count = wr_map.find(addr);
    if (count == wr_map.end()) { // First read to the variable
      wr_map.insert(std::make_pair(addr, 1));
      num_wrs++;
    } else {
      count->second++;
    }
  }
  void track_acq(uint32_t addr) {
    auto count = acq_map.find(addr);
    if (count == acq_map.end()) { // First read to the variable
      acq_map.insert(std::make_pair(addr, 1));
      num_acqs++;
    } else {
      count->second++;
    }
  }
  void track_rel(uint32_t addr) {
    auto count = rel_map.find(addr);
    if (count == rel_map.end()) { // First read to the variable
      rel_map.insert(std::make_pair(addr, 1));
      num_rels++;
    } else {
      count->second++;
    }
  }
  //  inline void incr_rd() { num_rds++; }
  //  inline void incr_wrs() { num_wrs++; }
  //  inline void incr_acqs() { num_acqs++; }
  //  inline void incr_rels() { num_rels++; }
};

// This class summarizes statistics across all threads and variables
class NGlobalStats {
private:
  //  std::mutex gs_lock;
public:
  my_lock gs_lock = 0;
  uint32_t rd_sameepoch = 0;
  uint32_t rd_exclusive = 0;
  uint32_t rd_share = 0;
  uint32_t rd_shared = 0;
  uint32_t wr_sameepoch = 0;
  uint32_t wr_exclusive = 0;
  uint32_t wr_shared = 0;
  // Cumulative of per-variable stats
  uint32_t max_rds = 0; // Max reader tasks across all variables
  uint32_t max_wrs = 0; // Max writer tasks across all variables
  // Cumulative of per-task stats
  uint32_t max_rd_tasks = 0, min_rds_tasks, max_wr_tasks = 0, min_wr_tasks;
  uint32_t nactive_tasks = 0, maxnactive_tasks = 0;
  
  uint32_t max_acq_tasks = 0, max_rel_tasks = 0;
  uint32_t max_task_depth = 0;
  void dump() {
    std::cout << "\n\n***********START STATISTICS DUMP***********\n";
    std::cout << "Max number of reader tasks across all variables: " << max_rds << "\n";
    std::cout << "Max number of writer tasks across all variables: " << max_wrs << "\n";
    std::cout << "Max number of variables read by a task: " << max_rd_tasks << "\n";
    std::cout << "Max number of variables written by a task: " << max_wr_tasks << "\n";
    std::cout << "Max number of locks acquired by a task: " << max_acq_tasks << "\n";
    std::cout << "Max number of locks released by a task: " << max_rel_tasks << "\n";
    std::cout << "Max depth of a task in the corresponding taskgraph: " << max_task_depth << "\n";
    std::cout << "Total number of tasks spawned in the program: " << Ntask_id_ctr << "\n";
    uint32_t rd_total = rd_shared + rd_exclusive + rd_sameepoch + rd_share ;
    std::cout << "Total number of reads: " << rd_total << "\n";
    if(rd_total){
        std::cout << " Read same epoch: " << rd_sameepoch << " (" << (rd_sameepoch*100.0/rd_total) << "%)\n";
        std::cout << " Read exclusive: " << rd_exclusive << " (" << (rd_exclusive*100.0/rd_total) << "%)\n";
        std::cout << " Read shared: " << rd_shared << " (" << (rd_shared*100.0/rd_total) << "%)\n";
        std::cout << " Read share: " << rd_share << " (" << (rd_share*100.0/rd_total) << "%)\n";
    }
    uint32_t wr_total = wr_shared + wr_exclusive + wr_sameepoch ;
    std::cout << "Total number of writes: " << wr_total << "\n";
    if(wr_total){
        std::cout << " Write same epoch: " << wr_sameepoch << " (" << (wr_sameepoch*100.0/wr_total) << "%)\n";
        std::cout << " Write exclusive: " << wr_exclusive << " (" << (wr_exclusive*100.0/wr_total) << "%)\n";
        std::cout << " Write shared: " << wr_shared << " (" << (wr_shared*100.0/wr_total) << "%)\n";
    }
    std::cout << "Maximum number of active tasks during the execution: " << maxnactive_tasks << "\n";
    std::cout << "***********END STATISTICS DUMP***********\n\n";
  }

  void track_read(uint16_t tid) {}
};

#endif
