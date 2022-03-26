#pragma GCC push_options
#pragma GCC optimize("O0")

#include "Pexec_calls.h"
#include "exec_calls.h"
#include "PCommon.H"
#include "Pt_debug_task.h"

tbb::atomic<size_t> Ptask_id_ctr;
PIN_LOCK lock;
AFTaskGraph* taskGraph;
std::stack<size_t> PtidToTaskIdMap[NUM_THREADS];

extern "C" {
__attribute__((noinline)) void __Pexec_begin__(unsigned long taskId) {
  taskGraph->CaptureExecute(get_cur_tid(), taskId);
  CaptureExecute(get_cur_tid());
}

__attribute__((noinline)) void __Pexec_end__(unsigned long taskId) {
  taskGraph->CaptureReturn(get_cur_tid());
  CaptureReturn(get_cur_tid());
}

}

#pragma GCC pop_options
