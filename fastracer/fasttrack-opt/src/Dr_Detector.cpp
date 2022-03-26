#include "Common.H"
#include "exec_calls.h"
#include "stats.h"
#include "tbb/concurrent_hash_map.h"
#include <bitset>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <vector>

using namespace std;
using namespace tbb;
#define USE_PINLOCK 1

#if USE_PINLOCK
typedef pair<PIN_LOCK, VarState*> subpair;
typedef pair<PIN_LOCK, subpair*> PAIR;
#else
typedef pair<tbb::atomic<size_t>, VarState*> subpair;
typedef pair<tbb::atomic<size_t>, subpair*> PAIR;
#endif

PAIR* shadow_space;
TaskState* tstate_nodes;
size_t* joinset;
my_lock shadow_space_lock(1);
//extern size_t num_dead_tasks;
//extern size_t dead_clock_value;

#if STATS
GlobalStats globalStats;
uint64_t numrds = 0;
uint64_t numwrs = 0;
#endif

#if DEBUG_TIME
Time_Task_Management time_task_management;
Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
// my_lock debug_lock(0);
// unsigned recordmemt = 0, recordmemn = 0;
// unsigned recordmemi = 0;
#endif

my_lock printLock(0); // Serialize print statements

size_t nthread = 0;
const size_t SS_PRIMARY_TABLE_ENTRIES = ((size_t)1024);
const size_t SS_SEC_TABLE_ENTRIES = ((size_t)4 * (size_t)1024 * (size_t)1024);

std::ofstream report;
#if TASK_GRAPH
my_lock graph_lock(0);
std::ofstream taskgraph;
#endif

std::map<ADDRINT, LockState*> lockmap; // only one lock
my_lock lock_map_lock(0);

std::ostream& operator<<(std::ostream& out, const FT_READ_TYPE value) {
  const char* s = 0;
#define PROCESS_VAL(p)                                                                             \
  case (p):                                                                                        \
    s = #p;                                                                                        \
    break;
  switch (value) {
    PROCESS_VAL(RD_EXCLUSIVE);
    PROCESS_VAL(RD_SAME_EPOCH);
    PROCESS_VAL(RD_SHARED_SAME_EPOCH);
    PROCESS_VAL(RD_SHARE);
    PROCESS_VAL(RD_SHARED);
    PROCESS_VAL(RD_INVALID);
  }
#undef PROCESS_VAL
  return out << s;
}

std::ostream& operator<<(std::ostream& out, const FT_WRITE_TYPE value) {
  const char* s = 0;
#define PROCESS_VAL(p)                                                                             \
  case (p):                                                                                        \
    s = #p;                                                                                        \
    break;
  switch (value) {
    PROCESS_VAL(WR_EXCLUSIVE);
    PROCESS_VAL(WR_SAME_EPOCH);
    PROCESS_VAL(WR_SHARED);
    PROCESS_VAL(WR_INVALID);
  }
#undef PROCESS_VAL
  return out << s;
}

#if REPORT_DATA_RACES
std::map<ADDRINT, struct Violation*> all_violations;
my_lock viol_lock(0);

void error(ADDRINT addr, size_t ftid, AccessType ftype, size_t stid, AccessType stype) {
  my_getlock(&viol_lock);
  all_violations.insert(make_pair(
      addr, new Violation(new ViolationData(ftid, ftype), new ViolationData(stid, stype))));
  my_releaselock(&viol_lock);
}
#endif

