#ifndef COMMON_H
#define COMMON_H

// https://en.cppreference.com/w/c/error/assert
#define NDEBUG // Uncomment to disable asserts
#include <cassert>

#include "tbb/atomic.h"
#include "tbb/task.h"
#include "tbb/tbb_thread.h"
#include <bitset>
#include <iostream>
#include <map>
#include <mutex>
#include <pthread.h>
#include <stack>
#include <unordered_set>
#include <utility>

typedef size_t THREADID;
typedef unsigned long ADDRINT;

// typedef pthread_mutex_t PIN_LOCK;
typedef tbb::atomic<size_t> my_lock;
typedef tbb::internal::tbb_thread_v3::id TBB_TID;

typedef pthread_mutex_t PIN_LOCK;

extern tbb::atomic<size_t> task_id_ctr;

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
enum FT_READ_TYPE {
  RD_SAME_EPOCH = 0,
  RD_SHARED_SAME_EPOCH,
  RD_EXCLUSIVE,
  RD_SHARE,
  RD_SHARED,
  RD_INVALID
};
enum FT_WRITE_TYPE { WR_SAME_EPOCH = 0, WR_EXCLUSIVE, WR_SHARED, WR_INVALID };

#define DEBUG 0
#define STATS 0
#define TASK_GRAPH 0
#define DEBUG_TIME 0
#define REPORT_DATA_RACES 1
#define NUM_THREADS 64

// Flags for different optimizations.
#define EPOCH_POINTER 0   //Slimfast
#define READ_SHARED_SAME_EPOCH_FLAG 0
#define JOINSET 1
#define CACHING 1

#define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)

// Use either one of them or none to use arrays to represent read vector clocks in variable metadata
#define ENABLE_MAPS 1   // Use stl::map for read vector
#define ENABLE_VECTOR 0 // Use stl::vector for read vector


#if JOINSET

#if CACHING
#define NUM_FIXED_TASK_ENTRIES 2
#else
#define NUM_FIXED_TASK_ENTRIES 0
#endif

#else
#define NUM_FIXED_TASK_ENTRIES 0
#endif

// Use std::map to represent read vector clocks in tasks
#define ENABLE_TASK_MAP 1
// FIXME: Add comment
#define ENABLE_BITS 0

// MSB is to indicate VC, 23 task bits, 40 clk bits for all epoch entries
// MSB is used to indicate data race for write epochs
#define NUM_TASK_BITS (22)
#define NUM_CLK_BITS (SIZE_T_BITS - NUM_TASK_BITS - 1)
#define MAX_NUM_TASKS ((1 << NUM_TASK_BITS))
#define MAX_MVC_SIZE 100

// BITS mean bit pattern, mask means zero out all other irrelevant bits
#define CLK_MASK ((1UL << NUM_CLK_BITS) - 1)

#define EPOCH_BIT ((1UL << (NUM_TASK_BITS + NUM_CLK_BITS)))
#define EPOCH_MASK EPOCH_BIT

#define TASKID_MASK ((EPOCH_BIT - 1) & ~(CLK_MASK))

// Synchronization helpers

// inline void PIN_GetLock(PIN_LOCK* lock, int tid) { pthread_mutex_lock(lock); }

// inline void PIN_ReleaseLock(PIN_LOCK* lock) { pthread_mutex_unlock(lock); }


#if EPOCH_POINTER
// Epoch is a pointer to an integer which is a combination of clock and task id
typedef uint64_t* epoch;
inline uint64_t* createEpoch(uint64_t clock , uint64_t tid  ){
  epoch temp = (uint64_t*)malloc(sizeof(uint64_t));
  *temp = (tid << NUM_CLK_BITS) | clock;
  return temp;
}
static epoch empty_epoch = createEpoch(0,0);
#else
// Epoch is a combination of clock and task id
typedef size_t epoch;
#endif


inline void my_getlock(my_lock* lock) {
  size_t oldx;
  while (1) {
    while ((oldx = *lock) == 1) {
    }
    if ((*lock).compare_and_swap(1, oldx) == oldx) {
      break;
    }
  }
}

inline void my_releaselock(my_lock* lock) { *lock = 0; }

inline void PIN_GetLock(PIN_LOCK* lock, int tid) { pthread_mutex_lock(lock); }
inline void PIN_ReleaseLock(PIN_LOCK* lock) { pthread_mutex_unlock(lock); }
// Definitions for reader/writer lock

typedef struct rwlock_t {
  long value;
} rwlock_t;

extern int atomic_add(long* ptr, long val);
extern void init_rwlock(rwlock_t* lock);
extern void write_lock(rwlock_t* lock);
extern void write_unlock(rwlock_t* lock);
extern void read_lock(rwlock_t* lock);
extern void read_unlock(rwlock_t* lock);

#if TASK_GRAPH
extern std::ofstream taskgraph;
extern my_lock graph_lock;
#endif

#endif
