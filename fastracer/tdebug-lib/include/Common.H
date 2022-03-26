#ifndef COMMON_H
#define COMMON_H

// https://en.cppreference.com/w/c/error/assert
#define NDEBUG // Uncomment to disable asserts
#include <cassert>

#include "tbb/atomic.h"
#include "tbb/tbb_thread.h"
#include <pthread.h>
#include <stack>

typedef unsigned long THREADID;
typedef unsigned long ADDRINT;
typedef pthread_mutex_t PIN_LOCK;
typedef tbb::atomic<size_t> my_lock;
typedef tbb::internal::tbb_thread_v3::id TBB_TID;

enum AccessType { READ = 0, WRITE = 1 };

#define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)
#define NUM_GRAPH_NODES 134217728 // PROSPAR: Max number of nodes in the DPST

// SB: Is this the same as tasks in FT?
#define NUM_THREADS 64

extern tbb::atomic<size_t> task_id_ctr;

#define DEBUG 0
#define STATS 0
#define DATA_RACES 0
#define DEBUG_TIME 0
#define REPORT_DATA_RACES 1
#define LINE_NO_PASS 0

#if DEBUG
extern my_lock printLock; // Serialize print statements
#endif

// extern PIN_LOCK lock;
// extern std::stack<size_t> tidToTaskIdMap[NUM_THREADS];
// inline void init_common() { tidToTaskIdMap[0].push(0); }

inline void PIN_GetLock(PIN_LOCK* lock, int tid) { pthread_mutex_lock(lock); }

inline void PIN_ReleaseLock(PIN_LOCK* lock) { pthread_mutex_unlock(lock); }

inline void my_getlock(my_lock* lock) {
  size_t oldx;
  // cout << *lock << "my_getlock\n";
  while (1) {
    while ((oldx = *lock) == 1)
      ; // cout << "waiting for lock to release";
    if ((*lock).compare_and_swap(1, oldx) == oldx)
      break;
  }
}

inline void my_releaselock(my_lock* lock) { *lock = 0; }
#endif
