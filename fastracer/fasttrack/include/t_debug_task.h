#ifndef __TBB_t_debug_task_H
#define __TBB_t_debug_task_H

// FIXME: SB: Is it necessary to turn off optimizations?
#include "Common.H"
#include <pthread.h>

#pragma GCC push_options
#pragma GCC optimize("O0")

//#define NUM_TASKS 30

#include "exec_calls.h"
#include <iostream>
#include <map>

#define CHECK_AV __attribute__((type_annotate("check_av")))

#if DEBUG
extern my_lock printLock;
#endif

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

  // TODO: What is the difference with get_cur_tid()?
  size_t getTaskId() { return taskId; }
};

// Puts task t into the ready pool and immediately returns.
inline void t_debug_task::spawn(task& t) {
#if DEBUG_TIME
  HRTimer spawn_start = HR::now();

#endif
  // Returns the pthread_t handle for pthread_self()
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
  size_t child_taskid = (static_cast<t_debug_task&>(t)).getTaskId();
  assert(child_taskid < MAX_NUM_TASKS);
  size_t parent_taskid = get_cur_tid();

#if DEBUG
  my_getlock(&printLock);
  std::cout << "t_debug_task::spawn: Thread id: " << pthd_id
            << " Parent task id : " << parent_taskid << " Id of spawned task: " << child_taskid
            << "\n";
  my_releaselock(&printLock);
#endif

  TaskState* parent_task_state = NULL;
  // my_getlock(&(taskid_map_lock));
  concurrent_hash_map<size_t, TaskState*>::accessor ac;
#if DEBUG_TIME
  HRTimer find_start = HR::now();
#endif
  bool found = taskid_taskstate_map.find(ac, parent_taskid);
#if DEBUG_TIME
  HRTimer find_end = HR::now();
  my_getlock(&time_dr_detector.time_DR_detector_lock);
  time_dr_detector.taskid_map_find_time +=
      duration_cast<nanoseconds>(find_end - find_start).count();
  time_dr_detector.num_tid_find += 1;
  my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
  // Can the parent task be absent from the thread map?
  assert(found);
  if (found) {
    parent_task_state = ac->second;
  } else {
    parent_task_state = new TaskState(parent_taskid, pthd_id);
    taskid_taskstate_map.insert(ac, parent_taskid);
    ac->second = parent_task_state;
    // taskid_map.insert(std::pair<size_t,threadstate*>(tid,cur_task));
  }
  ac.release();

  if (t.parent() != parent_task_state->mytask) {
    parent_task_state = parent_task_state->parent;
    parent_taskid = parent_task_state->m_taskid;
  }

  TaskState* child_task_state = new TaskState(child_taskid, pthd_id);
  taskid_taskstate_map.insert(ac, child_taskid);
  ac->second = child_task_state;
  ac.release();

  // taskid_map.insert(std::pair <size_t,threadstate*>(taskid,child_task));
  // my_releaselock(&(taskid_map_lock));

#if DEBUG
  // dumpTaskIdToTaskStateMap();
#endif

  my_getlock(&(parent_task_state->tlock));
  auto parent_vc = parent_task_state->m_vector_clock;
  auto child_vc = child_task_state->m_vector_clock;
  copy_vc(parent_vc, child_vc);
  update_vc(child_vc, child_taskid, child_vc[child_taskid] + 1);
  update_vc(parent_vc, parent_taskid, parent_vc[parent_taskid] + 1);

  child_task_state->mytask = &t;
  child_task_state->parent = parent_task_state;

  (parent_task_state->m_child).push_back(child_taskid);
  my_releaselock(&(parent_task_state->tlock));

#if STATS
  child_task_state->m_task_stats->depth = parent_task_state->m_task_stats->depth + 1;

  my_getlock(&globalStats.gs_lock);
  if (globalStats.max_task_depth < child_task_state->m_task_stats->depth)
    globalStats.max_task_depth = child_task_state->m_task_stats->depth;
  my_releaselock(&globalStats.gs_lock);
#endif
#if DEBUG_TIME
  HRTimer tbb_spawn_start = HR::now();
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
#endif
}

