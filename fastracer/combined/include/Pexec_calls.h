#ifndef PEXEC_CALLS_H
#define PEXEC_CALLS_H

#pragma GCC push_options
#pragma GCC optimize("O0")

#include "PAFTaskGraph.H"
#include "PDR_Detector.H"
#include "PTrace_Generator.H"
#include "tbb/atomic.h"
#include "tbb/tbb_thread.h"

typedef tbb::internal::tbb_thread_v3::id TBB_TID;
typedef tbb::atomic<size_t> my_lock;


extern tbb::atomic<size_t> Ptask_id_ctr;


extern AFTaskGraph* taskGraph;

extern "C" {
__attribute__((noinline)) void __Pexec_begin__(unsigned long taskId);

__attribute__((noinline)) void __Pexec_end__(unsigned long taskId);

}

///inline void Pmy_getlock(my_lock* lock){
///size_t oldx;
/////cout << *lock << "Pmy_getlock\n";
///while(1){
///while( (oldx=*lock) == 0);// cout << "waiting for lock to release";
///if((*lock).compare_and_swap(0,oldx) == oldx) break;
///}
///}
///
///
///inline void Pmy_releaselock(my_lock* lock){
///*lock = 1;
///}
#pragma GCC pop_options

#endif
