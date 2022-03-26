#pragma GCC push_options
#pragma GCC optimize("O0")

#include "Fexec_calls.h"
#include "exec_calls.h"
#include "FCommon.H"
#include <iostream>
#include "Ft_debug_task.h"

using namespace std;
using namespace tbb;

tbb::atomic<size_t> Ftask_id_ctr(0);
// tbb::atomic<size_t> tid_ctr(0);

// PIN_LOCK lock;
my_lock Ftid_map_lock(0);
my_lock Ftaskid_map_lock(0);
my_lock Fparent_map_lock(0);
my_lock Flock_map_lock(0);
std::map<TBB_TID, stack<size_t>> Ftid_map;
concurrent_hash_map<size_t, Fthreadstate*> Ftaskid_map;
std::stack<size_t> FtidToTaskIdMap[NUM_THREADS];

extern "C" {
__attribute__((noinline)) void __Fexec_begin__(unsigned long taskId) {
  size_t threadid = get_cur_tid();
  FtidToTaskIdMap[threadid].push(taskId);
}
__attribute__((noinline)) void __Fexec_end__(unsigned long taskid) {
  size_t threadid = get_cur_tid();
  FtidToTaskIdMap[threadid].pop();
  }

}

#pragma GCC pop_options