void TD_Activate() {
  size_t primary_length = (SS_PRIMARY_TABLE_ENTRIES) * sizeof(PAIR);
  shadow_space = (PAIR*)mmap(0, primary_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
  joinset = (size_t*)mmap(0, (MAX_NUM_TASKS + 1) * sizeof(size_t), PROT_READ | PROT_WRITE,
                          MMAP_FLAGS, -1, 0);
  tstate_nodes = (TaskState*)mmap(0, MAX_NUM_TASKS * sizeof(TaskState), PROT_READ | PROT_WRITE,
                                  MMAP_FLAGS, -1, 0);
  cur[0].push(0);
#if EPOCH_POINTER
  // Epoch for task 0
  tstate_nodes[0].epoch_ptr = createEpoch(1, 0);
#else
  tstate_nodes[0].clock = 1;
#endif
#if TASK_GRAPH
  my_getlock(&graph_lock);
  taskgraph.open("taskgraph.dot");
  taskgraph << "digraph program {\n ordering = out\n";
  my_releaselock(&graph_lock);
#endif
  //  assert(shadow_space != (void *)-1);
  // std::cout<<"TD_ACTIVATE END"<<std::endl;
}

inline size_t parent_clock(TaskState* cur_task, size_t taskId) {
  for (int i = 0; i < NUM_FIXED_TASK_ENTRIES; i++) {
    if (cur_task->cached_tid[i] == taskId)
      return cur_task->cached_clock[i];
  }
  if (cur_task->depth > NUM_FIXED_TASK_ENTRIES) {
    if(cur_task->my_vc != NULL){    
      std::unordered_map<size_t, size_t>::iterator it = (cur_task->my_vc)->find(taskId);
      if (it != (cur_task->my_vc->end()))
        return it->second;
    }
    if(cur_task->root_vc != NULL){    
      std::unordered_map<size_t, size_t>::iterator it = (cur_task->root_vc)->find(taskId);
      if (it != (cur_task->root_vc->end()))
        return it->second;
    }
  }
  return 0;
}

size_t find_root(int index) {
  int next_index = joinset[index];
  if (next_index != 0) {
    return find_root(next_index);
    //  joinset[index] = find_root(next_index);
    //  return joinset[index];
  } else
    return index;
}

extern "C" void RecordMem(size_t threadId, void* access_addr, AccessType accesstype) {
  // std::cout<<"RECORDMEM"<<std::endl;
  if (cur[threadId].empty())
    return;

  uint32_t cur_taskId = cur[threadId].top();
  TaskState* cur_task = &tstate_nodes[cur_taskId];
  uint32_t cur_clock = cur_task->getCurClock();

  // std::cout<< "CURTASK EPOCH="<<*(cur_task->epoch_ptr)<< " clk "<< cur_task->clock << " tid "<< cur_task->taskId<<std::endl;

  ADDRINT addr = (ADDRINT)access_addr;
#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_num_recordmems++;
  if (accesstype == READ) {
    numrds++;
  } else {
    numwrs++;
  }
  assert(numwrs + numrds == globalStats.gs_num_recordmems);
  my_releaselock(&globalStats.gs_lock);
#endif

#if 0
  my_getlock(&printLock);
  cout << "NUM_TASK_BITS: " << NUM_TASK_BITS << "\n"
       << "NUM_CLK_BITS: " << NUM_CLK_BITS << "\n"
       << "MAX_NUM_TASKS: " << MAX_NUM_TASKS << "\n"
       << "CLK_MASK: " << std::hex << CLK_MASK << "\n"
       << "EPOCH_BIT: " << std::hex << EPOCH_BIT << "\n"
       << "EPOCH_MASK:" << std::hex << EPOCH_MASK << "\n"
       << "TASKID_MASK: " << std::hex << TASKID_MASK << "\n";
  my_releaselock(&printLock);
  exit(1);
#endif

  assert(cur_task != NULL);

  size_t primary_index = (addr >> 22) & 0x3ff;
  PAIR* x = shadow_space + primary_index;

#if USE_PINLOCK
  PIN_GetLock(&(x->first), 0);
#else
  my_getlock(&(x->first));
#endif
  if (x->second == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(subpair);
    subpair* primary_entry =
        (subpair*)mmap(0, sec_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    x->second = primary_entry;
  }
#if USE_PINLOCK
  PIN_ReleaseLock(&(x->first));
#else
  my_releaselock(&(x->first));
#endif
  subpair* primary_entry = x->second;
  size_t offset = (addr & 0x3fffff);
  subpair* addrpair = primary_entry + offset;
  VarState* var_state = (addrpair->second);
  uint32_t access_clock = 0;
  uint32_t access_taskId = 0;
  uint32_t access_depth = 0;
  TaskState* access_task = NULL;
  uint32_t* access_vc = NULL;

  if (var_state != NULL) {
    // Have the common cases out of the critical section
    if (var_state->isRacy()) {
      // Data race already detected, so skip doing anything
#if STATS
      if (accesstype == READ) {
        cur_task->m_task_stats.num_racy_rds++;
      } else {
        cur_task->m_task_stats.num_racy_wrs++;
      }
#endif
      return;
    }

    size_t var_rd_md = var_state->get_m_rd_epoch();
    // Assert
    // if(var_rd_md != var_state->old_m_rd_epoch){
    // There is a race here, as var_state is not locked yet.
    //   std::cout<<var_state->old_m_rd_epoch << "==" << *(var_state->m_rd_epoch) << " == "<<var_rd_md<<std::endl;
    //   assert(var_rd_md == var_state->old_m_rd_epoch);
    // }

#if EPOCH_POINTER
    if (var_rd_md > 0 && accesstype == READ) { // Subsequent access
      if (cur_taskId == var_state->getReadEpochTaskID(var_rd_md) &&
          cur_clock == var_state->getReadEpochClock(var_rd_md)) {
        // RD SAME EPOCH
        // #if STATS
        //           (cur_task->m_task_stats).track_read(addr, RD_SAME_EPOCH);
        // #endif
        return;
      }
      // Not RD SAME EPOCH
    }
#if READ_SHARED_SAME_EPOCH_FLAG
    else if (var_state->isReadVector() && accesstype == READ) {
      // SB: We intentionally load m_rvc in a local variable, and then invoke find() and end()
      // separately. Otherwise it can violate atomicity of the two invocations on m_rvc.
      auto rvc = var_state->m_rvc;
      auto it = rvc.find(cur_taskId);
      if (it != rvc.end() && it->second == cur_clock) {
        // RD SHARED SAME EPOCH
        // #if STATS
        //           (cur_task->m_task_stats).track_read(addr, RD_SHARED_SAME_EPOCH);
        // #endif
        return;
      }
    }
#endif
#else
    // not epoch_pointer
    if (var_rd_md > 0 && accesstype == READ) { // Subsequent access
      if (!var_state->isReadVector()) {        // Read same epoch
        if (cur_taskId == var_state->getReadEpochTaskID(var_rd_md) &&
            cur_clock == var_state->getReadEpochClock(var_rd_md)) {
          // RD SAME EPOCH
          // #if STATS
          //           (cur_task->m_task_stats).track_read(addr, RD_SAME_EPOCH);
          // #endif
          return;
        }
        // Not RD SAME EPOCH
      }
#if READ_SHARED_SAME_EPOCH_FLAG
      else {
        // SB: We intentionally load m_rvc in a local variable, and then invoke find() and end()
        // separately. Otherwise it can violate atomicity of the two invocations on m_rvc.
        auto rvc = var_state->m_rvc;
        auto it = rvc.find(cur_taskId);
        if (it != rvc.end() && it->second == cur_clock) {
          // RD SHARED SAME EPOCH
          // #if STATS
          //           (cur_task->m_task_stats).track_read(addr, RD_SHARED_SAME_EPOCH);
          // #endif
          return;
        }
      }
#endif
    }
#endif

    // Common case, WR SAME EPOCH
    size_t var_wr_md = var_state->get_m_wr_epoch();
    // Assert
    // assert(var_wr_md == var_state->old_m_wr_epoch);
    if (var_wr_md > 0 && accesstype == WRITE) {
      if (var_state->getWriteTaskID(var_wr_md) == cur_taskId &&
          var_state->getWriteClock(var_wr_md) == cur_clock) {
        // #if STATS
        //         (cur_task->m_task_stats).track_write(addr, WR_SAME_EPOCH);
        // #endif
        return;
      }
    }
  }
#if USE_PINLOCK
  PIN_GetLock(&(addrpair->first), 0);
#else
  my_getlock(&(addrpair->first));
#endif

  if (var_state != NULL && accesstype == READ) {
    // #if STATS
    //     auto vstats = var_state->m_var_stats;
    //     vstats.track_read(cur_taskId);
    //     my_getlock(&globalStats.gs_lock);
    //     globalStats.pv_max_rd_tasks = std::max(globalStats.pv_max_rd_tasks, vstats.get_num_rd_tasks());
    //     globalStats.pv_max_num_rds = std::max(globalStats.pv_max_num_rds, vstats.get_num_rds());
    //     my_releaselock(&(globalStats.gs_lock));
    // #endif

#if 0
    my_getlock(&printLock);
    cout << "Begin read: address: " << std::showbase << std::hex << addr << std::dec
         << " Task id: " << cur_taskId << " Cur clock: " << std::hex << cur_clock << "\n";
    my_releaselock(&printLock);
#endif

    auto rd_epoch = var_state->get_m_rd_epoch();
    auto wr_epoch = var_state->get_m_wr_epoch();

    // Assert
    // assert(rd_epoch == var_state->old_m_rd_epoch);
    // assert(wr_epoch == var_state->old_m_wr_epoch);

#if 0
    my_getlock(&printLock);
    cout << std::showbase << std::hex << "Read epoch: " << rd_epoch << " Write epoch: " << wr_epoch
         << "\n";
    cout << std::dec << "Task id: " << cur_taskId << " has reached here read1\n";
    my_releaselock(&printLock);
#endif

    size_t wtid = var_state->getWriteTaskID(wr_epoch);
    size_t wclk = var_state->getWriteClock(wr_epoch);

#if 0
    my_getlock(&printLock);
    cout << "Write tid: " << wtid << " write clock: " << wclk << "\n";
    my_releaselock(&printLock);
#endif

    // Check for write-read race, independent of the read metadata
    // FIXME: Why not (wtid != cur_taskId && wclk > 0)?
    if (wclk > 0) {
      access_taskId = wtid;
      access_clock = 0;
      if (cur_task->getTaskId() == access_taskId)
        access_clock = cur_task->getCurClock();
      else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
        uint32_t root_tid = find_root(access_taskId + 1) - 1;
        if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
          access_clock = INT_MAX;
#endif
      }
      // globalStats.update_elemtime(elem_start);
      if (wclk > access_clock) {
#if DEBUG
        my_getlock(&printLock);
        cout << "Detected write-read race between Task: " << wtid << " and Task: " << cur_taskId
             << "\n";
        my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
        error(addr, wtid, WRITE, cur_taskId, READ);
#endif
        var_state->setRacy();
#if STATS
        cur_task->m_task_stats.num_racy_rds++;
#endif
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
        // globalStats.update_RecordMemtime(RecordMem_start);
        return;
      }
      // No race
    }

    if (!var_state->isReadVector()) { // Read epoch
      size_t last_rd_tid = var_state->getReadEpochTaskID(rd_epoch);
      auto last_rd_clk = var_state->getReadEpochClock(rd_epoch);

      // FIXME: What about the RD EXCLUSIVE case with (last_rd_tid == cur_taskId)?
      // The previous read is from the same task, made a special case to avoid the time
      // inefficient element call. The following is okay for a different last reader.

      // FIXME: The following check seems new.
      access_clock = 0;
      if (last_rd_clk > 0) {
        access_taskId = last_rd_tid;
        access_clock = 0;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
#endif
        }
      }
      if (last_rd_clk <= access_clock) {
        // RD EXCLUSIVE
        // Non-concurrent read with previous read, the previous read is not from same task
// #if STATS
//         (cur_task->m_task_stats).track_read(addr, RD_EXCLUSIVE);
// #endif
#if EPOCH_POINTER
        // Slimfast: Var epoch update
        var_state->m_rd_epoch = cur_task->epoch_ptr;
#else
        var_state->m_rd_epoch = var_state->createReadEpoch(cur_taskId, cur_clock);
#endif
        // assert(var_state->old_m_rd_epoch == *(var_state->m_rd_epoch));
      } else {
        // Concurrent last reader, upgrade epoch to vector clock, READ SHARE
        // #if STATS
        //         (cur_task->m_task_stats).track_read(addr, RD_SHARE);
        // #endif
        var_state->createReadVector(addr, last_rd_tid, last_rd_clk, cur_taskId, cur_clock);
      }
#if USE_PINLOCK
      PIN_ReleaseLock(&(addrpair->first));
#else
      my_releaselock(&(addrpair->first));
#endif
      // globalStats.update_RecordMemtime(RecordMem_start);
      return;
    }

    // RD SHARED
    assert(var_state->isReadVector());
    // #if STATS
    //     (cur_task->m_task_stats).track_read(addr, RD_SHARED);
    // #endif

#if ENABLE_VECTOR
    vector<size_t>::iterator it;
    for (it = (var_state->m_rvc).begin(); it != (var_state->m_rvc).end(); ++it) {
      if (getReadEpochTaskID(*it) == cur_taskId) {
        break;
      }
    }
#if EPOCH_POINTER
    auto tmp = createEpoch(cur_clock, cur_taskId);
#else
    auto tmp = createReadEpoch(cur_taskId, cur_clock);
#endif
    if (it == (var_state->m_rvc).end()) {
      (var_state->m_rvc).push_back(tmp);
    } else {
      *it = tmp;
    }
#else
#if ENABLE_MAPS
    (var_state->m_rvc).insert(std::make_pair(cur_taskId, cur_clock));
#else
    var_state->m_rvc[cur_taskId] = cur_clock;
#endif
#endif
#if USE_PINLOCK
    PIN_ReleaseLock(&(addrpair->first));
#else
    my_releaselock(&(addrpair->first));
#endif
    // globalStats.update_RecordMemtime(RecordMem_start);
    return;
  }

  if (var_state != NULL && accesstype == WRITE) {
    // #if STATS
    //     auto vstats = var_state->m_var_stats;
    //     vstats.track_write(cur_taskId);
    //     my_getlock(&globalStats.gs_lock);
    //     globalStats.pv_max_wr_tasks = std::max(globalStats.pv_max_wr_tasks, vstats.get_num_wr_tasks());
    //     globalStats.pv_max_num_wrs = std::max(globalStats.pv_max_num_wrs, vstats.get_num_wrs());
    //     my_releaselock(&globalStats.gs_lock);
    // #endif

#if 0
    my_getlock(&printLock);
    cout << "Begin write: address: " << std::showbase << std::hex << addr << std::dec
          << " Task id: " << cur_taskId << " Cur clock: " << curclk << "\n";
    my_releaselock(&printLock);
#endif

    auto wr_md = var_state->get_m_wr_epoch();
    //Assert
    // assert(wr_md == var_state->old_m_wr_epoch);
    // Cannot be WR SAME EPOCH
    assert(var_state->getWriteTaskID(wr_md) != cur_taskId ||
           var_state->getWriteClock(wr_md) != cur_clock);

    auto last_wtid = var_state->getWriteTaskID(wr_md);
    auto last_wclk = var_state->getWriteClock(wr_md);

#if DEBUG
    size_t tmpwtid = ((var_state->m_wr_epoch) & TASKID_MASK) >>
                     NUM_CLK_BITS; // ((var_state->m_wr_epoch) >> (64 - NUM_TASK_BITS));
    assert(last_wtid == tmpwtid);
#endif

#if 0
      my_getlock(&printLock);
      cout << std::dec << "Task id: " << cur_taskId << std::showbase << std::hex << " Read md: " << rd_md
          << " Write md: " << var_state->m_wr_epoch << "\n";
      my_releaselock(&printLock);
#endif

    // Check for W-W race

    // FIXME: Is the following check not more natural: if (wr_md > 0 && last_wtid != cur_taskId && w_clk >0)?
    // There is a previous write from a different thread

    if (last_wclk > 0) {
      access_taskId = last_wtid;
      access_clock = 0;
      if (cur_task->getTaskId() == access_taskId)
        access_clock = cur_task->getCurClock();
      else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
        uint32_t root_tid = find_root(access_taskId + 1) - 1;
        if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
          access_clock = INT_MAX;
#endif
      }

      if (last_wclk > access_clock) {
#if DEBUG
        my_getlock(&printLock);
        cout << "Detected write-write race between Task: " << last_wtid
             << " and Task: " << cur_taskId << "\n";
        my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
        error(addr, last_wtid, WRITE, cur_taskId, WRITE);
#endif
        var_state->setRacy();
// #if STATS
//         cur_task->m_task_stats.num_racy_wrs++;
// #endif
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
        // globalStats.update_RecordMemtime(RecordMem_start);
        return;
      }
    }

    if (wr_md > 0 && last_wtid == cur_taskId) {
      // Otherwise, it should be the WR SAME EPOCH case
      assert(last_wclk < cur_clock);
    }

    if (!var_state->isReadVector()) {
      // WRITE EXCLUSIVE, can be same thread or different thread
      auto rd_epoch = var_state->get_m_rd_epoch();
      // Assert
      // assert(rd_epoch == var_state->old_m_rd_epoch);
      auto rd_clk = var_state->getReadEpochClock(rd_epoch);
      auto rd_tid = var_state->getReadEpochTaskID(rd_epoch);

#if DEBUG
      size_t tmprtid = ((var_state->m_rd_epoch) & TASKID_MASK) >> NUM_CLK_BITS;
      //((var_state->m_rd_epoch) >> (64 - NUM_TASK_BITS)) & (~((size_t)1 << (NUM_TASK_BITS - 1)));
      assert(tmprtid == rd_tid);
#endif
#if 0
          my_getlock(&printLock);
          cout << "Read tid: " << rd_tid << " read clock: " << rd_clk << " Write clock: " << last_wclk
              << " write tid: " << last_wtid << "\n";
          my_releaselock(&printLock);
#endif

      // Check for a read-write data race
      // FIXME: The following seems more correct: if (rd_epoch > 0 && rd_tid != cur_taskId && rd_clk >0).

      if (rd_clk > 0) {
        access_taskId = rd_tid;
        access_clock = 0;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
#endif
        }

        if (rd_clk > access_clock) {
#if DEBUG
          my_getlock(&printLock);
          cout << "Detected read-write race on address " << std::showbase << std::hex << addr
               << std::dec << " between Task: " << rd_tid << " and Task: " << cur_taskId << "\n";
          my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
          error(addr, rd_tid, READ, cur_taskId, WRITE);
#endif
          var_state->setRacy();
// #if STATS
//           cur_task->m_task_stats.num_racy_wrs++;
// #endif
#if USE_PINLOCK
          PIN_ReleaseLock(&(addrpair->first));
#else
          my_releaselock(&(addrpair->first));
#endif
          // globalStats.update_RecordMemtime(RecordMem_start);
          return;
        }
        // No read-write race
      }
#if EPOCH_POINTER
      // Slimfast: Var epoch update
      // Reset read information
      var_state->m_rd_epoch = empty_epoch;
      // Update write information
      var_state->m_wr_epoch = cur_task->epoch_ptr;
#else
      // Reset read information
      var_state->m_rd_epoch = 0;
      // Update write information
      var_state->m_wr_epoch = var_state->createWriteEpoch(cur_taskId, cur_clock);
#endif
      // Assert
      // var_state->old_m_rd_epoch = 0;
      // var_state->old_m_wr_epoch = var_state->createWriteEpoch(cur_taskId, cur_clock);
      // assert(*(var_state->m_rd_epoch) == var_state->old_m_rd_epoch);
      // assert(*(var_state->m_wr_epoch) == var_state->old_m_wr_epoch);
// #if STATS
//       (cur_task->m_task_stats).track_write(addr, WR_EXCLUSIVE);
// #endif
#if USE_PINLOCK
      PIN_ReleaseLock(&(addrpair->first));
#else
      my_releaselock(&(addrpair->first));
#endif
      // globalStats.update_RecordMemtime(RecordMem_start);
      return;
    }

    // WR SHARED
    assert(var_state->isReadVector());

    // Check for R-W races for each reader task in the read vector
