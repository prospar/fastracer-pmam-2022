#ifndef STATS_H
#define STATS_H

#include "Common.H"
#include <cassert>

#define STATS 0

#if DEBUG
extern my_lock printLock;
#endif

#if DEBUG
extern uint64_t numrds;
extern uint64_t numwrs;
#endif

enum FT_READ_TYPE { RD_SAME_EPOCH = 0, RD_EXCLUSIVE, RD_SHARE, RD_SHARED, RD_INVALID };

enum FT_WRITE_TYPE { WR_SAME_EPOCH = 0, WR_EXCLUSIVE, WR_SHARED, WR_INVALID };

class PerThreadStats {
public:
  uint64_t num_rds = 0;
  uint64_t num_wrs = 0;
  uint64_t num_acqs = 0;
  uint64_t num_rels = 0;
};

extern std::ostream& operator<<(std::ostream& out, const FT_READ_TYPE value);
extern std::ostream& operator<<(std::ostream& out, const FT_WRITE_TYPE value);

// Should not require synchronization
class PerTaskStats {
private:
  // Address, count pair
  std::map<uint32_t, uint64_t> rd_map;
  std::map<uint32_t, uint64_t> wr_map;
  std::map<uint32_t, uint64_t> acq_map;
  std::map<uint32_t, uint64_t> rel_map;

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

