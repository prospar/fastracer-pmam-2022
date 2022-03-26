#ifndef __TBB_t_debug_task_H
#define __TBB_t_debug_task_H

// FIXME: SB: Is it necessary to turn off optimizations?
#pragma GCC push_options
#pragma GCC optimize("O0")

//#define NUM_TASKS 30

#include "debug_time.h"
#include "exec_calls.h"
#include "stats.h"
#include <iostream>
#include <map>

#define CHECK_AV __attribute__((type_annotate("check_av")))

#if DEBUG_TIME
extern Time_Task_Management time_task_management;
extern Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
// extern my_lock debug_lock;
// extern unsigned recordmemt = 0, recordmemi = 0, rd1 = 0, rd2 = 0, wr1 = 0, wr2 = 0, spawnt = 0,
//                 spawn_roott = 0, spawn_waitt = 0, waitt = 0;
// extern unsigned recordmemn = 0;
#endif
namespace tbb {

// PROSPAR: This wrapper class seems to be there to allow build the DPST.
class t_debug_task : public task {
private:
  size_t taskId;
  void setTaskId(size_t taskId, int sp_only) { this->taskId = taskId; }

public:
  // static void __TBB_EXPORTED_METHOD spawn( task& t, size_t taskId );__attribute__((optimize(0)));
  void __TBB_EXPORTED_METHOD spawn(task& t);                  //__attribute__((optimize(0)));
  static void __TBB_EXPORTED_METHOD spawn_root_and_wait(task& root); //__attribute__((optimize(0)));
  void spawn_and_wait_for_all(task& child);                          //__attribute__((optimize(0)));
  void wait_for_all();                                               //__attribute__((optimize(0)));
  size_t getTaskId() { return taskId; }
};

inline void print_state(task::state_type s) {
  if (s == task::executing)
    std::cout << "executing\n";
  else if (s == task::reexecute)
    std::cout << "reexecuting\n";
  else if (s == task::ready)
    std::cout << "ready\n";
  else if (s == task::allocated)
    std::cout << "allocated\n";
  else if (s == task::freed)
    std::cout << "freed\n";
  else if (s == task::recycle)
    std::cout << "recycle\n";
  //  else if(s == task::to_enqueue)
  //    std::cout << "to_enqueue\n";
}

inline void t_debug_task::spawn(task& t) {
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
  uint32_t child_taskId = (static_cast<t_debug_task&>(t)).getTaskId();

#if DEBUG_TIME
  HRTimer spawn_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time1 = HR::now();
  // my_releaselock(&debug_lock);
#endif
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  uint32_t cur_taskId = cur[threadId].top();
  if (t.parent() != this) {
    cur_taskId = tstate_nodes[cur_taskId].parent_taskId;
  }
  CaptureSpawn(t, cur_taskId, child_taskId);
#if DEBUG_TIME
  HRTimer tbb_spawn_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time2 = HR::now();
  // my_releaselock(&debug_lock);
#endif
  task::spawn(t);
#if DEBUG_TIME
  HRTimer tbb_spawn_end = HR::now();
  HRTimer spawn_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.spawn_time += duration_cast<nanoseconds>(spawn_end - spawn_start).count();
  time_task_management.tbb_spawn_time +=
      duration_cast<nanoseconds>(tbb_spawn_end - tbb_spawn_start).count();
  time_task_management.num_spawn += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
  // my_getlock(&debug_lock);
  // spawnt += duration_cast<nanoseconds>(HR::now() - time2).count();
  // my_releaselock(&debug_lock);
#endif
}

#if 0
  inline void t_debug_task::spawn(task& t) {
    (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
    task::spawn(t);

#endif

inline void t_debug_task::spawn_root_and_wait(task& t) {
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
  uint32_t child_taskId = (static_cast<t_debug_task&>(t)).getTaskId();
//  std::cout << "spanw_root_and_wait starting\n";
#if DEBUG_TIME
  HRTimer spawn_root_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time1 = HR::now();
  // my_releaselock(&debug_lock);
#endif
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  uint32_t cur_taskId = cur[threadId].top();
  CaptureSpawn(t, cur_taskId, child_taskId);
#if DEBUG_TIME
  HRTimer tbb_spawn_root_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time2 = HR::now();
  // my_releaselock(&debug_lock);
#endif
  tbb::task::spawn_root_and_wait(t);
#if DEBUG_TIME
  HRTimer tbb_spawn_root_end = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time3 = HR::now();
  // my_releaselock(&debug_lock);
#endif
  Capturewait_for_all();

#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_tot_wait_calls += 1;
  my_releaselock(&globalStats.gs_lock);
#endif

#if DEBUG_TIME
  HRTimer spawn_root_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.spawn_root_and_wait_time +=
      duration_cast<nanoseconds>(spawn_root_end - spawn_root_start).count();
  time_task_management.tbb_spawn_root_and_wait_time +=
      duration_cast<nanoseconds>(tbb_spawn_root_end - tbb_spawn_root_start).count();
  time_task_management.num_spawn_root_and_wait += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
  // my_getlock(&debug_lock);
  // spawn_roott += duration_cast<nanoseconds>(time3 - time2).count();
  // my_releaselock(&debug_lock);
#endif
  //  std::cout << "Spawn_root_and_wait ending\n";
}

inline void t_debug_task::spawn_and_wait_for_all(task& t) {
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
  uint32_t child_taskId = (static_cast<t_debug_task&>(t)).getTaskId();
#if DEBUG_TIME
  HRTimer spawn_wait_all_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time1 = HR::now();
  // my_releaselock(&debug_lock);
#endif
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  uint32_t cur_taskId = cur[threadId].top();
  CaptureSpawn(t, cur_taskId, child_taskId);
#if DEBUG_TIME
  HRTimer tbb_spawn_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time2 = HR::now();
  // my_releaselock(&debug_lock);
#endif

  task::spawn_and_wait_for_all(t);

  Capturewait_for_all();

#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_tot_wait_calls += 1;
  my_releaselock(&globalStats.gs_lock);
#endif

#if DEBUG_TIME
  HRTimer spawn_wait_all_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.spawn_and_wait_for_all_time +=
      duration_cast<nanoseconds>(spawn_wait_all_end - spawn_wait_all_start).count();
  time_task_management.tbb_spawn_and_wait_for_all_time +=
      duration_cast<nanoseconds>(tbb_spawn_wait_all_end - tbb_spawn_wait_all_start).count();
  time_task_management.num_spawn_and_wait_for_all += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
  // my_getlock(&debug_lock);
  // spawn_waitt += duration_cast<nanoseconds>(time3 - time2).count();
  // my_releaselock(&debug_lock);
#endif
}

inline void t_debug_task::wait_for_all() {
#if DEBUG_TIME
  HRTimer wait_all_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time1 = HR::now();
  // my_releaselock(&debug_lock);
#endif

#if DEBUG_TIME
  HRTimer tbb_wait_all_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time2 = HR::now();
  // my_releaselock(&debug_lock);
#endif
  task::wait_for_all();
#if DEBUG_TIME
  HRTimer tbb_wait_all_end = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time3 = HR::now();
  // my_releaselock(&debug_lock);
#endif

  Capturewait_for_all();

#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_tot_wait_calls += 1;
  my_releaselock(&globalStats.gs_lock);
#endif

#if DEBUG_TIME
  HRTimer wait_all_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.wait_for_all_time +=
      duration_cast<nanoseconds>(wait_all_end - wait_all_start).count();
  time_task_management.tbb_wait_for_all_time +=
      duration_cast<nanoseconds>(tbb_wait_all_end - tbb_wait_all_start).count();
  time_task_management.num_wait_for_all += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
  // my_getlock(&debug_lock);
  // waitt += duration_cast<nanoseconds>(time3 - time2).count();
  // my_releaselock(&debug_lock);
#endif
}
} // namespace tbb

#pragma GCC pop_options
#endif