#if ENABLE_MAPS
    auto rvc = var_state->m_rvc;
    for (auto it = rvc.begin(); it != rvc.end(); ++it) {
      if (it->second == 0)
        continue; // No need to do race check if read_tid == curtid
      access_taskId = it->first;
      access_clock = 0;
      if (cur_task->getTaskId() == access_taskId)
        access_clock = cur_task->getCurClock();
      else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
        uint32_t root_tid = find_root(access_taskId + 1) - 1;
        if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
          access_clock = INT_MAX;
#endif
      }
      if (it->second > access_clock) { // Read-write race with reader task
        var_state->setRacy();
// #if STATS
//         cur_task->m_task_stats.num_racy_wrs++;
// #endif
#if DEBUG
        my_getlock(&printLock);
        cout << "Detected read-write race on address: " << std::showbase << std::hex << addr
             << std::dec << " between Task: " << it->first << " and Task: " << cur_taskId << "\n";
        my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
        error(addr, it->first, READ, cur_taskId, WRITE);
#endif
        var_state->m_rvc.clear();
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
        // globalStats.update_RecordMemtime(RecordMem_start);
        return;
      }
    }
    // No read-write race was detected, reset to epoch state
    var_state->clearReadVector();
#else
#if ENABLE_VECTORS
    auto rvc = var_state->m_rvc;
    for (auto it = rvc.begin(); it != rvc.end(); ++it) {
      if (getReadEpochTaskID(*it) != cur_taskId &&
          getReadEpochClock(*it) > element(cur_task, getReadEpochTaskID(*it))) {
        var_state->setRacy();
// #if STATS
//         cur_task->m_task_stats.num_racy_wrs++;
// #endif
#if DEBUG
        my_getlock(&printLock);
        cout << "Detected read-write race on address: " << std::showbase << std::hex << addr
             << std::dec << " between Task: " << getTaskID(*it) << " and Task: " << cur_taskId
             << "\n ";
        my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
        error(addr, getTaskID(*it), READ, cur_taskId, WRITE);
#endif
        var_state->m_rvc.clear();
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
        // globalStats.update_RecordMemtime(RecordMem_start);
        return;
      }
      // No read-write race was detected, reset to epoch state
      var_state->clearReadVector();
    }
