#ifndef EXEC_CALLS_H
#define EXEC_CALLS_H

#pragma GCC push_options
#pragma GCC optimize("O0")

#include "Common.H"
#include "debug_time.h"
#include "stats.h"
#include "tbb/concurrent_hash_map.h"
#include "tbb/task.h"
#include "tbb/tbb_thread.h"
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <pthread.h>
#include <stack>
#include <unordered_map>
#include <vector>

using namespace tbb;
#if DEBUG_TIME
extern Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
#endif

extern "C" void TD_Activate();

// RootVc class to hold VC of root and all successors access this map.
//class RootVc {
//public:
//  std::unordered_map<size_t, size_t> m_vc;
//};
//
//class Order {
//	public:
//	size_t vc[30];
//	size_t vc_max = 30;
//	size_t vc_cur = 0;
//};
class TaskState {
public:
  uint32_t m_vc[MAX_TASK_DEPTH];
  size_t depth = 0;

  uint32_t cached_tid[NUM_FIXED_TASK_ENTRIES];
  uint32_t cached_clock[NUM_FIXED_TASK_ENTRIES];
  uint32_t lockset;
  uint32_t parent_taskId = 0;
  my_lock tlock = 0;
  task* mytask = NULL;
  std::list<TaskState*>* child_list;
  std::unordered_map<size_t, size_t>* my_vc = NULL;
  std::unordered_map<size_t, size_t>* root_vc = NULL;

#if EPOCH_POINTER
  // Slimfast: epoch_ptr contains pointer to epoch, not separate clock or taskid required.
  epoch epoch_ptr;
#else
  uint32_t taskId = UINT32_MAX;
  uint32_t clock = 0; // Just the scalar clock, no need to combine the task id
#endif

  inline uint32_t getCurClock() {
#if EPOCH_POINTER
    return (uint32_t)((*epoch_ptr) & CLK_MASK);
#else
    return clock;
#endif
  }
  inline uint32_t getTaskId() {
#if EPOCH_POINTER
    uint32_t tid = (uint32_t)(((*epoch_ptr) & TASKID_MASK) >> NUM_CLK_BITS);
    assert(/*tid >= 0 &&*/ tid < MAX_NUM_TASKS);
    return tid;
#else
    return taskId;
#endif
  }

  //  bool complete = false;
  //  bool execute = false;
  //  RootVc* root_vc = NULL;
  //  std::unordered_map<size_t, size_t> m_vc;
  //  RootVc* child_root_vc = NULL;
  //  std::unordered_map<size_t, size_t> child_m_vc;
  //  // std::vector<size_t> order;
  //  Order* order = NULL;
  //  my_lock child_lock = 0;

#if STATS
  PerTaskStats task_stats;
#endif

  TaskState() {
    tlock = 0;
    lockset = 0;
    parent_taskId = 0;
    mytask = NULL;

#if EPOCH_POINTER
    epoch_ptr = empty_epoch;
#else
    clock = 0;
    taskId = 0;
#endif
  }
};

class LockState {
public:
#if LINE_NO_PASS
  int line_no_r1 = -1, line_no_r2 = -1, line_no_w1 = -1, line_no_w2 = -1;
#endif
  size_t lockset;

#if EPOCH_POINTER
  epoch m_rd1_epoch = empty_epoch;
  epoch m_rd2_epoch = empty_epoch;

  epoch m_wr1_epoch = empty_epoch;
  epoch m_wr2_epoch = empty_epoch;

  LockState() {
    lockset = 0;
    m_rd1_epoch = empty_epoch;
    m_rd2_epoch = empty_epoch;
    m_wr1_epoch = empty_epoch;
    m_wr2_epoch = empty_epoch;
  }

  inline uint64_t getEpochValue(epoch cur_epoch) { return *cur_epoch; }
  inline size_t getEpochClock(uint64_t cur_epoch) { return cur_epoch & CLK_MASK; }

  inline size_t getEpochTaskId(uint64_t cur_epoch) {
    size_t tid = (cur_epoch & TASKID_MASK) >> NUM_CLK_BITS;
    return tid;
  }
#else
  uint32_t rtid1;
  uint32_t rc1;
  uint32_t rtid2;
  uint32_t rc2;
  uint32_t wtid1;
  uint32_t wc1;
  uint32_t wtid2;
  uint32_t wc2;

  LockState() {
    lockset = 0;
    rtid1 = 0;
    rc1 = 0;
    rtid2 = 0;
    rc2 = 0;
    wtid1 = 0;
    wc1 = 0;
    wtid2 = 0;
    wc2 = 0;
  }
#endif
};

