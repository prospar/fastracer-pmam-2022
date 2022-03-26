#ifndef __TBB_Nt_debug_task_H
#define __TBB_Nt_debug_task_H

// FIXME: SB: Is it necessary to turn off optimizations?
#pragma GCC push_options
#pragma GCC optimize("O0")

//#define NUM_TASKS 30

#include "Nexec_calls.h"
#include "Ft_debug_task.h"
#include <iostream>
#include <map>

#define CHECK_AV __attribute__((type_annotate("check_av")))

namespace tbb {

// Combined case
  
// PROSPAR: This wrapper class seems to be there to allow build the DPST.
class Nt_debug_task : public Ft_debug_task {
private:
  size_t taskId;
  void setTaskId(size_t taskId, int sp_only) { this->taskId = taskId; }

public:
  // static void __TBB_EXPORTED_METHOD spawn( task& t, size_t taskId );__attribute__((optimize(0)));
  static void __TBB_EXPORTED_METHOD spawn(task& t);                  //__attribute__((optimize(0)));
  static void __TBB_EXPORTED_METHOD spawn_root_and_wait(task& root); //__attribute__((optimize(0)));
  void spawn_and_wait_for_all(task& child);                          //__attribute__((optimize(0)));
  void wait_for_all();                                               //__attribute__((optimize(0)));
  static void wait_for_one(size_t taskId);
  size_t getTaskId() { return taskId; }
};


inline void Nt_debug_task::spawn(task& t) {
  //std::cout << "newalgo spawn starting\n";
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  (static_cast<Nt_debug_task&>(t)).setTaskId(++Ntask_id_ctr, 1);
  size_t taskid = (static_cast<Nt_debug_task&>(t)).getTaskId();

  size_t threadid = get_cur_tid();
  if(NtidToTaskIdMap[threadid].empty())
    NtidToTaskIdMap[threadid].push(0);
  size_t tid = NtidToTaskIdMap[threadid].top();
  Nthreadstate* cur_task;
  concurrent_hash_map<size_t, Nthreadstate*>::accessor ac;
  if (Ntaskid_map.find(ac, tid))
    cur_task = ac->second;
  else {
    cur_task = new Nthreadstate();
    cur_task->tid = tid;
    Ntaskid_map.insert(ac, tid);
    ac->second = cur_task;
  }

  ac.release();

  Nthreadstate* child_task = new Nthreadstate();
  child_task->clock = 1;
  child_task->tid = taskid;
  Ntaskid_map.insert(ac, taskid);
  ac->second = child_task;
  ac.release();

  child_task->C = cur_task->C;
  child_task->C[taskid] = 1;
  //child_task->order = cur_task->order;
//  child_task->order.push_back(((cur_task->cur_epoch) << NUM_TASK_BITS) >> NUM_TASK_BITS);
  for(int i=0;i < cur_task->order_cur;i++)
	  child_task->order[i] = cur_task->order[i];
  child_task->order[cur_task->order_cur] = cur_task->clock;
 child_task->order_cur = cur_task->order_cur+1;
  cur_task->clock++;
  cur_task->C[tid]++;
#ifdef DEBUG
  cur_task->task_stats.depth++;
  child_task->task_stats.depth = cur_task->task_stats.depth;
 Nmy_getlock(&(stats.gs_lock));
  if (stats.max_task_depth < cur_task->task_stats.depth)
    stats.max_task_depth = cur_task->task_stats.depth;
 Nmy_releaselock(&(stats.gs_lock));
#endif
  (cur_task->child).push_back(taskid);
    //std::cout << "newalgo spawn ending\n";

//  task::spawn(t);   //Combined case
}

#if 0
  inline voidNt_debug_task::spawn(task& t) {
    (static_castNt_debug_task&>(t)).setTaskId(++task_id_ctr, 1);
    task::spawn(t);
  }
#endif

inline void Nt_debug_task::spawn_root_and_wait(task& root) {
  //std::cout << "newalgo spawn root and wait starting\n";
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  (static_cast<Nt_debug_task&>(root)).setTaskId(++Ntask_id_ctr, 0);
  size_t taskid = (static_cast<Nt_debug_task&>(root)).getTaskId();

  size_t threadid = get_cur_tid();
  if(NtidToTaskIdMap[threadid].empty())
    NtidToTaskIdMap[threadid].push(0);
  size_t tid = NtidToTaskIdMap[threadid].top();

 Nthreadstate* cur_task;
  concurrent_hash_map<size_t,Nthreadstate*>::accessor ac;
  if (Ntaskid_map.find(ac, tid))
    cur_task = ac->second;
  else {
    cur_task = new Nthreadstate();
    cur_task->tid = tid;
   Ntaskid_map.insert(ac, tid);
    ac->second = cur_task;
  }

  ac.release();

 Nthreadstate* child_task = new Nthreadstate();
  child_task->tid = taskid;
  child_task->clock = 1;
 Ntaskid_map.insert(ac, taskid);
  ac->second = child_task;
  ac.release();
  child_task->C = cur_task->C;
  child_task->C[taskid] = 1;
 // child_task->order = cur_task->order;
 // child_task->order.push_back(((cur_task->cur_epoch) << NUM_TASK_BITS) >> NUM_TASK_BITS);
  for(int i=0;i < cur_task->order_cur;i++)
	  child_task->order[i] = cur_task->order[i];
  child_task->order[cur_task->order_cur] = cur_task->clock;
 child_task->order_cur = cur_task->order_cur+1;
  cur_task->clock++;
  cur_task->C[tid]++;
#ifdef DEBUG
  cur_task->task_stats.depth++;
  child_task->task_stats.depth = cur_task->task_stats.depth;
 Nmy_getlock(&(stats.gs_lock));
  if (stats.max_task_depth < cur_task->task_stats.depth)
    stats.max_task_depth = cur_task->task_stats.depth;
 Nmy_releaselock(&(stats.gs_lock));
#endif
//  (cur_task->child).push_back(taskid);
//  tbb::task::spawn_root_and_wait(root);   //Combined case



  // QQQQQ does this wait for all child tasks or only root......assumed only root
  // delete(child_task);
//              for(auto it = child_task->C.begin();it!=child_task->C.end();it++){
//		      cur_task->C[it->first] = std::max(cur_task->C[it->first],it->second);
//              }
  //std::cout << "newalgo spawn root and wait ending\n";

}

inline void Nt_debug_task::wait_for_one(size_t taskId){

  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  size_t threadid = get_cur_tid();
  if(NtidToTaskIdMap[threadid].empty())
    NtidToTaskIdMap[threadid].push(0);
  size_t tid = NtidToTaskIdMap[threadid].top();
  concurrent_hash_map<size_t,Nthreadstate*>::accessor ac;
 Ntaskid_map.find(ac, tid);
 Nthreadstate* cur_task = ac->second;
  ac.release();

    if (Ntaskid_map.find(ac, taskId)) {
     Nthreadstate* child_task = ac->second;
      ac.release();
              for(auto it = child_task->C.begin();it!=child_task->C.end();it++)
		      cur_task->C[it->first] = std::max(cur_task->C[it->first],it->second);
    }
}

inline void Nt_debug_task::spawn_and_wait_for_all(task& child) {
  (static_cast<Nt_debug_task&>(child)).setTaskId(++Ntask_id_ctr, 1);
  size_t taskid = (static_cast<Nt_debug_task&>(child)).getTaskId();

  size_t threadid = get_cur_tid();
  if(NtidToTaskIdMap[threadid].empty())
    NtidToTaskIdMap[threadid].push(0);
  size_t tid = NtidToTaskIdMap[threadid].top();
 Nthreadstate* cur_task;
  concurrent_hash_map<size_t,Nthreadstate*>::accessor ac;
  if (Ntaskid_map.find(ac, tid))
    cur_task = ac->second;
  else {
    cur_task = new Nthreadstate();
    cur_task->tid = tid;

   Ntaskid_map.insert(ac, tid);
    ac->second = cur_task;
    //Ntaskid_map.insert(std::pair<size_t, Nthreadstate*>(tid,cur_task));
  }

  ac.release();

 Nthreadstate* child_task = new Nthreadstate();
  child_task->clock = 1;
  child_task->tid = taskid;
 Ntaskid_map.insert(ac, taskid);
  ac->second = child_task;
  ac.release();
  child_task->C = cur_task->C;
  child_task->C[taskid] = 1;
  //child_task->order = cur_task->order;
  //child_task->order.push_back(((cur_task->cur_epoch) << NUM_TASK_BITS) >> NUM_TASK_BITS);
  for(int i=0;i < cur_task->order_cur;i++)
	  child_task->order[i] = cur_task->order[i];
  child_task->order[cur_task->order_cur] = cur_task->clock;
  child_task->order_cur = cur_task->order_cur+1;
  cur_task->clock++;
  cur_task->C[tid]++;
#ifdef DEBUG
  cur_task->task_stats.depth++;
  child_task->task_stats.depth = cur_task->task_stats.depth;
 Nmy_getlock(&(stats.gs_lock));
  if (stats.max_task_depth < cur_task->task_stats.depth)
    stats.max_task_depth = cur_task->task_stats.depth;
 Nmy_releaselock(&(stats.gs_lock));
#endif
  (cur_task->child).push_back(taskid);
  task::spawn_and_wait_for_all(child);
 Nmy_getlock(&Ntaskid_map_lock);

  for (std::list<size_t>::iterator it = cur_task->child.begin(); it != cur_task->child.end();
       it++) {
    if (Ntaskid_map.find(ac, *it)) {
     Nthreadstate* child_task = ac->second;
      ac.release();
      if (cur_task != child_task) {
              for(auto it = child_task->C.begin();it!=child_task->C.end();it++){
		      cur_task->C[it->first] = std::max(cur_task->C[it->first],it->second);
//                      auto it2 = cur_task->C.find(it->first);
//                        if( it2 != cur_task->C.end())
//                                it2->second = std::max(it2->second,it->second);
//                        else if( it->first != ((cur_task->cur_epoch)>>(64-NUM_TASK_BITS)))
//                                cur_task->C[it->first] = it->second;
              }
//              cur_task->C[(child_task->cur_epoch)>>(64-NUM_TASK_BITS)] = ((child_task->cur_epoch)<<NUM_TASK_BITS)>>NUM_TASK_BITS;
        // delete(child_task);
      }
     //Ntaskid_map.erase(*it);
    }
    // parent_map.erase(*it);
    // delete(child);
  }
  cur_task->child.clear();
}

inline void Nt_debug_task::wait_for_all() {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
//  task::wait_for_all();
  size_t threadid = get_cur_tid();
  if(NtidToTaskIdMap[threadid].empty())
    NtidToTaskIdMap[threadid].push(0);
  size_t tid = NtidToTaskIdMap[threadid].top();
  //Nmy_getlock(&Ntaskid_map_lock);
  concurrent_hash_map<size_t,Nthreadstate*>::accessor ac;
 Ntaskid_map.find(ac, tid);
 Nthreadstate* cur_task = ac->second;
  ac.release();
  for (std::list<size_t>::iterator it = cur_task->child.begin(); it != cur_task->child.end();
       it++) {
    if (Ntaskid_map.find(ac, *it)) {
     Nthreadstate* child_task = ac->second;
      ac.release();
      if (cur_task != child_task) {
              for(auto it = child_task->C.begin();it!=child_task->C.end();it++){
		      cur_task->C[it->first] = std::max(cur_task->C[it->first],it->second);
//                        auto it2 = cur_task->C.find(it->first);
//                        if( it2 != cur_task->C.end())
//                                it2->second = std::max(it2->second,it->second);
//                        else if( it->first != ((cur_task->cur_epoch)>>(64-NUM_TASK_BITS)))
//                                cur_task->C[it->first] = it->second;
              }
//              cur_task->C[(child_task->cur_epoch)>>(64-NUM_TASK_BITS)] = ((child_task->cur_epoch)<<NUM_TASK_BITS)>>NUM_TASK_BITS;
       //  delete(child_task);
      }
    }
  }
  cur_task->child.clear();
}

} // namespace tbb

#pragma GCC pop_options
#endif
