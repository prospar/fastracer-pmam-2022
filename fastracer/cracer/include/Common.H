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
typedef size_t epoch;
typedef tbb::atomic<size_t> my_lock;

extern tbb::atomic<size_t> task_id_ctr;
extern tbb::atomic<size_t> tid_ctr;
extern tbb::atomic<size_t> lock_ticker;
enum AccessType { READ = 0, WRITE = 1 };

#define DEBUG 0
#define STATS 0
#define TASK_GRAPH 0
#define DEBUG_TIME 0
#define REPORT_DATA_RACES 0
#define NUM_THREADS 64
#define TUSKER 0

#define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)

#define NUM_TASK_BITS 20

#ifndef ENABLE_MAPS
#define ENABLE_MAPS 1
#endif

#define ENABLE_TASK_MAP 10
#define MAX_MVC_SIZE 100
typedef size_t epoch;
typedef unsigned long ADDRINT;
typedef tbb::atomic<size_t> my_lock;

inline void PIN_GetLock(PIN_LOCK* lock, int tid) { pthread_mutex_lock(lock); }

inline void PIN_ReleaseLock(PIN_LOCK* lock) { pthread_mutex_unlock(lock); }

inline void my_getlock(my_lock* lock) {
  size_t oldx;
  while (1) {
    while ((oldx = *lock) == 1)
      sched_yield(); // std::cout << "waiting for lock to release";
    if ((*lock).compare_and_swap(1, oldx) == oldx)
      break;
  }
}

inline void my_releaselock(my_lock* lock) { *lock = 0; }

inline int my_trylock(my_lock* lock) {
  size_t oldx = 0;
  if (*lock == 1)
    return 0;
  if ((*lock).compare_and_swap(1, oldx) == oldx)
    return 1;
  return 0;
}

#if TASK_GRAPH
extern std::ofstream taskgraph;
extern my_lock graph_lock;
#endif

typedef unsigned long label_t;

#include <limits.h>
static label_t MAX_LABEL = ULONG_MAX;
static label_t NODE_INTERVAL = (((label_t)1) << 58); // N / log_2 N = 2^58
static label_t SUBLIST_SIZE = ((label_t)64);         // log_2 N = 64
#define FRAME_HELPER_MASK 0x1
#define FRAME_FULL_MASK 0x2
// #if defined _OM_H || defined _TRIE_H
static label_t MAX_LEVEL = (sizeof(label_t) * 8);
// #endif

#endif
