#ifndef EXEC_CALLS_H
#define EXEC_CALLS_H

#pragma GCC push_options
#pragma GCC optimize("O0")

#include "Common.H"
#include "debug_time.h"
#include "stats.h"
#include "tbb/concurrent_hash_map.h"
#include "tbb/task.h"
#include <list>
#include <stack>
#include <unordered_map>
#include <vector>

// Use either one of them or none to use arrays
#define ENABLE_MAPS 1   // Use stl::map for read vector
#define ENABLE_VECTOR 0 // Use stl::vector for read vector

#if DEBUG_TIME
extern Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
#endif

using namespace tbb;

extern "C" void TD_Activate();

// FIXME: Where is the epoch?
class TaskState {
public:
  size_t m_taskid;
  TBB_TID m_pthd_id;
  my_lock m_tlock;
  TaskState* parent = NULL;
  task* mytask = NULL;
  my_lock tlock = 0;

#if ENABLE_BITS
  // SB: Include an example. Is this the per-thread vector clock?
  size_t m_C[(NUM_TASKS * (64 - NUM_TASK_BITS)) / 64 + 2] = {0};
#else
  size_t m_vector_clock[MAX_NUM_TASKS] = {0};
#endif

  std::list<size_t> m_child;

#if STATS
  PerTaskStats* m_task_stats = NULL;
#endif

  TaskState() : m_taskid(UINT64_MAX), m_tlock(0) {
#if STATS
    m_task_stats = new PerTaskStats();
#endif
  }
  explicit TaskState(size_t ptaskid) : m_taskid(ptaskid), m_tlock(0) {
#if STATS
    m_task_stats = new PerTaskStats();
#endif
  }
  TaskState(size_t ptaskid, TBB_TID pthdid) : m_taskid(ptaskid), m_pthd_id(pthdid), m_tlock(0) {
#if STATS
    m_task_stats = new PerTaskStats();
#endif
  }
  ~TaskState() {
#if STATS
    delete m_task_stats;
#endif
  }
};

// PROSPAR: Include an example of the bit layouts for ease of understanding.
class VarState {
public:
  // MSB represents epoch or VC, next high 16 bits are tasks, next 47 bits are for the clock
  // VC is a size_t array or NUM_TASKS
  epoch m_rd_epoch;
  epoch m_wr_epoch;
  bool m_is_vector;

#if 1
#if ENABLE_MAPS
  std::unordered_map<size_t, size_t> m_rd_vc;
#else
#if ENABLE_VECTOR
  std::vector<size_t> m_rd_vc;
#else
  size_t m_rd_vc[NUM_TASKS] = {0};
#endif
#endif
#endif

#if STATS
  PerSharedVariableStat* m_var_stats = NULL;
#endif

  VarState() {
    // Read metadata is always initialized in the epoch form
    m_rd_epoch = 0;
    m_wr_epoch = 0;
    m_is_vector = false;
#if STATS
    m_var_stats = new PerSharedVariableStat();
#endif
  }

  ~VarState() {
#if STATS
    delete m_var_stats;
#endif
  }

  inline bool isReadVector() { return m_is_vector; }

  inline size_t getReadEpochClock(size_t epoch) {
    assert(epoch >= 0);
    assert(!isReadVector());
    return epoch & CLK_MASK;
  }

  inline size_t getReadEpochTaskID(size_t epoch) {
    assert(epoch >= 0);
    assert(!isReadVector());
    size_t tid = (epoch & TASKID_MASK) >> NUM_CLK_BITS;
    assert(tid >= 0 && tid < MAX_NUM_TASKS);
    return tid;
  }

  inline size_t getWriteClock(size_t epoch) {
    assert(epoch >= 0);
    return epoch & CLK_MASK;
  }

  inline size_t getWriteTaskID(size_t epoch) {
    assert(epoch >= 0);
    size_t tid = epoch >> NUM_CLK_BITS;
    assert(tid >= 0 && tid < MAX_NUM_TASKS);
    return tid;
  }

  inline size_t createReadEpoch(size_t taskid, size_t clk) {
    assert(taskid > 0 && taskid < MAX_NUM_TASKS);
    assert(clk > 0);
    return ((taskid << NUM_CLK_BITS) | clk);
  }

  inline size_t createWriteEpoch(size_t taskid, size_t clk) {
    assert(taskid > 0 && taskid < MAX_NUM_TASKS);
    assert(clk > 0);
    return ((taskid << NUM_CLK_BITS) | clk);
  }

