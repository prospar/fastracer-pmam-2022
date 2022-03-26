#ifndef __TBB_t_debug_task_H
#define __TBB_t_debug_task_H

#pragma GCC push_options
#pragma GCC optimize ("O0")

#include <map>
#include "tbb/task.h"
#include "exec_calls.h"
#define NUM_TASKS 30
#define CHECK_AV __attribute__((type_annotate("check_av")))

#if DEBUG_TIME
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
extern my_lock debug_lock;
extern unsigned spawnt,spawn_roott,spawn_waitt,waitt;
//extern unsigned recordmemn = 0;
#endif
namespace tbb {

  // PROSPAR: This wrapper class seems to be there to allow build the DPST.
  class t_debug_task: public task {
  private:
    size_t taskId;
    void setTaskId(size_t taskId, int sp_only) { this->taskId = taskId; }

  public:
    //static void __TBB_EXPORTED_METHOD spawn( task& t, size_t taskId );__attribute__((optimize(0)));
    static void __TBB_EXPORTED_METHOD spawn( task& t );//__attribute__((optimize(0)));
    static void __TBB_EXPORTED_METHOD spawn_root_and_wait( task& root );//__attribute__((optimize(0)));
    void spawn_and_wait_for_all( task& child );//__attribute__((optimize(0)));
    void wait_for_all( );//__attribute__((optimize(0)));
    size_t getTaskId() { return taskId; }
  };

  inline void t_debug_task::spawn(task& t) {
    (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
    taskGraph->CaptureSetTaskId(get_cur_tid(), (static_cast<t_debug_task&>(t)).getTaskId(), true);
#if DEBUG_TIME
  my_getlock(&debug_lock);
  HRTimer time2 = HR::now();
  my_releaselock(&debug_lock);
#endif
    task::spawn(t);
#if DEBUG_TIME
  my_getlock(&debug_lock);
  spawnt += duration_cast<milliseconds>(HR::now() - time2).count();
  my_releaselock(&debug_lock);
#endif
  }

#if 0
  inline void t_debug_task::spawn(task& t) {
    (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
    task::spawn(t);
  }
#endif

  inline void t_debug_task::spawn_root_and_wait( task& root ) {
    (static_cast<t_debug_task&>(root)).setTaskId(++task_id_ctr, 0);
    taskGraph->CaptureSetTaskId(get_cur_tid(), (static_cast<t_debug_task&>(root)).getTaskId(), false);
#if DEBUG_TIME
  my_getlock(&debug_lock);
  HRTimer time2 = HR::now();
  my_releaselock(&debug_lock);
#endif
    task::spawn_root_and_wait(root);
#if DEBUG_TIME
  my_getlock(&debug_lock);
  spawn_roott += duration_cast<milliseconds>(HR::now() - time2).count();
  my_releaselock(&debug_lock);
#endif
  }

  inline void t_debug_task::spawn_and_wait_for_all( task& child ) {
    (static_cast<t_debug_task&>(child)).setTaskId(++task_id_ctr, 1);
    taskGraph->CaptureSetTaskId(get_cur_tid(), (static_cast<t_debug_task&>(child)).getTaskId(), true);
#if DEBUG_TIME
  my_getlock(&debug_lock);
  HRTimer time2 = HR::now();
  my_releaselock(&debug_lock);
#endif
    task::spawn_and_wait_for_all(child);
#if DEBUG_TIME
  my_getlock(&debug_lock);
  spawn_waitt += duration_cast<milliseconds>(HR::now() - time2).count();
  my_releaselock(&debug_lock);
#endif
    taskGraph->CaptureWaitOnly(get_cur_tid());
  }

  inline void t_debug_task::wait_for_all( ) {
#if DEBUG_TIME
  my_getlock(&debug_lock);
  HRTimer time2 = HR::now();
  my_releaselock(&debug_lock);
#endif
    task::wait_for_all();
#if DEBUG_TIME
  my_getlock(&debug_lock);
  waitt += duration_cast<milliseconds>(HR::now() - time2).count();
  my_releaselock(&debug_lock);
#endif
    taskGraph->CaptureWaitOnly(get_cur_tid());
  }

}

#pragma GCC pop_options
#endif
