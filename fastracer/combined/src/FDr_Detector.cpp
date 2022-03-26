#include "Fexec_calls.h"
#include <bitset>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <vector>

using namespace std;
using namespace tbb;

typedef pair<tbb::atomic<size_t>, Fvarstate*> subpair;
typedef pair<tbb::atomic<size_t>, subpair*> PAIR;
PAIR* shadow_space;
my_lock shadow_space_lock(1);

#ifdef DEBUG
FGlobalStats stats;
#endif

#ifdef DEBUG_TIME
my_lock Fdebug_lock(0);
unsigned recordmemt = 0, recordmemn = 0;
unsigned Frecordmemi = 0;
#endif

const size_t SS_PRIMARY_TABLE_ENTRIES = ((size_t)1024);
const size_t SS_SEC_TABLE_ENTRIES = ((size_t)4 * (size_t)1024 * (size_t)1024);

std::ofstream report;

std::map<ADDRINT, Flockstate*> lockmap; // only one lock

std::map<ADDRINT, struct Fviolation*> all_violations;
my_lock viol_lock(0);

void Ferror(ADDRINT addr, size_t ftid, AccessType ftype, size_t stid, AccessType stype) {
  Fmy_getlock(&viol_lock);
  all_violations.insert(make_pair(
      addr, new Fviolation(new Fviolation_data(ftid, ftype), new Fviolation_data(stid, stype))));
  Fmy_releaselock(&viol_lock);
}

