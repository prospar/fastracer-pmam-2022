#ifndef __TBB_Ft_debug_task_H
#define __TBB_Ft_debug_task_H

// FIXME: SB: Is it necessary to turn off optimizations?
#pragma GCC push_options
#pragma GCC optimize("O0")

//#define NUM_TASKS 30

#include "Fexec_calls.h"
#include <iostream>
#include <map>

#define CHECK_AV __attribute__((type_annotate("check_av")))

namespace tbb {

// PROSPAR: This wrapper class seems to be there to allow build the DPST.
class Ft_debug_task : public task {
private:
  size_t taskId;
  void setTaskId(size_t taskId, int sp_only) { this->taskId = taskId; }

public:
  // static void __TBB_EXPORTED_METHOD spawn( task& t, size_t taskId );__attribute__((optimize(0)));
  static void __TBB_EXPORTED_METHOD spawn(task& t);                  //__attribute__((optimize(0)));
  static void __TBB_EXPORTED_METHOD spawn_root_and_wait(task& root); //__attribute__((optimize(0)));
  void spawn_and_wait_for_all(task& child);                          //__attribute__((optimize(0)));
  static void wait_for_one(size_t taskId);
  void wait_for_all();                                               //__attribute__((optimize(0)));
  size_t getTaskId() { return taskId; }
};

inline void Ft_debug_task::spawn(task& t) {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  (static_cast<Ft_debug_task&>(t)).setTaskId(++Ftask_id_ctr, 1);
  size_t taskid = (static_cast<Ft_debug_task&>(t)).getTaskId();

  size_t threadid = get_cur_tid();
  if(FtidToTaskIdMap[threadid].empty())
  FtidToTaskIdMap[threadid].push(0);
  size_t tid = FtidToTaskIdMap[threadid].top();

  Fthreadstate* cur_task;
  concurrent_hash_map<size_t, Fthreadstate*>::accessor ac;
  if (Ftaskid_map.find(ac, tid))
    cur_task = ac->second;
  else {
    cur_task = new Fthreadstate();
    cur_task->cur_epoch = (tid << (64 - NUM_TASK_BITS));
    cur_task->tlock = 0;

    Ftaskid_map.insert(ac, tid);
    ac->second = cur_task;
  }

  ac.release();

  Fthreadstate* child_task = new Fthreadstate();
  child_task->cur_epoch = (taskid << (64 - NUM_TASK_BITS));
  child_task->tlock = 0;
  Ftaskid_map.insert(ac, taskid);
  ac->second = child_task;
  ac.release();

#ifdef ENABLE_TASK_MAP
  child_task->C = cur_task->C;
  child_task->C[tid] = ((cur_task->cur_epoch) << NUM_TASK_BITS) >> NUM_TASK_BITS;
  cur_task->cur_epoch++;
  child_task->cur_epoch++;
#else
  size_t cur_epoch = cur_task->cur_epoch;
  size_t child_epoch = child_task->cur_epoch;
  for (int i = 0; i < NUM_TASKS; i++)
    store(child_task, i, element(cur_task, i) - cur_epoch + child_epoch);
  store(cur_task, tid, element(cur_task, tid) + 1);
  store(child_task, taskid, element(child_task, taskid) + 1);
#endif
#ifdef DEBUG
  cur_task->task_stats.depth++;
  child_task->task_stats.depth = cur_task->task_stats.depth;
  Fmy_getlock(&(stats.gs_lock));
  if (stats.max_task_depth < cur_task->task_stats.depth)
    stats.max_task_depth = cur_task->task_stats.depth;
  Fmy_releaselock(&(stats.gs_lock));
#endif
  (cur_task->child).push_back(taskid);
//  task::spawn(t);
}

#if 0
  inline void Ft_debug_task::spawn(task& t) {
    (static_cast<Ft_debug_task&>(t)).setTaskId(++Ftask_id_ctr, 1);
    task::spawn(t);
  }
#endif

inline void Ft_debug_task::spawn_root_and_wait(task& root) {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  (static_cast<Ft_debug_task&>(root)).setTaskId(++Ftask_id_ctr, 0);
  size_t taskid = (static_cast<Ft_debug_task&>(root)).getTaskId();

  size_t threadid = get_cur_tid();
  if(FtidToTaskIdMap[threadid].empty())
  FtidToTaskIdMap[threadid].push(0);
  size_t tid = FtidToTaskIdMap[threadid].top();


  Fthreadstate* cur_task;
  concurrent_hash_map<size_t, Fthreadstate*>::accessor ac;
  if (Ftaskid_map.find(ac, tid))
    cur_task = ac->second;
  else {
    cur_task = new Fthreadstate();
    cur_task->cur_epoch = (tid << (64 - NUM_TASK_BITS));
    cur_task->tlock = 0;

    Ftaskid_map.insert(ac, tid);
    ac->second = cur_task;
  }

  ac.release();

  Fthreadstate* child_task = new Fthreadstate();
  child_task->cur_epoch = (taskid << (64 - NUM_TASK_BITS));
  child_task->tlock = 0;

  Ftaskid_map.insert(ac, taskid);
  ac->second = child_task;
  ac.release();
#ifdef ENABLE_TASK_MAP
  child_task->C = cur_task->C;
  child_task->C[tid] = ((cur_task->cur_epoch) << NUM_TASK_BITS) >> NUM_TASK_BITS;
  cur_task->cur_epoch++;
  child_task->cur_epoch++;
#else
  size_t cur_epoch = cur_task->cur_epoch;
  size_t child_epoch = child_task->cur_epoch;
  for (int i = 0; i < NUM_TASKS; i++)
    store(child_task, i, element(cur_task, i) - cur_epoch + child_epoch);
  store(cur_task, tid, element(cur_task, tid) + 1);
  store(child_task, taskid, element(child_task, taskid) + 1);
#endif
#ifdef DEBUG
  cur_task->task_stats.depth++;
  child_task->task_stats.depth = cur_task->task_stats.depth;
  Fmy_getlock(&(stats.gs_lock));
  if (stats.max_task_depth < cur_task->task_stats.depth)
    stats.max_task_depth = cur_task->task_stats.depth;
  Fmy_releaselock(&(stats.gs_lock));
#endif
//  (cur_task->child).push_back(taskid);
//  tbb::task::spawn_root_and_wait(root); // QQQQQ does this wait for all child tasks or only root......assumed only root
  // delete(child_task);
}

inline void Ft_debug_task::wait_for_one(size_t taskId){

  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  size_t threadid = get_cur_tid();
  if(FtidToTaskIdMap[threadid].empty())
    FtidToTaskIdMap[threadid].push(0);
  size_t tid = FtidToTaskIdMap[threadid].top();
  concurrent_hash_map<size_t,Fthreadstate*>::accessor ac;
 Ftaskid_map.find(ac, tid);
 Fthreadstate* cur_task = ac->second;
  ac.release();

    if (Ftaskid_map.find(ac, taskId)) {
     Fthreadstate* child_task = ac->second;
      ac.release();
#if ENABLE_TASK_MAP
        for (auto it = child_task->C.begin(); it != child_task->C.end(); it++) {
          auto it2 = cur_task->C.find(it->first);
          if (it2 != cur_task->C.end())
            it2->second = std::max(it2->second, it->second);
          else if (it->first != ((cur_task->cur_epoch) >> (64 - NUM_TASK_BITS)))
            cur_task->C[it->first] = it->second;
        }
        cur_task->C[(child_task->cur_epoch) >> (64 - NUM_TASK_BITS)] =
            ((child_task->cur_epoch) << NUM_TASK_BITS) >> NUM_TASK_BITS;
#else
        for (int i = 0; i < NUM_TASKS; i++)
          store(cur_task, i,
                ((std::max(element(cur_task, i) << NUM_TASK_BITS, element(child_task, i)
                                                                      << NUM_TASK_BITS)) >>
                 NUM_TASK_BITS) +
                    cur_epoch);
#endif
    }
}
inline void Ft_debug_task::spawn_and_wait_for_all(task& child) {
  (static_cast<Ft_debug_task&>(child)).setTaskId(++Ftask_id_ctr, 1);
  size_t taskid = (static_cast<Ft_debug_task&>(child)).getTaskId();

  size_t threadid = get_cur_tid();
  if(FtidToTaskIdMap[threadid].empty())
  FtidToTaskIdMap[threadid].push(0);
  size_t tid = FtidToTaskIdMap[threadid].top();

  Fthreadstate* cur_task;
  concurrent_hash_map<size_t, Fthreadstate*>::accessor ac;
  if (Ftaskid_map.find(ac, tid))
    cur_task = ac->second;
  else {
    cur_task = new Fthreadstate();
    cur_task->cur_epoch = tid << (64 - NUM_TASK_BITS);
    cur_task->tlock = 0;

    Ftaskid_map.insert(ac, tid);
    ac->second = cur_task;
    // Ftaskid_map.insert(std::pair<size_t,Fthreadstate*>(tid,cur_task));
  }

  ac.release();

  Fthreadstate* child_task = new Fthreadstate();
  child_task->cur_epoch = taskid << (64 - NUM_TASK_BITS);
  child_task->tlock = 0;
  Ftaskid_map.insert(ac, taskid);
  ac->second = child_task;
  ac.release();
#ifdef ENABLE_TASK_MAP
  child_task->C = cur_task->C;
  child_task->C[tid] = ((cur_task->cur_epoch) << NUM_TASK_BITS) >> NUM_TASK_BITS;
  cur_task->cur_epoch++;
  child_task->cur_epoch++;
#else
  size_t cur_epoch = cur_task->cur_epoch;
  size_t child_epoch = child_task->cur_epoch;
  for (int i = 0; i < NUM_TASKS; i++)
    store(child_task, i, element(cur_task, i) - cur_epoch + child_epoch);
  store(cur_task, tid, element(cur_task, tid) + 1);
  store(child_task, taskid, element(child_task, taskid) + 1);
#endif
#ifdef DEBUG
  cur_task->task_stats.depth++;
  child_task->task_stats.depth = cur_task->task_stats.depth;
  Fmy_getlock(&(stats.gs_lock));
  if (stats.max_task_depth < cur_task->task_stats.depth)
    stats.max_task_depth = cur_task->task_stats.depth;
  Fmy_releaselock(&(stats.gs_lock));
#endif
  (cur_task->child).push_back(taskid);
  task::spawn_and_wait_for_all(child);
  Fmy_getlock(&Ftaskid_map_lock);

  for (std::list<size_t>::iterator it = cur_task->child.begin(); it != cur_task->child.end();
       it++) {
    if (Ftaskid_map.find(ac, *it)) {
      Fthreadstate* child_task = ac->second;
      ac.release();
      if (cur_task != child_task) {
#if ENABLE_TASK_MAP
        for (auto it = child_task->C.begin(); it != child_task->C.end(); it++) {
          auto it2 = cur_task->C.find(it->first);
          if (it2 != cur_task->C.end())
            it2->second = std::max(it2->second, it->second);
          else if (it->first != ((cur_task->cur_epoch) >> (64 - NUM_TASK_BITS)))
            cur_task->C[it->first] = it->second;
        }
        cur_task->C[(child_task->cur_epoch) >> (64 - NUM_TASK_BITS)] =
            ((child_task->cur_epoch) << NUM_TASK_BITS) >> NUM_TASK_BITS;
#else
        for (int i = 0; i < NUM_TASKS; i++)
          store(cur_task, i,
                ((std::max(element(cur_task, i) << NUM_TASK_BITS, element(child_task, i)
                                                                      << NUM_TASK_BITS)) >>
                 NUM_TASK_BITS) +
                    cur_epoch);
#endif
        delete (child_task);
      }
      Ftaskid_map.erase(*it);
    }
    // parent_map.erase(*it);
    // delete(child);
  }
  cur_task->child.clear();
}

inline void Ft_debug_task::wait_for_all() {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
//  task::wait_for_all();
  size_t threadid = get_cur_tid();
  if(FtidToTaskIdMap[threadid].empty())
  FtidToTaskIdMap[threadid].push(0);
  size_t tid = FtidToTaskIdMap[threadid].top();
  size_t cur_epoch = tid << (64 - NUM_TASK_BITS);
  // Fmy_getlock(&Ftaskid_map_lock);
  concurrent_hash_map<size_t, Fthreadstate*>::accessor ac;
  Ftaskid_map.find(ac, tid);
  Fthreadstate* cur_task = ac->second;
  ac.release();
  for (std::list<size_t>::iterator it = cur_task->child.begin(); it != cur_task->child.end();
       it++) {
    if (Ftaskid_map.find(ac, *it)) {
      Fthreadstate* child_task = ac->second;
      ac.release();
      if (cur_task != child_task) {
#if ENABLE_TASK_MAP
        for (auto it = child_task->C.begin(); it != child_task->C.end(); it++) {
          auto it2 = cur_task->C.find(it->first);
          if (it2 != cur_task->C.end())
            it2->second = std::max(it2->second, it->second);
          else if (it->first != ((cur_task->cur_epoch) >> (64 - NUM_TASK_BITS)))
            cur_task->C[it->first] = it->second;
        }
        cur_task->C[(child_task->cur_epoch) >> (64 - NUM_TASK_BITS)] =
            ((child_task->cur_epoch) << NUM_TASK_BITS) >> NUM_TASK_BITS;
#else
        for (int i = 0; i < NUM_TASKS; i++)
          store(cur_task, i,
                ((std::max(element(cur_task, i) << NUM_TASK_BITS, element(child_task, i)
                                                                      << NUM_TASK_BITS)) >>
                 NUM_TASK_BITS) +
                    cur_epoch);
#endif
        delete (child_task);
      }
    }
  }
  cur_task->child.clear();
}

} // namespace tbb

#pragma GCC pop_options
#endif
