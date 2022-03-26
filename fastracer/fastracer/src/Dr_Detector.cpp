#include "exec_calls.h"
#include <bitset>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <vector>

using namespace std;
using namespace tbb;
#define USE_PINLOCK 1

#if USE_PINLOCK
typedef pair<PIN_LOCK, VarState> subpair;
typedef pair<PIN_LOCK, subpair*> PAIR;
#else
typedef pair<tbb::atomic<size_t>, VarState> subpair;
typedef pair<tbb::atomic<size_t>, subpair*> PAIR;
#endif

PAIR* shadow_space;
TaskState* tstate_nodes;
size_t* joinset;
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
// my_lock debug_lock(0);
// unsigned recordmemt = 0, recordmemi = 0, rd1 = 0, rd2 = 0, wr1 = 0, wr2 = 0, spawnt = 0,
//          spawn_roott = 0, spawn_waitt = 0, waitt = 0;
// unsigned recordmemn = 0;
#endif
size_t nthread = 0;
const size_t SS_PRIMARY_TABLE_ENTRIES = ((size_t)1024);
const size_t SS_SEC_TABLE_ENTRIES = ((size_t)4 * (size_t)1024 * (size_t)1024);
unsigned read_1 = 0, read_2 = 0, write_1 = 0, write_2 = 0;

std::ofstream report;

std::map<ADDRINT, struct violation*> all_violations;
my_lock viol_lock(0);

#if LINE_NO_PASS
void error(ADDRINT addr, size_t ftid, AccessType ftype, int fline_no, size_t stid, AccessType stype,
           int sline_no) {
  my_getlock(&viol_lock);
  all_violations.insert(make_pair(addr, new violation(new violation_data(ftid, ftype, fline_no),
                                                      new violation_data(stid, stype, sline_no))));
  my_releaselock(&viol_lock);
}
#else
void error(ADDRINT addr, size_t ftid, AccessType ftype, size_t stid, AccessType stype) {
  my_getlock(&viol_lock);
  all_violations.insert(make_pair(
      addr, new violation(new violation_data(ftid, ftype), new violation_data(stid, stype))));
  my_releaselock(&viol_lock);
}
#endif

