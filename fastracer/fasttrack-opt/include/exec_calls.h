#ifndef EXEC_CALLS_H
#define EXEC_CALLS_H

#pragma GCC push_options
#pragma GCC optimize("O0")

#include "Common.H"
#include "stats.h"
#include "debug_time.h"
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

extern "C" void TD_Activate();

#if DEBUG
extern my_lock printLock;
#endif

#if DEBUG_TIME
extern Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
#endif

#if ENABLE_TASK_MAP
// RootVc class to hold VC of root and all successors access this map.
class RootVc {
public:
  std::unordered_map<size_t, size_t> m_vc;
};
#endif

class TaskState {
public:
  uint32_t cached_tid[NUM_FIXED_TASK_ENTRIES];
  uint32_t cached_clock[NUM_FIXED_TASK_ENTRIES];
  uint32_t depth = 0;
  uint32_t parent_taskId = 0;
  my_lock tlock = 0;
  task* mytask = NULL;
  std::list<TaskState*>* child_list;
  std::unordered_map<size_t,size_t>* my_vc = NULL;
  std::unordered_map<size_t,size_t>* root_vc = NULL;

#if EPOCH_POINTER
  // Slimfast: epoch_ptr contains pointer to epoch, not separate clock or taskid required.
  epoch epoch_ptr;
#else
  uint32_t taskId = UINT32_MAX;
  uint32_t clock = 0; // Just the scalar clock, no need to combine the task id
#endif
  inline uint32_t getCurClock(){
#if EPOCH_POINTER
    return (uint32_t)((*epoch_ptr) & CLK_MASK);
#else
    return clock;
#endif
  }
  inline uint32_t getTaskId(){
#if EPOCH_POINTER
    uint32_t tid = (uint32_t)(((*epoch_ptr) & TASKID_MASK) >> NUM_CLK_BITS);
    assert(/*tid >= 0 &&*/ tid < MAX_NUM_TASKS);
    return tid;
#else
    return taskId;
#endif
  }

#if STATS
  PerTaskStats m_task_stats;
#endif

  TaskState() { tlock = 0; 
#if EPOCH_POINTER
  epoch_ptr = empty_epoch;
#endif
}

  TaskState(size_t ptaskid) : tlock(0) {
#if EPOCH_POINTER
    epoch_ptr = createEpoch(0, ptaskid);
#endif
  }
};

class VarState {
public:
#if EPOCH_POINTER
  // m_rd_epoch and m_wr_epoch contains pointers of read and write epochs.
  epoch m_rd_epoch = empty_epoch;
  epoch m_wr_epoch = empty_epoch;

  bool is_read_vector = 0;
  bool is_racy = 0;
#else
    // MSB is zero indicates epoch, else use the vc attribute
  size_t m_rd_epoch = 0;
  // MSB is one indicates a race has already been detected
  size_t m_wr_epoch = 0;
#endif

#if ENABLE_MAPS
  std::unordered_map<size_t, size_t> m_rvc;
#else
#if ENABLE_VECTOR
  std::vector<size_t> m_rvc;
#else
  size_t m_rvc[NUM_TASKS] = {0};
#endif
#endif

#if STATS
  PerSharedVariableStat m_var_stats;
  // Accesses need to be synchronized, but we are using a lock on the metadata
  // my_lock m_var_stats_lock = 0;
#endif

  VarState() {
#if EPOCH_POINTER
    m_rd_epoch = empty_epoch;
    m_wr_epoch = empty_epoch;
#else
    m_rd_epoch = 0;
    m_wr_epoch = 0;
#endif    
  };


  inline uint64_t get_m_rd_epoch(){ 
#if EPOCH_POINTER    
    return *m_rd_epoch; 
#else
    return m_rd_epoch;
#endif    
    }

  inline uint64_t get_m_wr_epoch(){
#if EPOCH_POINTER    
    return *m_wr_epoch; 
#else
    return m_wr_epoch;
#endif
    }

#if EPOCH_POINTER     
  inline bool isReadVector() { return is_read_vector; }
  inline bool isReadVector(size_t epoch) { return is_read_vector; }
  // inline void set_m_rd_epoch(epoch epoch){ m_rd_epoch = epoch;}
  // inline void set_m_wr_epoch(epoch epoch){ m_wr_epoch = epoch; }
#else
  inline bool isReadVector() { return (m_rd_epoch >> (NUM_CLK_BITS + NUM_TASK_BITS)) == 1; }
  inline bool isReadVector(size_t epoch) { return (epoch >> (NUM_CLK_BITS + NUM_TASK_BITS)) == 1; }
#endif


  inline size_t getReadEpochClock(size_t epoch) {
    assert(!isReadVector(epoch));
    return epoch & CLK_MASK;
  }

