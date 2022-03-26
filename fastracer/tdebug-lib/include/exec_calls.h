#ifndef EXEC_CALLS_H
#define EXEC_CALLS_H

#pragma GCC push_options
#pragma GCC optimize("O0")

#include "AFTaskGraph.H"
#include "DR_Detector.H"
#include "Trace_Generator.H"
#include "tbb/atomic.h"
#include "tbb/tbb_thread.h"

typedef tbb::internal::tbb_thread_v3::id TBB_TID;
typedef tbb::atomic<size_t> my_lock;
extern std::map<TBB_TID, size_t> tid_map;

extern PIN_LOCK tid_map_lock;

extern tbb::atomic<size_t> task_id_ctr;

extern tbb::atomic<size_t> tid_ctr;

extern AFTaskGraph* taskGraph;

extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId);

__attribute__((noinline)) void __exec_end__(unsigned long taskId);

size_t __TBB_EXPORTED_METHOD get_cur_tid();
}

///inline void my_getlock(my_lock* lock){
///size_t oldx;
/////cout << *lock << "my_getlock\n";
///while(1){
///while( (oldx=*lock) == 0);// cout << "waiting for lock to release";
///if((*lock).compare_and_swap(0,oldx) == oldx) break;
///}
///}
///
///
///inline void my_releaselock(my_lock* lock){
///*lock = 1;
///}
#pragma GCC pop_options

#endif
