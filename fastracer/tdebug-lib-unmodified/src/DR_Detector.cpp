#include "DR_Detector.H"
#include "Common.H"
#include "exec_calls.h"
#include <bitset>
#include <iostream>
#include <sys/mman.h>

using std::endl;

extern PIN_LOCK lock;
extern std::stack<size_t> tidToTaskIdMap[NUM_THREADS];

#define USE_PINLOCK 1

//array of stacks of lockset_data
std::stack<struct Lockset_data*> thd_lockset[NUM_THREADS];

// 2^10 entries each will be 8 bytes each
const size_t SS_PRIMARY_TABLE_ENTRIES = ((size_t)1024);

// each secondary entry has 2^ 22 entries (64 bytes each)
const size_t SS_SEC_TABLE_ENTRIES = ((size_t)4 * (size_t)1024 * (size_t)1024);
#if USE_PINLOCK
typedef std::pair<PIN_LOCK, struct Dr_Address_Data*> PAIR;
#else
typedef std::pair<tbb::atomic<size_t>, struct Dr_Address_Data*> PAIR;
#endif


PAIR* shadow_space;
//struct Dr_Address_Data** shadow_space;
//PIN_LOCK lock;
#if REPORT_DATA_RACES
std::ofstream report;
#endif
my_lock viol_lock;
std::map<ADDRINT, struct violation*> all_violations;

#if DEBUG_TIME
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;
my_lock debug_lock(0);
unsigned recordmemt = 0, recordmemn = 0, recordaccesst = 0, recordaccessn = 0,spawnt=0,spawn_roott=0,spawn_waitt=0,waitt=0;
#endif

#if STATS
GlobalStats globalStats;
#endif

#if DEBUG
my_lock printLock(0);
#endif