// Example parent task id is 0, root task id is 1 at the beginning.
inline void t_debug_task::spawn_root_and_wait(task& root) {
#if DEBUG_TIME
  HRTimer spawn_root_start = HR::now();
#endif
  // Returns the pthread_t handle for pthread_self()
  TBB_TID parent_pthd_id = tbb::this_tbb_thread::get_id();
  (static_cast<t_debug_task&>(root)).setTaskId(++task_id_ctr, 0);
  size_t root_taskid = (static_cast<t_debug_task&>(root)).getTaskId();
  assert(root_taskid < MAX_NUM_TASKS);
  size_t parent_taskid = get_cur_tid();

#if DEBUG
  my_getlock(&printLock);
  std::cout << "t_debug_task::spawn_root_and_wait: Thread id: " << parent_pthd_id
            << " Parent task id: " << parent_taskid << " Root task id: " << root_taskid << "\n";
  my_releaselock(&printLock);
#endif

  TaskState* parent_task_state = NULL;
  // my_getlock(&(taskid_map_lock));
  concurrent_hash_map<size_t, TaskState*>::accessor ac;
#if DEBUG_TIME
  HRTimer find_start = HR::now();
#endif
  bool found = taskid_taskstate_map.find(ac, parent_taskid);
#if DEBUG_TIME
  HRTimer find_end = HR::now();
  my_getlock(&time_dr_detector.time_DR_detector_lock);
  time_dr_detector.taskid_map_find_time +=
      duration_cast<nanoseconds>(find_end - find_start).count();
  time_dr_detector.num_tid_find += 1;
  my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
  // Can the parent task be absent from the thread map?
  // assert(found);
  if (found) {
    parent_task_state = ac->second;
  } else {
    parent_task_state = new TaskState(parent_taskid, parent_pthd_id);
    taskid_taskstate_map.insert(ac, parent_taskid);
    ac->second = parent_task_state;
    // taskid_map.insert(std::pair<size_t,threadstate*>(tid,cur_task));
  }
  ac.release();

  TaskState* root_task_state = new TaskState(root_taskid, parent_pthd_id);
  taskid_taskstate_map.insert(ac, root_taskid);
  ac->second = root_task_state;
  ac.release();

  // taskid_map.insert(std::pair <size_t,threadstate*>(taskid,child_task));
  // my_releaselock(&(taskid_map_lock));

#if DEBUG
  // dumpTaskIdToTaskStateMap();
#endif

  my_getlock(&(parent_task_state->tlock));
  auto parent_vc = parent_task_state->m_vector_clock;
  auto root_vc = root_task_state->m_vector_clock;
  copy_vc(parent_vc, root_vc);
  update_vc(root_vc, root_taskid, root_vc[root_taskid] + 1);
  update_vc(parent_vc, parent_taskid, parent_vc[parent_taskid] + 1);

  root_task_state->mytask = &root;
  root_task_state->parent = parent_task_state;
  (parent_task_state->m_child).push_back(root_taskid);
  my_releaselock(&(parent_task_state->tlock));

#if STATS
  root_task_state->m_task_stats->depth = parent_task_state->m_task_stats->depth + 1;

  my_getlock(&globalStats.gs_lock);
  if (globalStats.max_task_depth < root_task_state->m_task_stats->depth) {
    globalStats.max_task_depth = root_task_state->m_task_stats->depth;
  }
  my_releaselock(&globalStats.gs_lock);
#endif
#if DEBUG_TIME
  HRTimer tbb_spawn_root_start = HR::now();
#endif
  // FIXME: Does this wait for all child tasks or only root? Assumed only root.
  tbb::task::spawn_root_and_wait(root);
  // FIXME: Perform a join with the root_task

#if DEBUG_TIME
  HRTimer tbb_spawn_root_end = HR::now();
#endif

  Capturewait_for_all();

#if DEBUG_TIME
  HRTimer spawn_root_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.spawn_root_and_wait_time +=
      duration_cast<nanoseconds>(spawn_root_end - spawn_root_start).count();
  time_task_management.tbb_spawn_root_and_wait_time +=
      duration_cast<nanoseconds>(tbb_spawn_root_end - tbb_spawn_root_start).count();
  time_task_management.num_spawn_root_and_wait += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
#endif
}

