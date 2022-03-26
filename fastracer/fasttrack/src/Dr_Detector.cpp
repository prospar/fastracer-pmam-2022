#include "Common.H"
#include "exec_calls.h"
#include <bitset>
#include <cassert>
#include <cstddef>
#include <features.h>
#include <fstream>
#include <ios>
#include <sys/mman.h>
#include <utility>
#include <vector>

using namespace std;
using namespace tbb;

typedef pair<tbb::atomic<size_t>, VarState*> subpair;
typedef pair<tbb::atomic<size_t>, subpair*> PAIR;

PAIR* shadow_space;
my_lock shadow_space_lock(1);
size_t nthread = 0;
const size_t SS_PRIMARY_TABLE_ENTRIES = ((size_t)1024);
const size_t SS_SEC_TABLE_ENTRIES = ((size_t)4 * (size_t)1024 * (size_t)1024);

std::ofstream report;

#if DEBUG
my_lock printLock(0); // Serialize print statements
#endif

#if DEBUG_TIME
Time_Task_Management time_task_management;
Time_DR_Detector time_dr_detector;
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
#endif

#if STATS
GlobalStats globalStats;
uint64_t numrds = 0;
uint64_t numwrs = 0;
#endif

// FIXME: This is bad, TD_Activate() is not getting called from the microbenchmarks.
bool mmapCalled = false;
std::map<ADDRINT, LockState*> lockmap; // only one lock

std::ostream& operator<<(std::ostream& out, const FT_READ_TYPE value) {
  const char* s = 0;
#define PROCESS_VAL(p)                                                                             \
  case (p):                                                                                        \
    s = #p;                                                                                        \
    break;
  switch (value) {
    PROCESS_VAL(RD_EXCLUSIVE);
    PROCESS_VAL(RD_SAME_EPOCH);
    PROCESS_VAL(RD_SHARE);
    PROCESS_VAL(RD_SHARED);
    PROCESS_VAL(RD_INVALID);
  }
#undef PROCESS_VAL
  return out << s;
}

std::ostream& operator<<(std::ostream& out, const FT_WRITE_TYPE value) {
  const char* s = 0;
#define PROCESS_VAL(p)                                                                             \
  case (p):                                                                                        \
    s = #p;                                                                                        \
    break;
  switch (value) {
    PROCESS_VAL(WR_EXCLUSIVE);
    PROCESS_VAL(WR_SAME_EPOCH);
    PROCESS_VAL(WR_SHARED);
    PROCESS_VAL(WR_INVALID);
  }
#undef PROCESS_VAL
  return out << s;
}

#if REPORT_DATA_RACES
std::map<ADDRINT, struct Violation*> all_violations;
my_lock viol_lock(0);

void error(ADDRINT addr, size_t ftid, AccessType ftype, size_t stid, AccessType stype) {
  my_getlock(&viol_lock);
  all_violations.insert(make_pair(
      addr, new Violation(new ViolationData(ftid, ftype), new ViolationData(stid, stype))));
  my_releaselock(&viol_lock);
}
#endif

