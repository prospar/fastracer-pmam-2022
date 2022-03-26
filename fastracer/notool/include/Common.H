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
#include <mutex>
#include <pthread.h>
#include <utility>

typedef size_t THREADID;
typedef unsigned long ADDRINT;

typedef pthread_mutex_t PIN_LOCK;
typedef tbb::atomic<size_t> my_lock;
typedef tbb::internal::tbb_thread_v3::id TBB_TID;

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

#define DEBUG 0
#define STATS 0
#define DEBUG_TIME 0
#define REPORT_DATA_RACES 0

#define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)

#endif
