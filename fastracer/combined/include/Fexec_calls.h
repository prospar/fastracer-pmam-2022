#ifndef FEXEC_CALLS_H
#define FEXEC_CALLS_H

#pragma GCC push_options
#pragma GCC optimize("O0")

#include "FCommon.H"
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

#define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)

#define NUM_TASK_BITS 20
#define NUM_TASKS 10000

#ifndef ENABLE_MAPS
#define ENABLE_MAPS 100
#endif

#define DEBUG_TIME 10
#define ENABLE_TASK_MAP 10

#define NUM_THREADS 64
extern "C" void FTD_Activate();

class Fthreadstate {
public:
  size_t cur_epoch;
  my_lock tlock = 0;
#ifdef ENABLE_BITS
  size_t C[(NUM_TASKS * (64 - NUM_TASK_BITS)) / 64 + 2] = {0};
#else
#ifdef ENABLE_TASK_MAP
  std::unordered_map<size_t,size_t> C;
#else  
  size_t C[NUM_TASKS] = {0};
#endif
#endif

  std::list<size_t> child;
#ifdef DEBUG
  FPerTaskStats task_stats;
#endif

  Fthreadstate() { tlock = 0; }
};

class Fvarstate {
public:
  epoch R = 0; // if R & (1<<63) == 0 so read_metadata is in epoch form else in vector clock form.
  epoch W = 0;
#ifdef ENABLE_MAPS
  std::unordered_map<size_t, size_t> rvc;
#else
#ifdef ENABLE_VECTOR
  std::vector<size_t> rvc;
#else
  size_t rvc[NUM_TASKS] = {0};
#endif
#endif
#ifdef DEBUG
  PerSharedVariableStat var_stats;
  my_lock ntasks = 0;
#endif
  Fvarstate() {
    R = 0;
    W = 0;
  };

#if ENABLE_STATS

#endif
};

class Flockstate {
public:
  my_lock llock = 0;
#ifdef ENABLE_TASK_MAP
  std::unordered_map<size_t,size_t> lvc;
#else
  size_t lvc[NUM_TASKS] = {0};
#endif

#ifdef DEBUG
  PerLockStats lock_stats;
#endif

  Flockstate() { llock = 0; }
};

struct Fviolation_data {
  size_t tid;
  AccessType accessType;

  Fviolation_data(size_t tid, AccessType accessType) {
    this->tid = tid;
    this->accessType = accessType;
  }
};

struct Fviolation {
  struct Fviolation_data* a1;
  struct Fviolation_data* a2;

  Fviolation(Fviolation_data* a1, Fviolation_data* a2) {
    this->a1 = a1;
    this->a2 = a2;
  }
};

inline void Fmy_getlock(my_lock* lock) {
  size_t oldx;
  while (1) {
    while ((oldx = *lock) == 1)
      ; // std::cout << "waiting for lock to release";
    if ((*lock).compare_and_swap(1, oldx) == oldx)
      break;
  }
}

inline size_t element(Fthreadstate* thread, size_t curtid) {
#ifdef ENABLE_BITS
  std::cout << "ENABLE_BITS enabled\n";
  size_t index = (curtid * (64 - NUM_TASK_BITS) / 64);
  size_t first = (curtid * (64 - NUM_TASK_BITS) % 64);
  size_t curepoch = (((thread->C)[index]) << first) >> NUM_TASK_BITS;
  if (first > NUM_TASK_BITS)
    curepoch = curepoch + ((thread->C)[index + 1] >> (64 - first + NUM_TASK_BITS));
  curepoch = curepoch + (curtid << (64 - NUM_TASK_BITS));
#else 
#ifdef ENABLE_TASK_MAP
  size_t curepoch = 0;
auto it = (thread->C).find(curtid);
  if(it != (thread->C).end())
         curepoch = it->second;
  else if(((thread->cur_epoch) >> (64-NUM_TASK_BITS)) == curtid)
          curepoch = (thread->cur_epoch << NUM_TASK_BITS ) >> NUM_TASK_BITS;
#else
  size_t curepoch = (thread->C)[curtid];
#endif
#endif
  return curepoch;
}

inline void store(Fthreadstate* t, size_t i, size_t val) {
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
  t->C[i] = val;
#endif
}
inline void Fmy_releaselock(my_lock* lock) { *lock = 0; }

typedef tbb::internal::tbb_thread_v3::id TBB_TID;
extern std::stack<size_t> FtidToTaskIdMap [NUM_THREADS];
extern std::map<TBB_TID, std::stack<size_t>> Ftid_map;
extern my_lock Ftid_map_lock;
extern my_lock Ftaskid_map_lock;
extern my_lock Fparent_map_lock;
extern my_lock Flock_map_lock;
extern tbb::concurrent_hash_map<size_t, Fthreadstate*> Ftaskid_map;
extern "C" {
__attribute__((noinline)) void __Fexec_begin__(unsigned long taskId);

__attribute__((noinline)) void __Fexec_end__(unsigned long taskId);

}
#ifdef DEBUG
extern FGlobalStats stats;
#endif

extern "C" void FFini();
extern "C" void FRecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void FRecordAccess(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void FCaptureLockAcquire(size_t tid, ADDRINT lock_addr);

extern "C" void FCaptureLockRelease(size_t tid, ADDRINT lock_addr);

#endif
