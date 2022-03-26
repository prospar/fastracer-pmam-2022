#pragma GCC push_options
#pragma GCC optimize("O0")

#include "exec_calls.h"
#include "Common.H"
#include <iostream>
//#include "t_debug_task.h"

using namespace std;
using namespace tbb;

tbb::atomic<size_t> task_id_ctr(0);
tbb::atomic<size_t> tid_ctr(0);
tbb::atomic<size_t> test_count(0);
tbb::atomic<size_t> lock_ticker(0);

my_lock tid_map_lock(0);
my_lock parent_map_lock(0);
my_lock lock_map_lock(0);
my_lock temp_cur_map_lock(0);
std::map<TBB_TID, size_t> tid_map;
std::stack<uint32_t> cur[NUM_THREADS];
std::map<size_t, TaskState*> temp_cur_map;
std::map<size_t, size_t> lock_map;
size_t num_dead_tasks = 0;
size_t dead_clock_value = 0;
my_lock clear_child_lock(0);
extern size_t* joinset;

#if DEBUG_TIME
extern Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
#endif

//void set_incomplete(){
//  size_t threadId = get_cur_tid();
//  assert(!cur[threadId].empty());
//  TaskState* cur_task = cur[threadId].top();
//  assert(cur_task != NULL);
//  cur_task->complete = false;
//}

//void set_complete(){
//  size_t threadId = get_cur_tid();
//  assert(!cur[threadId].empty());
//  TaskState* cur_task = cur[threadId].top();
//  assert(cur_task != NULL);
//  cur_task->complete = true;
//}

//void set_execute_true(){
//  size_t threadId = get_cur_tid();
//  assert(!cur[threadId].empty());
//  TaskState* cur_task = cur[threadId].top();
//  assert(cur_task != NULL);
//  cur_task->execute = true;
//}

//void set_execute_false(){
//
//  size_t threadId = get_cur_tid();
//  assert(!cur[threadId].empty());
//  TaskState* cur_task = cur[threadId].top();
//  assert(cur_task != NULL);
//  cur_task->execute = false;
//}

//void clear_child_list(){
//  my_getlock(&clear_child_lock);
//  size_t threadId = get_cur_tid();
//  assert(!cur[threadId].empty());
//  TaskState* cur_task = cur[threadId].top();
//  assert(cur_task != NULL);
////  cur_task->execute = true;
//  TaskState* parent_task = cur_task->parent;
//  assert(parent_task != NULL);
////  my_getlock(&cur_task->child_lock);
////  my_getlock(&parent_task->child_lock);
//  parent_task->child.insert(parent_task->child.end(),cur_task->child.begin(),cur_task->child.end());
////  my_releaselock(&parent_task->child_lock);
//
//  for(list<TaskState*>::iterator it = cur_task->child.begin(); it != cur_task->child.end(); it++){
//    TaskState* child_task = *it;
//  child_task->parent = parent_task;
//  }
//  cur_task->child.clear();
////  my_releaselock(&cur_task->child_lock);
//
//  my_releaselock(&clear_child_lock);
//
//}
//

void CaptureSpawn(task& t, uint32_t cur_taskId, uint32_t child_taskId) {
  TaskState* cur_task = &tstate_nodes[cur_taskId];
  TaskState* child_task = &tstate_nodes[child_taskId];
#if EPOCH_POINTER
  child_task->epoch_ptr = createEpoch(0, child_taskId);
#else
  child_task->taskId = child_taskId;
#endif
  uint32_t cur_depth = cur_task->depth;
  uint32_t* cur_m_vc = cur_task->m_vc;
  child_task->depth = cur_depth + 1;

  for (int i = 0; i < cur_depth; i++)
    child_task->m_vc[i] = cur_m_vc[i];

  unordered_map<size_t, size_t>*child_root_vc = NULL, *child_my_vc = NULL;
  for (int i = 0; i < std::min(cur_depth, (uint32_t)NUM_FIXED_TASK_ENTRIES); i++) {
    child_task->cached_tid[i] = cur_task->cached_tid[i];
    child_task->cached_clock[i] = cur_task->cached_clock[i];
  }
  if (cur_depth >= NUM_FIXED_TASK_ENTRIES)
    child_task->my_vc = new unordered_map<size_t, size_t>();

  if (cur_task->my_vc != NULL and cur_task->my_vc->size() > MAX_MVC_SIZE) {
    child_root_vc = new unordered_map<size_t, size_t>();
    child_root_vc->insert(cur_task->my_vc->begin(), cur_task->my_vc->end());
    if (cur_task->root_vc)
      child_root_vc->insert(cur_task->root_vc->begin(), cur_task->root_vc->end());
    child_task->root_vc = child_root_vc;
    // cur_task->root_vc = child_root_vc;
    // free(cur_task->my_vc);
    // cur_task->my_vc = NULL;
  } else {
    child_task->root_vc = cur_task->root_vc;
    if (cur_task->my_vc != NULL)
      *(child_task->my_vc) = *(cur_task->my_vc);
  }

  my_getlock(&cur_task->tlock);
  if (cur_depth < NUM_FIXED_TASK_ENTRIES) {
    child_task->cached_tid[cur_depth] = cur_taskId;
    child_task->cached_clock[cur_depth] = cur_task->getCurClock();
  } else {
    (*(child_task->my_vc))[cur_taskId] = cur_task->getCurClock();
  }
// FIXME: *******************
// Do we need to increase clock value of current task here?
// Answer:(SK) Why not? We increment clock value at each spawn, right?
//child_task->m_vc[cur_depth] = cur_task->clock++;
#if EPOCH_POINTER
  cur_task->epoch_ptr = createEpoch(cur_task->getCurClock() + 1, cur_taskId);
#else
  cur_task->clock++;
#endif

  child_task->m_vc[cur_depth] = cur_task->getCurClock();

  if (cur_task->child_list == NULL) {
    cur_task->child_list = new std::list<TaskState*>();
  }
  (cur_task->child_list)->push_back(child_task);
  my_releaselock(&cur_task->tlock);

#if TASK_GRAPH
  my_getlock(&graph_lock);
  taskgraph << "Tid" + std::to_string(cur_taskId) + " -> " + "Tid" + std::to_string(child_taskId) +
                   "\n";
  my_releaselock(&graph_lock);
#endif

  child_task->parent_taskId = cur_taskId;
  child_task->mytask = &t;
  child_task->lockset = cur_task->lockset;
#if EPOCH_POINTER
  child_task->epoch_ptr = createEpoch(child_task->getCurClock() + 1, child_taskId);
#else
  child_task->clock++;
#endif
}

extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId) {
  size_t threadId = get_cur_tid();
  //  std::cout << "exec begin threadId " << threadId << " taskid " << taskId << "\n";
  cur[threadId].push(taskId);
  TaskState* cur_task = &tstate_nodes[taskId];
  if (cur_task->child_list != NULL)
    cur_task->child_list->clear();
}
__attribute__((noinline)) void __exec_end__(unsigned long taskid) {
  size_t threadId = get_cur_tid();
  //  std::cout << "exec end threadId " << threadId << " taskid " << taskid << "\n";
  TaskState* cur_task = &tstate_nodes[cur[threadId].top()];
  cur[threadId].pop();
#if STATS
  my_getlock(&(globalStats.gs_lock));
  uint32_t cursize = cur_task->depth;
  globalStats.max_vc_size = std::max(globalStats.max_vc_size, cursize);
  my_releaselock(&(globalStats.gs_lock));
#endif
  //#if STATS
  //       stats.nactive_tasks--;
  //#endif
  //
  //#if STATS
  //  concurrent_hash_map<size_t, TaskState*>::accessor ac;
  //  if (taskid_map.find(ac, taskid))
  //  {
  //        TaskState* cur_task = ac->second;
  //        my_getlock(&(stats.gs_lock));
  //        stats.rd_sameepoch += (cur_task->task_stats).num_rd_sameepoch;
  //        stats.rd_exclusive += (cur_task->task_stats).num_rd_exclusive;
  //        stats.rd_shared += (cur_task->task_stats).num_rd_shared;
  //        stats.rd_share += (cur_task->task_stats).num_rd_share;
  //
  //        stats.wr_shared += (cur_task->task_stats).num_wr_shared;
  //        stats.wr_exclusive += (cur_task->task_stats).num_wr_exclusive;
  //        stats.wr_sameepoch += (cur_task->task_stats).num_wr_sameepoch;
  //
  //
  //        if (stats.max_rd_tasks < cur_task->task_stats.num_rds)
  //               stats.max_rd_tasks = cur_task->task_stats.num_rds;
  //        if (stats.max_wr_tasks < cur_task->task_stats.num_wrs)
  //               stats.max_wr_tasks = cur_task->task_stats.num_wrs;
  //        my_releaselock(&(stats.gs_lock));
  //        ac.release();
  //
  //  }
  //#endif
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
  //#if TASK_GRAPH
  //  my_getlock(&graph_lock);
  //  taskgraph << "Tid" + std::to_string(cur_taskid) + " -> " + "Wait_for_all" +
  //                   std::to_string(cur_taskid) + "\n";
  //  my_releaselock(&graph_lock);
  //#endif
}

void Capturewait_for_all() {
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = &tstate_nodes[cur[threadId].top()];
#if TASK_GRAPH
  my_getlock(&graph_lock);
  taskgraph << "Tid" + std::to_string(cur_task->taskId) + " -> " + "Wait_for_all" +
                   std::to_string(cur_task->taskId) + "\n";
  my_releaselock(&graph_lock);
#endif
  Capturewait_for_all_task(cur_task);
}

void Capturewait_for_all_task(TaskState* cur_task_state) {
  if (cur_task_state->child_list == NULL || cur_task_state->child_list->size() == 0)
    return;

  if (cur_task_state->getTaskId() == 0) {
    cur_task_state->child_list->clear();
    num_dead_tasks = task_id_ctr;
    dead_clock_value = cur_task_state->getCurClock();
#if EPOCH_POINTER
    cur_task_state->epoch_ptr =
        createEpoch(cur_task_state->getCurClock() + 1, cur_task_state->getTaskId());
#else
    cur_task_state->clock++;
#endif
    //    std::cout << "resetting in Capturewait_for_all_task num_dead_tasks " << num_dead_tasks << " dead_clock_value " << dead_clock_value << "\n";
    return;
  }

  for (std::list<TaskState*>::iterator it = cur_task_state->child_list->begin();
       it != cur_task_state->child_list->end(); ++it) {
    TaskState* tmp_child_state = *it;
    assert(tmp_child_state != NULL);

    if (tmp_child_state->child_list != NULL && tmp_child_state->child_list->size() > 0)
      Capturewait_for_all_task(tmp_child_state);

#if JOINSET
    joinset[tmp_child_state->getTaskId() + 1] = cur_task_state->getTaskId() + 1;
#else
    cur_task_state->my_vc->insert(tmp_child_state->my_vc->begin(), tmp_child_state->my_vc->end());
    (*(cur_task_state->my_vc))[tmp_child_state->getTaskId()] = tmp_child_state->getCurClock();
#endif
  }

  cur_task_state->child_list->clear();
}
}

#pragma GCC pop_options
