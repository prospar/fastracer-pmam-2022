#pragma GCC push_options
#pragma GCC optimize("O0")

#include "exec_calls.h"
#include "Common.H"
#include "omrd.h"
#include <iostream>

using namespace std;
using namespace tbb;

tbb::atomic<size_t> task_id_ctr(0);
tbb::atomic<size_t> tid_ctr(0);
tbb::atomic<size_t> test_count(0);
tbb::atomic<size_t> lock_ticker(0);

my_lock tid_map_lock(0);
//my_lock taskid_map_lock(0);
my_lock parent_map_lock(0);
my_lock lock_map_lock(0);
my_lock temp_cur_map_lock(0);
std::map<TBB_TID, size_t> tid_map;
// concurrent_hash_map<size_t, TaskState*> taskid_map;
concurrent_hash_map<ADDRINT, VarState*> var_map;
std::stack<TaskState*> cur[NUM_THREADS];
std::map<size_t, TaskState*> temp_cur_map;
std::map<size_t, size_t> lock_map;
size_t num_dead_tasks = 0;
size_t dead_clock_value = 0;
my_lock clear_child_lock(0);

#if DEBUG_TIME
extern Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
#endif

bool var_access::races_with(om_node* curr_estrand, om_node* curr_hstrand) {
  // std::cout<<"*********** races with ************" << std::endl;
  bool prec_in_english, prec_in_hebrew;
  size_t relabel_id = 0;
  // do {
  //   relabel_id = g_relabel_id;
  //   // QUERY_START;
  size_t threadId = get_cur_tid();
  my_getlock(&thread_local_lock[threadId]);
  prec_in_english = om_precedes(estrand, curr_estrand);
  prec_in_hebrew = om_precedes(hstrand, curr_hstrand);
  my_releaselock(&thread_local_lock[threadId]);
  // QUERY_END;
  // } while ( !( (relabel_id & 0x1) == 0 && relabel_id == g_relabel_id));

  // race if the ordering in english and hebrew differ
  return (prec_in_english != prec_in_hebrew);
}

void set_incomplete() {
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();
  assert(cur_task != NULL);
  cur_task->complete = false;
}
void set_complete() {
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();
  assert(cur_task != NULL);
  cur_task->complete = true;
}
void set_execute_true() {
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();
  assert(cur_task != NULL);
  cur_task->execute = true;
}

void set_execute_false() {
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();
  assert(cur_task != NULL);
  cur_task->execute = false;
}

void clear_child_list() {
  my_getlock(&clear_child_lock);
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();
  assert(cur_task != NULL);
  //  cur_task->execute = true;
  TaskState* parent_task = cur_task->parent;
  assert(parent_task != NULL);
  //  my_getlock(&cur_task->child_lock);
  //  my_getlock(&parent_task->child_lock);
  parent_task->child.insert(parent_task->child.end(), cur_task->child.begin(),
                            cur_task->child.end());
  //  my_releaselock(&parent_task->child_lock);
  for (list<TaskState*>::iterator it = cur_task->child.begin(); it != cur_task->child.end(); it++) {
    TaskState* child_task = *it;
    child_task->parent = parent_task;
  }
  cur_task->child.clear();
  //  my_releaselock(&cur_task->child_lock);
  my_releaselock(&clear_child_lock);
}

extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId) {
  size_t threadId = get_cur_tid();
  my_getlock(&temp_cur_map_lock);
  assert(temp_cur_map.count(taskId) != 0);
  TaskState* cur_task = temp_cur_map[taskId];
  temp_cur_map.erase(taskId);
  my_releaselock(&temp_cur_map_lock);

  assert(cur_task != NULL);
  cur[threadId].push(cur_task);
  cur_task->child.clear();
  set_incomplete();
  //  my_releaselock(&tid_map_lock);
}
__attribute__((noinline)) void __exec_end__(unsigned long taskid) {
  size_t threadId = get_cur_tid();

  assert(!cur[threadId].empty());
  TaskState* cur_task = cur[threadId].top();
  assert(cur_task != NULL);
  set_complete();
  cur[threadId].pop();
}

__attribute__((noinline)) size_t get_cur_tid() {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  size_t my_tid;
  my_getlock(&tid_map_lock);
  if (tid_map.count(pthd_id) == 0) {
    my_tid = tid_ctr++;
    tid_map.insert(std::pair<TBB_TID, size_t>(pthd_id, my_tid));
    my_releaselock(&tid_map_lock);
  } else {
    my_releaselock(&tid_map_lock);
    my_tid = tid_map.at(pthd_id);
  }

  return my_tid;
}

void Capturewait_for_all(TaskState* cur_task_state) {
  // std::cout << "ENTER CAPTURE" << "\n";
  for (std::list<TaskState*>::iterator it = cur_task_state->child.begin();
       it != cur_task_state->child.end(); ++it) {
    //  std::cout << "CHILD CAPTURE" << "\n";
    TaskState* tmp_child_state = *it;
    assert(tmp_child_state != NULL);
    if (!tmp_child_state->complete) {
      assert(tmp_child_state->execute);
      assert(cur_task_state->execute);
    }

    assert(tmp_child_state->complete);
    if (tmp_child_state->child.size() > 0)
      Capturewait_for_all(tmp_child_state);

    delete (tmp_child_state);
  }
  if (cur_task_state->child.size() > 0)
    cur_task_state->child.clear();

  if (cur_task_state->sync_english) { // spawned
    assert(cur_task_state->sync_hebrew);

    cur_task_state->current_english = cur_task_state->sync_english;
    cur_task_state->current_hebrew = cur_task_state->sync_hebrew;

    cur_task_state->sync_english = NULL;
    cur_task_state->sync_hebrew = NULL;
  } else { // function didn't spawn, or this is a function-ending sync
    assert(!cur_task_state->sync_english);
    assert(!cur_task_state->sync_hebrew);
  }
  // std::cout << "EXIT CAPTURE" << "\n";
}
}

#pragma GCC pop_options