// FIXME: This fails if instrumentation is called from Task 0.
void TD_Activate() {
  if (!mmapCalled) {
    size_t primary_length = (SS_PRIMARY_TABLE_ENTRIES) * sizeof(PAIR);
    shadow_space = (PAIR*)mmap(0, primary_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    assert(shadow_space != (void*)-1);
  }
  mmapCalled = true;
}

// This can also be the first read after a write
void ftread(ADDRINT addr, VarState* var_state, TaskState* task, size_t curtid, size_t curclock) {
#if 0
  my_getlock(&printLock);
  cout << "Begin read: address: " << std::showbase << std::hex << addr << std::dec
       << " Task id: " << curtid << " Cur clock: " << curclock << "\n";
  my_releaselock(&printLock);
#endif

  assert(var_state != NULL);
  auto rd_md = var_state->m_rd_epoch;
  auto wr_md = var_state->m_wr_epoch;

#if 0
  my_getlock(&printLock);
  cout << std::showbase << std::hex << "Read epoch: " << rd_md << " Write md: " << wr_md << "\n";
  cout << std::dec << "Task id: " << curtid << " has reached here read1\n";
  my_releaselock(&printLock);
#endif

  size_t wtid = var_state->getWriteTaskID(wr_md);
  size_t wclk = var_state->getWriteClock(wr_md);

  // Check for write-read race, independent of the read metadata
  if (wtid != curtid && wclk > (element(task, wtid))) {
#if DEBUG
    my_getlock(&printLock);
    cout << "Detected write-read race between Task: " << wtid << " and Task: " << curtid << "\n";
    my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
    error(addr, wtid, WRITE, curtid, READ);
#endif
    var_state->setRacy();
#if STATS
#endif
    return;
  }
  if (!var_state->isReadVector()) {
    auto last_rd_tid = var_state->getReadEpochTaskID(rd_md);
    auto last_rd_clk = var_state->getReadEpochClock(rd_md);
    if (last_rd_clk <= element(task, last_rd_tid)) {
      // Check RD EXCLUSIVE
      // Non-concurrent read with previous read, the previous read can be from the same thread
#if STATS
      (task->m_task_stats)->track_read(addr, RD_EXCLUSIVE);
#endif
      rd_md = var_state->createReadEpoch(curtid, curclock);
      var_state->m_rd_epoch = rd_md;
#if 0
      my_getlock(&printLock);
      cout << "End read exclusive: Task id: " << curtid << " Cur clock: " << curclock << "\n";
      my_releaselock(&printLock);
#endif
    } else {
      // Upgrade epoch to vector clock, READ SHARE
      // Concurrent read, but cannot be from the same task
      assert(last_rd_tid != curtid);
#if STATS
      (task->m_task_stats)->track_read(addr, RD_SHARE);
#endif
      void* rd_vc = var_state->createReadVector(addr, last_rd_tid, last_rd_clk, curtid, curclock);
      var_state->m_rd_epoch = reinterpret_cast<epoch>(rd_vc);
#if 0
      my_getlock(&printLock);
      cout << "End read share: Task id: " << curtid << " Cur clock: " << curclock << "\n";
      my_releaselock(&printLock);
#endif
    }
    return;
  }
#if 0
  my_getlock(&printLock);
  cout << std::dec << "Task id: " << curtid << " has reached here read2\n";
  my_releaselock(&printLock);
#endif

  // FIXME: This is actually more common than READ SHARE
  // RD SHARED
  assert(var_state->isReadVector());
#if STATS
  (task->m_task_stats)->track_read(addr, FT_READ_TYPE::RD_SHARED);
#endif

#if ENABLE_VECTOR
  vector<size_t>::iterator it;
  std::vector<epoch>* tmp = reinterpret_cast<std::vector<epoch>>(static_cast<void*>(rd_md));
  for (it = tmp->begin(); it != tmp->end(); ++it) {
    if (getTaskID(*it) == curtid) {
      break;
    }
  }
  if (it == tmp->end()) {
    tmp->push_back(createEpoch(curtid, curclock));
  } else {
    *it = createEpoch(curtid, curclock);
  }
#else
#if ENABLE_MAPS
  std::unordered_map<size_t, size_t>* tmp =
      reinterpret_cast<std::unordered_map<size_t, size_t>*>(reinterpret_cast<void*>(rd_md));
  tmp->insert(make_pair(curtid, curclock));
#else
  size_t* tmp = reinterpret_cast<size_t*>(rd_md);
  tmp[curtid] = curclock;
#endif
#endif
#if 0
  my_getlock(&printLock);
  cout << "End read shared: Task id: " << curtid << " Cur clock: " << curclock << "\n";
  my_releaselock(&printLock);
#endif
}

void ftwrite(ADDRINT addr, VarState* var_state, TaskState* task_state, size_t curtid,
             size_t curclk) {
#if 0
  my_getlock(&printLock);
  cout << "Begin write: address: " << std::showbase << std::hex << addr << std::dec
       << " Task id: " << curtid << " Cur clock: " << curclk << "\n";
  my_releaselock(&printLock);
#endif

  assert(var_state != NULL);
  auto rd_md = var_state->m_rd_epoch;
  auto wr_md = var_state->m_wr_epoch;
  auto last_wtid = var_state->getWriteTaskID(wr_md);
  auto last_wclk = var_state->getWriteClock(wr_md);

#if 0
  my_getlock(&printLock);
  cout << std::dec << "Task id: " << curtid << std::showbase << std::hex << " Read md: " << rd_md
       << " Write md: " << var_state->m_wr_epoch << "\n";
  my_releaselock(&printLock);
#endif

  // Check for W-W race
  if (wr_md > 0 && last_wtid != curtid && last_wclk > (element(task_state, last_wtid))) {
#if DEBUG
    my_getlock(&printLock);
    cout << "Detected write-write race between Task: " << last_wtid << " and Task: " << curtid
         << "\n";
    my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
    error(addr, last_wtid, WRITE, curtid, WRITE);
#endif
    var_state->setRacy();
    return;
  }

  if (wr_md > 0 && last_wtid == curtid) {
    assert(last_wclk < curclk);
  }

#if 0
  my_getlock(&printLock);
  cout << std::dec << "Task id: " << curtid << " has reached here write1\n";
  my_releaselock(&printLock);
#endif

  if (!var_state->isReadVector()) {
    // WRITE EXCLUSIVE
#if STATS
    (task_state->m_task_stats)->track_write(addr, WR_EXCLUSIVE);
#endif
    if (curtid != last_wtid) {
      auto rd_clk = var_state->getReadEpochClock(rd_md);
      auto rd_tid = var_state->getReadEpochTaskID(rd_md);
#if 0
      my_getlock(&printLock);
      cout << "Read tid: " << rd_tid << " read clock: " << rd_clk << " Write clock: " << last_wclk
           << " write tid: " << last_wtid << "\n";
      my_releaselock(&printLock);
#endif
      if (rd_tid != curtid && rd_clk > element(task_state, rd_tid)) {
#if DEBUG
        my_getlock(&printLock);
        cout << "Detected read-write race on address " << std::showbase << std::hex << addr
             << std::dec << " between Task: " << rd_tid << " and Task: " << curtid << "\n";
        my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
        error(addr, rd_tid, READ, curtid, WRITE);
#endif
        var_state->setRacy();
      }
    }
#if 0
    my_getlock(&printLock);
    cout << "End write exclusive: Task id: " << curtid << " Cur clock: " << curclk << "\n";
    my_releaselock(&printLock);
#endif
    // Reset read information
    var_state->m_rd_epoch = 0;
    var_state->m_wr_epoch = var_state->createWriteEpoch(curtid, curclk);
    // FIXME: Update write information
    return;
  }
#if 0
  my_getlock(&printLock);
  cout << std::dec << "Task id: " << curtid << " has reached here write2\n";
  my_releaselock(&printLock);
#endif

  assert(var_state->isReadVector());
  // WR SHARED
#if STATS
  (task_state->m_task_stats)->track_write(addr, WR_SHARED);
#endif
#if ENABLE_MAPS
#if 0
  my_getlock(&printLock);
  cout << "write exclusive: Task id: " << curtid << " checking last readers\n";
  my_releaselock(&printLock);
#endif
  std::unordered_map<size_t, size_t>* tmp =
      reinterpret_cast<std::unordered_map<size_t, size_t>*>(reinterpret_cast<void*>(rd_md));

  for (auto it = tmp->begin(); it != tmp->end(); ++it)
    if (it->first != curtid && it->second > element(task_state, it->first)) {
#if DEBUG
      my_getlock(&printLock);
      cout << "Detected read-write race on address: " << std::showbase << std::hex << addr
           << std::dec << " between Task: " << it->first << " and Task: " << curtid << "\n";
      my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
      error(addr, it->first, READ, curtid, WRITE);
#endif
      var_state->setRacy();
    }
  tmp->clear();
#else
#if ENABLE_VECTOR
  std::vector<epoch>* tmp = reinterpret_cast<std::vector<epoch>>(static_cast<void*>(rd_md));
  for (auto it = tmp->begin(); it != tmp->end(); ++it) {
    if (getTaskID(*it) != curtid && getClock(*it) > element(task_state, getTaskID(curtid))) {
#if DEBUG
      my_getlock(&printLock);
      cout << "Detected read-write race on address: " << std::showbase << std::hex << addr
           << std::dec << " between Task: " << getTaskID(*it) << " and Task: " << curtid << "\n ";
      my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
      error(addr, getTaskID(*it), READ, curtid, WRITE);
#endif
      var_state->setRacy();
    }
  }
  tmp->clear();
#else
  my_getlock(&printLock);
  cout << "write shared: Task id: " << curtid << " read md: " << std::showbase << std::hex << rd_md
       << "\n";
  my_releaselock(&printLock);
  size_t* tmp = reinterpret_cast<size_t*>(rd_md);
  for (size_t i = 0; i < MAX_NUM_TASKS; i++) {
    if (i != curtid && tmp[i] > element(task_state, i)) {
#if DEBUG
      my_getlock(&printLock);
      cout << "Detected read-write race on address: " << std::showbase << std::hex << addr
           << std::dec << " between Task: " << i << " and Task:" << curtid << "\n";
      my_releaselock(&printLock);
#endif
#if REPORT_DATA_RACES
      error(addr, i, READ, curtid, WRITE);
#endif
      var_state->setRacy();
    }
  }
  delete[] tmp;
#endif
#endif
  rd_md = 0;
  var_state->clearReadVector();
  var_state->m_wr_epoch = var_state->createWriteEpoch(curtid, curclk);
#if 0
  my_getlock(&printLock);
  cout << "End write shared: Task id: " << curtid << " Cur clock: " << curclk << "\n";
  my_releaselock(&printLock);
#endif
}

// FIXME: Reorder declarations and definitions across .h and .cpp files

// Print taskid_map for debugging, not thread safe
void dumpTaskIdToTaskStateMap() {
#if DEBUG
  my_getlock(&printLock);
  cout << "*********Task id to Task State Map*********\n";
  // my_getlock(&taskid_taskstate_map_lock);
  for (concurrent_hash_map<size_t, TaskState*>::iterator i = taskid_taskstate_map.begin();
       i != taskid_taskstate_map.end(); ++i) {
    cout << "Task id: " << i->first << "\tThread state:" << i->second << "\n";
  }
  // my_releaselock(&taskid_taskstate_map_lock);
  cout << endl;
  my_releaselock(&printLock);
#endif
}

extern "C" void RecordMem(size_t tid, void* access_addr, AccessType accesstype) {
#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_num_recordmems++;
#if DEBUG
  uint64_t count = globalStats.gs_num_recordmems;
  if (accesstype == READ) {
    numrds++;
  } else {
    numwrs++;
  }
  assert(numwrs + numrds == count);
#endif
  my_releaselock(&globalStats.gs_lock);
#endif

#if DEBUG
  assert(access_addr != NULL);
  my_getlock(&printLock);
  cout << "RecordMem ";
#if STATS
  cout << count;
#endif
  cout << ": Task id: " << std::dec << tid << "\tAddress: " << access_addr << "\t"
       << ((accesstype == READ) ? "READ" : "WRITE") << "\n";
  my_releaselock(&printLock);
#endif

  // FIXME: This is bad, TD_Activate() is not getting called from the microbenchmarks.
  //  TD_Activate();

  TaskState* task_state = NULL;
  concurrent_hash_map<size_t, TaskState*>::accessor ac;
#if DEBUG_TIME
  HRTimer find_start = HR::now();
#endif
  bool found = taskid_taskstate_map.find(ac, tid);
#if DEBUG_TIME
  HRTimer find_end = HR::now();
  my_getlock(&time_dr_detector.time_DR_detector_lock);
  time_dr_detector.taskid_map_find_time +=
      duration_cast<nanoseconds>(find_end - find_start).count();
  time_dr_detector.num_tid_find += 1;
  my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
  if (found) {
    task_state = ac->second;
  } else {
    task_state = new TaskState(tid);
    taskid_taskstate_map.insert(ac, tid);
    ac->second = task_state;
    // taskid_map.insert(std::pair<size_t,taskstate*>(tid,cur_task));
    assert(task_state->m_vector_clock != NULL);
    update_vc(task_state->m_vector_clock, tid, 1);
  }
  //taskid_map.find(ac, tid);
  // taskstate* thread = ac->second;
  ac.release();

  size_t curclock = element(task_state, tid);
  assert((tid == 0 && curclock >= 0) || curclock > 0);
#if 0
  my_getlock(&printLock);
  cout << "Task id: " << tid << " Current clock: " << curclock << "\n";
  my_releaselock(&printLock);
#endif

  ADDRINT addr = (ADDRINT)access_addr;
  size_t primary_index = (addr >> 22) & 0x3ff;
  PAIR* x = shadow_space + primary_index;
  assert(x != NULL);
  my_getlock(&(x->first));
  if (x->second == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(subpair);
    subpair* primary_entry =
        (subpair*)mmap(0, sec_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    x->second = primary_entry;
  }
  my_releaselock(&(x->first));
  subpair* primary_entry = x->second;
  size_t offset = (addr & 0x3fffff);
  subpair* addrpair = primary_entry + offset;

#if FALSE
  if (addr == 0x611490) {
    my_getlock(&printLock);
    std::cout << "Addr: " << std::hex << addr << "\tPrimary index: " << primary_index
              << "\tThread state: " << thread << "\tTask stats: " << &(thread->task_stats) << "\n";
    my_releaselock(&printLock);
    dumpTaskidMap();
  }
#endif

  // FIXME: Accessing a variable may be racy, so we need a per-variable lock. Is the following the lock?
  my_getlock(&(addrpair->first));
  VarState* var_state = addrpair->second;

  // FIXME: Skip analyzing a variable having a data race

  if (var_state != NULL && var_state->isRacy()) {
// Data race already detected, so skip doing anything
#if STATS
#endif
    my_releaselock(&(addrpair->first));
    return;
  }
  if (var_state != NULL && accesstype == READ) {
    assert(var_state->m_wr_epoch >= 0 && var_state->m_rd_epoch >= 0);
#if STATS
    auto vstats = var_state->m_var_stats;
    vstats->track_read(tid);
    my_getlock(&globalStats.gs_lock);
    auto num_rds_tasks = vstats->get_num_rd_tasks();
    if (globalStats.pv_max_rd_tasks < num_rds_tasks) {
      globalStats.pv_max_rd_tasks = num_rds_tasks;
    }
    if (globalStats.pv_max_num_rds < vstats->get_num_rds()) {
      globalStats.pv_max_num_rds = vstats->get_num_rds();
    }
    my_releaselock(&(globalStats.gs_lock));
#endif

    // Common case, RD SAME EPOCH
    size_t var_rd_md = var_state->m_rd_epoch;
    if (var_rd_md > 0 && !var_state->isReadVector() &&
        tid == var_state->getReadEpochTaskID(var_rd_md) &&
        curclock == var_state->getReadEpochClock(var_rd_md)) {
#if STATS
      (task_state->m_task_stats)->track_read(addr, RD_SAME_EPOCH);
#endif

      my_releaselock(&(addrpair->first));
      return;
    } else { // Slow path
      ftread(addr, var_state, task_state, tid, curclock);
    }
  }

  if (var_state != NULL && accesstype == WRITE) {
    assert(var_state->m_wr_epoch >= 0 && var_state->m_rd_epoch >= 0);
#if STATS
    auto vstats = var_state->m_var_stats;
    vstats->track_write(tid);
    my_getlock(&globalStats.gs_lock);
    auto num_wrs_tasks = vstats->get_num_wr_tasks();
    assert(num_wrs_tasks > 0);
    if (globalStats.pv_max_wr_tasks < num_wrs_tasks) {
      globalStats.pv_max_wr_tasks = num_wrs_tasks;
    }
    if (globalStats.pv_max_num_wrs < vstats->get_num_wrs()) {
      globalStats.pv_max_num_wrs = vstats->get_num_wrs();
    }
    my_releaselock(&globalStats.gs_lock);
#endif

    // Common case, WR SAME EPOCH
    size_t var_wr_epoch = var_state->m_wr_epoch;
    if (var_wr_epoch > 0 && var_state->getWriteTaskID(var_wr_epoch) == tid &&
        var_state->getWriteClock(var_wr_epoch) == curclock) {
#if STATS
      (task_state->m_task_stats)->track_write(addr, WR_SAME_EPOCH);
#endif

      my_releaselock(&(addrpair->first));
      return;
    } else {
      ftwrite(addr, var_state, task_state, tid, curclock);
    }
  }

  // Uncommon cases
  if (var_state == NULL && accesstype == READ) {
    var_state = new VarState();
    var_state->m_rd_epoch = var_state->createReadEpoch(tid, curclock);
    addrpair->second = var_state;
#if STATS
    (task_state->m_task_stats)->track_read(addr, RD_EXCLUSIVE);
    auto vstats = var_state->m_var_stats;
    vstats->track_read(tid);
    my_getlock(&globalStats.gs_lock);
    auto max_rds_tasks = vstats->get_num_rd_tasks();
    if (globalStats.pv_max_rd_tasks < max_rds_tasks) {
      globalStats.pv_max_rd_tasks = max_rds_tasks;
    }
    if (globalStats.pv_max_num_rds < vstats->get_num_rds()) {
      globalStats.pv_max_num_rds = vstats->get_num_rds();
    }
    my_releaselock(&globalStats.gs_lock);
#endif
  }

  if (var_state == NULL && accesstype == WRITE) {
    var_state = new VarState();
    var_state->m_wr_epoch = var_state->createWriteEpoch(tid, curclock);
    addrpair->second = var_state;
#if STATS
    // #if DEBUG
    // my_getlock(&printLock);
    // std::cout << "Write: Task id: " << tid << " Var addr: " << std::hex << addr << "\n";
    // my_releaselock(&printLock);
    // #endif
    task_state->m_task_stats->track_write(addr, WR_EXCLUSIVE);
    auto vstats = var_state->m_var_stats;
    vstats->track_write(tid);
    my_getlock(&globalStats.gs_lock);
    auto num_wrs_tasks = vstats->get_num_wr_tasks();
    assert(num_wrs_tasks > 0);
    if (globalStats.pv_max_wr_tasks < num_wrs_tasks) {
      globalStats.pv_max_wr_tasks = num_wrs_tasks;
    }
    if (globalStats.pv_max_num_wrs < vstats->get_num_wrs()) {
      globalStats.pv_max_num_wrs = vstats->get_num_wrs();
    }
    my_releaselock(&globalStats.gs_lock);
#endif
  }

  my_releaselock(&(addrpair->first));
  return;
}

// TODO: Where is this method called from?
extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype) {
  RecordMem(tid, access_addr, accesstype);
#if DEBUG
  assert(access_addr != NULL);
  // my_getlock(&printLock);
  // std::cout << "RecordAccess: Task id: " << std::dec << tid << "\tAddress: " << access_addr << "\t"
  //           << ((accesstype == READ) ? "READ" : "WRITE") << "\n";
  // my_releaselock(&printLock);
#endif

#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_num_recordaccess++;
  my_releaselock(&globalStats.gs_lock);
#endif
}

void CaptureLockAcquire(size_t taskid, ADDRINT lock_addr) {
  concurrent_hash_map<size_t, TaskState*>::accessor ac;
#if DEBUG_TIME
  HRTimer find_start = HR::now();
#endif
  bool found = taskid_taskstate_map.find(ac, taskid);
#if DEBUG_TIME
  HRTimer find_end = HR::now();
  my_getlock(&time_dr_detector.time_DR_detector_lock);
  time_dr_detector.taskid_map_find_time +=
      duration_cast<nanoseconds>(find_end - find_start).count();
  time_dr_detector.num_tid_find += 1;
  my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
  assert(found);
  TaskState* t = ac->second;
  ac.release();

  my_getlock(&lock_map_lock);
  LockState* l;
  if (lockmap.find(lock_addr) == lockmap.end()) {
    l = new LockState();
    lockmap.insert(pair<ADDRINT, LockState*>(lock_addr, l));
  }
  l = lockmap[lock_addr];
  my_releaselock(&lock_map_lock);
  assert(l != NULL);

#if STATS
  (t->m_task_stats)->track_acq(lock_addr);
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_num_lock_acqs++;
  if (globalStats.pt_max_num_acqs < t->m_task_stats->num_acqs)
    globalStats.pt_max_num_acqs = t->m_task_stats->num_acqs;
  my_releaselock(&globalStats.gs_lock);
#endif

  // FIXME: Use ENABLE_MAPS flag.
  my_getlock(&(l->m_llock));
  join_vc(t->m_vector_clock, l->m_lvc);
  my_releaselock(&l->m_llock);
}

void CaptureLockRelease(size_t tid, ADDRINT lock_addr) {
  concurrent_hash_map<size_t, TaskState*>::accessor ac;
#if DEBUG_TIME
  HRTimer find_start = HR::now();
#endif
  bool found = taskid_taskstate_map.find(ac, tid);
#if DEBUG_TIME
  HRTimer find_end = HR::now();
  my_getlock(&time_dr_detector.time_DR_detector_lock);
  time_dr_detector.taskid_map_find_time +=
      duration_cast<nanoseconds>(find_end - find_start).count();
  time_dr_detector.num_tid_find += 1;
  my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
  assert(found);
  TaskState* t = ac->second;
  ac.release();

  my_getlock(&lock_map_lock);
  LockState* l = lockmap.at(lock_addr);
  my_releaselock(&lock_map_lock);
  assert(l != NULL);

#if STATS
  (t->m_task_stats)->track_rel(lock_addr);
  my_getlock(&globalStats.gs_lock);
  globalStats.gs_num_lock_rels++;
  if (globalStats.pt_max_num_rels < t->m_task_stats->num_rels)
    globalStats.pt_max_num_rels = t->m_task_stats->num_rels;
  my_releaselock(&globalStats.gs_lock);
#endif

  // FIXME: Use ENABLE_MAPS flag.
  my_getlock(&l->m_llock);
  copy_vc(t->m_vector_clock, l->m_lvc);
  update_vc(t->m_vector_clock, t->m_taskid, t->m_vector_clock[t->m_taskid] + 1);
  my_releaselock(&(l->m_llock));
}

void paccesstype(AccessType accesstype) {
  if (accesstype == READ)
    report << "READ\n";
  if (accesstype == WRITE)
    report << "WRITE\n";
}

extern "C" void Fini() {
  size_t taskid = get_cur_tid();
  assert(taskid == 0);

#if STATS
  concurrent_hash_map<size_t, TaskState*>::accessor ac;
  bool found = taskid_taskstate_map.find(ac, taskid);
  assert(found);
  TaskState* cur_task_state = ac->second;
  PerTaskStats* ptstats = cur_task_state->m_task_stats;
  assert(ptstats != NULL);
  globalStats.accumulate(ptstats);
#endif

#if DEBUG_TIME
  time_task_management.dump();
  time_dr_detector.dump();
#endif

#if REPORT_DATA_RACES
  report.open("violations.out");
  for (std::map<ADDRINT, struct Violation*>::iterator i = all_violations.begin();
       i != all_violations.end(); ++i) {
    struct Violation* viol = i->second;
    report << "** Data Race Detected**\n";
    report << viol->a1->tid;
    paccesstype(viol->a1->accessType);
    report << viol->a2->tid;
    paccesstype(viol->a2->accessType);
    report << "**************************************\n";
  }
  std::cout << "Number of violations = " << all_violations.size() << std::endl;
  report.close();
#endif

#if STATS
  globalStats.dump();
#endif
}
