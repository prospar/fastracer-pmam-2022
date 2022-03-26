#pragma GCC push_options
#pragma GCC optimize("O0")

#include "exec_calls.h"
#include "tbb/concurrent_hash_map.h"
#include <iostream>

using namespace std;
using namespace tbb;

// Next available task id counter
tbb::atomic<size_t> task_id_ctr(0);

extern "C" {

// Beginning of task taskId
__attribute__((noinline)) void __exec_begin__(unsigned long taskId) {}

// Task end
__attribute__((noinline)) void __exec_end__(unsigned long taskid) {}

__attribute__((noinline)) size_t get_cur_tid() {}
}

#pragma GCC pop_options
