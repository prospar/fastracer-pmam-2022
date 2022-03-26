#ifndef EXEC_CALLS_H
#define EXEC_CALLS_H

#pragma GCC push_options
#pragma GCC optimize("O0")

#include "Common.H"
#include "stats.h"
#include "debug_time.h"
#include "tbb/concurrent_hash_map.h"
#include "tbb/task.h"
#include "tbb/tbb_thread.h"
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <pthread.h>
#include <stack>
#include <unordered_map>
#include <vector>
#include "om.h"
// #include "omrd.h"


#if DEBUG_TIME
extern Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
#endif
using namespace tbb;

extern "C" void TD_Activate();

struct violation_data {
  size_t tid;
  AccessType accessType;

  violation_data(size_t tid, AccessType accessType) {
    this->tid = tid;
    this->accessType = accessType;
  }
};

struct violation {
  struct violation_data* a1;
  struct violation_data* a2;

  violation(violation_data* a1, violation_data* a2) {
    this->a1 = a1;
    this->a2 = a2;
  }
};

class TaskState {
  public:
    size_t tid;
    om_node* current_english;
    om_node* current_hebrew;
    om_node* cont_english;
    om_node* cont_hebrew;
    om_node* sync_english;
    om_node* sync_hebrew;
    uint32_t flags;
    std::list<TaskState*> child;
    bool complete = false;
    bool execute = false;
    my_lock tlock = 0;
    TaskState* parent = NULL;
    task* mytask = NULL;

    TaskState() {}
    TaskState(TaskState* parent) {
    current_english = parent->cont_english;
    current_hebrew = parent->cont_hebrew;
    // current_english = NULL;
    // current_hebrew = NULL;
    sync_english = NULL;
    sync_hebrew = NULL;
    cont_english = NULL;
    cont_hebrew = NULL;
    flags = parent->flags;
    }
} ;

class var_access {
  public:
    om_node *estrand; // the strand that made this access stored in g_english
    om_node *hstrand; // the strand that made this access stored in g_hebrew
    var_access(om_node *_estrand, om_node *_hstrand){
      estrand = _estrand;
      hstrand = _hstrand;
    }

    bool races_with(om_node *curr_estrand, om_node *curr_hstrand);

    inline void update_acc_info(om_node *_estrand, om_node *_hstrand) {
      estrand = _estrand;
      hstrand = _hstrand;
    }
};

class VarState{
  public:
    var_access *lreader;
    var_access *rreader;
    var_access *writer;

    // locks for updating lreaders, rreaders, and writers
    pthread_spinlock_t lreader_lock;
    pthread_spinlock_t rreader_lock;
    pthread_spinlock_t writer_lock;

    VarState(bool is_read, om_node *estrand, om_node *hstrand){
      if(is_read) {
        lreader = new var_access(estrand, hstrand);
        rreader = new var_access(estrand, hstrand);
        writer = NULL;
      } else {
        lreader = NULL;
        rreader = NULL;
        writer = new var_access(estrand, hstrand);
      }
      pthread_spin_init(&lreader_lock, PTHREAD_PROCESS_PRIVATE);
      pthread_spin_init(&rreader_lock, PTHREAD_PROCESS_PRIVATE);
      pthread_spin_init(&writer_lock, PTHREAD_PROCESS_PRIVATE);
    }
};

typedef tbb::internal::tbb_thread_v3::id TBB_TID;

// extern my_lock global_relable_lock;
// extern my_lock thread_local_lock[NUM_THREADS];
// extern pthread_spinlock_t global_relable_lock;
// extern pthread_spinlock_t thread_local_lock[NUM_THREADS];

extern std::map<TBB_TID, size_t> tid_map;
extern my_lock tid_map_lock;
//extern my_lock taskid_map_lock;
extern my_lock parent_map_lock;
extern my_lock lock_map_lock;
extern my_lock temp_cur_map_lock;
// extern tbb::concurrent_hash_map<size_t, TaskState*> taskid_map;
extern tbb::concurrent_hash_map<ADDRINT, VarState*> var_map;
extern std::map<size_t,TaskState*> temp_cur_map;
extern std::map<size_t, size_t> lock_map;
extern std::stack<TaskState*> cur[NUM_THREADS];

extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId);

__attribute__((noinline)) void __exec_end__(unsigned long taskId);

size_t __TBB_EXPORTED_METHOD get_cur_tid();
// void Capturewait_for_all_task(TaskState* cur_task_state);
void Capturewait_for_all(TaskState*);
}
#if STATS
extern GlobalStats stats;
#endif

extern "C" void Fini();
extern "C" void RecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void CaptureLockAcquire(size_t tid, ADDRINT lock_addr);

extern "C" void CaptureLockRelease(size_t tid, ADDRINT lock_addr);
extern "C" void set_incomplete();
extern "C" void set_complete();
extern "C" void clear_child_list();
extern "C" void set_execute_true();
extern "C" void set_execute_false();

#endif
