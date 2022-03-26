#ifndef EXEC_CALLS_H
#define EXEC_CALLS_H

#include "tbb/concurrent_hash_map.h"
#include "tbb/task.h"
#include "tbb/tbb_thread.h"
#include "NCommon.h"
typedef tbb::internal::tbb_thread_v3::id TBB_TID;
#define newalgo 
#define fasttrack
#define ptracer 
typedef unsigned long ADDRINT;
extern tbb::atomic<size_t> tid_ctr;
extern tbb::atomic<size_t> task_id_ctr;
extern my_lock tid_map_lock;
extern std::map<TBB_TID, size_t> tid_map;
#define NUM_THREADS 64

extern "C" {
__attribute__((noinline)) void __Nexec_begin__(unsigned long taskId);

__attribute__((noinline)) void __Nexec_end__(unsigned long taskId);

}

extern "C" {
__attribute__((noinline)) void __Pexec_begin__(unsigned long taskId);

__attribute__((noinline)) void __Pexec_end__(unsigned long taskId);

}
extern "C" {
__attribute__((noinline)) void __Fexec_begin__(unsigned long taskId);

__attribute__((noinline)) void __Fexec_end__(unsigned long taskId);

}
extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId);

__attribute__((noinline)) void __exec_end__(unsigned long taskId);

size_t __TBB_EXPORTED_METHOD get_cur_tid();
}

inline void my_getlock(my_lock* lock) {
  size_t oldx;
  while (1) {
    while ((oldx = *lock) == 1)
      ; // std::cout << "waiting for lock to release";
    if ((*lock).compare_and_swap(1, oldx) == oldx)
      break;
  }
}

inline void my_releaselock(my_lock* lock) { *lock = 0; }
#endif