#else
    auto rvc = var_state->m_rvc;
    for (size_t i = 0; i < MAX_NUM_TASKS; i++) {
      if (i != cur_taskId && rvc[i] > element(cur_task, i)) {
        var_state->setRacy();
// #if STATS
//         cur_task->m_task_stats.num_racy_wrs++;
// #endif
#if DEBUG
        my_getlock(&printLock);
        cout << "Detected read-write race on address: " << std::showbase << std::hex << addr
             << std::dec << " between Task: " << i << " and Task:" << cur_taskId << "\n";
        my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
        error(addr, i, READ, cur_taskId, WRITE);
#endif
        var_state->m_rvc = {0};
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
        // globalStats.update_RecordMemtime(RecordMem_start);
        return;
      }
    }
    // No read-write race was detected, reset to epoch state
    var_state->clearReadVector();
#endif
#endif

    assert(!var_state->isReadVector());

#if EPOCH_POINTER
    // Read information is reset, update write information
    // Slimfast: Var epoch update
    var_state->m_wr_epoch = cur_task->epoch_ptr;
#else
    var_state->m_wr_epoch = var_state->createWriteEpoch(cur_taskId, cur_clock);
#endif
    // Assert
    // assert(var_state->old_m_wr_epoch == *(var_state->m_wr_epoch));
