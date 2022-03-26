#ifndef __TBB_Pt_debug_task_H
#define __TBB_Pt_debug_task_H

#pragma GCC push_options
#pragma GCC optimize ("O0")

#include <map>
#include "tbb/task.h"
#include "Pexec_calls.h"
#include "Nt_debug_task.h"
#include <iostream>
using namespace std;
#define CHECK_AV __attribute__((type_annotate("check_av")))

namespace tbb {

  // PROSPAR: This wrapper class seems to be there to allow build the DPST.
  class Pt_debug_task: public Nt_debug_task {
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

  inline void Pt_debug_task::spawn(task& t) {
//    std::cout << "ptracer spawn starting\n";
    (static_cast<Pt_debug_task&>(t)).setTaskId(++Ptask_id_ctr, 1);
    taskGraph->CaptureSetTaskId(get_cur_tid(), (static_cast<Pt_debug_task&>(t)).getTaskId(), true);
//    std::cout << "ptracer spawn ending\n";
  }

#if 0
  inline void t_debug_task::spawn(task& t) {
    (static_cast<t_debug_task&>(t)).setTaskId(++Ptask_id_ctr, 1);
    task::spawn(t);
  }
#endif

  inline void Pt_debug_task::spawn_root_and_wait( task& root ) {
//  std::cout << "ptracer spawn root and wait starting\n";
    (static_cast<Pt_debug_task&>(root)).setTaskId(++Ptask_id_ctr, 0);
    taskGraph->CaptureSetTaskId(get_cur_tid(), (static_cast<Pt_debug_task&>(root)).getTaskId(), false);
//  std::cout << "ptracer spawn root and wait ending\n";
  }

  inline void Pt_debug_task::spawn_and_wait_for_all( task& child ) {
    (static_cast<Pt_debug_task&>(child)).setTaskId(++Ptask_id_ctr, 1);
    taskGraph->CaptureSetTaskId(get_cur_tid(), (static_cast<Pt_debug_task&>(child)).getTaskId(), true);
    task::spawn_and_wait_for_all(child);
    taskGraph->CaptureWaitOnly(get_cur_tid());
  }

  inline void Pt_debug_task::wait_for_all( ) {
 //   task::wait_for_all();
    taskGraph->CaptureWaitOnly(get_cur_tid());
  }

}

#pragma GCC pop_options
#endif
