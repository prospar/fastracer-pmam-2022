#ifndef __TBB_t_debug_task_H
#define __TBB_t_debug_task_H


#include "tbb/concurrent_hash_map.h"
#include "tbb/task.h"
#include "tbb/tbb_thread.h"
#include "Dr_Detector.h"
#include "Pt_debug_task.h"


#define CHECK_AV __attribute__((type_annotate("check_av")))
#pragma GCC push_options
#pragma GCC optimize("O0")
namespace tbb {


class t_debug_task : public Pt_debug_task {
private:
  size_t taskId;
  void setTaskId(size_t taskId, int sp_only) { this->taskId = taskId; }

public:
  // static void __TBB_EXPORTED_METHOD spawn( task& t, size_t taskId );__attribute__((optimize(0)));
  static void __TBB_EXPORTED_METHOD spawn(task& t);                  //__attribute__((optimize(0)));
  static void __TBB_EXPORTED_METHOD spawn_root_and_wait(task& root); //__attribute__((optimize(0)));
  void spawn_and_wait_for_all(task& child);                          //__attribute__((optimize(0)));
  void wait_for_all();                                               //__attribute__((optimize(0)));
  size_t getTaskId() { return taskId; }
};


inline void t_debug_task::spawn(task& t) {
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
#ifdef newalgo
  Nt_debug_task::spawn(t);
#endif
#ifdef ptracer
  Pt_debug_task::spawn(t);
#endif
#ifdef fasttrack
  Ft_debug_task::spawn(t);
#endif
  task::spawn(t);

}


inline void t_debug_task::spawn_root_and_wait(task& t) {
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
  size_t taskId = (static_cast<t_debug_task&>(t)).getTaskId();
#ifdef newalgo
  Nt_debug_task::spawn_root_and_wait(t);
#endif
#ifdef ptracer
  Pt_debug_task::spawn_root_and_wait(t);
#endif
#ifdef fasttrack
  Ft_debug_task::spawn_root_and_wait(t);
#endif
  task::spawn_root_and_wait(t);

#ifdef newalgo
  Nt_debug_task::wait_for_one(taskId);
#endif

#ifdef fasttrack
  Ft_debug_task::wait_for_one(taskId);
#endif
}

inline void t_debug_task::spawn_and_wait_for_all(task& t) {
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
#ifdef newalgo
  Nt_debug_task::spawn(t);
#endif
#ifdef ptracer
  Pt_debug_task::spawn(t);
#endif
#ifdef fasttrack
  Ft_debug_task::spawn(t);
#endif


  task::spawn_and_wait_for_all(t);


#ifdef newalgo
  Nt_debug_task::wait_for_all();
#endif
#ifdef ptracer
  Pt_debug_task::wait_for_all();
#endif
#ifdef fasttrack
  Ft_debug_task::wait_for_all();
#endif
}

inline void t_debug_task::wait_for_all(){
  task::wait_for_all();
#ifdef newalgo
  Nt_debug_task::wait_for_all();
#endif
#ifdef ptracer
  Pt_debug_task::wait_for_all();
#endif
#ifdef fasttrack
  Ft_debug_task::wait_for_all();
#endif
}

}
#pragma GCC pop_options
#endif