// #if STATS
//     (cur_task->m_task_stats).track_write(addr, WR_SHARED);
// #endif
#if USE_PINLOCK
    PIN_ReleaseLock(&(addrpair->first));
#else
    my_releaselock(&(addrpair->first));
#endif
    // globalStats.update_RecordMemtime(RecordMem_start);
    return;
  }

  // Uncommon case

  if (var_state == NULL && accesstype == READ) {
    var_state = new VarState();

#if EPOCH_POINTER
    // Slimfast: Var epoch update
    var_state->m_rd_epoch = cur_task->epoch_ptr;
#else
    var_state->m_rd_epoch = var_state->createReadEpoch(cur_taskId, cur_clock);
#endif

    // Assert
    // assert(var_state->old_m_rd_epoch == *(var_state->m_rd_epoch));
    addrpair->second = var_state;

// #if STATS
//     (task_state->m_task_stats).track_read(addr, RD_EXCLUSIVE);
//     auto vstats = var_state->m_var_stats;
//     vstats.track_read(tid);
//     my_getlock(&globalStats.gs_lock);
//     auto max_rds_tasks = vstats.get_num_rd_tasks();
//     globalStats.pv_max_rd_tasks = std::max(globalStats.pv_max_rd_tasks, max_rds_tasks);
//     globalStats.pv_max_num_rds = std::max(globalStats.pv_max_num_rds, vstats.get_num_rds());
//     my_releaselock(&globalStats.gs_lock);
// #endif
#if USE_PINLOCK
    PIN_ReleaseLock(&(addrpair->first));
#else
    my_releaselock(&(addrpair->first));
#endif
    // globalStats.update_RecordMemtime(RecordMem_start);
    return;
  }

  if (var_state == NULL && accesstype == WRITE) {
    var_state = new VarState();
#if EPOCH_POINTER
    // Slimfast: Var epoch update
    var_state->m_wr_epoch = cur_task->epoch_ptr;
    // var_state->set_m_wr_epoch(cur_task->epoch_ptr);
#else
    var_state->m_wr_epoch = var_state->createWriteEpoch(cur_taskId, cur_clock);
#endif
    // Assert
    // assert(var_state->old_m_wr_epoch == *(var_state->m_wr_epoch));
#if 0
    my_getlock(&printLock);
    cout << std::hex << "Address: " << addr << " write epoch: " << var_state->m_wr_epoch << " Tid"
         << tid << " cur clock: " << curclock << "\n";
    my_releaselock(&printLock);
#endif
    addrpair->second = var_state;

// #if STATS
//     (task_state->m_task_stats).track_write(addr, WR_EXCLUSIVE);
//     auto vstats = var_state->m_var_stats;
//     vstats.track_write(tid);
//     my_getlock(&globalStats.gs_lock);
//     auto num_wrs_tasks = vstats.get_num_wr_tasks();
//     globalStats.pv_max_wr_tasks = std::max(globalStats.pv_max_wr_tasks, num_wrs_tasks);
//     globalStats.pv_max_num_wrs = std::max(globalStats.pv_max_num_wrs, vstats.get_num_wrs());
//     my_releaselock(&globalStats.gs_lock);
// #endif
#if USE_PINLOCK
    PIN_ReleaseLock(&(addrpair->first));
#else
    my_releaselock(&(addrpair->first));
#endif
    // globalStats.update_RecordMemtime(RecordMem_start);
    return;
  }

  assert(false && "Should not reach here!");
}