  void track_read(uint32_t addr, FT_READ_TYPE type) {
#if FALSE
    my_getlock(&printLock);
    std::cout << "track_read: Address: " << std::showbase << std::hex << addr << std::dec
              << " READ Type: " << type << std::endl;
    my_releaselock(&printLock);
#endif
    auto count = rd_map.find(addr);
    if (count == rd_map.end()) { // First read to the variable
      rd_map.insert(std::make_pair(addr, 1));
    } else {
      // assert(count->second < UINT64_MAX - 1);
      count->second++;
    }
    // assert(num_rds < UINT64_MAX - 1);
    assert(rd_map.size() > 0);
    num_rds++;
    switch (type) {
      case RD_SAME_EPOCH: {
        num_rd_sameepoch++;
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

#if FALSE
    my_getlock(&printLock);
    std::cout << "track_read: Address: " << addr << "\tREAD END" << std::endl;
    my_releaselock(&printLock);
#endif
  }

  uint32_t get_num_vars_read() const { return rd_map.size(); }

  uint32_t get_num_vars_written() const { return wr_map.size(); }

  void track_write(uint32_t addr, FT_WRITE_TYPE type) {
    auto count = wr_map.find(addr);
    if (count == wr_map.end()) { // First write to the variable
      wr_map.insert(std::make_pair(addr, 1));
    } else {
      // assert(count->second < UINT64_MAX - 1);
      count->second++;
    }
    // assert(num_wrs < UINT64_MAX - 1);
    // assert(wr_map.size() > 0);
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
    if (count == acq_map.end()) {
      acq_map.insert(std::make_pair(addr, 1));
    } else {
      // assert(count->second < UINT64_MAX - 1);
      count->second++;
    }
    // assert(num_acqs < UINT64_MAX - 1);
    // assert(acq_map.size() > 0);
    num_acqs++;
  }

  void track_rel(uint32_t addr) {
    auto count = rel_map.find(addr);
    if (count == rel_map.end()) {
      rel_map.insert(std::make_pair(addr, 1));
    } else {
      // assert(count->second < UINT64_MAX - 1);
      count->second++;
    }
    // assert(num_rels < UINT64_MAX - 1);
    // assert(rel_map.size() > 0);
    num_rels++;
  }
};

// This should require synchronization since multiple tasks/threads may access the variable concurrently
class PerSharedVariableStat {
private:
  // Number of reads by each task <task id, count>
  std::map<uint16_t, uint64_t> rd_access;
  // Number of writes by each task
  std::map<uint16_t, uint64_t> wr_access;
  uint64_t num_rds = 0;
  uint64_t num_wrs = 0;

public:
  void track_read(uint16_t tkid) {
    auto count = rd_access.find(tkid);
    if (count == rd_access.end()) { // First access by tkid
      rd_access.insert(std::make_pair(tkid, 1));
    } else {
      // assert(count->second < UINT64_MAX - 1);
      count->second++;
    }
    // assert(rd_access.size() > 0);
    num_rds++;
  }

  inline uint16_t get_num_rd_tasks() const { return rd_access.size(); }

  inline uint64_t get_num_rds() const { return num_rds; }

  inline uint64_t get_num_wrs() const { return num_wrs; }

  inline uint16_t get_num_wr_tasks() const { return wr_access.size(); }

  void track_write(uint16_t taskid) {
    // #if DEBUG
    //     my_getlock(&printLock);
    //     std::cout << "track_write: Task id: " << taskid << std::endl;
    //     my_releaselock(&printLock);
    // #endif
    auto count = wr_access.find(taskid);
    if (count == wr_access.end()) { // First access by tid
      wr_access.insert(std::make_pair(taskid, 1));
    } else {
      count->second++;
      // assert(count->second < UINT64_MAX - 1);
    }
    // assert(wr_access.size() > 0);
    num_wrs++;
  }
};

// Accumulate statistics across all threads and variables. We should compute the
// final results only at termination and at application termination, to avoid the overheads of
// frequent locking.
class GlobalStats {
public:
  my_lock gs_lock = 0;

  uint64_t gs_num_recordmems = 0;
  uint64_t gs_num_recordaccess = 0;

  uint64_t gs_num_rds = 0;
  uint64_t gs_num_wrs = 0;
  uint64_t gs_num_lock_acqs = 0;
  uint64_t gs_num_lock_rels = 0;

  uint64_t gs_num_rd_sameepoch = 0;
  uint64_t gs_num_rd_exclusive = 0;
  uint64_t gs_num_rd_share = 0;
  uint64_t gs_num_rd_shared = 0;

  uint64_t gs_num_wr_sameepoch = 0;
  uint64_t gs_num_wr_exclusive = 0;
  uint64_t gs_num_wr_shared = 0;

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
  uint64_t max_task_depth = 0;

  void dump() {
    std::cout << "\n\n***********START STATISTICS DUMP***********\n" << std::dec;

    std::cout << "Total number of tasks spawned: " << task_id_ctr << "\n"
              << "Max depth of a task in the corresponding task graph across all tasks: "
              << max_task_depth << "\n";

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

    gs_num_rds = gs_num_rd_shared + gs_num_rd_exclusive + gs_num_rd_sameepoch + gs_num_rd_share;
    std::cout << "Total number of reads: " << gs_num_rds << "\n";
    if (gs_num_rds) {
      std::cout << " Read same epoch: " << gs_num_rd_sameepoch << " ("
                << (gs_num_rd_sameepoch * 100.0 / gs_num_rds) << "%)\n"
                << " Read exclusive: " << gs_num_rd_exclusive << " ("
                << (gs_num_rd_exclusive * 100.0 / gs_num_rds) << "%)\n"
                << " Read shared: " << gs_num_rd_shared << " ("
                << (gs_num_rd_shared * 100.0 / gs_num_rds) << "%)\n"
                << " Read share: " << gs_num_rd_share << " ("
                << (gs_num_rd_share * 100.0 / gs_num_rds) << "%)\n";
    }

    gs_num_wrs = gs_num_wr_shared + gs_num_wr_exclusive + gs_num_wr_sameepoch;
    std::cout << "Total number of writes: " << gs_num_wrs << "\n";
    if (gs_num_wrs) {
      std::cout << " Write same epoch: " << gs_num_wr_sameepoch << " ("
                << (gs_num_wr_sameepoch * 100.0 / gs_num_wrs) << "%)\n"
                << " Write exclusive: " << gs_num_wr_exclusive << " ("
                << (gs_num_wr_exclusive * 100.0 / gs_num_wrs) << "%)\n"
                << " Write shared: " << gs_num_wr_shared << " ("
                << (gs_num_wr_shared * 100.0 / gs_num_wrs) << "%)\n";
    }
#if DEBUG
    std::cout << "New reads: " << numrds << " New writes: " << numwrs << "\n";
    assert(numrds + numwrs == gs_num_recordmems);
#endif
    if (gs_num_rds + gs_num_wrs != gs_num_recordmems) {
      std::cout << "Total num reads: " << gs_num_rds << " Total num writes: " << gs_num_wrs << "\n";
    }
    assert(gs_num_rds + gs_num_wrs == gs_num_recordmems);
    std::cout << "***********END STATISTICS DUMP***********\n\n";
  }

  void accumulate(PerTaskStats* ptstats) {
    assert(ptstats->num_rds == ptstats->num_rd_exclusive + ptstats->num_rd_sameepoch +
                                   ptstats->num_rd_share + ptstats->num_rd_shared);
    assert(ptstats->num_wrs ==
           ptstats->num_wr_exclusive + ptstats->num_wr_sameepoch + ptstats->num_wr_shared);
    assert(ptstats->num_acqs == ptstats->num_rels);

    my_getlock(&gs_lock);
    gs_num_rd_sameepoch += ptstats->num_rd_sameepoch;
    gs_num_rd_exclusive += ptstats->num_rd_exclusive;
    gs_num_rd_shared += ptstats->num_rd_shared;
    gs_num_rd_share += ptstats->num_rd_share;

    gs_num_wr_shared += ptstats->num_wr_shared;
    gs_num_wr_exclusive += ptstats->num_wr_exclusive;
    gs_num_wr_sameepoch += ptstats->num_wr_sameepoch;

    if (pt_max_num_rds < ptstats->num_rds) {
      pt_max_num_rds = ptstats->num_rds;
    }
    if (pt_min_num_rds >= ptstats->num_rds) {
      pt_min_num_rds = ptstats->num_rds;
    }

    if (pt_max_num_wrs < ptstats->num_wrs) {
      pt_max_num_wrs = ptstats->num_wrs;
    }
    if (pt_min_num_wrs >= ptstats->num_wrs) {
      pt_min_num_wrs = ptstats->num_wrs;
    }

    gs_num_lock_acqs += ptstats->num_acqs;
    gs_num_lock_rels += ptstats->num_rels;

    if (max_task_depth < ptstats->depth) {
      max_task_depth = ptstats->depth;
    }

    auto vars_read = ptstats->get_num_vars_read();
    if (pt_max_vars_read < vars_read) {
      pt_max_vars_read = vars_read;
    }
    auto vars_written = ptstats->get_num_vars_written();
    if (pt_max_vars_written < vars_written) {
      pt_max_vars_written = vars_written;
    }
    my_releaselock(&gs_lock);
  }
};

#endif