extern "C" void TD_Activate() {
#if 0
  std::cout << "Address data size = " << sizeof(struct Dr_Address_Data)
            << " addr_data = " << sizeof(struct Address_data) << std::endl;
#endif
  taskGraph = new AFTaskGraph();
  thd_lockset[0].push(new Lockset_data());
  size_t primary_length = (SS_PRIMARY_TABLE_ENTRIES) * sizeof(PAIR);
  shadow_space = (PAIR*)mmap(0, primary_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
  assert(shadow_space != (void*)-1);
}

extern "C" void CaptureLockAcquire(THREADID threadid, ADDRINT lock_addr) {
  PIN_GetLock(&lock, 0);
  struct Lockset_data* curLockset = thd_lockset[threadid].top();
  curLockset->addLockToLockset(lock_addr);
  PIN_ReleaseLock(&lock);

#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.total_num_lock_acqs++;
  globalStats.track_acq(tidToTaskIdMap[threadid].top(), lock_addr);
  my_releaselock(&globalStats.gs_lock);
#endif
}

extern "C" void CaptureLockRelease(THREADID threadid, ADDRINT lock_addr) {
  PIN_GetLock(&lock, 0);
  struct Lockset_data* curLockset = thd_lockset[threadid].top();
  curLockset->removeLockFromLockset();
  PIN_ReleaseLock(&lock);

#if STATS
  my_getlock(&(globalStats.gs_lock));
  globalStats.total_num_lock_rels++;
  globalStats.track_rel(tidToTaskIdMap[threadid].top(), lock_addr);
  my_releaselock(&globalStats.gs_lock);
#endif
}

void CaptureExecute(THREADID threadid) {
  PIN_GetLock(&lock, 0);

  thd_lockset[threadid].push(new Lockset_data());

  PIN_ReleaseLock(&lock);
}

void CaptureReturn(THREADID threadid) {
  PIN_GetLock(&lock, 0);

  struct Lockset_data* curLockset = thd_lockset[threadid].top();
  thd_lockset[threadid].pop();
  delete (curLockset);

  PIN_ReleaseLock(&lock);
}

static bool exceptions(THREADID threadid, ADDRINT addr) {
  return (tidToTaskIdMap[threadid].empty() || tidToTaskIdMap[threadid].top() == 0 ||
          thd_lockset[threadid].empty() || all_violations.count(addr) != 0);
}

#if LINE_NO_PASS
extern "C" void RecordAccess(THREADID threadid, void* access_addr, ADDRINT* locks_acq,
                             size_t locks_acq_size, ADDRINT* locks_rel, size_t locks_rel_size,
                             AccessType accessType, int line_no) {
#else
extern "C" void RecordAccess(THREADID threadid, void* access_addr, ADDRINT* locks_acq,
                             size_t locks_acq_size, ADDRINT* locks_rel, size_t locks_rel_size,
                             AccessType accessType) {
#endif


#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.total_num_recordaccess++;
  my_releaselock(&globalStats.gs_lock);
#endif

#if DEBUG_TIME
  my_getlock(&debug_lock);
  std::cout << "In recordaccess\n";
  recordaccessn++;
  my_releaselock(&debug_lock);
  unsigned time0 = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  unsigned time1;
#endif

  // Exceptions
  ADDRINT addr = (ADDRINT)access_addr;
  if (exceptions(threadid, addr)) {
    //PIN_ReleaseLock(&lock);
#if DEBUG_TIME
    time1 = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
    my_getlock(&debug_lock);
    recordaccesst += time1 - time0;
    my_releaselock(&debug_lock);
#endif
    return;
  }

  //get lockset and current step node
  size_t curLockset = (thd_lockset[threadid].top())->createLockset();

  /////////// extra for adding lockset /////////////////////////

  for (size_t i = 0; i < locks_acq_size; i++) {
    curLockset = curLockset & Lockset_data::getLockId(locks_acq[i]);
  }

  for (size_t i = 0; i < locks_rel_size; i++) {
    curLockset = curLockset | ~(Lockset_data::getLockId(locks_rel[i]));
  }

  /////////////////////////////////////////////////////////////

  struct AFTask* curStepNode = taskGraph->getCurTask(threadid);

  /////////////////////////////////////////////////////
  // check for access pattern and update shadow space

  size_t primary_index = (addr >> 22) & 0x3ff;
  PAIR* x = shadow_space + primary_index;
#if USE_PINLOCK
  PIN_GetLock(&(x->first),0);
#else
  my_getlock(&(x->first));
#endif
  //  struct Dr_Address_Data* primary_entry = shadow_space[primary_index];

  if (x->second == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(struct Dr_Address_Data);
    struct Dr_Address_Data* primary_entry =
        (struct Dr_Address_Data*)mmap(0, sec_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    x->second = primary_entry;

    //initialize all locksets to 0xffffffff
    // for (size_t i = 0; i< SS_SEC_TABLE_ENTRIES;i++) {
    //   struct Dr_Address_Data& dr_address_data = primary_entry[i];
    //   for (int j = 0 ; j < NUM_FIXED_ENTRIES ; j++) {
    // 	(dr_address_data.f_entries[j]).lockset = 0xffffffff;
    //   }
    // }
  }
#if USE_PINLOCK
  PIN_ReleaseLock(&(x->first));
#else
  my_releaselock(&(x->first));
#endif

  struct Dr_Address_Data* primary_entry = x->second;
  size_t offset = (addr)&0x3fffff;
  struct Dr_Address_Data* dr_address_data = primary_entry + offset;

  PIN_GetLock(&dr_address_data->addr_lock, 0);

  if (dr_address_data == NULL) {
    // first access to the location. add access history to shadow space
    if (accessType == READ) {
      (dr_address_data->f_entries[0]).lockset = curLockset;
      (dr_address_data->f_entries[0]).r1_task = curStepNode;
#if LINE_NO_PASS
      (dr_address_data->f_entries[0]).line_no_r1 = line_no;
#endif
    } else {
      (dr_address_data->f_entries[0]).lockset = curLockset;
      (dr_address_data->f_entries[0]).w1_task = curStepNode;
#if LINE_NO_PASS
      (dr_address_data->f_entries[0]).line_no_w1 = line_no;
#endif
    }
  } else {
    // check for data race with each access history entry

    bool race_detected = false;
    int f_insert_index = -1;

    for (int i = 0; i < NUM_FIXED_ENTRIES; i++) {
      if (race_detected && f_insert_index != -1)
        break;

      struct Address_data& f_entry = dr_address_data->f_entries[i];

      if (f_insert_index == -1 && f_entry.lockset == 0) {
        f_insert_index = i;
        break;
      }

      if ((f_entry.lockset != 0) && (((~curLockset) & (~f_entry.lockset)) == 0)) {
        //check for data race
        if (f_entry.w1_task != NULL &&
            taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.w1_task, threadid)) {
          my_getlock(&viol_lock);
#if LINE_NO_PASS
          all_violations.insert(std::pair<ADDRINT, struct violation*>(
              addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                  new violation_data(f_entry.w1_task, WRITE, f_entry.line_no_w1))));
#else
          all_violations.insert(std::pair<ADDRINT, struct violation*>(
              addr, new violation(new violation_data(curStepNode, accessType),
                                  new violation_data(f_entry.w1_task, WRITE))));
#endif
          my_releaselock(&viol_lock);
          race_detected = true;
          break;
        }
        if (f_entry.w2_task != NULL &&
            taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.w2_task, threadid)) {
          my_getlock(&viol_lock);
#if LINE_NO_PASS
          all_violations.insert(std::pair<ADDRINT, struct violation*>(
              addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                  new violation_data(f_entry.w2_task, WRITE, f_entry.line_no_w2))));
#else
          all_violations.insert(std::pair<ADDRINT, struct violation*>(
              addr, new violation(new violation_data(curStepNode, accessType),
                                  new violation_data(f_entry.w2_task, WRITE))));
#endif
          my_releaselock(&viol_lock);
          race_detected = true;
          break;
        }
        if (accessType == WRITE) {
          if (f_entry.r1_task != NULL &&
              taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.r1_task, threadid)) {
            my_getlock(&viol_lock);
#if LINE_NO_PASS
          all_violations.insert(std::pair<ADDRINT, struct violation*>(
              addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                  new violation_data(f_entry.r1_task, READ, f_entry.line_no_r1))));
#else
            all_violations.insert(std::pair<ADDRINT, struct violation*>(
                addr, new violation(new violation_data(curStepNode, accessType),
                                    new violation_data(f_entry.r1_task, READ))));
#endif
            my_releaselock(&viol_lock);
            race_detected = true;
            break;
          }
          if (f_entry.r2_task != NULL &&
              taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.r2_task, threadid)) {
            my_getlock(&viol_lock);
#if LINE_NO_PASS
            all_violations.insert(std::pair<ADDRINT, struct violation*>(
                addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                    new violation_data(f_entry.r2_task, READ, f_entry.line_no_r2))));
#else
            all_violations.insert(std::pair<ADDRINT, struct violation*>(
                addr, new violation(new violation_data(curStepNode, accessType),
                                    new violation_data(f_entry.r2_task, READ))));
#endif
            my_releaselock(&viol_lock);
            race_detected = true;
            break;
          }
        }
      }
      if (f_entry.lockset != 0 && curLockset == f_entry.lockset) {
        f_insert_index = i;
      }
    }

    if (!race_detected) {
      int insert_index = -1;

      std::vector<struct Address_data>* access_list = dr_address_data->access_list;

      if (access_list != NULL) {
        for (std::vector<struct Address_data>::iterator it = access_list->begin();
             it != access_list->end(); ++it) {
          struct Address_data& add_data = *it;
          //check if intersection of lockset is empty
          if (((~curLockset) & (~add_data.lockset)) == 0) {
            //check for data race
            if (add_data.w1_task != NULL && taskGraph->areParallel(tidToTaskIdMap[threadid].top(),
                                                                   add_data.w1_task, threadid)) {
              my_getlock(&viol_lock);
#if LINE_NO_PASS
              all_violations.insert(std::pair<ADDRINT, struct violation*>(
                  addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                      new violation_data(add_data.w1_task, WRITE, add_data.line_no_w1))));
#else
              all_violations.insert(std::pair<ADDRINT, struct violation*>(
                  addr, new violation(new violation_data(curStepNode, accessType),
                                      new violation_data(add_data.w1_task, WRITE))));
#endif
              my_releaselock(&viol_lock);
              race_detected = true;
              break;
            }
            if (add_data.w2_task != NULL && taskGraph->areParallel(tidToTaskIdMap[threadid].top(),
                                                                   add_data.w2_task, threadid)) {
              my_getlock(&viol_lock);
#if LINE_NO_PASS
              all_violations.insert(std::pair<ADDRINT, struct violation*>(
                  addr, new violation(new violation_data(curStepNode, accessType,line_no),
                                      new violation_data(add_data.w2_task, WRITE,add_data.line_no_w2))));
#else
              all_violations.insert(std::pair<ADDRINT, struct violation*>(
                  addr, new violation(new violation_data(curStepNode, accessType),
                                      new violation_data(add_data.w2_task, WRITE))));
#endif
              my_releaselock(&viol_lock);
              race_detected = true;
              break;
            }
            if (accessType == WRITE) {
              if (add_data.r1_task != NULL && taskGraph->areParallel(tidToTaskIdMap[threadid].top(),
                                                                     add_data.r1_task, threadid)) {
                my_getlock(&viol_lock);
#if LINE_NO_PASS
                all_violations.insert(std::pair<ADDRINT, struct violation*>(
                    addr, new violation(new violation_data(curStepNode, accessType,line_no),
                                        new violation_data(add_data.r1_task, READ,add_data.line_no_r1))));
#else
                all_violations.insert(std::pair<ADDRINT, struct violation*>(
                    addr, new violation(new violation_data(curStepNode, accessType),
                                        new violation_data(add_data.r1_task, READ))));
#endif
                my_releaselock(&viol_lock);
                race_detected = true;
                break;
              }
              if (add_data.r2_task != NULL && taskGraph->areParallel(tidToTaskIdMap[threadid].top(),
                                                                     add_data.r2_task, threadid)) {
                my_getlock(&viol_lock);
#if LINE_NO_PASS
                all_violations.insert(std::pair<ADDRINT, struct violation*>(
                    addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                        new violation_data(add_data.r2_task, READ, add_data.line_no_r2))));
#else
                all_violations.insert(std::pair<ADDRINT, struct violation*>(
                    addr, new violation(new violation_data(curStepNode, accessType),
                                        new violation_data(add_data.r2_task, READ))));
#endif
                my_releaselock(&viol_lock);
                race_detected = true;
                break;
              }
            }
          }
          if (curLockset == add_data.lockset) {
            insert_index = it - access_list->begin();
          }
        }
      }

      if (!race_detected) {
        if (f_insert_index != -1) {
          struct Address_data& f_entry = dr_address_data->f_entries[f_insert_index];
          if (f_entry.lockset == 0) {
            f_entry.lockset = curLockset;
            if (accessType == READ) {
              f_entry.r1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_r1 = line_no;
#endif
            } else {
              f_entry.w1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_w1 = line_no;
#endif
            }
          } else {
            if (accessType == WRITE) {
              //f_entry.wr_task = taskGraph->rightmostNode(curStepNode, f_entry.wr_task);
              bool par_w1 = false, par_w2 = false;
              if (f_entry.w1_task)
                par_w1 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.w1_task,
                                                threadid);
              if (f_entry.w2_task)
                par_w2 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.w2_task,
                                                threadid);

              if ((f_entry.w1_task == NULL && f_entry.w2_task == NULL) ||
                  (f_entry.w1_task == NULL && !par_w2) || (f_entry.w2_task == NULL && !par_w1) ||
                  (!par_w1 && !par_w2)) {
                f_entry.w1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_w1 = line_no;
#endif
                f_entry.w2_task = NULL;
              } else if (par_w1 && par_w2) {
                struct AFTask* lca12 = taskGraph->LCA(f_entry.w1_task, f_entry.w2_task);
                struct AFTask* lca1s = taskGraph->LCA(f_entry.w1_task, curStepNode);
                //struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
                if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
                  f_entry.w1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_w1 = line_no;
#endif
              } else if (f_entry.w2_task == NULL && par_w1) {
                f_entry.w2_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_w2 = line_no;
#endif
              }
            } else { //accessType == READ
              bool par_r1 = false, par_r2 = false;
              if (f_entry.r1_task)
                par_r1 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.r1_task,
                                                threadid);
              if (f_entry.r2_task)
                par_r2 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.r2_task,
                                                threadid);

              if ((f_entry.r1_task == NULL && f_entry.r2_task == NULL) ||
                  (f_entry.r1_task == NULL && !par_r2) || (f_entry.r2_task == NULL && !par_r1) ||
                  (!par_r1 && !par_r2)) {
                f_entry.r1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_r1 = line_no;
#endif
                f_entry.r2_task = NULL;
              } else if (par_r1 && par_r2) {
                struct AFTask* lca12 = taskGraph->LCA(f_entry.r1_task, f_entry.r2_task);
                struct AFTask* lca1s = taskGraph->LCA(f_entry.r1_task, curStepNode);
                //struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
                if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
                  f_entry.r1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_r1 = line_no;
#endif
              } else if (f_entry.r2_task == NULL && par_r1) {
                f_entry.r2_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_r2 = line_no;
#endif
              }
            }
          }
        } else {
          if (insert_index == -1) {
            std::vector<struct Address_data>* access_list = dr_address_data->access_list;

            if (access_list == NULL) {
              dr_address_data->access_list = new std::vector<struct Address_data>();
              access_list = dr_address_data->access_list;
            }
            // form address data
            struct Address_data address_data;
            address_data.lockset = curLockset;
            if (accessType == READ) {
              address_data.r1_task = curStepNode;
#if LINE_NO_PASS
              address_data.line_no_r1 = line_no;
#endif
            } else {
              address_data.w1_task = curStepNode;
#if LINE_NO_PASS
              address_data.line_no_w1 = line_no;
#endif
            }
            // add to access history
            access_list->push_back(address_data);
          } else {
            // update access history
            struct Address_data& update_data = access_list->at(insert_index);

            if (accessType == WRITE) {
              //update_data.wr_task = taskGraph->rightmostNode(curStepNode, update_data.wr_task);
              bool par_w1 = false, par_w2 = false;
              if (update_data.w1_task)
                par_w1 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), update_data.w1_task,
                                                threadid);
              if (update_data.w2_task)
                par_w2 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), update_data.w2_task,
                                                threadid);

              if ((update_data.w1_task == NULL && update_data.w2_task == NULL) ||
                  (update_data.w1_task == NULL && !par_w2) ||
                  (update_data.w2_task == NULL && !par_w1) || (!par_w1 && !par_w2)) {
                update_data.w1_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_w1 = line_no;
#endif
                update_data.w2_task = NULL;
              } else if (par_w1 && par_w2) {
                struct AFTask* lca12 = taskGraph->LCA(update_data.w1_task, update_data.w2_task);
                struct AFTask* lca1s = taskGraph->LCA(update_data.w1_task, curStepNode);
                //struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
                if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
                  update_data.w1_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_w1 = line_no;
#endif
              } else if (update_data.w2_task == NULL && par_w1) {
                update_data.w2_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_w2 = line_no;
#endif
              }
            } else { //accessType == READ
              bool par_r1 = false, par_r2 = false;
              if (update_data.r1_task)
                par_r1 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), update_data.r1_task,
                                                threadid);
              if (update_data.r2_task)
                par_r2 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), update_data.r2_task,
                                                threadid);

              if ((update_data.r1_task == NULL && update_data.r2_task == NULL) ||
                  (update_data.r1_task == NULL && !par_r2) ||
                  (update_data.r2_task == NULL && !par_r1) || (!par_r1 && !par_r2)) {
                update_data.r1_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_r1 = line_no;
#endif
                update_data.r2_task = NULL;
              } else if (par_r1 && par_r2) {
                struct AFTask* lca12 = taskGraph->LCA(update_data.r1_task, update_data.r2_task);
                struct AFTask* lca1s = taskGraph->LCA(update_data.r1_task, curStepNode);
                //struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
                if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
                  update_data.r1_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_r1 = line_no;
#endif
              } else if (update_data.r2_task == NULL && par_r1) {
                update_data.r2_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_r2 = line_no;
#endif
              }
            }
          }
        }
      }
    }
  }
  /////////////////////////////////////////////////////

  PIN_ReleaseLock(&dr_address_data->addr_lock);

