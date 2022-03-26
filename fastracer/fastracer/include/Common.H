#ifndef COMMON_H
#define COMMON_H

// https://en.cppreference.com/w/c/error/assert
#define NDEBUG // Uncomment to disable asserts
#include <cassert>

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
typedef tbb::atomic<size_t> my_lock;

extern tbb::atomic<size_t> task_id_ctr;
extern tbb::atomic<size_t> tid_ctr;
extern tbb::atomic<size_t> lock_ticker;

enum AccessType { READ = 0, WRITE = 1 };

#define DEBUG 0
#define STATS 0
#define TASK_GRAPH 0
#define DEBUG_TIME 0
#define REPORT_DATA_RACES 1
#define NUM_THREADS 64
#define TUSKER 0
#define JOINSET 1
#define MAX_NUM_TASKS 4194304 // PROSPAR: Max number of tasks in Taskgraph(2^23)
#define MAX_TASK_DEPTH 30     // Max depth of a task in taskgraph

#if JOINSET
#define NUM_FIXED_TASK_ENTRIES 2
#else
#define NUM_FIXED_TASK_ENTRIES 0
#endif

#define NUM_FIXED_VAR_ENTRIES 3
#define LINE_NO_PASS 0

#define EPOCH_POINTER 0

#define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)

#define NUM_TASK_BITS 20

#ifndef ENABLE_MAPS
#define ENABLE_MAPS 1
#endif

#define ENABLE_TASK_MAP 10
#define MAX_MVC_SIZE 100

inline void PIN_GetLock(PIN_LOCK* lock, int tid) { pthread_mutex_lock(lock); }

inline void PIN_ReleaseLock(PIN_LOCK* lock) { pthread_mutex_unlock(lock); }

inline void my_getlock(my_lock* lock) {
  // One possible implementation: but inefficient
  // while((*lock).compare_and_swap(1,0));

  size_t oldx;
  while (1) {
    while ((oldx = *lock) == 1) {
      sched_yield();
      // std::cout << "waiting for lock to release";
    }
    if ((*lock).compare_and_swap(1, oldx) == oldx)
      break;
  }
}

inline void my_releaselock(my_lock* lock) { *lock = 0; }

#if (SIZE_MAX == 0xFFFF)
#define SIZE_T_BITS 16
#elif (SIZE_MAX == 0xFFFFFFFF)
#define SIZE_T_BITS 32
#elif (SIZE_MAX == 0xFFFFFFFFFFFFFFFF)
#define SIZE_T_BITS 64
#else
#error "Unknown SIZE_MAX"
#endif

#define NUM_CLK_BITS (SIZE_T_BITS - NUM_TASK_BITS - 1)
// BITS mean bit pattern, mask means zero out all other irrelevant bits
#define CLK_MASK ((1UL << NUM_CLK_BITS) - 1)

#define EPOCH_BIT ((1UL << (NUM_TASK_BITS + NUM_CLK_BITS)))
#define EPOCH_MASK EPOCH_BIT

#define TASKID_MASK ((EPOCH_BIT - 1) & ~(CLK_MASK))

#if EPOCH_POINTER
// Epoch is a pointer to an integer which is a combination of clock and task id
typedef uint64_t* epoch;
inline uint64_t* createEpoch(uint64_t clock, uint64_t tid) {
  epoch temp = (uint64_t*)malloc(sizeof(uint64_t));
  *temp = (tid << NUM_CLK_BITS) | clock;
  return temp;
}
static epoch empty_epoch = createEpoch(0, 0);
#else
// Epoch is a combination of clock and task id
typedef size_t epoch;
#endif

#if TASK_GRAPH
extern std::ofstream taskgraph;
extern my_lock graph_lock;
#endif
#endif