void TD_Activate() {
  size_t primary_length = (SS_PRIMARY_TABLE_ENTRIES) * sizeof(PAIR);
  shadow_space = (PAIR*)mmap(0, primary_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
  joinset = (size_t*)mmap(0, (MAX_NUM_TASKS + 1) * sizeof(size_t), PROT_READ | PROT_WRITE,
                          MMAP_FLAGS, -1, 0);
  tstate_nodes = (TaskState*)mmap(0, MAX_NUM_TASKS * sizeof(TaskState), PROT_READ | PROT_WRITE,
                                  MMAP_FLAGS, -1, 0);

#if EPOCH_POINTER
  tstate_nodes[0].epoch_ptr = createEpoch(1, 0);
#else
  tstate_nodes[0].clock = 1;
#endif
  cur[0].push(0);
  num_dead_tasks = 0;
  dead_clock_value = 0;
#if TASK_GRAPH
  my_getlock(&graph_lock);
  taskgraph.open("taskgraph.dot");
  taskgraph << "digraph program {\n ordering = out\n";
  my_releaselock(&graph_lock);
#endif
  //  assert(shadow_space != (void *)-1);
}

inline bool check_parent(TaskState* cur_task, uint32_t taskId) {
  //        uint32_t access_depth = tstate_nodes[taskId].depth;
  //        if(access_depth >= cur_task->depth) return false;
  //        uint32_t* access_vc = tstate_nodes[taskId].m_vc;
  //        uint32_t* cur_vc = tstate_nodes[cur_task->taskId].m_vc;
  //          bool check_par = true;
  //          for(int i=0;i<access_depth;i++){
  //            if(access_vc[i] ^ cur_vc[i] != 0){
  //              check_par = false;break;
  //            }
  //          }
  //          return check_par;
  //        //  if(check_par)
  //        //      access_clock = cur_vc[access_depth];

  uint32_t access_depth = tstate_nodes[taskId].depth;
  if (access_depth >= cur_task->depth)
    return false;
  if (access_depth < NUM_FIXED_TASK_ENTRIES) {
    if (cur_task->cached_tid[access_depth] == taskId)
      return true;
    return false;
  } else {
    return (cur_task->my_vc)->find(taskId) != cur_task->my_vc->end();
  }

  //  return ((cur_task->my_vc != NULL ) && (cur_task->my_vc)->find(taskId) != cur_task->my_vc->end());
}

inline size_t parent_clock(TaskState* cur_task, size_t taskId) {
//  uint32_t access_depth = tstate_nodes[taskId].depth;
//  return (size_t)((cur_task->m_vc)[access_depth]);
#if JOINSET
  uint32_t access_depth = tstate_nodes[taskId].depth;
  if (access_depth >= cur_task->depth)
    return 0;
  if (access_depth < NUM_FIXED_TASK_ENTRIES) {
    if (cur_task->cached_tid[access_depth] == taskId)
      return cur_task->cached_clock[access_depth];
    return 0;
  } else {
    std::unordered_map<size_t, size_t>::iterator it = (cur_task->my_vc)->find(taskId);
    if (it != (cur_task->my_vc->end()))
      return it->second;
    return 0;
  }
#else
  size_t clock = 0;
  if (cur_task->my_vc != NULL) {
    auto it = (cur_task->my_vc)->find(taskId);
    if (it != (cur_task->my_vc)->end())
      clock = it->second;
    else if (cur_task->root_vc != NULL) {
      auto it2 = cur_task->root_vc->find(taskId);
      if (it2 != (cur_task->root_vc->end()))
        clock = it2->second;
    }
  }
  return clock;
#endif

  //    return ((cur_task->my_vc)->find(taskId))->second;
  //  return (*(cur_task->my_vc))[taskId];
}
size_t find_root(int index) {
  int next_index = joinset[index];
  if (next_index != 0) {
    return find_root(next_index);
    //  joinset[index] = find_root(next_index);
    //  return joinset[index];
  } else
    return index;
}

static bool exceptions(size_t addr, size_t threadId) { return (all_violations.count(addr) != 0); }

inline bool check_dead(size_t taskid, size_t clock) {
  bool ret = (taskid > 0 && taskid <= num_dead_tasks) || (taskid == 0 && clock <= dead_clock_value);
  bool ret1 = (taskid > 0 && taskid <= num_dead_tasks);
  bool ret2 = (taskid == 0 && clock <= dead_clock_value);
  //  if(ret1)
  //    std::cout << "ret1 true\n";
  //  if(ret2)
  //    std::cout << "ret2 true taskid " << taskid << " clock " << clock << " dead_clock_value " << dead_clock_value <<  "\n";
  //  if(ret)
  //    std::cout << "check_dead returning true\n";
  return ret;
}

#if LINE_NO_PASS
extern "C" void RecordMem(size_t threadId, void* access_addr, AccessType accesstype, int line_no) {
#else
extern "C" void RecordMem(size_t threadId, void* access_addr, AccessType accesstype) {
#endif

  ADDRINT addr = (ADDRINT)access_addr;
  //  if(exceptions(addr,threadId))return;

  //Shivam:This assert is failing, but it shoudln't
  //  assert(!cur[threadId].empty());
  if (cur[threadId].empty())
    return;

  uint32_t cur_taskId = cur[threadId].top();
  TaskState* cur_task = &tstate_nodes[cur_taskId];
  uint32_t cur_clock = cur_task->getCurClock();
  uint32_t cur_depth = cur_task->depth;
  uint32_t* cur_vc = cur_task->m_vc;

//  std::cout << "RecordMem addr " << addr << " taskid " << tid << " " ;
//  if(accesstype == READ) std::cout << "READ\n";
//  else std::cout << "WRITE\n";
//  size_t tid;
//  if(!cur[threadId].empty())
//    tid = cur[threadId].top();
//  else
//    tid = 0;
#if DEBUG_TIME
  HRTimer recordmem_time_start = HR::now();
  // my_getlock(&debug_lock);
  // HRTimer time1 = HR::now();
  // my_releaselock(&debug_lock);
#endif
#if STATS
  my_getlock(&globalStats.gs_lock);
  //    globalStats.maxnactive_tasks = std::max(globalStats.maxnactive_tasks, (uint32_t)taskid_map.size());
  globalStats.gs_num_recordmems++;
  if (accesstype == READ) {
    globalStats.gs_numrds++;
  } else {
    globalStats.gs_numwrs++;
  }
  my_releaselock(&globalStats.gs_lock);
#endif
  //  ac.release();

  size_t primary_index = (addr >> 22) & 0x3ff;
  PAIR* x = shadow_space + primary_index;

#if USE_PINLOCK
  PIN_GetLock(&(x->first), 0);
#else
  my_getlock(&(x->first));
#endif
  if (x->second == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(subpair);
    subpair* primary_entry =
        (subpair*)mmap(0, sec_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    x->second = primary_entry;
  }
#if USE_PINLOCK
  PIN_ReleaseLock(&(x->first));
#else
  my_releaselock(&(x->first));
#endif
  subpair* primary_entry = x->second;
  size_t offset = (addr & 0x3fffff);
  subpair* addrpair = primary_entry + offset;

#if USE_PINLOCK
  PIN_GetLock(&(addrpair->first), 0);
#else
  my_getlock(&(addrpair->first));
#endif

  //#if USE_PINLOCK
  //  PIN_ReleaseLock(&(addrpair->first));
  //#else
  //  my_releaselock(&(addrpair->first));
  //#endif
  //
  //  return;

  VarState* var_state = &(addrpair->second);
  uint32_t access_clock = 0;
  uint32_t access_taskId = 0;
  uint32_t access_depth = 0;
  TaskState* access_task = NULL;
  uint32_t* access_vc = NULL;

  if (var_state != NULL && var_state->isRacy()) {
#if USE_PINLOCK
    PIN_ReleaseLock(&(addrpair->first));
#else
    my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
    HRTimer recordmem_time_end = HR::now();
    my_getlock(&time_dr_detector.time_DR_detector_lock);
    time_dr_detector.total_recordmem_time +=
        duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
    my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
    return;
  }

  // #if DEBUG_TIME
  //   my_getlock(&debug_lock);
  //   HRTimer time2 = HR::now();
  //   recordmemi += duration_cast<nanoseconds>(time2 - time1).count();
  //   my_releaselock(&debug_lock);
  // #endif

  bool first_access = true;
  if (var_state != NULL && accesstype == READ) {
    LockState x;
    size_t curlockset = cur_task->lockset;
    uint32_t cursize = var_state->cursize;
    LockState* curLockState = NULL;
    first_access = true;
    for (int i = 0; i <= cursize; ++i) {
      x = (var_state->v)[i];
#if EPOCH_POINTER
      if (x.m_rd1_epoch != NULL) {
        first_access = false;
        break;
      }
#else
      if (!(check_dead(x.rtid1, x.rc1) && check_dead(x.rtid2, x.rc2) &&
            check_dead(x.wtid1, x.wc1) && check_dead(x.wtid2, x.wc2))) {
        // std::cout << "Read first_access false\n";
        first_access = false;
        break;
      }
#endif
    }
    if (first_access) {
      // std::cout << "FIRST Read Access\n";
      memset(var_state->v, 0, (var_state->cursize + 1) * sizeof(LockState));
      var_state->cursize = 0;
#if EPOCH_POINTER
      var_state->v[0].m_rd1_epoch = cur_task->epoch_ptr;
      var_state->v[0].m_rd2_epoch = empty_epoch;
      var_state->v[0].m_wr2_epoch = empty_epoch;
      var_state->v[0].m_wr1_epoch = empty_epoch;
#else
      var_state->v[0].rtid1 = cur_taskId;
      var_state->v[0].rc1 = cur_clock;
#endif

#if LINE_NO_PASS
      var_state->v[0].line_no_r1 = line_no;
#endif
      var_state->v[0].lockset = cur_task->lockset;
#if USE_PINLOCK
      PIN_ReleaseLock(&(addrpair->first));
#else
      my_releaselock(&(addrpair->first));
#endif
      return;
    }

    for (int i = 0; i <= cursize; ++i) {
      x = (var_state->v)[i];
      if (x.lockset == curlockset) {
        curLockState = &((var_state->v)[i]);
      }

      if ((x.lockset) & (curlockset))
        continue;

#if EPOCH_POINTER
      if (((x.getEpochTaskId(*(x.m_rd1_epoch))) == cur_taskId &&
           (x.getEpochClock(*(x.m_rd1_epoch))) == cur_clock) ||
          ((x.getEpochTaskId(*(x.m_rd2_epoch))) == cur_taskId &&
           (x.getEpochClock(*(x.m_rd2_epoch))) == cur_clock)) { //same epoch
#else
      if (((x.rtid1) == cur_taskId && (x.rc1) == cur_clock) ||
          ((x.rtid2) == cur_taskId && (x.rc2) == cur_clock)) { //same epoch
#endif

// #if DEBUG_TIME
//         my_getlock(&debug_lock);
//         HRTimer time3 = HR::now();
//         rd1 += duration_cast<nanoseconds>(time3 - time2).count();
//         my_releaselock(&debug_lock);
// #endif
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
        HRTimer recordmem_time_end = HR::now();
        my_getlock(&time_dr_detector.time_DR_detector_lock);
        time_dr_detector.total_recordmem_time +=
            duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
        my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
        return;
      }

#if EPOCH_POINTER
      if (x.getEpochClock(*(x.m_wr1_epoch)) > 0) {
        access_taskId = x.getEpochTaskId(*(x.m_wr1_epoch));
#else
      if (x.wc1 > 0) {
        access_taskId = x.wtid1;
#endif
        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        access_clock = 0;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par)
        //        //      access_clock = cur_vc[access_depth];
        //        //}
        //#if DEBUG_TIME
        //          HRTimer find_end = HR::now();
        //          my_getlock(&time_dr_detector.time_DR_detector_lock);
        //          time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
        //          time_dr_detector.num_vc_find += 1;
        //          // time_dr_detector.vc_find_map[(elem_task->m_vc).size()] += 1;
        //          my_releaselock(&time_dr_detector.time_DR_detector_lock);
        //#endif
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
#if EPOCH_POINTER
        if (x.getEpochClock(*(x.m_wr1_epoch)) > access_clock) {
#else
        if (x.wc1 > access_clock) {
#endif
          var_state->setRacy();
#if REPORT_DATA_RACES
#if EPOCH_POINTER
#if LINE_NO_PASS
          error(addr, x.getEpochTaskId(*(x.m_wr1_epoch)), WRITE, x.line_no_w1, cur_taskId, READ,
                line_no);
#else
          error(addr, x.getEpochTaskId(*(x.m_wr1_epoch)), WRITE, cur_taskId, READ);
#endif
#else
#if LINE_NO_PASS
          error(addr, x.wtid1, WRITE, x.line_no_w1, cur_taskId, READ, line_no);
#else
          error(addr, x.wtid1, WRITE, cur_taskId, READ);
#endif
#endif
#endif
// #if DEBUG_TIME
//           my_getlock(&debug_lock);
//           HRTimer time3 = HR::now();
//           rd1 += duration_cast<nanoseconds>(time3 - time2).count();
//           my_releaselock(&debug_lock);
// #endif
#if USE_PINLOCK
          PIN_ReleaseLock(&(addrpair->first));
#else
          my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
          HRTimer recordmem_time_end = HR::now();
          my_getlock(&time_dr_detector.time_DR_detector_lock);
          time_dr_detector.total_recordmem_time +=
              duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
          my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
          return;
        } // detecting write-read race
      }
#if EPOCH_POINTER
      if (x.getEpochClock(*(x.m_wr2_epoch)) > 0) {
        access_taskId = x.getEpochTaskId(*(x.m_wr2_epoch));
#else
      if (x.wc2 > 0) {
        access_taskId = x.wtid2;
#endif
        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        access_clock = 0;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par){
        //        //      access_clock = cur_vc[access_depth];
        //        //  }
        //        //}
        //#if DEBUG_TIME
        //          HRTimer find_end = HR::now();
        //          my_getlock(&time_dr_detector.time_DR_detector_lock);
        //          time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
        //          time_dr_detector.num_vc_find += 1;
        //          // time_dr_detector.vc_find_map[(elem_task->m_vc).size()] += 1;
        //          my_releaselock(&time_dr_detector.time_DR_detector_lock);
        //#endif
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
#if EPOCH_POINTER
        if (x.getEpochClock(*(x.m_wr2_epoch)) > access_clock) {
          var_state->setRacy();
#if REPORT_DATA_RACES
#if LINE_NO_PASS
          error(addr, x.getEpochTaskId(*(x.m_wr2_epoch)), WRITE, x.line_no_w2, cur_taskId, READ,
                line_no);
#else
          error(addr, x.getEpochTaskId(*(x.m_wr2_epoch)), WRITE, cur_taskId, READ);
#endif
#endif
#else
        if (x.wc2 > access_clock) {
          var_state->setRacy();
#if REPORT_DATA_RACES
#if LINE_NO_PASS
          error(addr, x.wtid2, WRITE, x.line_no_w2, cur_taskId, READ, line_no);
#else
          error(addr, x.wtid2, WRITE, cur_taskId, READ);
#endif
#endif
#endif
// #if DEBUG_TIME
//           my_getlock(&debug_lock);
//           HRTimer time3 = HR::now();
//           rd1 += duration_cast<nanoseconds>(time3 - time2).count();
//           my_releaselock(&debug_lock);
// #endif
#if USE_PINLOCK
          PIN_ReleaseLock(&(addrpair->first));
#else
          my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
          HRTimer recordmem_time_end = HR::now();
          my_getlock(&time_dr_detector.time_DR_detector_lock);
          time_dr_detector.total_recordmem_time +=
              duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
          my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
          return;
        } // detecting write-read race
      }
    }

    if (curLockState) {
      access_clock = 0;
#if EPOCH_POINTER
      if (curLockState->getEpochClock(*(curLockState->m_rd1_epoch)) > 0) {
        access_taskId = curLockState->getEpochTaskId(*(curLockState->m_rd1_epoch));
#else
      if (curLockState->rc1 > 0) {
        access_taskId = curLockState->rtid1;
#endif

        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par){
        //        //      access_clock = cur_vc[access_depth];
        //        //  }
        //        //}
        //#if DEBUG_TIME
        //          HRTimer find_end = HR::now();
        //          my_getlock(&time_dr_detector.time_DR_detector_lock);
        //          time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
        //          time_dr_detector.num_vc_find += 1;
        //          // time_dr_detector.vc_find_map[(elem_task->m_vc).size()] += 1;
        //          my_releaselock(&time_dr_detector.time_DR_detector_lock);
        //#endif
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
      }
#if EPOCH_POINTER
      if (curLockState->getEpochClock(*(curLockState->m_rd1_epoch)) <= access_clock) {
        curLockState->m_rd1_epoch = cur_task->epoch_ptr;
#else
      if (curLockState->rc1 <= access_clock) {
        curLockState->rtid1 = cur_taskId;
        curLockState->rc1 = cur_clock;
#endif
#if LINE_NO_PASS
        curLockState->line_no_r1 = line_no;
#endif

#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
        HRTimer recordmem_time_end = HR::now();
        my_getlock(&time_dr_detector.time_DR_detector_lock);
        time_dr_detector.total_recordmem_time +=
            duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
        my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
        return;
      }

      access_clock = 0;
#if EPOCH_POINTER
      if (curLockState->getEpochClock(*(curLockState->m_rd2_epoch)) > 0) {
        access_taskId = curLockState->getEpochTaskId(*(curLockState->m_rd2_epoch));
#else
      if (curLockState->rc2 > 0) {
        access_taskId = curLockState->rtid2;
#endif
        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par){
        //        //      access_clock = cur_vc[access_depth];
        //        //  }
        //        //}
        //#if DEBUG_TIME
        //          HRTimer find_end = HR::now();
        //          my_getlock(&time_dr_detector.time_DR_detector_lock);
        //          time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
        //          time_dr_detector.num_vc_find += 1;
        //          // time_dr_detector.vc_find_map[(elem_task->m_vc).size()] += 1;
        //          my_releaselock(&time_dr_detector.time_DR_detector_lock);
        //#endif
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
      }
#if EPOCH_POINTER
      if (curLockState->getEpochClock(*(curLockState->m_rd2_epoch)) <= access_clock) {
        curLockState->m_rd2_epoch = cur_task->epoch_ptr;
#else
      if (curLockState->rc2 <= access_clock) {
        curLockState->rtid2 = cur_taskId;
        curLockState->rc2 = cur_clock;
#endif
#if LINE_NO_PASS
        curLockState->line_no_r2 = line_no;
#endif
// #if DEBUG_TIME
//         my_getlock(&debug_lock);
//         HRTimer time3 = HR::now();
//         rd1 += duration_cast<nanoseconds>(time3 - time2).count();
//         my_releaselock(&debug_lock);
// #endif
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
        HRTimer recordmem_time_end = HR::now();
        my_getlock(&time_dr_detector.time_DR_detector_lock);
        time_dr_detector.total_recordmem_time +=
            duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
        my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
        return;
      }
#if EPOCH_POINTER
      uint32_t* vc1 = tstate_nodes[curLockState->getEpochTaskId(*(curLockState->m_rd1_epoch))].m_vc;
      uint32_t* vc2 = tstate_nodes[curLockState->getEpochTaskId(*(curLockState->m_rd2_epoch))].m_vc;
      uint32_t depth1 =
          tstate_nodes[curLockState->getEpochTaskId(*(curLockState->m_rd1_epoch))].depth;
      uint32_t depth2 =
          tstate_nodes[curLockState->getEpochTaskId(*(curLockState->m_rd2_epoch))].depth;

#else
      uint32_t* vc1 = tstate_nodes[curLockState->rtid1].m_vc;
      uint32_t* vc2 = tstate_nodes[curLockState->rtid2].m_vc;
      uint32_t depth1 = tstate_nodes[curLockState->rtid1].depth;
      uint32_t depth2 = tstate_nodes[curLockState->rtid2].depth;
#endif
      uint32_t min = std::min(std::min(depth1, depth2), cur_depth);
      int i = 0;
      while (i < min && vc1[i] == vc2[i] && vc1[i] == cur_vc[i]) {
        i++;
      }
      if (i == cur_depth || ((cur_vc[i] != vc1[i]) && (cur_vc[i] != vc2[i]))) {
#if EPOCH_POINTER
        curLockState->m_rd1_epoch = cur_task->epoch_ptr;
#else
        curLockState->rc1 = cur_clock;
        curLockState->rtid1 = cur_taskId;
#endif
#if LINE_NO_PASS
        curLockState->line_no_r1 = line_no;
#endif
      }
      // #if DEBUG_TIME
      //       my_getlock(&debug_lock);
      //       rd2 += duration_cast<nanoseconds>(HR::now() - time3).count();
      //       my_releaselock(&debug_lock);
      // #endif
    } else {
      curLockState = new LockState();
#if EPOCH_POINTER
      curLockState->m_rd1_epoch = cur_task->epoch_ptr;
#else
      curLockState->rc1 = cur_clock;
      curLockState->rtid1 = cur_taskId;
#endif
#if LINE_NO_PASS
      curLockState->line_no_r1 = line_no;
#endif
      curLockState->lockset = curlockset;
      (var_state->v)[var_state->cursize] = *curLockState;
      var_state->cursize++;
      // assert(var_state->size >= var_state->cursize);
    }
#if USE_PINLOCK
    PIN_ReleaseLock(&(addrpair->first));
#else
    my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
    HRTimer recordmem_time_end = HR::now();
    my_getlock(&time_dr_detector.time_DR_detector_lock);
    time_dr_detector.total_recordmem_time +=
        duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
    my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
    return;
  }

  if (var_state != NULL && accesstype == WRITE) {
    LockState x;
    size_t curlockset = cur_task->lockset;
    LockState* curLockState = NULL;
    int cursize = var_state->cursize;
    first_access = true;
    for (int i = 0; i <= cursize; ++i) {
      x = (var_state->v)[i];
#if EPOCH_POINTER
      if (x.m_rd1_epoch != NULL)
#else
      if (!(check_dead(x.rtid1, x.rc1) && check_dead(x.rtid2, x.rc2) &&
            check_dead(x.wtid1, x.wc1) && check_dead(x.wtid2, x.wc2)))
#endif
      {
        first_access = false;
        break;
      }
    }
    if (first_access) {
      memset(var_state->v, 0, (var_state->cursize + 1) * sizeof(LockState));
      var_state->cursize = 0;
#if EPOCH_POINTER
      var_state->v[0].m_rd1_epoch = empty_epoch;
      var_state->v[0].m_rd2_epoch = empty_epoch;
      var_state->v[0].m_wr2_epoch = empty_epoch;
      var_state->v[0].m_wr1_epoch = cur_task->epoch_ptr;
#else
      var_state->v[0].wtid1 = cur_taskId;
      var_state->v[0].wc1 = cur_clock;
#endif
#if LINE_NO_PASS
      var_state->v[0].line_no_w1 = line_no;
#endif
#if USE_PINLOCK
      PIN_ReleaseLock(&(addrpair->first));
#else
      my_releaselock(&(addrpair->first));
#endif
      return;
    }

    for (int i = 0; i <= cursize; ++i) {
      x = (var_state->v)[i];
      if (x.lockset == curlockset) {
        curLockState = &((var_state->v)[i]);
      }

      if ((x.lockset) & (curlockset))
        continue;
#if EPOCH_POINTER
      if (((x.getEpochTaskId(*(x.m_wr1_epoch))) == cur_taskId &&
           (x.getEpochClock(*(x.m_wr1_epoch))) == cur_clock) ||
          ((x.getEpochTaskId(*(x.m_wr2_epoch))) == cur_taskId &&
           (x.getEpochClock(*(x.m_wr2_epoch))) == cur_clock)) { //same epoch

#else
      if (((x.wtid1) == cur_taskId && (x.wc1) == cur_clock) ||
          ((x.wtid2) == cur_taskId && (x.wc2) == cur_clock)) { //same epoch
#endif
// #if DEBUG_TIME
//         my_getlock(&debug_lock);
//         wr1 += duration_cast<nanoseconds>(HR::now() - time4).count();
//         my_releaselock(&debug_lock);
// #endif
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
        HRTimer recordmem_time_end = HR::now();
        my_getlock(&time_dr_detector.time_DR_detector_lock);
        time_dr_detector.total_recordmem_time +=
            duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
        my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
        return;
      }
#if EPOCH_POINTER
      if (x.getEpochClock(*(x.m_wr1_epoch)) > 0) {
        access_taskId = x.getEpochTaskId(*(x.m_wr1_epoch));
#else
      if (x.wc1 > 0) {
        access_taskId = x.wtid1;
#endif
        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        access_clock = 0;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par)
        //        //      access_clock = cur_vc[access_depth];
        //        //}
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
#if EPOCH_POINTER
        if (x.getEpochClock(*(x.m_wr1_epoch)) > access_clock) {
#else
        if (x.wc1 > access_clock) {
#endif
          var_state->setRacy();

#if REPORT_DATA_RACES
#if EPOCH_POINTER
#if LINE_NO_PASS
          error(addr, x.getEpochTaskId(*(x.m_wr1_epoch)), WRITE, x.line_no_w1, cur_taskId, WRITE,
                line_no);
#else
          error(addr, x.getEpochTaskId(*(x.m_wr1_epoch)), WRITE, cur_taskId, WRITE);
#endif
#else
#if LINE_NO_PASS
          error(addr, x.wtid1, WRITE, x.line_no_w1, cur_taskId, WRITE, line_no);
#else
          error(addr, x.wtid1, WRITE, cur_taskId, WRITE);
#endif
#endif
#endif

#if USE_PINLOCK
          PIN_ReleaseLock(&(addrpair->first));
#else
          my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
          HRTimer recordmem_time_end = HR::now();
          my_getlock(&time_dr_detector.time_DR_detector_lock);
          time_dr_detector.total_recordmem_time +=
              duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
          my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
          return;
        } // detecting write-write race
      }
#if EPOCH_POINTER
      if (x.getEpochClock(*(x.m_wr2_epoch)) > 0) {
        access_taskId = x.getEpochTaskId(*(x.m_wr2_epoch));
#else
      if (x.wc2 > 0) {
        access_taskId = x.wtid2;
#endif
        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        access_clock = 0;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par)
        //        //      access_clock = cur_vc[access_depth];
        //        //}
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
#if EPOCH_POINTER
        if (x.getEpochClock(*(x.m_wr2_epoch)) > access_clock) {
#else
        if (x.wc2 > access_clock) {
#endif
          var_state->setRacy();
#if REPORT_DATA_RACES
#if EPOCH_POINTER
#if LINE_NO_PASS
          error(addr, x.getEpochTaskId(*(x.m_wr2_epoch)), WRITE, x.line_no_w2, cur_taskId, WRITE,
                line_no);
#else
          error(addr, x.getEpochTaskId(*(x.m_wr2_epoch)), WRITE, cur_taskId, WRITE);
#endif
#else
#if LINE_NO_PASS
          error(addr, x.wtid2, WRITE, x.line_no_w2, cur_taskId, WRITE, line_no);
#else
          error(addr, x.wtid2, WRITE, cur_taskId, WRITE);
#endif
#endif
#endif

#if USE_PINLOCK
          PIN_ReleaseLock(&(addrpair->first));
#else
          my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
          HRTimer recordmem_time_end = HR::now();
          my_getlock(&time_dr_detector.time_DR_detector_lock);
          time_dr_detector.total_recordmem_time +=
              duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
          my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
          return;
        } // detecting write-write race
      }
#if EPOCH_POINTER
      if (x.getEpochClock(*(x.m_rd1_epoch)) > 0) {
        access_taskId = x.getEpochTaskId(*(x.m_rd1_epoch));
#else
      if (x.rc1 > 0) {
        access_taskId = x.rtid1;
#endif
        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        access_clock = 0;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par)
        //        //      access_clock = cur_vc[access_depth];
        //        //}
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
#if EPOCH_POINTER
        if (x.getEpochClock(*(x.m_rd1_epoch)) > access_clock) {
#else
        if (x.rc1 > access_clock) {
#endif
          var_state->setRacy();
#if REPORT_DATA_RACES
#if EPOCH_POINTER
#if LINE_NO_PASS
          error(addr, x.getEpochTaskId(*(x.m_rd1_epoch)), READ, x.line_no_r1, cur_taskId, WRITE,
                line_no);
#else
          error(addr, x.getEpochTaskId(*(x.m_rd1_epoch)), READ, cur_taskId, WRITE);
#endif
#else
#if LINE_NO_PASS
          error(addr, x.rtid1, READ, x.line_no_r1, cur_taskId, WRITE, line_no);
#else
          error(addr, x.rtid1, READ, cur_taskId, WRITE);
#endif
#endif
#endif
// #if DEBUG_TIME
//           my_getlock(&debug_lock);
//           wr1 += duration_cast<nanoseconds>(HR::now() - time4).count();
//           my_releaselock(&debug_lock);
// #endif
#if USE_PINLOCK
          PIN_ReleaseLock(&(addrpair->first));
#else
          my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
          HRTimer recordmem_time_end = HR::now();
          my_getlock(&time_dr_detector.time_DR_detector_lock);
          time_dr_detector.total_recordmem_time +=
              duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
          my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
          return;
        } // detecting Read-write race
      }
#if EPOCH_POINTER
      if (x.getEpochClock(*(x.m_rd2_epoch)) > 0) {
        access_taskId = x.getEpochTaskId(*(x.m_rd2_epoch));
#else
      if (x.rc2 > 0) {
        access_taskId = x.rtid2;
#endif
        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        access_clock = 0;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par)
        //        //      access_clock = cur_vc[access_depth];
        //        //}
        //#if DEBUG_TIME
        //          HRTimer find_end = HR::now();
        //          my_getlock(&time_dr_detector.time_DR_detector_lock);
        //          time_dr_detector.vc_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
        //          time_dr_detector.num_vc_find += 1;
        //          // time_dr_detector.vc_find_map[(elem_task->m_vc).size()] += 1;
        //          my_releaselock(&time_dr_detector.time_DR_detector_lock);
        //#endif
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
#if EPOCH_POINTER
        if (x.getEpochClock(*(x.m_rd2_epoch)) > access_clock) {
#else
        if (x.rc2 > access_clock) {
#endif
          var_state->setRacy();
#if REPORT_DATA_RACES
#if EPOCH_POINTER
#if LINE_NO_PASS
          error(addr, x.getEpochTaskId(*(x.m_rd2_epoch)), READ, x.line_no_r2, cur_taskId, WRITE,
                line_no);
#else
          error(addr, x.getEpochTaskId(*(x.m_rd2_epoch)), READ, cur_taskId, WRITE);
#endif
#else
#if LINE_NO_PASS
          error(addr, x.rtid2, READ, x.line_no_r2, cur_taskId, WRITE, line_no);
#else
          error(addr, x.rtid2, READ, cur_taskId, WRITE);
#endif
#endif
#endif
// #if DEBUG_TIME
//           my_getlock(&debug_lock);
//           wr1 += duration_cast<nanoseconds>(HR::now() - time4).count();
//           my_releaselock(&debug_lock);
// #endif
#if USE_PINLOCK
          PIN_ReleaseLock(&(addrpair->first));
#else
          my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
          HRTimer recordmem_time_end = HR::now();
          my_getlock(&time_dr_detector.time_DR_detector_lock);
          time_dr_detector.total_recordmem_time +=
              duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
          my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
          return;
        } // detecting Read-write race
      }
    }

    if (curLockState) {
      access_clock = 0;
#if EPOCH_POINTER
      if (curLockState->getEpochClock(*(curLockState->m_wr1_epoch)) > 0) {
        access_taskId = curLockState->getEpochTaskId(*(curLockState->m_wr1_epoch));
#else
      if (curLockState->wc1 > 0) {
        access_taskId = curLockState->wtid1;
#endif
        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par){
        //        //      access_clock = cur_vc[access_depth];
        //        //  }
        //        //}
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
      }
#if EPOCH_POINTER
      if (curLockState->getEpochClock(*(curLockState->m_wr1_epoch)) <= access_clock) {
        curLockState->m_wr1_epoch = cur_task->epoch_ptr;
#else
      if (curLockState->wc1 <= access_clock) {
        curLockState->wtid1 = cur_taskId;
        curLockState->wc1 = cur_clock;
#endif
#if LINE_NO_PASS
        curLockState->line_no_w1 = line_no;
#endif
// #if DEBUG_TIME
//         my_getlock(&debug_lock);
//         wr1 += duration_cast<nanoseconds>(HR::now() - time4).count();
//         my_releaselock(&debug_lock);
// #endif
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
        HRTimer recordmem_time_end = HR::now();
        my_getlock(&time_dr_detector.time_DR_detector_lock);
        time_dr_detector.total_recordmem_time +=
            duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
        my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
        return;
      }
      access_clock = 0;
#if EPOCH_POINTER
      if (curLockState->getEpochClock(*(curLockState->m_wr2_epoch)) > 0) {
        access_taskId = curLockState->getEpochTaskId(*(curLockState->m_wr2_epoch));
#else
      if (curLockState->wc2 > 0) {
        access_taskId = curLockState->wtid2;
#endif
        access_vc = tstate_nodes[access_taskId].m_vc;
        access_depth = tstate_nodes[access_taskId].depth;
        if (cur_task->getTaskId() == access_taskId)
          access_clock = cur_task->getCurClock();
        //        else if(check_parent(cur_task,access_taskId))
        //          access_clock = get_clock(cur_task,access_taskId);
        //        //else if(access_depth < cur_depth) {
        //#if DEBU//G_TIME
        //        //  HRTimer find_start = HR::now();
        //#endif
        //        //  bool check_par = true;
        //        //  for(int i=0;i<access_depth;i++){
        //        //    if(access_vc[i] ^ cur_vc[i] != 0){
        //        //      check_par = false;break;
        //        //    }
        //        //  }
        //        //  if(check_par){
        //        //      access_clock = cur_vc[access_depth];
        //        //  }
        //        //}
        //            else{
        else if ((access_clock = parent_clock(cur_task, access_taskId)) == 0) {
#if JOINSET
          uint32_t root_tid = find_root(access_taskId + 1) - 1;
          uint32_t* root_vc = tstate_nodes[root_tid].m_vc;
          uint32_t root_depth = tstate_nodes[root_tid].depth;

          if (root_tid == cur_taskId || parent_clock(cur_task, root_tid))
            access_clock = INT_MAX;
//            else if(check_parent(cur_task,root_tid))
//              access_clock = INT_MAX;
//else if(root_depth < cur_depth){
//  bool check_par = true;
//  for(int i=0;i<root_depth;i++){
//    if(root_vc[i] ^ cur_vc[i] != 0){
//      check_par = false;break;
//    }
//  }
//  if(check_par)
//    access_clock = INT_MAX;
//}
#endif
        }
      }
#if EPOCH_POINTER
      if (curLockState->getEpochClock(*(curLockState->m_wr2_epoch)) <= access_clock) {
        curLockState->m_wr2_epoch = cur_task->epoch_ptr;
#else
      if (curLockState->wc2 <= access_clock) {
        curLockState->wtid2 = cur_taskId;
        curLockState->wc2 = cur_clock;
#endif
#if LINE_NO_PASS
        curLockState->line_no_w2 = line_no;
#endif
// #if DEBUG_TIME
//         my_getlock(&debug_lock);
//         wr1 += duration_cast<nanoseconds>(HR::now() - time4).count();
//         my_releaselock(&debug_lock);
// #endif
#if USE_PINLOCK
        PIN_ReleaseLock(&(addrpair->first));
#else
        my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
        HRTimer recordmem_time_end = HR::now();
        my_getlock(&time_dr_detector.time_DR_detector_lock);
        time_dr_detector.total_recordmem_time +=
            duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
        my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
        return;
      }

#if EPOCH_POINTER
      uint32_t* vc1 = tstate_nodes[curLockState->getEpochTaskId(*(curLockState->m_wr1_epoch))].m_vc;
      uint32_t* vc2 = tstate_nodes[curLockState->getEpochTaskId(*(curLockState->m_wr2_epoch))].m_vc;
      uint32_t depth1 =
          tstate_nodes[curLockState->getEpochTaskId(*(curLockState->m_wr1_epoch))].depth;
      uint32_t depth2 =
          tstate_nodes[curLockState->getEpochTaskId(*(curLockState->m_wr2_epoch))].depth;
#else
      uint32_t* vc1 = tstate_nodes[curLockState->wtid1].m_vc;
      uint32_t* vc2 = tstate_nodes[curLockState->wtid2].m_vc;
      uint32_t depth1 = tstate_nodes[curLockState->wtid1].depth;
      uint32_t depth2 = tstate_nodes[curLockState->wtid2].depth;
#endif
      uint32_t min = std::min(std::min(depth1, depth2), cur_depth);
      int i = 0;
      while (i < min && vc1[i] == vc2[i] && vc1[i] == cur_vc[i]) {
        i++;
      }

      if (i == cur_depth || ((cur_vc[i] != vc1[i]) && (cur_vc[i] != vc2[i]))) {
#if EPOCH_POINTER
        curLockState->m_wr1_epoch = cur_task->epoch_ptr;
#else
        curLockState->wc1 = cur_clock;
        curLockState->wtid1 = cur_taskId;
#endif
#if LINE_NO_PASS
        curLockState->line_no_w1 = line_no;
#endif
      }
      // #if DEBUG_TIME
      //       my_getlock(&debug_lock);
      //       wr2 += duration_cast<nanoseconds>(HR::now() - time5).count();
      //       my_releaselock(&debug_lock);
      // #endif

    } else {
      curLockState = new LockState();
#if EPOCH_POINTER
      curLockState->m_wr1_epoch = cur_task->epoch_ptr;
#else
      curLockState->wc1 = cur_clock;
      curLockState->wtid1 = cur_taskId;
#endif
#if LINE_NO_PASS
      curLockState->line_no_w1 = line_no;
#endif
      curLockState->lockset = curlockset;
      (var_state->v)[var_state->cursize] = *curLockState;
      var_state->cursize++;
      //assert(var_state->size >= var_state->cursize);
    }
#if USE_PINLOCK
    PIN_ReleaseLock(&(addrpair->first));
#else
    my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
    HRTimer recordmem_time_end = HR::now();
    my_getlock(&time_dr_detector.time_DR_detector_lock);
    time_dr_detector.total_recordmem_time +=
        duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
    my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
    return;
  }

  // Uncommon case
  if (var_state == NULL && accesstype == READ) {
    var_state = new VarState();
    LockState* l = (var_state->v);
    l->lockset = cur_task->lockset;
#if EPOCH_POINTER
    l->m_rd1_epoch = cur_task->epoch_ptr;
#else
    l->rc1 = cur_clock;
    l->rtid1 = cur_taskId;
#endif
#if LINE_NO_PASS
    l->line_no_r1 = line_no;
#endif
    //  addrpair->second = var_state;
#if USE_PINLOCK
    PIN_ReleaseLock(&(addrpair->first));
#else
    my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
    HRTimer recordmem_time_end = HR::now();
    my_getlock(&time_dr_detector.time_DR_detector_lock);
    time_dr_detector.total_recordmem_time +=
        duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
    my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
    return;
  }

  if (var_state == NULL && accesstype == WRITE) {
    var_state = new VarState();
    LockState* l = var_state->v;
    l->lockset = cur_task->lockset;
#if EPOCH_POINTER
    l->m_wr1_epoch = cur_task->epoch_ptr;
#else
    l->wc1 = cur_clock;
    l->wtid1 = cur_taskId;
#endif
#if LINE_NO_PASS
    l->line_no_w1 = line_no;
#endif
#if USE_PINLOCK
    PIN_ReleaseLock(&(addrpair->first));
#else
    my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
    HRTimer recordmem_time_end = HR::now();
    my_getlock(&time_dr_detector.time_DR_detector_lock);
    time_dr_detector.total_recordmem_time +=
        duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
    my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
    return;
  }

#if USE_PINLOCK
  PIN_ReleaseLock(&(addrpair->first));
#else
  my_releaselock(&(addrpair->first));
#endif
#if DEBUG_TIME
  HRTimer recordmem_time_end = HR::now();
  my_getlock(&time_dr_detector.time_DR_detector_lock);
  time_dr_detector.total_recordmem_time +=
      duration_cast<nanoseconds>(recordmem_time_end - recordmem_time_start).count();
  my_releaselock(&time_dr_detector.time_DR_detector_lock);
#endif
  return;
}

#if LINE_NO_PASS
extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype, int line_no) {
  RecordMem(tid, access_addr, accesstype);
}
#else
extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype) {
  RecordMem(tid, access_addr, accesstype);
}
#endif

void CaptureLockAcquire(size_t threadId, ADDRINT lock_addr) {
  assert(!cur[threadId].empty());
  size_t tid = cur[threadId].top();
  TaskState* t = &tstate_nodes[tid];
  //  size_t tid = cur[threadId].top();

  //  concurrent_hash_map<size_t, TaskState*>::accessor ac;
  //#if DEBUG_TIME
  //  HRTimer find_start = HR::now();
  //#endif
  //  taskid_map.find(ac, tid);
  //#if DEBUG_TIME
  //  HRTimer find_end = HR::now();
  //  my_getlock(&time_dr_detector.time_DR_detector_lock);
  //  time_dr_detector.taskid_map_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
  //  time_dr_detector.num_tid_find += 1;
  //  // time_dr_detector.tid_find_map[taskid_map.size()] += 1;
  //  my_releaselock(&time_dr_detector.time_DR_detector_lock);
  //#endif
  //  TaskState* t = ac->second;
  //  ac.release();

  size_t lockId;
  my_getlock(&lock_map_lock);
  if (lock_map.count(lock_addr) == 0) {
    lockId = ((size_t)1 << lock_ticker++);
    lock_map.insert(std::pair<ADDRINT, size_t>(lock_addr, lockId));
  } else {
    lockId = lock_map.at(lock_addr);
  }
  my_releaselock(&lock_map_lock);
  t->lockset = t->lockset | lockId;
}

void CaptureLockRelease(size_t threadId, ADDRINT lock_addr) {
  assert(!cur[threadId].empty());
  size_t tid = cur[threadId].top();
  TaskState* t = &tstate_nodes[tid];

  //  concurrent_hash_map<size_t, TaskState*>::accessor ac;
  //#if DEBUG_TIME
  //  HRTimer find_start = HR::now();
  //#endif
  //  taskid_map.find(ac, tid);
  //#if DEBUG_TIME
  //  HRTimer find_end = HR::now();
  //  my_getlock(&time_dr_detector.time_DR_detector_lock);
  //  time_dr_detector.taskid_map_find_time += duration_cast<nanoseconds>(find_end - find_start).count();
  //  time_dr_detector.num_tid_find += 1;
  //  // time_dr_detector.tid_find_map[taskid_map.size()] += 1;
  //  my_releaselock(&time_dr_detector.time_DR_detector_lock);
  //#endif
  //  TaskState* t = ac->second;
  //  ac.release();
  //
  size_t lockId;
  my_getlock(&lock_map_lock);
  if (lock_map.count(lock_addr) == 0) {
    lockId = ((size_t)1 << lock_ticker++);
    lock_map.insert(std::pair<ADDRINT, size_t>(lock_addr, lockId));
  } else {
    lockId = lock_map.at(lock_addr);
  }
  my_releaselock(&lock_map_lock);
  t->lockset = t->lockset & (~(lockId));
}

void paccesstype(AccessType accesstype) {
  if (accesstype == READ)
    report << "READ";
  if (accesstype == WRITE)
    report << "WRITE";
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
#if LINE_NO_PASS
    report << " line_no " << viol->a1->line_no << "\n";
#else
    report << "\n";
#endif
    report << viol->a2->tid;
    paccesstype(viol->a2->accessType);
#if LINE_NO_PASS
    report << " line_no " << viol->a2->line_no << "\n";
#else
    report << "\n";
#endif
    report << "**************************************\n";
  }
  report.close();
#endif

#if STATS
  globalStats.dump();
  std::cout << "STATS Mode\n";
#endif
  std::cout << "Number of violations = " << all_violations.size() << std::endl;
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
