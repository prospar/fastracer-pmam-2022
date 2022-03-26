#pragma GCC push_options
#pragma GCC optimize("O0")

#include "exec_calls.h"
#include "Common.H"
#include "stats.h"
#include <cassert>

using namespace std;
using namespace tbb;

#if DEBUG
extern my_lock printLock; // Serialize print statements
#endif

#if STATS
extern GlobalStats globalStats;
#endif

#if DEBUG_TIME
extern Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
#endif

// TODO: How are these two different?
// Next available task id counter
tbb::atomic<size_t> task_id_ctr(0);

tbb::atomic<size_t> tid_ctr(0);
// tbb::atomic<size_t> test_count(0);

my_lock parent_map_lock(0);
my_lock lock_map_lock(0);

// Map from thread id to task id
std::map<TBB_TID, stack<size_t>> thrid_taskid_map;
my_lock thrid_taskid_map_lock(0);

// Map from task id to per-task state
concurrent_hash_map<size_t, TaskState*> taskid_taskstate_map;
my_lock taskid_taskstate_map_lock(0);

typedef std::pair<size_t, TaskState*> TASKID_MAP_PAIR;

void Capturewait_for_all() {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  size_t parent_taskid = get_cur_tid();
  assert(parent_taskid < MAX_NUM_TASKS);

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
  parent_task_state = ac->second;
  ac.release();

  Capturewait_for_all_task(parent_task_state);
}

void Capturewait_for_all_task(TaskState* parent_task_state) {
  concurrent_hash_map<size_t, TaskState*>::accessor ac;
  if (parent_task_state->m_child.size() == 0)
    return;

  //  if(cur_task_state->taskId == 0){
  //    cur_task_state->child_list->clear();
  //    num_dead_tasks = task_id_ctr;
  //    dead_clock_value = cur_task_state->clock;
  //    cur_task_state->clock++;
  ////    std::cout << "resetting in Capturewait_for_all_task num_dead_tasks " << num_dead_tasks << " dead_clock_value " << dead_clock_value << "\n";
  //    return;
  //  }

  for (std::list<size_t>::iterator it = parent_task_state->m_child.begin();
       it != parent_task_state->m_child.end(); ++it) {
    my_getlock(&taskid_taskstate_map_lock);
    bool found = taskid_taskstate_map.find(ac, *it);
    assert(found);
    TaskState* child_task_state = ac->second;
    ac.release();
    my_releaselock(&taskid_taskstate_map_lock);

    if (child_task_state->m_child.size() > 0)
      Capturewait_for_all_task(child_task_state);

    assert(parent_task_state != child_task_state);
    join_vc(parent_task_state->m_vector_clock, child_task_state->m_vector_clock);
  }
  parent_task_state->m_child.clear();
}

extern "C" {

// Beginning of task taskId
__attribute__((noinline)) void __exec_begin__(unsigned long taskId) {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id(); // Pthread handle

  my_getlock(&thrid_taskid_map_lock);
  (thrid_taskid_map[pthd_id]).push(taskId);
  my_releaselock(&thrid_taskid_map_lock);

#if DEBUG
  my_getlock(&printLock);
  if (get_cur_tid() != taskId) {
    std::cout << " Get cur tid: " << get_cur_tid() << " Task id: " << taskId << "\n";
  }
  assert(get_cur_tid() == taskId);
  std::cout << "__exec_begin__: Thread id: " << pthd_id << " Task id : " << taskId << "\n";
  my_releaselock(&printLock);
#endif
}

// Task end
__attribute__((noinline)) void __exec_end__(unsigned long taskid) {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();

  // concurrent_hash_map<TBB_TID,stack<size_t> >::accessor ac;
  // if(tid_map.find(ac,pthd_id))
  //{(ac->second).pop();
  // ac.release();}

  assert(get_cur_tid() == taskid);
  my_getlock(&thrid_taskid_map_lock);
  assert(!thrid_taskid_map[pthd_id].empty());
  (thrid_taskid_map[pthd_id]).pop();
  my_releaselock(&thrid_taskid_map_lock);

#if DEBUG
  my_getlock(&printLock);
  std::cout << "__exec_end__: Thread id: " << pthd_id << " Task id: " << taskid << "\n";
  my_releaselock(&printLock);
#endif

#if STATS
  concurrent_hash_map<size_t, TaskState*>::accessor ac;
  bool found = taskid_taskstate_map.find(ac, taskid);
  assert(found);
  TaskState* cur_task_state = ac->second;
  PerTaskStats* ptstats = cur_task_state->m_task_stats;
  assert(ptstats != NULL);
  globalStats.accumulate(ptstats);
  ac.release();
#endif
}

__attribute__((noinline)) size_t get_cur_tid() {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id(); // Pthread handle
  stack<size_t> s;
  size_t my_tid;

  my_getlock(&thrid_taskid_map_lock);
  if (thrid_taskid_map.count(pthd_id) != 0) {
    if ((thrid_taskid_map[pthd_id]).empty()) {
      my_tid = 0;
      (thrid_taskid_map[pthd_id]).push(0);
    }
    my_tid = (thrid_taskid_map[pthd_id]).top();
  } else {
    s.push(0);
    my_tid = 0;
    thrid_taskid_map.insert(pair<TBB_TID, stack<size_t>>(pthd_id, s));
  }
  my_releaselock(&thrid_taskid_map_lock);
  return my_tid;
}
}

#pragma GCC pop_options