extern "C" void RecordAccess(size_t cur_taskId, void* access_addr, AccessType accesstype) {
  // RecordMem(cur_taskId, access_addr, accesstype);

#if 0
  assert(access_addr != NULL);
  my_getlock(&printLock);
  std::cout << "RecordAccess: Task id: " << std::dec << cur_taskId << "\tAddress: " << access_addr << "\t"
            << ((accesstype == READ) ? "READ" : "WRITE") << "\n";
  my_releaselock(&printLock);
#endif

#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_num_recordaccess++;
  my_releaselock(&globalStats.gs_lock);
#endif
}

void CaptureLockAcquire(size_t threadId, ADDRINT lock_addr) {
  size_t cur_taskId = cur[threadId].top();
  TaskState* cur_task = &tstate_nodes[cur_taskId];

  my_getlock(&lock_map_lock);
  LockState* l = NULL;
  if (lockmap.find(lock_addr) == lockmap.end()) {
    l = new LockState();
    lockmap.insert(pair<ADDRINT, LockState*>(lock_addr, l));
  }
  l = lockmap[lock_addr];
  my_releaselock(&lock_map_lock);
  assert(l != NULL);

  // #if STATS
  //   PerTaskStats ptstats = t->m_task_stats;
  //   ptstats.track_acq(lock_addr);
  //   ptstats.lock_nesting_depth++;
  //   ptstats.max_lock_nesting_depth =
  //       std::max(ptstats.max_lock_nesting_depth, ptstats.lock_nesting_depth);
  //   my_getlock(&globalStats.gs_lock);
  //   globalStats.gs_num_lock_acqs++;
  //   globalStats.pt_max_num_acqs = std::max(globalStats.pt_max_num_acqs, ptstats.num_acqs);
  //   my_releaselock(&globalStats.gs_lock);

  // #endif

  my_getlock(&(l->m_lock));
  // Perform VC join of lock's vc and task's vc into task's vc, lock's vc is not modified
  int cur_depth = cur_task->depth;
  for (auto it = (l->m_lvc).begin(); it != (l->m_lvc).end(); ++it) {
    if (it->first == (cur_task->getTaskId()))
      continue;
    bool found = false;
    for (int i = 0; i < min(NUM_FIXED_TASK_ENTRIES, cur_depth); i++)
      if (cur_task->cached_tid[i] == it->first) {
        cur_task->cached_clock[i] = max(cur_task->cached_clock[i], it->second);
        found = true;
        break;
      }
    if (found)
      continue;
    if (cur_depth < NUM_FIXED_TASK_ENTRIES) {
      cur_task->cached_tid[cur_depth] = it->first;
      cur_task->cached_clock[cur_depth] = it->second;
      cur_depth++;
      continue;
    }
    if (cur_task->my_vc == NULL)
      cur_task->my_vc = new unordered_map<size_t, size_t>();
    auto it_task = cur_task->my_vc->find(it->first);
    if (it_task == cur_task->my_vc->end()) {
      (*(cur_task->my_vc))[it->first] = it->second;
      cur_depth++;
    } else if (it_task->second < it->second)
      (*(cur_task->my_vc))[it->first] = it->second;
  }
  cur_task->depth = cur_depth;
  my_releaselock(&l->m_lock);

  //  TaskState* cur_cur_task = t;
  //  if(cur_cur_task->child_m_vc.size() > MAX_MVC_SIZE){
  //  RootVc* root_vc = new RootVc();
  //  root_vc->m_vc = cur_cur_task->child_m_vc;
  //  if(cur_cur_task->child_root_vc)
  //  root_vc->m_vc.insert((cur_cur_task->child_root_vc->m_vc).begin(), (cur_cur_task->child_root_vc->m_vc).end());
  //  cur_cur_task->child_root_vc = root_vc;
  //#if STATS
  //  my_getlock(&globalStats.gs_lock);
  //  globalStats.gs_totTaskvcSize += root_vc->m_vc.size() - cur_cur_task->child_m_vc.size();
  //    my_releaselock(&globalStats.gs_lock);
  //#endif
  //  cur_cur_task->child_m_vc.clear();
  //  }
  //  else if(cur_cur_task->child_root_vc == NULL && cur_cur_task->m_vc.size() > MAX_MVC_SIZE){
  //   RootVc* root_vc = new RootVc();
  //   root_vc->m_vc = cur_cur_task->m_vc;
  //   if(cur_cur_task->root_vc)
  //   root_vc->m_vc.insert((cur_cur_task->root_vc->m_vc).begin(), (cur_cur_task->root_vc->m_vc).end());
  //   cur_cur_task->child_root_vc = root_vc;
  //#if STATS
  //   my_getlock(&globalStats.gs_lock);
  //   globalStats.gs_totTaskvcSize += root_vc->m_vc.size() - cur_cur_task->child_m_vc.size();
  //     my_releaselock(&globalStats.gs_lock);
  //#endif
  //   cur_cur_task->child_m_vc.clear();
  //  }
  //
  //
}

