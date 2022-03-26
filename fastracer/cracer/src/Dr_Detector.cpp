#include "exec_calls.h"
#include "omrd.h"
#include <bitset>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <vector>

using namespace std;
using namespace tbb;
#define USE_PINLOCK 1

#if USE_PINLOCK
// typedef pair<PIN_LOCK, VarState> subpair;
// typedef pair<PIN_LOCK,subpair*> PAIR;
#else
typedef pair<tbb::atomic<size_t>, VarState> subpair;
typedef pair<tbb::atomic<size_t>, subpair*> PAIR;
#endif

// PAIR* shadow_space;
my_lock shadow_space_lock(1);
extern size_t num_dead_tasks;
extern size_t dead_clock_value;

#if STATS
GlobalStats globalStats;
#endif

#if TASK_GRAPH
my_lock graph_lock(0);
std::ofstream taskgraph;
#endif
#if DEBUG_TIME
Time_Task_Management time_task_management;
Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;

#endif
size_t nthread = 0;
const size_t SS_PRIMARY_TABLE_ENTRIES = ((size_t)1024);
const size_t SS_SEC_TABLE_ENTRIES = ((size_t)4 * (size_t)1024 * (size_t)1024);
unsigned read_1 = 0, read_2 = 0, write_1 = 0, write_2 = 0;

std::ofstream report;

std::map<ADDRINT, struct violation*> all_violations;
my_lock viol_lock(0);
my_lock debug_lock(0);

void error(ADDRINT addr, size_t ftid, AccessType ftype, size_t stid, AccessType stype) {
  my_getlock(&viol_lock);
  all_violations.insert(make_pair(
      addr, new violation(new violation_data(ftid, ftype), new violation_data(stid, stype))));
  my_releaselock(&viol_lock);
}

void TD_Activate() {
  std::cout << "ACTIVATE************************" << std::endl;

  my_lock global_relable_lock(0);
  my_lock thread_local_lock[NUM_THREADS]{0};
  // initialize OM structure
  g_english = new omrd_t();
  g_hebrew = new omrd_t();
  TaskState* main_task = new TaskState();
  main_task->tid = 0;
  main_task->current_english = g_english->get_base();
  main_task->current_hebrew = g_hebrew->get_base();
  main_task->cont_english = NULL;
  main_task->cont_hebrew = NULL;
  main_task->sync_english = NULL;
  main_task->sync_hebrew = NULL;

  cur[0].push(main_task);
}

static bool exceptions(size_t addr, size_t threadId) { return (all_violations.count(addr) != 0); }