#if DEBUG_TIME
  time1 = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
  my_getlock(&debug_lock);
  recordmemt += time1 - time0;
  my_releaselock(&debug_lock);
#endif
}

#if LINE_NO_PASS
extern "C" void RecordMem(THREADID threadid, void* access_addr, AccessType accessType, int line_no) {
#else
extern "C" void RecordMem(THREADID threadid, void* access_addr, AccessType accessType) {
#endif

//  std::cout << line_no << "\n";

#if 0
  assert(access_addr != NULL);
  my_getlock(&printLock);
  std::cout << "RecordMem: Task id: " << std::dec << threadid << "\tAddress: " << access_addr
            << "\t" << ((accessType == READ) ? "READ" : "WRITE") << "\n";
  my_releaselock(&printLock);
#endif
#if STATS
  my_getlock(&globalStats.gs_lock);
  globalStats.total_num_recordmems++;
  if (accessType == READ) {
    globalStats.total_num_rds++;
    if (tidToTaskIdMap[threadid].empty() || tidToTaskIdMap[threadid].top() == 0) {
      // No child tasks spawned?
      globalStats.track_read(0, access_addr);
    } else {
      globalStats.track_read(tidToTaskIdMap[threadid].top(), access_addr);
    }
  } else {
    globalStats.total_num_wrs++;
    if (tidToTaskIdMap[threadid].empty() || tidToTaskIdMap[threadid].top() == 0) {
      // No child tasks spawned?
      globalStats.track_write(0, access_addr);
    } else {
      globalStats.track_write(tidToTaskIdMap[threadid].top(), access_addr);
    }
  }
  my_releaselock(&globalStats.gs_lock);
#endif

#if DEBUG_TIME
  my_getlock(&debug_lock);
  recordmemn++;
  HRTimer time1 = HR::now();
  my_releaselock(&debug_lock);
#endif

  // Exceptions
  ADDRINT addr = (ADDRINT)access_addr;
  if (exceptions(threadid, addr)) {
    //PIN_ReleaseLock(&lock);
#if DEBUG_TIME
    my_getlock(&debug_lock);
  recordmemt += duration_cast<milliseconds>(HR::now() - time1).count();
    my_releaselock(&debug_lock);
#endif
    return;
  }

  //get lockset and current step node
  size_t curLockset = (thd_lockset[threadid].top())->createLockset();
  struct AFTask* curStepNode = taskGraph->getCurTask(threadid);

#if STATS
  my_getlock(&globalStats.gs_lock);
  if (globalStats.max_task_depth < curStepNode->depth)
    globalStats.max_task_depth = curStepNode->depth;
  my_releaselock(&globalStats.gs_lock);
#endif

  //  std::cout << "Recordmem call addr=" << addr << " Step_tid=" << curStepNode->taskId  << " Accesstype=";
  //  paccess(accessType);
  //  std::cout << endl;

  /////////////////////////////////////////////////////
  // check for access pattern and update shadow space

  size_t primary_index = (addr >> 22) & 0x3ff;
  PAIR* x = shadow_space + primary_index;

  //return;
#if USE_PINLOCK
  PIN_GetLock(&(x->first),0);
#else
  my_getlock(&(x->first));
#endif
  //  struct Dr_Address_Data* primary_entry = shadow_space[primary_index];

  if (x->second == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(struct Dr_Address_Data);
    struct Dr_Address_Data* primary_entry =
        (struct Dr_Address_Data*)mmap(0, sec_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    x->second = primary_entry;

    //initialize all locksets to 0xffffffff
    // for (size_t i = 0; i< SS_SEC_TABLE_ENTRIES;i++) {
    //   struct Dr_Address_Data& dr_address_data = primary_entry[i];
    //   for (int j = 0 ; j < NUM_FIXED_ENTRIES ; j++) {
    // 	(dr_address_data.f_entries[j]).lockset = 0xffffffff;
    //   }
    // }
  }
#if USE_PINLOCK
  PIN_ReleaseLock(&(x->first));
#else
  my_releaselock(&(x->first));
#endif

//  return;

  struct Dr_Address_Data* primary_entry = x->second;
  //size_t primary_index = (addr >> 22) & 0x3ff;
  //struct Dr_Address_Data* primary_entry = shadow_space[primary_index];

  //if (primary_entry == NULL) {
  //  size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(struct Dr_Address_Data);
  //  primary_entry = (struct Dr_Address_Data*)mmap(0, sec_length, PROT_READ| PROT_WRITE,
  //      					     MMAP_FLAGS, -1, 0);
  //  shadow_space[primary_index] = primary_entry;

  //  //initialize all locksets to 0xffffffff
  //  // for (size_t i = 0; i< SS_SEC_TABLE_ENTRIES;i++) {
  //  //   struct Dr_Address_Data& dr_address_data = primary_entry[i];
  //  //   for (int j = 0 ; j < NUM_FIXED_ENTRIES ; j++) {
  //  // 	(dr_address_data.f_entries[j]).lockset = 0xffffffff;
  //  //   }
  //  //}
  //}

  size_t offset = (addr)&0x3fffff;
  struct Dr_Address_Data* dr_address_data = primary_entry + offset;

  PIN_GetLock(&dr_address_data->addr_lock, 0);

  if (dr_address_data == NULL) {
    // first access to the location. add access history to shadow space
    std::cout << "FIRST ACCESS\n";
    if (accessType == READ) {
      (dr_address_data->f_entries[0]).lockset = curLockset;
      (dr_address_data->f_entries[0]).r1_task = curStepNode;
#if LINE_NO_PASS
      (dr_address_data->f_entries[0]).line_no_r1 = line_no;
#endif
    } else {
      (dr_address_data->f_entries[0]).lockset = curLockset;
      (dr_address_data->f_entries[0]).w1_task = curStepNode;
#if LINE_NO_PASS
      (dr_address_data->f_entries[0]).line_no_w1 = line_no;
#endif
    }
  } else {
    // check for data race with each access history entry

    bool race_detected = false;
    int f_insert_index = -1;

    for (int i = 0; i < NUM_FIXED_ENTRIES; i++) {
      if (race_detected && f_insert_index != -1)
        break;

      struct Address_data& f_entry = dr_address_data->f_entries[i];

      if (f_insert_index == -1 && f_entry.lockset == 0) {
        f_insert_index = i;
        break;
      }

      //std::bitset<64> x(curLockset);
      //std::bitset<64> y(f_entry.lockset);
      //std::cout << "Cur = " << x << " fentry = " << y << std::endl;

      // if (addr == /*6325048*/6329176) {
      // 	std::cout << "ADDR INTEREST - ACC TYPE = " << accessType << "tid = " << tidToTaskIdMap[threadid].top() << std::endl;
      // 	//std::cout << "lockset = " << curLockset;
      // 	std::bitset<64> x(curLockset);
      // 	std::bitset<64> y(f_entry.lockset);
      // 	std::cout << "Cur = " << x << " fentry = " << y << std::endl;
      // }

      if ((f_entry.lockset != 0) && (((~curLockset) & (~f_entry.lockset)) == 0)) {
        //check for data race
        if (f_entry.w1_task != NULL &&
            taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.w1_task, threadid)) {
          my_getlock(&viol_lock);
#if LINE_NO_PASS
          all_violations.insert(std::pair<ADDRINT, struct violation*>(
              addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                  new violation_data(f_entry.w1_task, WRITE, f_entry.line_no_w1))));
#else
          all_violations.insert(std::pair<ADDRINT, struct violation*>(
              addr, new violation(new violation_data(curStepNode, accessType),
                                  new violation_data(f_entry.w1_task, WRITE))));
#endif
          my_releaselock(&viol_lock);
          race_detected = true;
          break;
        }
        if (f_entry.w2_task != NULL &&
            taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.w2_task, threadid)) {
          my_getlock(&viol_lock);
#if LINE_NO_PASS
          all_violations.insert(std::pair<ADDRINT, struct violation*>(
              addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                  new violation_data(f_entry.w2_task, WRITE, f_entry.line_no_w2))));
#else
          all_violations.insert(std::pair<ADDRINT, struct violation*>(
              addr, new violation(new violation_data(curStepNode, accessType),
                                  new violation_data(f_entry.w2_task, WRITE))));
#endif
          my_releaselock(&viol_lock);
          race_detected = true;
          break;
        }

        if (accessType == WRITE) {
          if (f_entry.r1_task != NULL &&
              taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.r1_task, threadid)) {
            my_getlock(&viol_lock);
#if LINE_NO_PASS
            all_violations.insert(std::pair<ADDRINT, struct violation*>(
                addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                    new violation_data(f_entry.r1_task, READ, f_entry.line_no_r1))));
#else
            all_violations.insert(std::pair<ADDRINT, struct violation*>(
                addr, new violation(new violation_data(curStepNode, accessType),
                                    new violation_data(f_entry.r1_task, READ))));
#endif
            my_releaselock(&viol_lock);
            race_detected = true;
            break;
          }
          if (f_entry.r2_task != NULL &&
              taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.r2_task, threadid)) {
            my_getlock(&viol_lock);
#if LINE_NO_PASS
            all_violations.insert(std::pair<ADDRINT, struct violation*>(
                addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                    new violation_data(f_entry.r2_task, READ, f_entry.line_no_r2))));