void FTD_Activate() {
  size_t primary_length = (SS_PRIMARY_TABLE_ENTRIES) * sizeof(PAIR);
  shadow_space = (PAIR*)mmap(0, primary_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
  //  assert(shadow_space != (void *)-1);
}

void read(ADDRINT addr, Fvarstate* x, Fthreadstate* t) {
  size_t rtid = ((x->R) >> (64 - NUM_TASK_BITS)) & (~((size_t)1 << (NUM_TASK_BITS - 1)));
  size_t wtid = ((x->W) >> (64 - NUM_TASK_BITS));
  size_t curepoch = t->cur_epoch;
  // cout << (curepoch >> (64-NUM_TASK_BITS)) << "later read tid " << ((curepoch << NUM_TASK_BITS) >> NUM_TASK_BITS) << "clock value\n";
  if (((x->R) & ~((size_t)1 << 63)) == curepoch) { //same epoch
#ifdef DEBUG
    (t->task_stats).num_rd_sameepoch++;
#endif
    return;
  }

  if (((x->W) << NUM_TASK_BITS) > (element(t, wtid) << NUM_TASK_BITS)) {
    x->W = ((size_t)1 << 63);
    Ferror(addr, wtid, WRITE, curepoch >> (64 - NUM_TASK_BITS), READ);
  } // detecting write-read race

  if ((x->R & ((size_t)1 << 63)) != 0) { // if Read-shared data
#ifdef DEBUG
    (t->task_stats).num_rd_shared++;
#endif
#ifdef ENABLE_VECTOR
    vector<size_t>::iterator it;
    for (it = (x->rvc).begin(); it != (x->rvc).end(); it++)
      if ((*it >> (64 - NUM_TASK_BITS)) == (curepoch >> (64 - NUM_TASK_BITS)))
        break;
    if (it == (x->rvc).end())
      (x->rvc).push_back(curepoch);
    else
      *it = curepoch;
#else
#ifdef ENABLE_MAPS
    x->rvc[curepoch >> (64 - NUM_TASK_BITS)] = curepoch;
#else
    x->rvc[curepoch >> (64 - NUM_TASK_BITS)] = curepoch;
#endif
#endif
  } else {
    if (((x->R) << NUM_TASK_BITS) <=
        (element(t, rtid) << NUM_TASK_BITS)) { // non-concurrent read with previous read
#ifdef DEBUG
      (t->task_stats).num_rd_exclusive++;
#endif
      (x->R) = curepoch;
    } else {
#ifdef DEBUG
      (t->task_stats).num_rd_share++;
#endif

#ifdef ENABLE_VECTOR
      (x->rvc).push_back(x->R);
      (x->rvc).push_back(curepoch);
#else
#ifdef ENABLE_MAPS
      x->rvc[rtid] = (x->R);
      x->rvc[curepoch >> (64 - NUM_TASK_BITS)] = curepoch;
#else
      x->rvc[rtid] = (x->R);
      x->rvc[curepoch >> (64 - NUM_TASK_BITS)] = curepoch;
#endif
#endif
      (x->R) = ((size_t)1 << 63);
    }
  }
}

void write(ADDRINT addr, Fvarstate* x, Fthreadstate* t) {
  if (x == NULL) {
    cout << "NULL Fvarstate in write--------------\n";
    return;
  }

  size_t rtid = ((x->R) >> (64 - NUM_TASK_BITS)) & (~((size_t)1 << (NUM_TASK_BITS - 1)));
  size_t wtid = ((x->W) >> (64 - NUM_TASK_BITS));
  size_t curepoch = t->cur_epoch;
  //      cout << (curepoch >> (64-NUM_TASK_BITS)) << "later write tid " << ((curepoch << NUM_TASK_BITS) >> NUM_TASK_BITS) << "clock value\n";
  if (x->W == curepoch) {
#ifdef DEBUG
    (t->task_stats).num_wr_sameepoch++;
#endif
    return;
  }

  if (((x->W) << NUM_TASK_BITS) > (element(t, wtid) << NUM_TASK_BITS)) {
    x->W = ((size_t)1 << 63);
    Ferror(addr, wtid, WRITE, curepoch >> (64 - NUM_TASK_BITS), WRITE);
  }

  if ((x->R & ((size_t)1 << 63)) == 0) {
#ifdef DEBUG
    (t->task_stats).num_wr_exclusive++;
#endif
    if (((x->R) << NUM_TASK_BITS) > (element(t, rtid) << NUM_TASK_BITS)) {
      x->W = ((size_t)1 << 63);
      Ferror(addr, rtid, READ, curepoch >> (64 - NUM_TASK_BITS), WRITE);
    }
  } else {
#ifdef DEBUG
    (t->task_stats).num_wr_shared++;
#endif
#ifdef ENABLE_MAPS
    for (auto it = (x->rvc).begin(); it != (x->rvc).end(); it++)
      if (((it->second) << NUM_TASK_BITS) > (element(t, it->first) << NUM_TASK_BITS)) {
        x->W = ((size_t)1 << 63);
        Ferror(addr, ((it->second) >> (64 - NUM_TASK_BITS)), READ, curepoch >> (64 - NUM_TASK_BITS),
              WRITE);
      }
#else
#ifdef ENABLE_VECTOR
    for (auto it = (x->rvc).begin(); it != (x->rvc).end(); it++)
      if ((*it << NUM_TASK_BITS) > (element(t, (*it) >> (64 - NUM_TASK_BITS)) << NUM_TASK_BITS)) {
        x->W = ((size_t)1 << 63);
        Ferror(addr, (*it >> (64 - NUM_TASK_BITS)), READ, curepoch >> (64 - NUM_TASK_BITS), WRITE);
      }
#else
    for (int i = 0; i < NUM_TASKS; i++)
      if (((x->rvc)[i] << NUM_TASK_BITS) > (element(t, i) << NUM_TASK_BITS)) {
        x->W = ((size_t)1 << 63);
        Ferror(addr, i, READ, curepoch >> (64 - NUM_TASK_BITS), WRITE);
      }
#endif
#endif
#ifdef ENABLE_MAPS
    (x->rvc).clear();
#else
#ifdef ENABLE_VECTOR
    (x->rvc).clear();
#endif
#endif
    (x->R) = 0;
  }
  if ((x->W) != (size_t)1 << 63)
    (x->W) = curepoch;
}

extern "C" void FRecordMem(size_t threadid, void* access_addr, AccessType accesstype) {
  size_t tid = FtidToTaskIdMap[threadid].top();
#ifdef DEBUG_TIME
  Fmy_getlock(&Fdebug_lock);
	recordmemn++;
  Fmy_releaselock(&Fdebug_lock);
 unsigned  time0 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 unsigned time2=0,time3=0,timee=0;
#endif
  Fthreadstate* thread;
  concurrent_hash_map<size_t, Fthreadstate*>::accessor ac;
  if (Ftaskid_map.find(ac, tid))
    thread = ac->second;
  else {
	  ac.release();
	  return;
//    thread = new Fthreadstate();
//    thread->cur_epoch = tid << (64 - NUM_TASK_BITS);
//    thread->tlock = 0;
//
//    Ftaskid_map.insert(ac, tid);
//    ac->second = thread;
  }
  ac.release();
  size_t curepoch = thread->cur_epoch;
  ADDRINT addr = (ADDRINT)access_addr;
  size_t primary_index = (addr >> 22) & 0x3ff;
  PAIR* x = shadow_space + primary_index;
  Fmy_getlock(&(x->first));
  if (x->second == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(subpair);
    subpair* primary_entry =
        (subpair*)mmap(0, sec_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    x->second = primary_entry;
  }
  Fmy_releaselock(&(x->first));
  subpair* primary_entry = x->second;
  size_t offset = (addr & 0x3fffff);
  subpair* addrpair = primary_entry + offset;
  Fmy_getlock(&(addrpair->first));
  Fvarstate* var_state = addrpair->second;
  if (var_state == NULL) {
    var_state = new Fvarstate();
    if (accesstype == READ) {
      var_state->W = 0;
      var_state->R = curepoch;
    } else {
      var_state->R = 0;
      var_state->W = curepoch;
    }
    addrpair->second = var_state;
  } else {
    if (var_state->W == ((size_t)1 << 63)) {
      Fmy_releaselock(&(addrpair->first));
      goto end;
    } //Accessing the variable having data race
    if (accesstype == READ) {
#ifdef DEBUG_TIME
	time2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
      read(addr, var_state, thread);
#ifdef DEBUG_TIME
	time3 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
    } else {
#ifdef DEBUG_TIME
	time2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
      write(addr, var_state, thread);
#ifdef DEBUG_TIME
	time3 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
    }
  }
  Fmy_releaselock(&(addrpair->first));
end:
#ifdef DEBUG_TIME
	timee = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  Fmy_getlock(&Fdebug_lock);
	recordmemt += timee-time0;
	Frecordmemi += (timee-time0) - (time3-time2);
  Fmy_releaselock(&Fdebug_lock);
#endif
	return;
}

extern "C" void FRecordAccess(size_t threadid, void* access_addr, AccessType accesstype) {
  
  FRecordMem(threadid, access_addr, accesstype);
}

void FCaptureLockAcquire(size_t threadid, ADDRINT lock_addr) {
  if(FtidToTaskIdMap[threadid].empty())
    return;
  size_t tid = FtidToTaskIdMap[threadid].top();
  concurrent_hash_map<size_t, Fthreadstate*>::accessor ac;
  Ftaskid_map.find(ac, tid);
  Fthreadstate* t = ac->second;
  ac.release();

  Fmy_getlock(&Flock_map_lock);
  Flockstate* l;
  if (lockmap.find(lock_addr) == lockmap.end()) {
    l = new Flockstate();
    lockmap.insert(pair<ADDRINT, Flockstate*>(lock_addr, l));
  }
  l = lockmap[lock_addr];
  Fmy_releaselock(&Flock_map_lock);
#ifdef DEBUG
  (t->task_stats).track_acq(lock_addr);
  (t->task_stats).num_simultaneousacqs++;
  if ((t->task_stats).num_simultaneousacqs > (t->task_stats).max_num_simultaneousacqs)
    t->task_stats.max_num_simultaneousacqs = t->task_stats.num_simultaneousacqs;
#endif
  Fmy_getlock(&(l->llock));

#ifdef DEBUG
  (l->lock_stats).tid_set.insert(tid);
#endif

#ifdef ENABLE_TASK_MAP
  for (auto it = (l->lvc).begin(); it != (l->lvc).end(); it++) {
    auto it2 = t->C.find(it->first);
    if (it2 != t->C.end())
      it2->second = std::max(it2->second, it->second);
    else if (it->first == (t->cur_epoch >> (64 - NUM_TASK_BITS)))
      t->cur_epoch = std::max(t->cur_epoch, ((it->first) << (64 - NUM_TASK_BITS)) + it->second);
    else
      t->C[it->first] = it->second;
  }
#else
  size_t shifttid = (t->tid) << (64 - NUM_TASK_BITS);
  for (size_t i = 0; i < NUM_TASKS; i++)
    store(t, i,
          (std::max((element(t, i) << NUM_TASK_BITS), ((l->lvc)[i] << NUM_TASK_BITS)) >>
           NUM_TASK_BITS) +
              shifttid); // what if new lock
#endif
  Fmy_releaselock(&l->llock);
}

void FCaptureLockRelease(size_t threadid, ADDRINT lock_addr) {
  if(FtidToTaskIdMap[threadid].empty())
    return;
  size_t tid = FtidToTaskIdMap[threadid].top();
  concurrent_hash_map<size_t, Fthreadstate*>::accessor ac;
  Ftaskid_map.find(ac, tid);
  Fthreadstate* t = ac->second;
  ac.release();
#ifdef DEBUG
  (t->task_stats).num_simultaneousacqs--;
#endif

  Fmy_getlock(&Flock_map_lock);
  Flockstate* l = lockmap.at(lock_addr);
  Fmy_releaselock(&Flock_map_lock);
  if (l == NULL) {
    cout << "releasing without acquiring\n";
  }
  Fmy_getlock(&l->llock);
#ifdef ENABLE_TASK_MAP
  l->lvc = t->C;
  l->lvc[(t->cur_epoch) >> (64 - NUM_TASK_BITS)] =
      ((t->cur_epoch) << NUM_TASK_BITS) >> (NUM_TASK_BITS);
  t->cur_epoch++;
#else
  for (size_t i = 0; i < NUM_TASKS; i++)
    (l->lvc)[i] = element(t, i);
  store(t, t->tid, element(t, t->tid) + 1);
#endif
  Fmy_releaselock(&(l->llock));
}

void Fpaccesstype(AccessType accesstype) {
  if (accesstype == READ)
    report << "READ\n";
  if (accesstype == WRITE)
    report << "WRITE\n";
}

extern "C" void FFini() {
  report.open("violations.out");
  for (std::map<ADDRINT, struct Fviolation*>::iterator i = all_violations.begin();
       i != all_violations.end(); i++) {
    struct Fviolation* viol = i->second;
    report << "** Data Race Detected**\n";
    report << " Address is :";
    report << i->first;
    report << "\n";
    report << viol->a1->tid;
    Fpaccesstype(viol->a1->accessType);
    report << viol->a2->tid;
    Fpaccesstype(viol->a2->accessType);
    report << "**************************************\n";
  }
#ifdef DEBUG
  for (auto it = lockmap.begin(); it != lockmap.end(); it++)
    if (stats.max_acq_locks < (it->second)->lock_stats.tid_set.size())
      stats.max_acq_locks = (it->second)->lock_stats.tid_set.size();
  stats.dump();
  std::cout << "DEBUG Mode\n";
#endif
  std::cout << "\n\nNumber of violations = " << all_violations.size() << std::endl;
#ifdef DEBUG_TIME
  std::cout << "DEBUG_TIME Mode in Fasttrack\n";
   std::cout << "Number of tasks spawned " << Ftask_id_ctr << endl; 
 std::cout << "Total time in recordmem function " << recordmemt << " milliseconds" << endl;
  std::cout << "Total time in recordmem function without read and write  " << Frecordmemi << "milliseconds" <<  endl;
  std::cout << "Total time in recordmem function without read and write in percentage " << (Frecordmemi*100.0)/recordmemt <<  endl;
  std::cout << "No of times recordmem is called is " << recordmemn  << endl;
#endif
  report.close();
}
