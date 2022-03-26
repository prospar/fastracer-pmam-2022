#ifndef __TBB_t_debug_task_H
#define __TBB_t_debug_task_H

// FIXME: SB: Is it necessary to turn off optimizations?
#pragma GCC push_options
#pragma GCC optimize("O0")

//#define NUM_TASKS 30

#include "debug_time.h"
#include "exec_calls.h"
#include "omrd.h"
#include "stats.h"
#include <iostream>
#include <map>

#define CHECK_AV __attribute__((type_annotate("check_av")))

#if STATS
extern GlobalStats globalStats;
#endif

#if DEBUG_TIME
extern Time_Task_Management time_task_management;
extern Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
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

inline void t_debug_task::spawn(task& t) {
  // std::cout<<"SPAWN************************"<<std::endl;
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
  size_t taskid = (static_cast<t_debug_task&>(t)).getTaskId();

  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();

  if (t.parent() != this) {
    cur_task = cur_task->parent;
  }
  my_getlock(&cur_task->tlock);

  TaskState* child_task = new TaskState(cur_task);
  child_task->tid = taskid;
  assert(cur_task);
  assert(child_task);
  //  my_getlock(&thread_local_lock[threadId]);

  if (!cur_task->sync_english) { // first of spawn group
    assert(!cur_task->sync_hebrew);
    cur_task->sync_english = g_english->insert(cur_task->current_english);
    cur_task->sync_hebrew = g_hebrew->insert(cur_task->current_hebrew);
  }
  child_task->current_english = g_english->insert(cur_task->current_english);
  cur_task->cont_english = g_english->insert(child_task->current_english);
  cur_task->cont_hebrew = g_hebrew->insert(cur_task->current_hebrew);
  child_task->current_hebrew = g_hebrew->insert(cur_task->cont_hebrew);

  child_task->cont_english = NULL;
  child_task->cont_hebrew = NULL;
  child_task->sync_english = NULL;
  child_task->sync_hebrew = NULL;
  cur_task->current_english = cur_task->cont_english;
  cur_task->current_hebrew = cur_task->cont_hebrew;

  // my_releaselock(&thread_local_lock[threadId]);
  // my_getlock(&cur_task->tlock);
  (cur_task->child).push_back(child_task);
  my_releaselock(&cur_task->tlock);

  child_task->parent = cur_task;
  child_task->mytask = &t;
  my_getlock(&temp_cur_map_lock);
  temp_cur_map.insert(std::pair<size_t, TaskState*>(taskid, child_task));
  my_releaselock(&temp_cur_map_lock);
  // TBB task spawn
  task::spawn(t);
  // std::cout<<"SPAWN--------------------------"<<std::endl;
}

inline void t_debug_task::spawn_root_and_wait(task& root) {
  // std::cout<<"SPAWNROOT************************"<<std::endl;

  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  (static_cast<t_debug_task&>(root)).setTaskId(++task_id_ctr, 0);
  size_t taskid = (static_cast<t_debug_task&>(root)).getTaskId();

  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();
  my_getlock(&cur_task->tlock);
  assert(cur_task != NULL);
  TaskState* child_task = new TaskState(cur_task);
  child_task->tid = taskid;

  //  my_getlock(&thread_local_lock[threadId]);
  if (!cur_task->sync_english) { // first of spawn group
    assert(!cur_task->sync_hebrew);
    cur_task->sync_english = g_english->insert(cur_task->current_english);
    cur_task->sync_hebrew = g_hebrew->insert(cur_task->current_hebrew);
  }
  child_task->current_english = g_english->insert(cur_task->current_english);
  cur_task->cont_english = g_english->insert(child_task->current_english);

  cur_task->cont_hebrew = g_hebrew->insert(cur_task->current_hebrew);
  child_task->current_hebrew = g_hebrew->insert(cur_task->cont_hebrew);
  child_task->cont_english = NULL;
  child_task->cont_hebrew = NULL;
  child_task->sync_english = NULL;
  child_task->sync_hebrew = NULL;
  cur_task->current_english = cur_task->cont_english;
  cur_task->current_hebrew = cur_task->cont_hebrew;
  // my_releaselock(&thread_local_lock[threadId]);

  // my_getlock(&cur_task->tlock);
  (cur_task->child).push_back(child_task);
  my_releaselock(&cur_task->tlock);

  my_getlock(&temp_cur_map_lock);
  temp_cur_map.insert(std::pair<size_t, TaskState*>(taskid, child_task));
  my_releaselock(&temp_cur_map_lock);
  child_task->parent = cur_task;
  child_task->mytask = &root;

  // TBB call
  tbb::task::spawn_root_and_wait(root);
  //  my_getlock(&thread_local_lock[threadId]);
  Capturewait_for_all(cur_task);
  // my_releaselock(&thread_local_lock[threadId]);
  // std::cout<<"SPAWNROOT----------------------------"<<std::endl;
}