class VarState {
public:
  LockState v[NUM_FIXED_VAR_ENTRIES];
  uint32_t cursize = 0;
  uint32_t is_racy = 0; // is_racy = 1 means variable is already detected to be racy

  VarState() {
    is_racy = 0;
    cursize = 0;
  }
  inline bool isRacy() { return is_racy == 1; }

  inline void setRacy() { is_racy = 1; }
};

// Return the clock for tid from task_state
//inline size_t element(TaskState* task_state, size_t tid) {
//  size_t curclock = 0;
//#if ENABLE_TASK_MAP
//  if (task_state->tid == tid) {
//    curclock = task_state->clock;
//  } else {
//#if DEBUG_TIME
//    HRTimer find_start = HR::now();
//#endif
//    auto it = (task_state->m_vc).find(tid);
//#if DEBUG_TIME
//    HRTimer find_end = HR::now();
//    my_getlock(&time_dr_detector.time_DR_detector_lock);
//    time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
//    time_dr_detector.num_vc_find += 1;
//    // time_dr_detector.vc_find_map[(task_state->m_vc).size()] += 1;
//    my_releaselock(&time_dr_detector.time_DR_detector_lock);
//#endif
//    if (it == (task_state->m_vc).end()) {
//      if (task_state->root_vc != NULL) {
//#if DEBUG_TIME
//        find_start = HR::now();
//#endif
//        auto it2 = task_state->root_vc->m_vc.find(tid);
//#if DEBUG_TIME
//    find_end = HR::now();
//    my_getlock(&time_dr_detector.time_DR_detector_lock);
//    time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
//    time_dr_detector.num_root_vc_find += 1;
//    // time_dr_detector.root_vc_find_map[(task_state->root_vc->m_vc).size()] += 1;
//    my_releaselock(&time_dr_detector.time_DR_detector_lock);
//#endif
//        if (it2 == (task_state->root_vc->m_vc).end()) {
//          curclock = 0;
//        } else {
//          curclock = it2->second;
//        }
//      }
//    } else {
//      curclock = it->second;
//    }
//  }
//#endif
//  return curclock;
//}

struct violation_data {
  size_t tid;
  AccessType accessType;
#if LINE_NO_PASS
  int line_no = -1;
#endif

#if LINE_NO_PASS
  violation_data(size_t tid, AccessType accessType, int line_no = -1){
#else
  violation_data(size_t tid, AccessType accessType) {
#endif
      this->tid = tid;
  this->accessType = accessType;
#if LINE_NO_PASS
  this->line_no = line_no;
#endif
}
}
;

struct violation {
  struct violation_data* a1;
  struct violation_data* a2;

  violation(violation_data* a1, violation_data* a2) {
    this->a1 = a1;
    this->a2 = a2;
  }
};

typedef tbb::internal::tbb_thread_v3::id TBB_TID;

extern std::map<TBB_TID, size_t> tid_map;
extern my_lock tid_map_lock;
//extern my_lock taskid_map_lock;
extern my_lock parent_map_lock;
extern my_lock lock_map_lock;
//extern my_lock temp_cur_map_lock;
//extern tbb::concurrent_hash_map<size_t, TaskState*> taskid_map;
//extern std::map<size_t,TaskState*> temp_cur_map;
extern std::map<size_t, size_t> lock_map;
extern std::stack<uint32_t> cur[NUM_THREADS];
extern TaskState* tstate_nodes;

extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId);

__attribute__((noinline)) void __exec_end__(unsigned long taskId);

size_t __TBB_EXPORTED_METHOD get_cur_tid();
void Capturewait_for_all_task(TaskState* cur_task_state);
void Capturewait_for_all();
void CaptureSpawn(task& t, uint32_t cur_taskId, uint32_t child_taskId);
}

#if STATS
extern GlobalStats globalStats;
#endif
extern "C" void Fini();

#if LINE_NO_PASS
extern "C" void RecordMem(size_t tid, void* access_addr, AccessType accesstype, int line_no = -1);
extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype,
                             int line_no = -1);
#else
extern "C" void RecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype);
#endif
extern "C" void CaptureLockAcquire(size_t tid, ADDRINT lock_addr);

extern "C" void CaptureLockRelease(size_t tid, ADDRINT lock_addr);
//extern "C" void set_incomplete();
//extern "C" void set_complete();
//extern "C" void clear_child_list();
//extern "C" void set_execute_true();
//extern "C" void set_execute_false();

#endif