  void* createReadVector(ADDRINT addr, size_t ftid, size_t fclock, size_t stid, size_t sclock) {
#if 0
    my_getlock(&printLock);
    std::cout << "Task id: " << stid << " is creating read vector for address: " << std::showbase
              << std::hex << addr << "\n";
    my_releaselock(&printLock);
#endif

    void* rd_vc = NULL;
#if ENABLE_VECTOR
    std::vector<epoch>* tmp = new std::vector<epoch>();
    tmp->push_back(createEpoch(ftid, fclk));
    tmp->push_back(createEpoch(stid, sclk));
    rd_vc = tmp;
#else
#if ENABLE_MAPS
    std::unordered_map<size_t, size_t>* tmp = new std::unordered_map<size_t, size_t>();
    tmp->insert(std::make_pair(ftid, fclock));
    tmp->insert(std::make_pair(stid, sclock));
    rd_vc = tmp;
#else
    size_t* tmp = new size_t[MAX_NUM_TASKS]{0};
    tmp[ftid] = fclk;
    tmp[stid] = sclk;
    rd_vc = tmp;
#endif
#endif

    m_is_vector = true;
    // my_getlock(&printLock);
    // cout << "Create read vector was called \n";
    // my_releaselock(&printLock);

    return rd_vc;
  }

  inline void clearReadVector() {
    assert(isReadVector());
#if ENABLE_MAPS
    m_rd_vc.clear();
#endif
    m_is_vector = false;
  }

  inline bool isRacy() { return m_wr_epoch == EPOCH_BIT; }

  inline void setRacy() { m_wr_epoch = EPOCH_BIT; }
};

class LockState {
public:
  my_lock m_llock = 0;
  size_t m_lvc[MAX_NUM_TASKS] = {0};

  LockState() { m_llock = 0; }
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

inline size_t element(TaskState* thread, size_t curtid) {
  size_t curepoch;
#ifdef ENABLE_BITS
  // std::cout << "ENABLE_BITS enabled\n";
  size_t index = (curtid * (64 - NUM_TASK_BITS) / 64);
  size_t first = (curtid * (64 - NUM_TASK_BITS) % 64);
  curepoch = (((thread->C)[index]) << first) >> NUM_TASK_BITS;
  if (first > NUM_TASK_BITS)
    curepoch = curepoch + ((thread->C)[index + 1] >> (64 - first + NUM_TASK_BITS));
  curepoch = curepoch + (curtid << (64 - NUM_TASK_BITS));
#else
  curepoch = (thread->m_vector_clock)[curtid];
#endif
  return curepoch;
}

// Copy contents of source vector clock into dest
inline void copy_vc(size_t* src, size_t* dst) {
  assert(src != NULL);
  assert(dst != NULL);
  for (int i = 0; i < MAX_NUM_TASKS; i++) {
    dst[i] = src[i];
  }
}

// Join contents of source and target vector clocks into source
inline void join_vc(size_t* src, size_t* tgt) {
  assert(src != NULL);
  assert(tgt != NULL);
  for (int i = 0; i < MAX_NUM_TASKS; i++) {
    src[i] = std::max(src[i], tgt[i]);
  }
}

inline void update_vc(size_t* vc, size_t index, size_t new_val) {
  assert(vc != NULL);
  assert(new_val > vc[index]);
#ifdef ENABLE_BITS
  val = (val << NUM_TASK_BITS);
  size_t index = (i * (64 - NUM_TASK_BITS) / 64);
  size_t first = (i * (64 - NUM_TASK_BITS) % 64);

  if (first <= NUM_TASK_BITS) {
    newepoch = ((t->C)[index] >> (64 - first)) << (64 - first);
    newepoch = newepoch + (val >> first);
    newepoch = newepoch +
               (((t->C)[index] << (64 - NUM_TASK_BITS + first)) >> (64 - NUM_TASK_BITS + first));
    t->C[index] = newepoch;
  } else {
    newepoch = ((t->C)[index] >> (64 - first)) << (64 - first);
    newepoch = newepoch + (val >> first);
    t->C[index] = newepoch;
    t->C[index + 1] = (((t->C[index + 1]) << (first - NUM_TASK_BITS)) >> (first - NUM_TASK_BITS)) +
                      (val << (64 - first));
  }
#else
  vc[index] = new_val;
#endif
}

extern std::map<TBB_TID, std::stack<size_t>> thrid_taskid_map;
extern my_lock thrid_taskid_map_lock;

extern tbb::concurrent_hash_map<size_t, TaskState*> taskid_taskstate_map;
extern my_lock taskid_taskstate_map_lock;

extern my_lock parent_map_lock;
extern my_lock lock_map_lock;

extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId);
__attribute__((noinline)) void __exec_end__(unsigned long taskId);
size_t __TBB_EXPORTED_METHOD get_cur_tid();
void Capturewait_for_all_task(TaskState* cur_task_state);
void Capturewait_for_all();
}

extern "C" void Fini();
extern "C" void RecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void CaptureLockAcquire(size_t tid, ADDRINT lock_addr);
extern "C" void CaptureLockRelease(size_t tid, ADDRINT lock_addr);

#if DEBUG
void dumpTaskIdToTaskStateMap();
#endif

#endif
