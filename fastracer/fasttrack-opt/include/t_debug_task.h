#ifndef __TBB_t_debug_task_H
#define __TBB_t_debug_task_H

// FIXME: Is it necessary to turn off optimizations?
#pragma GCC push_options
#pragma GCC optimize("O0")

#include "exec_calls.h"
#include "stats.h"
#include <iostream>
#include <map>
#include <string>

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
inline void t_debug_task::spawn(task& t) {
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
  uint32_t child_taskId = (static_cast<t_debug_task&>(t)).getTaskId();

#if DEBUG_TIME
  HRTimer spawn_start = HR::now();
#endif
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  uint32_t cur_taskId = cur[threadId].top();
  if(t.parent() !=  tstate_nodes[cur_taskId].mytask){
  cur_taskId = tstate_nodes[cur_taskId].parent_taskId;
  }
  CaptureSpawn(t,cur_taskId,child_taskId);
//
//  if(cur_task_state->child_m_vc.size() > MAX_MVC_SIZE){
//  RootVc* root_vc = new RootVc(); 
//  root_vc->m_vc = cur_task_state->child_m_vc;
//  if(cur_task_state->child_root_vc)
//  root_vc->m_vc.insert((cur_task_state->child_root_vc->m_vc).begin(), (cur_task_state->child_root_vc->m_vc).end());
//  cur_task_state->child_root_vc = root_vc;
//#if STATS
//  my_getlock(&globalStats.gs_lock);
//  globalStats.gs_totTaskvcSize += root_vc->m_vc.size() - cur_task_state->child_m_vc.size();
//    my_releaselock(&globalStats.gs_lock);
//#endif
//  cur_task_state->child_m_vc.clear();
//  }
//  else if(cur_task_state->child_root_vc == NULL && cur_task_state->m_vc.size() > MAX_MVC_SIZE){
//   RootVc* root_vc = new RootVc(); 
//   root_vc->m_vc = cur_task_state->m_vc;
//   if(cur_task_state->root_vc)
//   root_vc->m_vc.insert((cur_task_state->root_vc->m_vc).begin(), (cur_task_state->root_vc->m_vc).end());
//   cur_task_state->child_root_vc = root_vc;
//#if STATS
//   my_getlock(&globalStats.gs_lock);
//   globalStats.gs_totTaskvcSize += root_vc->m_vc.size() - cur_task_state->child_m_vc.size();
//     my_releaselock(&globalStats.gs_lock);
//#endif
//   cur_task_state->child_m_vc.clear();
//  }

#if (DEBUG & 0)
  dumpTaskIdToTaskStateMap();
#endif

//#if ENABLE_TASK_MAP
//  if(cur_task_state->child_root_vc == NULL){
//  child_task_state->root_vc = cur_task_state->root_vc; 
//  child_task_state->m_vc = cur_task_state->m_vc;
//  }
//  else{
//  child_task_state->root_vc = cur_task_state->child_root_vc;
//  child_task_state->m_vc = cur_task_state->child_m_vc;
//  }
//  child_task_state->m_vc[cur_taskid] = cur_task_state->m_cur_clock;
//  cur_task_state->m_cur_clock++;
//  child_task_state->m_cur_clock++;
//#else
//  size_t cur_epoch = (tid << (64 - NUM_TASK_BITS)) + cur_task->m_cur_clock;
//  size_t child_epoch = (taskid << (64 - NUM_TASK_BITS)) + child_task->m_cur_clock;
//  for (int i = 0; i < NUM_TASKS; i++)
//    store(child_task, i, element(cur_task, i) - cur_epoch + child_epoch);
//  store(cur_task, tid, element(cur_task, tid) + 1);
//  store(child_task, taskid, element(child_task, taskid) + 1);
//#endif

#if DEBUG_TIME
  HRTimer tbb_spawn_start = HR::now();
#endif
  task::spawn(t);
#if DEBUG_TIME
  HRTimer tbb_spawn_end = HR::now();
  HRTimer spawn_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.spawn_time += duration_cast<nanoseconds>(spawn_end - spawn_start).count();
  time_task_management.tbb_spawn_time += duration_cast<nanoseconds>(tbb_spawn_end - tbb_spawn_start).count();
  time_task_management.num_spawn += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
#endif
}

