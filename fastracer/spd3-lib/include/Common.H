#ifndef COMMON_H
#define COMMON_H

// https://en.cppreference.com/w/c/error/assert
#define NDEBUG // Uncomment to disable asserts
#include <cassert>

#define NUM_THREADS 64
#include "tbb/atomic.h"
#include "tbb/task.h"
#include "tbb/tbb_thread.h"
#include <pthread.h>
#include <stack>

#define STATS 0
#define REPORT_DATA_RACES 1

typedef unsigned long THREADID;
typedef unsigned long ADDRINT;
typedef pthread_mutex_t PIN_LOCK;
typedef tbb::atomic<size_t> my_lock;

enum AccessType { READ = 0, WRITE = 1 };

extern PIN_LOCK lock;
extern std::stack<size_t> tidToTaskIdMap[NUM_THREADS];
extern tbb::atomic<size_t> task_id_ctr;

inline void init_common() { tidToTaskIdMap[0].push(0); }

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
