#pragma GCC push_options
#pragma GCC optimize("O0")

#include "Nexec_calls.h"
#include "exec_calls.h"
#include "NCommon.h"
#include <iostream>
#include "Nt_debug_task.h"

using namespace std;
using namespace tbb;

tbb::atomic<size_t> Ntask_id_ctr(0);
tbb::atomic<size_t> test_count(0);
tbb::atomic<size_t> Nlock_ticker(0);

my_lock Ntaskid_map_lock(0);
my_lock Nparent_map_lock(0);
my_lock Nlock_map_lock(0);
std::map<TBB_TID, stack<size_t>>Ntid_map;
concurrent_hash_map<size_t,Nthreadstate*>Ntaskid_map;
std::map<size_t,size_t>Nlock_map;
std::stack<size_t> NtidToTaskIdMap[NUM_THREADS];

extern "C" {
__attribute__((noinline)) void __Nexec_begin__(unsigned long taskId){
  size_t threadid = get_cur_tid();
  NtidToTaskIdMap[threadid].push(taskId);
}
__attribute__((noinline)) void __Nexec_end__(unsigned long taskid) {
  size_t threadid = get_cur_tid();
  NtidToTaskIdMap[threadid].pop();
}

}

#pragma GCC pop_options