void CaptureLockRelease(size_t threadId, ADDRINT lock_addr) {
  size_t cur_taskId = cur[threadId].top();
  TaskState* cur_task = &tstate_nodes[cur_taskId];
  int cur_depth = cur_task->depth;

  my_getlock(&lock_map_lock);
  LockState* l = lockmap.at(lock_addr);
  my_releaselock(&lock_map_lock);
  assert(l != NULL);

  // #if STATS
  //   PerTaskStats ptstats = t->m_task_stats;
  //   ptstats.track_rel(lock_addr);
  //   ptstats.lock_nesting_depth--;
  //   my_getlock(&globalStats.gs_lock);
  //   globalStats.gs_num_lock_rels++;
  //   if (globalStats.pt_max_num_rels < ptstats.num_rels)
  //     globalStats.pt_max_num_rels = ptstats.num_rels;
  //   my_releaselock(&globalStats.gs_lock);
  // #endif

  // Perform VC copy and increment.
  my_getlock(&l->m_lock);

  l->m_lvc.clear();
  std::unordered_map<uint32_t, uint32_t> combined_vc_map;
  for (int i = 0; i < min(NUM_FIXED_TASK_ENTRIES, cur_depth); i++)
    combined_vc_map[cur_task->cached_tid[i]] = cur_task->cached_clock[i];
  if (cur_task->my_vc != NULL)
    combined_vc_map.insert((*(cur_task->my_vc)).begin(), (*(cur_task->my_vc)).end());
  l->m_lvc = combined_vc_map;
  l->m_lvc[cur_taskId] = cur_task->getCurClock();
#if EPOCH_POINTER
  // Slimfast: Task epoch update
  cur_task->epoch_ptr = createEpoch(cur_task->getCurClock() + 1, cur_task->getTaskId());
#else
  cur_task->clock++;
#endif
  my_releaselock(&(l->m_lock));
}