#else
            all_violations.insert(std::pair<ADDRINT, struct violation*>(
                addr, new violation(new violation_data(curStepNode, accessType),
                                    new violation_data(f_entry.r2_task, READ))));
#endif
            my_releaselock(&viol_lock);
            race_detected = true;
            break;
          }
        }
      }
      if (f_entry.lockset != 0 && curLockset == f_entry.lockset) {
        f_insert_index = i;
      }
    }

    if (!race_detected) {
      int insert_index = -1;

      std::vector<struct Address_data>* access_list = dr_address_data->access_list;

      if (access_list != NULL) {
        for (std::vector<struct Address_data>::iterator it = access_list->begin();
             it != access_list->end(); ++it) {
          struct Address_data& add_data = *it;
          //check if intersection of lockset is empty
          if (((~curLockset) & (~add_data.lockset)) == 0) {
            //check for data race
            if (add_data.w1_task != NULL && taskGraph->areParallel(tidToTaskIdMap[threadid].top(),
                                                                   add_data.w1_task, threadid)) {
              my_getlock(&viol_lock);
#if LINE_NO_PASS
              all_violations.insert(std::pair<ADDRINT, struct violation*>(
                  addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                      new violation_data(add_data.w1_task, WRITE, add_data.line_no_w1))));
#else
              all_violations.insert(std::pair<ADDRINT, struct violation*>(
                  addr, new violation(new violation_data(curStepNode, accessType),
                                      new violation_data(add_data.w1_task, WRITE))));
#endif
              my_releaselock(&viol_lock);
              race_detected = true;
              break;
            }
            if (add_data.w2_task != NULL && taskGraph->areParallel(tidToTaskIdMap[threadid].top(),
                                                                   add_data.w2_task, threadid)) {
              my_getlock(&viol_lock);
#if LINE_NO_PASS
              all_violations.insert(std::pair<ADDRINT, struct violation*>(
                  addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                      new violation_data(add_data.w2_task, WRITE, add_data.line_no_w2))));
#else
              all_violations.insert(std::pair<ADDRINT, struct violation*>(
                  addr, new violation(new violation_data(curStepNode, accessType),
                                      new violation_data(add_data.w2_task, WRITE))));
#endif
              my_releaselock(&viol_lock);
              race_detected = true;
              break;
            }
            if (accessType == WRITE) {
              if (add_data.r1_task != NULL && taskGraph->areParallel(tidToTaskIdMap[threadid].top(),
                                                                     add_data.r1_task, threadid)) {
                my_getlock(&viol_lock);
#if LINE_NO_PASS
                all_violations.insert(std::pair<ADDRINT, struct violation*>(
                    addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                        new violation_data(add_data.r1_task, READ, add_data.line_no_r1))));
#else
                all_violations.insert(std::pair<ADDRINT, struct violation*>(
                    addr, new violation(new violation_data(curStepNode, accessType),
                                        new violation_data(add_data.r1_task, READ))));
#endif
                my_releaselock(&viol_lock);
                race_detected = true;
                break;
              }
              if (add_data.r2_task != NULL && taskGraph->areParallel(tidToTaskIdMap[threadid].top(),
                                                                     add_data.r2_task, threadid)) {
                my_getlock(&viol_lock);
#if LINE_NO_PASS
                all_violations.insert(std::pair<ADDRINT, struct violation*>(
                    addr, new violation(new violation_data(curStepNode, accessType, line_no),
                                        new violation_data(add_data.r2_task, READ,add_data.line_no_r2))));
#else
                all_violations.insert(std::pair<ADDRINT, struct violation*>(
                    addr, new violation(new violation_data(curStepNode, accessType),
                                        new violation_data(add_data.r2_task, READ))));
#endif
                my_releaselock(&viol_lock);
                race_detected = true;
                break;
              }
            }
          }
          if (curLockset == add_data.lockset) {
            insert_index = it - access_list->begin();
          }
        }
      }

      if (!race_detected) {
        if (f_insert_index != -1) {
          struct Address_data& f_entry = dr_address_data->f_entries[f_insert_index];
          if (f_entry.lockset == 0) {
            f_entry.lockset = curLockset;
            if (accessType == READ) {
              f_entry.r1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_r1 = line_no;
#endif
            } else {
              f_entry.w1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_w1 = line_no;
#endif
            }
          } else {
            if (accessType == WRITE) {
              //f_entry.wr_task = taskGraph->rightmostNode(curStepNode, f_entry.wr_task);
              bool par_w1 = false, par_w2 = false;
              if (f_entry.w1_task)
                par_w1 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.w1_task,
                                                threadid);
              if (f_entry.w2_task)
                par_w2 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.w2_task,
                                                threadid);

              if ((f_entry.w1_task == NULL && f_entry.w2_task == NULL) ||
                  (f_entry.w1_task == NULL && !par_w2) || (f_entry.w2_task == NULL && !par_w1) ||
                  (!par_w1 && !par_w2)) {
                f_entry.w1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_w1 = line_no;
#endif
                f_entry.w2_task = NULL;
              } else if (par_w1 && par_w2) {
                struct AFTask* lca12 = taskGraph->LCA(f_entry.w1_task, f_entry.w2_task);
                struct AFTask* lca1s = taskGraph->LCA(f_entry.w1_task, curStepNode);
                //struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
                if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
                  f_entry.w1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_w1 = line_no;
#endif
              } else if (f_entry.w2_task == NULL && par_w1) {
                f_entry.w2_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_w2 = line_no;
#endif
              }
            } else { //accessType == READ
              bool par_r1 = false, par_r2 = false;
              if (f_entry.r1_task)
                par_r1 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.r1_task,
                                                threadid);
              if (f_entry.r2_task)
                par_r2 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), f_entry.r2_task,
                                                threadid);

              if ((f_entry.r1_task == NULL && f_entry.r2_task == NULL) ||
                  (f_entry.r1_task == NULL && !par_r2) || (f_entry.r2_task == NULL && !par_r1) ||
                  (!par_r1 && !par_r2)) {
                f_entry.r1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_r1 = line_no;
#endif
                f_entry.r2_task = NULL;
              } else if (par_r1 && par_r2) {
                struct AFTask* lca12 = taskGraph->LCA(f_entry.r1_task, f_entry.r2_task);
                struct AFTask* lca1s = taskGraph->LCA(f_entry.r1_task, curStepNode);
                //struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
                if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
                  f_entry.r1_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_r1 = line_no;
#endif
              } else if (f_entry.r2_task == NULL && par_r1) {
                f_entry.r2_task = curStepNode;
#if LINE_NO_PASS
              f_entry.line_no_r2 = line_no;
#endif
              }
            }
          }
        } else {
          if (insert_index == -1) {
            std::vector<struct Address_data>* access_list = dr_address_data->access_list;

            if (access_list == NULL) {
              dr_address_data->access_list = new std::vector<struct Address_data>();
              access_list = dr_address_data->access_list;
            }
            // form address data
            struct Address_data address_data;
            address_data.lockset = curLockset;
            if (accessType == READ) {
              address_data.r1_task = curStepNode;
#if LINE_NO_PASS
              address_data.line_no_r1 = line_no;
#endif
            } else {
              address_data.w1_task = curStepNode;
#if LINE_NO_PASS
              address_data.line_no_w1 = line_no;
#endif
            }
            // add to access history
            access_list->push_back(address_data);
          } else {
            // update access history
            struct Address_data& update_data = access_list->at(insert_index);

            if (accessType == WRITE) {
              //update_data.wr_task = taskGraph->rightmostNode(curStepNode, update_data.wr_task);
              bool par_w1 = false, par_w2 = false;
              if (update_data.w1_task)
                par_w1 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), update_data.w1_task,
                                                threadid);
              if (update_data.w2_task)
                par_w2 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), update_data.w2_task,
                                                threadid);

              if ((update_data.w1_task == NULL && update_data.w2_task == NULL) ||
                  (update_data.w1_task == NULL && !par_w2) ||
                  (update_data.w2_task == NULL && !par_w1) || (!par_w1 && !par_w2)) {
                update_data.w1_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_w1 = line_no;
#endif
                update_data.w2_task = NULL;
              } else if (par_w1 && par_w2) {
                struct AFTask* lca12 = taskGraph->LCA(update_data.w1_task, update_data.w2_task);
                struct AFTask* lca1s = taskGraph->LCA(update_data.w1_task, curStepNode);
                //struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
                if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
                  update_data.w1_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_w1 = line_no;
#endif
              } else if (update_data.w2_task == NULL && par_w1) {
                update_data.w2_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_w2 = line_no;
#endif
              }
            } else { //accessType == READ
              bool par_r1 = false, par_r2 = false;
              if (update_data.r1_task)
                par_r1 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), update_data.r1_task,
                                                threadid);
              if (update_data.r2_task)
                par_r2 = taskGraph->areParallel(tidToTaskIdMap[threadid].top(), update_data.r2_task,
                                                threadid);

              if ((update_data.r1_task == NULL && update_data.r2_task == NULL) ||
                  (update_data.r1_task == NULL && !par_r2) ||
                  (update_data.r2_task == NULL && !par_r1) || (!par_r1 && !par_r2)) {
                update_data.r1_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_r1 = line_no;
#endif
                update_data.r2_task = NULL;
              } else if (par_r1 && par_r2) {
                struct AFTask* lca12 = taskGraph->LCA(update_data.r1_task, update_data.r2_task);
                struct AFTask* lca1s = taskGraph->LCA(update_data.r1_task, curStepNode);
                //struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
                if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
                  update_data.r1_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_r1 = line_no;
#endif
              } else if (update_data.r2_task == NULL && par_r1) {
                update_data.r2_task = curStepNode;
#if LINE_NO_PASS
              update_data.line_no_r2 = line_no;
#endif
              }
            }
          }
        }
      }
    }
  }

  /////////////////////////////////////////////////////
  PIN_ReleaseLock(&dr_address_data->addr_lock);
  //PIN_ReleaseLock(&lock);
