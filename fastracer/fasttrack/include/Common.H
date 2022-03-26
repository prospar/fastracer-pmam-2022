#ifndef COMMON_H
#define COMMON_H

// https://en.cppreference.com/w/c/error/assert
#define NDEBUG // Uncomment to disable asserts
#include <cassert>

#include "tbb/atomic.h"
#include "tbb/tbb_thread.h"
#include <cassert>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <map>
#include <pthread.h>

typedef size_t THREADID;
typedef unsigned long ADDRINT;
typedef pthread_mutex_t PIN_LOCK;
typedef tbb::atomic<size_t> my_lock;
typedef tbb::internal::tbb_thread_v3::id TBB_TID;
typedef size_t epoch; // FIXME: 4B may be sufficient and more efficient

#if (SIZE_MAX == 0xFFFF)
#define SIZE_T_BITS 16
#elif (SIZE_MAX == 0xFFFFFFFF)
#define SIZE_T_BITS 32
#elif (SIZE_MAX == 0xFFFFFFFFFFFFFFFF)
#define SIZE_T_BITS 64
#else
#error "Unknown SIZE_MAX"
#endif

enum AccessType { READ = 0, WRITE = 1 };

#define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)

// FIXME: This should be the same for all tools.

#define NUM_TASK_BITS (23)
#define NUM_CLK_BITS (SIZE_T_BITS - NUM_TASK_BITS - 1)
#define MAX_NUM_TASKS ((1 << NUM_TASK_BITS) - 1)

// BITS mean bit pattern, mask means zero out all other irrelevant bits
#define CLK_MASK ((1UL << NUM_CLK_BITS) - 1)

#define EPOCH_BIT ((1UL << (NUM_TASK_BITS + NUM_CLK_BITS)))
#define EPOCH_MASK EPOCH_BIT

#define TASKID_MASK ((EPOCH_BIT - 1) & ~(CLK_MASK))

extern tbb::atomic<size_t> task_id_ctr;

#define DEBUG 0
#define REPORT_DATA_RACES 0
#define DEBUG_TIME 0
#define STATS 0

inline void PIN_GetLock(PIN_LOCK* lock) { pthread_mutex_lock(lock); }

inline void PIN_ReleaseLock(PIN_LOCK* lock) { pthread_mutex_unlock(lock); }

inline void my_getlock(my_lock* lock) {
  size_t oldx;
  while (1) {
    while ((oldx = *lock) == 1) {
    }
    if ((*lock).compare_and_swap(1, oldx) == oldx)
      break;
  }
}

inline void my_releaselock(my_lock* lock) { *lock = 0; }

inline void copy_vc(size_t* src, size_t* dst);
inline void update_vc(size_t* vc, size_t index, size_t new_val);
inline void join_vc(size_t* src, size_t* tgt);

#endif
