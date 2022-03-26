#pragma GCC push_options
#pragma GCC optimize("O0")

#include "exec_calls.h"
#include "stats.h"
#include "tbb/concurrent_hash_map.h"
#include <iostream>

using namespace std;
using namespace tbb;

extern my_lock printLock; // Serialize print statements

#if STATS
extern GlobalStats globalStats;
#endif

// Next available task id counter
tbb::atomic<size_t> task_id_ctr(0);
tbb::atomic<size_t> tid_ctr(0);
extern size_t* joinset;

// my_lock parent_map_lock(0);

// Map from pthread id to task id
std::map<TBB_TID, size_t> tid_map;
my_lock tid_map_lock(0);
std::stack<size_t> cur[NUM_THREADS];

concurrent_hash_map<size_t, TaskState*> taskid_map;
my_lock taskid_map_lock(0);

tbb::concurrent_hash_map<uint64_t, std::unordered_map<size_t, size_t>> root_vc_map;
my_lock root_vc_map_lock(0);

void CaptureSpawn(task& t,uint32_t cur_taskId,uint32_t child_taskId){
  TaskState* cur_task = &tstate_nodes[cur_taskId];
  TaskState* child_task = &tstate_nodes[child_taskId];
#if EPOCH_POINTER
  child_task->epoch_ptr = createEpoch(0, child_taskId);
#else
  child_task->taskId = child_taskId;
#endif
  uint32_t cur_depth = cur_task->depth;
  child_task->depth = cur_depth + 1;

  unordered_map<size_t,size_t> *child_root_vc = NULL,*child_my_vc = NULL;
  for(int i=0;i<std::min(cur_depth,(uint32_t)NUM_FIXED_TASK_ENTRIES);i++){
    child_task->cached_tid[i] = cur_task->cached_tid[i];
    child_task->cached_clock[i] = cur_task->cached_clock[i];
  }
  if(cur_depth >= NUM_FIXED_TASK_ENTRIES) 
    child_task->my_vc = new unordered_map<size_t,size_t>();
  
  if(cur_task->my_vc != NULL and cur_task->my_vc->size() > MAX_MVC_SIZE){
    child_root_vc = new unordered_map<size_t,size_t>(); 
    child_root_vc->insert(cur_task->my_vc->begin(), cur_task->my_vc->end());
    if(cur_task->root_vc)
      child_root_vc->insert(cur_task->root_vc->begin(), cur_task->root_vc->end());
    child_task->root_vc = child_root_vc;
    // cur_task->root_vc = child_root_vc;
    // free(cur_task->my_vc);
    // cur_task->my_vc = NULL;
  }
  else{
    child_task->root_vc = cur_task->root_vc;
    if(cur_task->my_vc != NULL)
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
  cur_task->epoch_ptr = createEpoch(cur_task->getCurClock()+1, cur_taskId);
#else
  cur_task->clock++;
#endif

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
#if EPOCH_POINTER
  child_task->epoch_ptr = createEpoch(child_task->getCurClock() + 1, child_taskId);
#else
  child_task->clock++;
#endif
}
extern "C" {

// Beginning of task taskId
__attribute__((noinline)) void __exec_begin__(unsigned long taskId) {
  size_t threadId = get_cur_tid();
  cur[threadId].push(taskId);
  TaskState* cur_task = &tstate_nodes[taskId];
    if(cur_task->child_list != NULL)
  cur_task->child_list->clear();

#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.num_active_tasks++;
  globalStats.max_num_active_tasks =
      std::max(globalStats.max_num_active_tasks, globalStats.num_active_tasks);
  my_releaselock(&globalStats.gs_lock);
#endif

#if (0)
  my_getlock(&printLock);
  std::cout << "__exec_begin__: Thread id: " << threadId << " Task id : " << taskId << "\n";
  my_releaselock(&printLock);
#endif
}

// Task end
__attribute__((noinline)) void __exec_end__(unsigned long taskid) {
  size_t threadId = get_cur_tid();
  cur[threadId].pop();
//  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
//  assert(get_cur_tid() == taskid);
//
//  my_getlock(&tid_map_lock);
//  assert(!tid_map[pthd_id].empty());
//  (tid_map[pthd_id]).pop();
//  my_releaselock(&tid_map_lock);
//
#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.num_active_tasks--;
  my_releaselock(&globalStats.gs_lock);
#endif

#if (0)
  my_getlock(&printLock);
  std::cout << "__exec_end__: Thread id: " << threadId << " Task id: " << taskid << "\n";
  my_releaselock(&printLock);
#endif
}

void Capturewait_for_all(){
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  TaskState* cur_task = &tstate_nodes[cur[threadId].top()];
#if TASK_GRAPH
  my_getlock(&graph_lock);
  taskgraph << "Tid" + std::to_string(cur_task->taskId) + " -> " + "Wait_for_all" + std::to_string(cur_task->taskId) + "\n";
  my_releaselock(&graph_lock);
#endif
  Capturewait_for_all_task(cur_task);
}

void Capturewait_for_all_task(TaskState* cur_task_state) {

  if(cur_task_state->child_list == NULL || cur_task_state->child_list->size() == 0)
      return;

//  if(cur_task_state->taskId == 0){
//    cur_task_state->child_list->clear();
//    num_dead_tasks = task_id_ctr;
//    dead_clock_value = cur_task_state->clock;
//    cur_task_state->clock++;
////    std::cout << "resetting in Capturewait_for_all_task num_dead_tasks " << num_dead_tasks << " dead_clock_value " << dead_clock_value << "\n";
//    return;
//  }

  for (std::list<TaskState*>::iterator it = cur_task_state->child_list->begin();
       it != cur_task_state->child_list->end(); ++it) {
    TaskState* tmp_child_state = *it;
    assert(tmp_child_state != NULL);

    if(tmp_child_state->child_list != NULL && tmp_child_state->child_list->size() > 0)
      Capturewait_for_all_task(tmp_child_state);

#if JOINSET
    joinset[tmp_child_state->getTaskId() + 1] = cur_task_state->getTaskId() + 1;
#else
  if(cur_task_state->my_vc == NULL)
    cur_task_state->my_vc = new unordered_map<size_t,size_t>();
  if(tmp_child_state->my_vc != NULL){
    cur_task_state->my_vc->insert(tmp_child_state->my_vc->begin(),tmp_child_state->my_vc->end());
    cur_task_state->depth = cur_task_state->depth + tmp_child_state->my_vc->size();
  }
  (*(cur_task_state->my_vc))[tmp_child_state->getTaskId()] = tmp_child_state->getCurClock();
  cur_task_state->depth = cur_task_state->depth + 1;
#endif
  }

  cur_task_state->child_list->clear();
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
  //  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  //  stack<size_t> s;
  //  size_t my_tid;
  //
  //  my_getlock(&tid_map_lock);
  //  if (tid_map.count(pthd_id) != 0) {
  //    if ((tid_map[pthd_id]).empty()) {
  //      my_tid = 0;
  //      (tid_map[pthd_id]).push(0);
  //    }
  //    my_tid = (tid_map[pthd_id]).top();
  //  } else {
  //    s.push(0);
  //    my_tid = 0;
  //    tid_map.insert(pair<TBB_TID, stack<size_t>>(pthd_id, s));
  //  }
  //  my_releaselock(&tid_map_lock);
  //  return my_tid;
}

//void Capturewait_for_all(TaskState* cur_task_state) {
//  concurrent_hash_map<size_t, TaskState*>::accessor ac;
//
//  for (std::list<size_t>::iterator it = cur_task_state->child_list.begin();
//       it != cur_task_state->child_list.end(); ++it) {
//    my_getlock(&taskid_map_lock);
//#if DEBUG_TIME
//    HRTimer find_start = HR::now();
//#endif    
//    bool fnd = taskid_map.find(ac, *it);
//#if DEBUG_TIME
//  HRTimer find_end = HR::now();
//  my_getlock(&time_dr_detector.time_DR_detector_lock);
//  time_dr_detector.taskid_map_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
//  time_dr_detector.num_tid_find += 1;
//  // time_dr_detector.tid_find_map[taskid_map.size()] += 1;
//  my_releaselock(&time_dr_detector.time_DR_detector_lock);
//#endif    
//    assert(fnd);
//    TaskState* tmp_child_state = ac->second;
//    ac.release();
//    my_releaselock(&taskid_map_lock);
//    if(tmp_child_state->child_list.size() > 0)
//      Capturewait_for_all(tmp_child_state);
//#if ENABLE_TASK_MAP
//  for (auto it = tmp_child_state->m_vc.begin(); it != tmp_child_state->m_vc.end(); ++it) {
//#if DEBUG_TIME
//    HRTimer find_start = HR::now(); 
//#endif    
//    auto it2 = cur_task_state->m_vc.find(it->first);
//#if DEBUG_TIME
//          HRTimer find_end = HR::now();
//          my_getlock(&time_dr_detector.time_DR_detector_lock);
//          time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
//          time_dr_detector.num_vc_find += 1;
//          // time_dr_detector.vc_find_map[(elem_task->m_vc).size()] += 1;
//          my_releaselock(&time_dr_detector.time_DR_detector_lock);
//#endif   
//    if (it2 != cur_task_state->m_vc.end() ){
//      if(it->second > it2->second){
//      cur_task_state->m_vc[it->first] = it->second;
//      if(cur_task_state->child_root_vc != NULL)
//      cur_task_state->child_m_vc[it->first] = it->second;
//    }
//    }
//    else if(cur_task_state->root_vc != NULL){
//#if DEBUG_TIME
//            find_start = HR::now();
//#endif      
//    auto it3 = cur_task_state->root_vc->m_vc.find(it->first);
//#if DEBUG_TIME
//            find_end = HR::now();
//            my_getlock(&time_dr_detector.time_DR_detector_lock);
//            time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
//            time_dr_detector.num_root_vc_find += 1;
//            // time_dr_detector.root_vc_find_map[(elem_task->root_vc->m_vc).size()] += 1;
//            my_releaselock(&time_dr_detector.time_DR_detector_lock);
//#endif    
//    if (it3 != cur_task_state->root_vc->m_vc.end()){
//      if(it->second > it3->second){
//      cur_task_state->m_vc[it->first] = it->second;
//      if(cur_task_state->child_root_vc != NULL)
//      cur_task_state->child_m_vc[it->first] = it->second;
//     
//    }
//    }
//    else if (it->first != (cur_task_state->m_taskid)){
//      cur_task_state->m_vc[it->first] = it->second;
//      if(cur_task_state->child_root_vc != NULL)
//      cur_task_state->child_m_vc[it->first] = it->second;
//    }
//  }
//    else if (it->first != (cur_task_state->m_taskid)){
//      cur_task_state->m_vc[it->first] = it->second;
//      if(cur_task_state->child_root_vc != NULL)
//      cur_task_state->child_m_vc[it->first] = it->second;
//    }
//  }
//  cur_task_state->m_vc[tmp_child_state->m_taskid] = tmp_child_state->m_cur_clock;
//  if(cur_task_state->child_root_vc != NULL)
//  cur_task_state->child_m_vc[tmp_child_state->m_taskid] = tmp_child_state->m_cur_clock;
//#else
//      for (int i = 0; i < NUM_TASKS; i++)
//        store(cur_task, i,
//              ((std::max(element(cur_task, i) << NUM_TASK_BITS, element(child_task, i)
//                                                                    << NUM_TASK_BITS)) >>
//               NUM_TASK_BITS) +
//                  cur_epoch);
//#endif
//      delete (tmp_child_state);
//      my_getlock(&taskid_map_lock);
//      taskid_map.erase(*it);
//      my_releaselock(&taskid_map_lock);
//  }
//
//
//
//
//
//  cur_task_state->child_list.clear();
//}
}

#pragma GCC pop_options