#if DEBUG_TIME
    my_getlock(&debug_lock);
  recordmemt += duration_cast<milliseconds>(HR::now() - time1).count();
    my_releaselock(&debug_lock);
#endif
}
#if REPORT_DATA_RACES
static void report_access(struct violation_data* a) {
  report << a->task->taskId << "          ";
  if (a->accessType == READ)
#if LINE_NO_PASS
    report << "READ line_no " << a->line_no << "\n";
#else
  report << "READ\n";
#endif
  else
#if LINE_NO_PASS
    report << "WRITE line_no " << a->line_no << "\n";
#else
  report << "WRITE\n";
#endif
}

static void report_DR(ADDRINT addr, struct violation_data* a1, struct violation_data* a2) {
  report << "** Data Race Detected at " << addr << " **\n";
  report << "Accesses:\n";
  report << "TaskId    AccessType\n";
  report_access(a1);
  report_access(a2);
  report << "*******************************\n";
}
#endif

extern "C" void Fini() {
#if REPORT_DATA_RACES
  report.open("violations.out");
  for (std::map<ADDRINT, struct violation*>::iterator it = all_violations.begin();
       it != all_violations.end(); ++it) {
    struct violation* viol = it->second;
    report_DR(it->first, viol->a1, viol->a2);
  }
  std::cout << "Number of violations = " << all_violations.size() << std::endl;
  report.close();
#endif
#if DEBUG_TIME
  std::cout << "DEBUG_TIME Mode\n";
  std::cout << "Number of tasks spawned " << task_id_ctr << endl;
  std::cout << "Number of threads created " << tid_ctr << endl;
  std::cout << "Total time in recordmem function " << recordmemt << " milliseconds" << endl;
  std::cout << "No of times recordmem is called is " << recordmemn << endl;
  std::cout << "spawn time " << spawnt << " milliseconds " << endl;
  std::cout << "spawn root time " << spawn_roott << " milliseconds " << endl;
  std::cout << "sapwn wait time " << spawn_waitt << " milliseconds " << endl;
  std::cout << "wait time " << waitt << " milliseconds " << endl;
  taskGraph->print_taskgraph();
#endif

#if STATS
  globalStats.total_num_viol = all_violations.size();
  globalStats.calFinalStats();
  globalStats.dump();
#endif
}