  inline size_t getReadEpochTaskID(size_t epoch) {
    assert(!isReadVector(epoch));
    size_t tid = (epoch & TASKID_MASK) >> NUM_CLK_BITS;
    assert(/*tid >= 0 &&*/ tid < MAX_NUM_TASKS);
    return tid;
  }

  inline size_t getWriteClock(size_t epoch) { return epoch & CLK_MASK; }

  inline size_t getWriteTaskID(size_t epoch) {
    size_t tid = (epoch & TASKID_MASK) >> NUM_CLK_BITS;
    assert(/*tid >= 0 &&*/ tid < MAX_NUM_TASKS);
    return tid;
  }

  inline size_t createReadEpoch(size_t taskid, size_t clk) {
    assert(/*taskid >= 0 &&*/ taskid < MAX_NUM_TASKS);
    assert(clk > 0);
    return ((taskid << NUM_CLK_BITS) | clk);
  }

  inline size_t createWriteEpoch(size_t taskid, size_t clk) {
    assert(/*taskid >= 0 &&*/ taskid < MAX_NUM_TASKS);
    assert(clk > 0);
    return ((taskid << NUM_CLK_BITS) | clk);
  }

  void createReadVector(ADDRINT addr, size_t ftid, size_t fclock, size_t stid, size_t sclock) {

#if ENABLE_VECTOR
    m_rvc.clear();
#if EPOCH_POINTER
    m_rvc->push_back(createEpoch(fclk, ftid ));
    m_rvc->push_back(createEpoch(sclk, stid ));
#else
    m_rvc->push_back(createEpoch(ftid, fclk));
    m_rvc->push_back(createEpoch(stid, sclk));
#endif
#else
#if ENABLE_MAPS
    m_rvc.clear();
    m_rvc.insert(std::make_pair(ftid, fclock));
    m_rvc.insert(std::make_pair(stid, sclock));
#else
    m_rvc[ftid] = fclk;
    m_rvc[stid] = sclk;
#endif
#endif

#if EPOCH_POINTER
    m_rd_epoch = empty_epoch;
    is_read_vector = 1;
#else
    m_rd_epoch = EPOCH_BIT;
#endif
    
    assert(isReadVector());
  }

  // Clear read vector and reset to epoch
  inline void clearReadVector() {
    assert(isReadVector());
#if ENABLE_MAPS
    m_rvc.clear();
#else
#if ENABLE_VECTOR
    m_rvc.clear();
#else
    m_rvc = {0};
#endif
#endif

#if EPOCH_POINTER
    m_rd_epoch = empty_epoch;
    is_read_vector = 0;
#else
    m_rd_epoch = 0;
#endif

    assert(!isReadVector());
  }
#if EPOCH_POINTER
  inline bool isRacy() { return is_racy; }
  inline void setRacy() { is_racy = 1; }
#else
  inline bool isRacy() { return m_wr_epoch == EPOCH_BIT; }
  inline void setRacy() { m_wr_epoch = EPOCH_BIT; }
#endif



};

class LockState {
public:
  my_lock m_lock = 0;

// Accesses should be synchronized
#if ENABLE_TASK_MAP
  std::unordered_map<uint32_t, uint32_t> m_lvc;
#else
  uint32_t m_lvc[MAX_NUM_TASKS] = {0};
#endif

  LockState() { m_lock = 0; }
};

struct ViolationData {
  size_t tid;
  AccessType accessType;

  ViolationData(size_t tid, AccessType accessType) {
    this->tid = tid;
    this->accessType = accessType;
  }
};

struct Violation {
  struct ViolationData* a1;
  struct ViolationData* a2;

  Violation(ViolationData* a1, ViolationData* a2) {
    this->a1 = a1;
    this->a2 = a2;
  }
};

extern std::map<TBB_TID, std::stack<size_t>> thrid_taskid_map;
extern my_lock thrid_taskid_map_lock;
extern my_lock tid_map_lock;
extern TaskState* tstate_nodes;

extern tbb::concurrent_hash_map<size_t, TaskState*> taskid_map;
extern std::map<TBB_TID, size_t> tid_map;
extern my_lock taskid_map_lock;
extern std::stack<size_t> cur[NUM_THREADS];

extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId);

__attribute__((noinline)) void __exec_end__(unsigned long taskId);

size_t __TBB_EXPORTED_METHOD get_cur_tid();
void Capturewait_for_all_task(TaskState* cur_task_state);
void Capturewait_for_all();
void CaptureSpawn(task& t,uint32_t cur_taskId, uint32_t child_taskId);
}

extern "C" void Fini();
extern "C" void RecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void CaptureLockAcquire(size_t tid, ADDRINT lock_addr);
extern "C" void CaptureLockRelease(size_t tid, ADDRINT lock_addr);

#endif