inline void t_debug_task::spawn_and_wait_for_all(task& child) {
  // std::cout<<"SPAWNWAIT************************"<<std::endl;
  (static_cast<t_debug_task&>(child)).setTaskId(++task_id_ctr, 1);
  size_t taskid = (static_cast<t_debug_task&>(child)).getTaskId();

  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();
  my_getlock(&cur_task->tlock);
  TaskState* child_task = new TaskState(cur_task);
  child_task->tid = taskid;

  //  my_getlock(&thread_local_lock[threadId]);
  if (!cur_task->sync_english) { // first of spawn group
    assert(!cur_task->sync_hebrew);
    cur_task->sync_english = g_english->insert(cur_task->current_english);
    cur_task->sync_hebrew = g_hebrew->insert(cur_task->current_hebrew);
  }

  child_task->current_english = g_english->insert(cur_task->current_english);
  cur_task->cont_english = g_english->insert(child_task->current_english);

  cur_task->cont_hebrew = g_hebrew->insert(cur_task->current_hebrew);
  child_task->current_hebrew = g_hebrew->insert(cur_task->cont_hebrew);

  child_task->cont_english = NULL;
  child_task->cont_hebrew = NULL;
  child_task->sync_english = NULL;
  child_task->sync_hebrew = NULL;
  cur_task->current_english = cur_task->cont_english;
  cur_task->current_hebrew = cur_task->cont_hebrew;
  // my_releaselock(&thread_local_lock[threadId]);

  // my_getlock(&cur_task->tlock);
  (cur_task->child).push_back(child_task);
  my_releaselock(&cur_task->tlock);

  child_task->parent = cur_task;
  child_task->mytask = &child;
  my_getlock(&temp_cur_map_lock);
  temp_cur_map.insert(std::pair<size_t, TaskState*>(taskid, child_task));
  my_releaselock(&temp_cur_map_lock);

  // TBB call
  task::spawn_and_wait_for_all(child);

  //  my_getlock(&thread_local_lock[threadId]);
  Capturewait_for_all(cur_task);
  // my_releaselock(&thread_local_lock[threadId]);
  // std::cout<<"SPAWNWAIT-----------------"<<std::endl;
}

inline void t_debug_task::wait_for_all() {
  // std::cout<<"WAIT************************"<<std::endl;
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  size_t threadId = get_cur_tid();
  //  my_getlock(&thread_local_lock[threadId]);
  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();
  size_t cur_taskid = cur_task->tid;

  // TBB call
  // my_releaselock(&thread_local_lock[threadId]);
  task::wait_for_all();
  //  my_getlock(&thread_local_lock[threadId]);
  Capturewait_for_all(cur_task);
  // my_releaselock(&thread_local_lock[threadId]);
  // std::cout<<"WAIT-------------------------"<<std::endl;
}
} // namespace tbb

#pragma GCC pop_options
#endif