void paccesstype(AccessType accesstype) {
  if (accesstype == READ) {
    report << "READ\n";
  }
  if (accesstype == WRITE) {
    report << "WRITE\n";
  }
}

extern "C" void Fini() {
  size_t threadId = get_cur_tid();
  size_t cur_taskId = cur[threadId].top();
  TaskState* cur_task = &tstate_nodes[cur_taskId];
  assert(cur_taskId == 0);

#if STATS
  PerTaskStats ptstats = cur_task->m_task_stats;
  // assert(ptstats != NULL);
  globalStats.accumulate(ptstats);

  // for (auto it = lockmap.begin(); it != lockmap.end(); ++it) {
  //   globalStats.max_acq_locks =
  //       std::max((it->second)->m_lock_stats.tid_set.size(), globalStats.max_acq_locks);
  // }

  globalStats.dump();
#endif

#if REPORT_DATA_RACES
  report.open("violations.out");
  for (std::map<ADDRINT, struct Violation*>::iterator i = all_violations.begin();
       i != all_violations.end(); i++) {
    struct Violation* viol = i->second;
    report << "** Data Race Detected **\n";
    report << " Address is :";
    report << i->first;
    report << "\n";
    report << viol->a1->tid;
    paccesstype(viol->a1->accessType);
    report << viol->a2->tid;
    paccesstype(viol->a2->accessType);
    report << "**************************************\n";
  }
  cout << "Number of violations = " << all_violations.size() << endl;
  report.close();
#endif

#if DEBUG_TIME
  time_task_management.dump();
  time_dr_detector.dump();
  // std::cout << "DEBUG_TIME Mode\n";
  // std::cout << "Number of tasks spawned " << task_id_ctr << endl;
  // std::cout << "Total time in recordmem function " << recordmemt << " milliseconds" << endl;
  // std::cout << "Total time in recordmem function without read and write  " << recordmemi
  //           << "milliseconds" << endl;
  // std::cout << "Total time in recordmem function without read and write in percentage "
  //           << (recordmemi * 100.0) / recordmemt << endl;
  // std::cout << "No of times recordmem is called is " << recordmemn << endl;
#endif

#if TASK_GRAPH
  my_getlock(&graph_lock);
  std::cout << "acquired_lock in finii\n";
  taskgraph << "}\n";
  taskgraph.close();
  my_releaselock(&graph_lock);
  std::cout << "released_lock in finii\n";
#endif
}

void init_rwlock(rwlock_t* lock) { lock->value = 0x1000000000000; }

/*
  Return value is as follows,
   0 ==> result is zero
   1 ==> result is positive
   -1 ==> result is negative
*/
int atomic_add(long* ptr, long val) {
  int ret = 0;
  asm volatile("lock add %%rsi, (%%rdi);"
               "pushf;"
               "pop %%rax;"
               "movl %%eax, %0;"
               : "=r"(ret)
               :
               : "memory", "rax");

  if (ret & 0x80)
    ret = -1;
  else if (ret & 0x40)
    ret = 0;
  else
    ret = 1;
  return ret;
}

void write_lock(rwlock_t* lock) {
  long adder = -1 * 0x1000000000000;
  while (lock->value < 0x1000000000000)
    sched_yield();
  int ret = 1;
  while (1) {
    ret = atomic_add(&(lock->value), adder);
    if (ret)
      atomic_add(&(lock->value), -1 * adder);
    else
      break;
  }
}

void write_unlock(rwlock_t* lock) {
  long adder = 0x1000000000000;
  atomic_add(&(lock->value), adder);
}

void read_lock(rwlock_t* lock) {
  int ret = 1;
  while (1) {
    ret = atomic_add(&(lock->value), -1);
    if (ret == -1)
      atomic_add(&(lock->value), 1);
    else
      break;
  }
}

void read_unlock(rwlock_t* lock) { atomic_add(&(lock->value), 1); }