inline void t_debug_task::spawn_and_wait_for_all(task& child) {
  // Returns the pthread_t handle for pthread_self()
#if DEBUG_TIME
  HRTimer spawn_wait_all_start = HR::now();
#endif
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  (static_cast<t_debug_task&>(child)).setTaskId(++task_id_ctr, 1);
  size_t child_taskid = (static_cast<t_debug_task&>(child)).getTaskId();
  assert(child_taskid < MAX_NUM_TASKS);
  size_t parent_taskid = get_cur_tid();

#if DEBUG
  my_getlock(&printLock);
  std::cout << "t_debug_task::spawn_root_and_wait: Thread id: " << pthd_id
            << " Child task id: " << child_taskid << "\n";
  std::cout << "Pthread handle: " << pthread_self() << "\n";
  my_releaselock(&printLock);
#endif

  TaskState* parent_task_state = NULL;
  concurrent_hash_map<size_t, TaskState*>::accessor ac;
#if DEBUG_TIME
  HRTimer find_start = HR::now();
#endif
  bool found = taskid_taskstate_map.find(ac, parent_taskid);
#if DEBUG_TIME
  HRTimer find_end = HR::now();
  my_getlock(&time_dr_detector.time_DR_detector_lock);
  time_dr_detector.taskid_map_find_time +=
      duration_cast<nanoseconds>(find_end - find_start).count();
  time_dr_detector.num_tid_find += 1;
  my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
  // Can the parent task be absent from the thread map?
  assert(found);
  if (found) {
    parent_task_state = ac->second;
  } else {
    parent_task_state = new TaskState(parent_taskid, pthd_id);
    taskid_taskstate_map.insert(ac, parent_taskid);
    ac->second = parent_task_state;
    // taskid_map.insert(std::pair<size_t,threadstate*>(tid,cur_task));
  }
  ac.release();

  TaskState* child_task_state = new TaskState(child_taskid, pthd_id);
  taskid_taskstate_map.insert(ac, child_taskid);
  ac->second = child_task_state;
  ac.release();

  // taskid_map.insert(std::pair <size_t,threadstate*>(taskid,child_task));
  // my_releaselock(&(taskid_map_lock));

#if DEBUG
  dumpTaskIdToTaskStateMap();
#endif

  my_getlock(&(parent_task_state->tlock));
  auto parent_vc = parent_task_state->m_vector_clock;
  auto child_vc = child_task_state->m_vector_clock;
  copy_vc(parent_vc, child_vc);
  update_vc(child_vc, child_taskid, child_vc[child_taskid] + 1);
  update_vc(parent_vc, parent_taskid, parent_vc[parent_taskid] + 1);

  child_task_state->mytask = &child;
  child_task_state->parent = parent_task_state;

  (parent_task_state->m_child).push_back(child_taskid);
  my_releaselock(&(parent_task_state->tlock));

#if STATS
  child_task_state->m_task_stats->depth = parent_task_state->m_task_stats->depth + 1;

  my_getlock(&globalStats.gs_lock);
  if (globalStats.max_task_depth < child_task_state->m_task_stats->depth)
    globalStats.max_task_depth = child_task_state->m_task_stats->depth;
  my_releaselock(&globalStats.gs_lock);
#endif
#if DEBUG_TIME
  HRTimer tbb_spawn_wait_all_start = HR::now();
#endif
  task::spawn_and_wait_for_all(child);
#if DEBUG_TIME
  HRTimer tbb_spawn_wait_all_end = HR::now();
#endif
  // Now perform a join with the parent task

  Capturewait_for_all();

#if DEBUG_TIME
  HRTimer spawn_wait_all_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.spawn_and_wait_for_all_time +=
      duration_cast<nanoseconds>(spawn_wait_all_end - spawn_wait_all_start).count();
  time_task_management.tbb_spawn_and_wait_for_all_time +=
      duration_cast<nanoseconds>(tbb_spawn_wait_all_end - tbb_spawn_wait_all_start).count();
  time_task_management.num_spawn_and_wait_for_all += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
#endif
}

inline void t_debug_task::wait_for_all() {
#if DEBUG_TIME
  HRTimer wait_all_start = HR::now();
#endif
  // Returns the pthread_t handle for pthread_self()

#if DEBUG
  my_getlock(&printLock);
  std::cout << "t_debug_task::wait_for_all: Thread id: " << pthd_id << " Task id: " << parent_taskid
            << "\n";
  my_releaselock(&printLock);
#endif
#if DEBUG_TIME
  HRTimer tbb_wait_all_start = HR::now();
#endif
  task::wait_for_all();
#if DEBUG_TIME
  HRTimer tbb_wait_all_end = HR::now();
#endif
  // Now perform a join with the parent task

  Capturewait_for_all();

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