extern "C" void RecordMem(size_t threadId, void* access_addr, AccessType accesstype) {
  ADDRINT addr = (ADDRINT)access_addr;
  if (all_violations.count(addr) != 0) {
    return;
  }

  // my_getlock(&thread_local_lock[threadId]);

  if (cur[threadId].empty()) {
    // my_releaselock(&thread_local_lock[threadId]);
    return;
  }

  TaskState* task_state = cur[threadId].top();
  size_t tid = task_state->tid;

  concurrent_hash_map<ADDRINT, VarState*>::accessor ac;
  VarState* var_state;
  bool fnd = var_map.find(ac, addr);
  if (fnd)
    var_state = ac->second;
  else {
    if (accesstype == READ) {
      var_state = new VarState(1, task_state->current_english, task_state->current_hebrew);
    } else {
      var_state = new VarState(0, task_state->current_english, task_state->current_hebrew);
    }
    var_map.insert(ac, addr);
    ac->second = var_state;
    ac.release();
    // my_releaselock(&thread_local_lock[threadId]);
    return;
  }

  if (accesstype == READ) {
    om_node* curr_estrand = task_state->current_english;
    om_node* curr_hstrand = task_state->current_hebrew;

    var_access* writer = var_state->writer;

    if (writer && writer->races_with(curr_estrand, curr_hstrand)) {
      error(addr, 0, WRITE, task_state->tid, READ);
      ac.release();
      // my_releaselock(&thread_local_lock[threadId]);
      return;
      // race detected.
    }

    // Update leftmost and rightmost readers
    var_access* reader = var_state->lreader;
    // rreader = var_state->rreader;
    if (reader == NULL) {
      reader = new var_access(task_state->current_english, task_state->current_hebrew);
      pthread_spin_lock(&var_state->lreader_lock);
      if (var_state->lreader == NULL) {
        var_state->lreader = reader;
      }
      pthread_spin_unlock(&var_state->lreader_lock);
      if (reader != var_state->lreader) {
        // delete reader;
        reader = var_state->lreader;
      }
    }
    assert(reader != NULL);

    // potentially update the left-most reader; replace it if
    // - the new reader is to the left of the old lreader
    //   (i.e., comes first in serially execution)  OR
    // - there is a path from old lreader to this reader

    bool is_leftmost;
    // QUERY_START;
    // size_t relabel_id = 0;
    // do {
    //   relabel_id = g_relabel_id;
    assert(reader->estrand != NULL);
    assert(reader->hstrand != NULL);
    assert(curr_estrand);
    assert(curr_hstrand);
    my_getlock(&thread_local_lock[threadId]);
    is_leftmost =
        om_precedes(curr_estrand, reader->estrand) || om_precedes(reader->hstrand, curr_hstrand);
    my_releaselock(&thread_local_lock[threadId]);
    // } while ( !( (relabel_id & 0x1) == 0 && relabel_id == g_relabel_id));
    // QUERY_END;
    if (is_leftmost) {
      pthread_spin_lock(&var_state->lreader_lock);
      var_state->lreader->update_acc_info(curr_estrand, curr_hstrand);
      pthread_spin_unlock(&var_state->lreader_lock);
    }

    reader = var_state->rreader;

    if (reader == NULL) {
      reader = new var_access(task_state->current_english, task_state->current_hebrew);
      pthread_spin_lock(&var_state->rreader_lock);
      if (var_state->rreader == NULL) {
        var_state->rreader = reader;
      }
      pthread_spin_unlock(&var_state->rreader_lock);

      if (reader == var_state->rreader) {
        // delete reader;
        reader = var_state->rreader;
      }
    }
    assert(reader != NULL);
    // potentially update the left-most reader; replace it if
    // - the new reader is to the left of the old lreader
    //   (i.e., comes first in serially execution)  OR
    // - there is a path from old lreader to this reader
    bool is_rightmost;
    // QUERY_START;
    // relabel_id = 0;
    // do {
    //   relabel_id = g_relabel_id;
    my_getlock(&thread_local_lock[threadId]);
    is_rightmost = om_precedes(reader->estrand, curr_estrand);
    my_releaselock(&thread_local_lock[threadId]);
    // } while ( !( (relabel_id & 0x1) == 0 && relabel_id == g_relabel_id));
    // QUERY_END;
    if (is_rightmost) {
      pthread_spin_lock(&var_state->rreader_lock);
      var_state->rreader->update_acc_info(curr_estrand, curr_hstrand);
      pthread_spin_unlock(&var_state->rreader_lock);
    }
  }

  if (accesstype == WRITE) {
    om_node* curr_estrand = task_state->current_english;
    om_node* curr_hstrand = task_state->current_hebrew;

    var_access* writer = var_state->writer;
    if (writer == NULL) {
      writer = new var_access(task_state->current_english, task_state->current_hebrew);
      pthread_spin_lock(&var_state->writer_lock);
      if (var_state->writer == NULL) {
        var_state->writer = writer;
      }
      pthread_spin_unlock(&var_state->writer_lock);
      if (writer != var_state->writer) {
        // delete writer;
        writer = var_state->writer;
      }
    }
    assert(writer);

    if (writer->races_with(curr_estrand, curr_hstrand)) {
      // report race
      error(addr, 0, WRITE, task_state->tid, WRITE);
      ac.release();
      // my_releaselock(&thread_local_lock[threadId]);
      return;
    }
    pthread_spin_lock(&var_state->writer_lock);
    // replace the last writer regardless
    writer->update_acc_info(curr_estrand, curr_hstrand);
    pthread_spin_unlock(&var_state->writer_lock);

    // Now we detect races with the lreaders
    var_access* reader = var_state->lreader;
    if (reader && reader->races_with(curr_estrand, curr_hstrand)) {
      error(addr, 0, READ, task_state->tid, WRITE);
      ac.release();
      // my_releaselock(&thread_local_lock[threadId]);
      return;
    }

    // Now we detect races with the rreaders
    reader = var_state->rreader;
    if (reader && reader->races_with(curr_estrand, curr_hstrand)) {
      error(addr, 0, READ, task_state->tid, WRITE);
      ac.release();
      // my_releaselock(&thread_local_lock[threadId]);
      return;
    }
  }
  ac.release();
  // my_releaselock(&thread_local_lock[threadId]);
  return;
}

extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype) {
  RecordMem(tid, access_addr, accesstype);
}

void CaptureLockAcquire(size_t threadId, ADDRINT lock_addr) { return; }

void CaptureLockRelease(size_t threadId, ADDRINT lock_addr) { return; }

void paccesstype(AccessType accesstype) {
  if (accesstype == READ)
    report << "READ\n";
  if (accesstype == WRITE)
    report << "WRITE\n";
}

extern "C" void Fini() {
#if REPORT_DATA_RACES
  report.open("violations.out");
  for (std::map<ADDRINT, struct violation*>::iterator i = all_violations.begin();
       i != all_violations.end(); ++i) {
    struct violation* viol = i->second;
    report << "** Data Race Detected**\n";
    report << " Address is :";
    report << i->first;
    report << "\n";
    report << viol->a1->tid;
    paccesstype(viol->a1->accessType);
    report << viol->a2->tid;
    paccesstype(viol->a2->accessType);
    report << "**************************************\n";
  }
  report.close();
#endif

#if STATS
  globalStats.dump();
  std::cout << "STATS Mode\n";
#endif
  std::cout << "Number of violations = " << all_violations.size() << std::endl;
  std::cout << "Number of relabel operations = " << total_num_relabel << std::endl;
  std::cout << "Total time of relabel operation = " << relabel_time / 1000000 << " milliseconds"
            << std::endl;
#if DEBUG_TIME
  time_task_management.dump();
  time_dr_detector.dump();

#endif
#if TASK_GRAPH
  my_getlock(&graph_lock);
  std::cout << "acquired_lock in finii\n";
  taskgraph << "}\n";
  taskgraph.close();
  my_releaselock(&graph_lock);
  std::cout << "released_lock in finii\n";
#endif
}