// Example parent task id is 0, root task id is 1 at the beginning.
inline void t_debug_task::spawn_root_and_wait(task& t) {
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
  uint32_t child_taskId = (static_cast<t_debug_task&>(t)).getTaskId();
//  std::cout << "spanw_root_and_wait starting\n";
#if DEBUG_TIME
  HRTimer spawn_root_start = HR::now();
#endif
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  uint32_t cur_taskId = cur[threadId].top();
  CaptureSpawn(t,cur_taskId,child_taskId);

#if (DEBUG & 0)
  dumpTaskIdToTaskStateMap();
#endif

#if DEBUG_TIME
  HRTimer tbb_spawn_root_start = HR::now();
#endif
  tbb::task::spawn_root_and_wait(t);
#if DEBUG_TIME
  HRTimer tbb_spawn_root_end = HR::now();
#endif
  Capturewait_for_all();

#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_tot_wait_calls += 1;
  my_releaselock(&globalStats.gs_lock);
#endif
// We merge the two vector clocks
// FIXME: This block seems expensive.
//#if ENABLE_TASK_MAP
//  for (auto it = root_task_state->m_vc.begin(); it != root_task_state->m_vc.end(); ++it) {
////    if(it->first == 3978)
////      std::cout << 3978 << "\n";
//#if DEBUG_TIME
//    find_start = HR::now();
//#endif
//    auto it2 = cur_task_state->m_vc.find(it->first);
//#if DEBUG_TIME
//    HRTimer find_end = HR::now();
//    my_getlock(&time_dr_detector.time_DR_detector_lock);
//    time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
//    time_dr_detector.num_vc_find += 1;
//    my_releaselock(&time_dr_detector.time_DR_detector_lock);
//#endif
//    if (it2 != cur_task_state->m_vc.end() ){
//      if(it->second > it2->second){
//      cur_task_state->m_vc[it->first] = it->second;
//      if(cur_task_state->child_root_vc != NULL)
//        cur_task_state->child_m_vc[it->first] = it->second;
//    }
//    }
//    else if(cur_task_state->root_vc != NULL){
//#if DEBUG_TIME
//    find_start = HR::now();
//#endif      
//    auto it3 = cur_task_state->root_vc->m_vc.find(it->first);
//#if DEBUG_TIME
//    find_end = HR::now();
//    my_getlock(&time_dr_detector.time_DR_detector_lock);
//    time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
//    time_dr_detector.num_root_vc_find += 1;
//    my_releaselock(&time_dr_detector.time_DR_detector_lock);
//#endif    
//    if (it3 != cur_task_state->root_vc->m_vc.end()){
//      if(it->second > it3->second){
//      cur_task_state->m_vc[it->first] = it->second;
//      if(cur_task_state->child_root_vc != NULL)
//        cur_task_state->child_m_vc[it->first] = it->second;
//     
//    }
//    }
//    else if (it->first != (cur_task_state->m_taskid)){
//      cur_task_state->m_vc[it->first] = it->second;
//      if(cur_task_state->child_root_vc != NULL)
//        cur_task_state->child_m_vc[it->first] = it->second;
//    }
//  }
//    else if (it->first != (cur_task_state->m_taskid)){
//      cur_task_state->m_vc[it->first] = it->second;
//      if(cur_task_state->child_root_vc != NULL)
//        cur_task_state->child_m_vc[it->first] = it->second;
//    }
//  }
//  cur_task_state->m_vc[root_task_state->m_taskid] = root_task_state->m_cur_clock;
//  if(cur_task_state->child_root_vc != NULL)
//    cur_task_state->child_m_vc[root_task_state->m_taskid] = root_task_state->m_cur_clock;
//
//
//  
//
//
//#else
//  for (int i = 0; i < NUM_TASKS; i++)
//    store(cur_task, i,
//          ((std::max(element(cur_task, i) << NUM_TASK_BITS, element(child_task, i)
//                                                                << NUM_TASK_BITS)) >>
//           NUM_TASK_BITS) +
//              cur_epoch);
//#endif
//  if(root_task_state->m_taskid == 3978){
//    std::cout << "*** Debugging for root(in spawn_root_and_wait) 3978\n";
//    std::cout << "Parent taskid " << cur_task_state->m_taskid << "\n";
//    std::cout <<  " Value of task 3979 in parent " <<  element(cur_task_state, 3979) << "\n";
//    std::cout <<  " Value of task 3979 in root child " <<  element(root_task_state, 3979) << "\n";
//  }
#if DEBUG_TIME
  HRTimer spawn_root_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.spawn_root_and_wait_time += duration_cast<nanoseconds>(spawn_root_end - spawn_root_start).count();
  time_task_management.tbb_spawn_root_and_wait_time += duration_cast<nanoseconds>(tbb_spawn_root_end - tbb_spawn_root_start).count();
  time_task_management.num_spawn_root_and_wait += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
#endif
}

