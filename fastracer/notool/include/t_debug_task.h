#ifndef __TBB_t_debug_task_H
#define __TBB_t_debug_task_H

// FIXME: Is it necessary to turn off optimizations?
#pragma GCC push_options
#pragma GCC optimize("O0")

#include "exec_calls.h"
#include "tbb/task.h"
#include <iostream>

#define CHECK_AV __attribute__((type_annotate("check_av")))

namespace tbb {

// PROSPAR: This wrapper class seems to be there to allow build the DPST.
class t_debug_task : public task {
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

// Puts task t into the ready pool and immediately returns.
inline void t_debug_task::spawn(task& t) { task::spawn(t); }

// Example parent task id is 0, root task id is 1 at the beginning.
inline void t_debug_task::spawn_root_and_wait(task& root) { task::spawn_root_and_wait(root); }

inline void t_debug_task::spawn_and_wait_for_all(task& child) {
  task::spawn_and_wait_for_all(child);
}

inline void t_debug_task::wait_for_all() { task::wait_for_all(); }

} // namespace tbb

#pragma GCC pop_options
#endif