inline void t_debug_task::spawn_and_wait_for_all(task& t) {
  (static_cast<t_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
  uint32_t child_taskId = (static_cast<t_debug_task&>(t)).getTaskId();
#if DEBUG_TIME
  HRTimer spawn_wait_all_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time1 = HR::now();
  // my_releaselock(&debug_lock);
#endif
  size_t threadId = get_cur_tid();
  assert(!cur[threadId].empty());
  uint32_t cur_taskId = cur[threadId].top();
  TaskState* cur_task = &tstate_nodes[cur_taskId];
  CaptureSpawn(t,cur_taskId,child_taskId);

#if (DEBUG & 0)
  dumpTaskIdToTaskStateMap();
#endif
#if DEBUG_TIME
  HRTimer tbb_spawn_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time2 = HR::now();
  // my_releaselock(&debug_lock);
#endif

  task::spawn_and_wait_for_all(t);
#if DEBUG_TIME
  HRTimer tbb_spawn_wait_all_end = HR::now();
#endif

  if(cur_task->child_list->size() > 0)
  Capturewait_for_all();
#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_tot_wait_calls += 1;
  my_releaselock(&globalStats.gs_lock);
#endif
  //    assert(cur_task_state != child_ts);
  //#if ENABLE_TASK_MAP
  //    for (auto it = child_ts->m_vc.begin(); it != child_ts->m_vc.end(); ++it) {
  //      auto it2 = cur_task_state->m_vc.find(it->first);
  //      if (it2 != cur_task_state->m_vc.end()) {
  //        it2->second = std::max(it2->second, it->second);
  //      } else if (it->first != (cur_task_state->m_taskid)) {
  //        cur_task_state->m_vc[it->first] = it->second;
  //      }
  //    }
  //    cur_task_state->m_vc[child_ts->m_taskid] = child_ts->m_cur_clock;
  //#else
  //    for (int i = 0; i < NUM_TASKS; i++)
  //      store(cur_task, i,
  //            ((std::max(element(cur_task, i) << NUM_TASK_BITS, element(child_task, i)
  //                                                                  << NUM_TASK_BITS)) >>
  //             NUM_TASK_BITS) +
  //                cur_epoch);
  //#endif
  //    // This is wait for all, so we do not need the task states after this.
  //    delete (child_ts);
  //    taskid_map.erase(*it);
  //  }
  //  my_releaselock(&taskid_map_lock);
  //  cur_task_state->child_list.clear();
#if DEBUG_TIME
  HRTimer spawn_wait_all_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.spawn_and_wait_for_all_time += duration_cast<nanoseconds>(spawn_wait_all_end - spawn_wait_all_start).count();
  time_task_management.tbb_spawn_and_wait_for_all_time += duration_cast<nanoseconds>(tbb_spawn_wait_all_end - tbb_spawn_wait_all_start).count();
  time_task_management.num_spawn_and_wait_for_all += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
#endif
}

inline void t_debug_task::wait_for_all() {
#if DEBUG_TIME
  HRTimer wait_all_start = HR::now();
#endif


#if DEBUG_TIME
  HRTimer tbb_wait_all_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time2 = HR::now();
  // my_releaselock(&debug_lock);
#endif
  task::wait_for_all();
#if DEBUG_TIME
  HRTimer tbb_wait_all_end = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time3 = HR::now();
  // my_releaselock(&debug_lock);
#endif

  Capturewait_for_all();

#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_tot_wait_calls += 1;
  my_releaselock(&globalStats.gs_lock);
#endif
#if DEBUG_TIME
  HRTimer wait_all_end = HR::now();
  my_getlock(&time_task_management.time_task_management_lock);
  time_task_management.wait_for_all_time += duration_cast<nanoseconds>(wait_all_end - wait_all_start).count();
  time_task_management.tbb_wait_for_all_time += duration_cast<nanoseconds>(tbb_wait_all_end - tbb_wait_all_start).count();
  time_task_management.num_wait_for_all += 1;
  my_releaselock(&time_task_management.time_task_management_lock);
#endif
}

} // namespace tbb

#pragma GCC pop_options
#endif
