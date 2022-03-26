#ifndef NEXEC_CALLS_H
#define NEXEC_CALLS_H

#pragma GCC push_options
#pragma GCC optimize("O0")

#include "NCommon.h"
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

#ifndef mymmap
#define mymmap
#define MMAP_FLAGS (MAP_PRIVATE| MAP_ANONYMOUS| MAP_NORESERVE)
#endif

#define NUM_TASK_BITS 20
#define NUM_TASKS 10000

#ifndef ENABLE_MAPS
#define ENABLE_MAPS 100
#endif

#define DEBUG_TIME 10
#define ENABLE_TASK_MAP 10
#define NUM_THREADS 64

typedef size_t epoch;
typedef unsigned long ADDRINT;
typedef tbb::atomic<size_t> my_lock;
extern "C" void NTD_Activate();

class Nthreadstate {
public:
    size_t lockset;
  size_t clock;
  size_t tid;
  my_lock tlock = 0;
  std::unordered_map<size_t,size_t> C;
 // std::vector<size_t> order;
 size_t order[30];
 size_t order_max = 30;
 size_t order_cur = 0;

  std::list<size_t> child;

#ifdef DEBUG
  NPerTaskStats task_stats;
#endif

  Nthreadstate() { tlock = 0; order_max = 30; order_cur=0; clock = 0; tid  = 0; lockset = 0;}
};

class Nlockstate {
public:
        size_t lockset;
        uint32_t rtid1; uint32_t rc1;
        Nthreadstate* pr1;
        uint32_t rtid2; uint32_t rc2;
        Nthreadstate* pr2;
        uint32_t wtid1; uint32_t wc1;
        Nthreadstate* pw1;
        uint32_t wtid2; uint32_t wc2;
        Nthreadstate* pw2;

        Nlockstate(){
            lockset = 0;
            rtid1=0;rc1=0;
            rtid2=0;rc2=0;
            wtid1=0;wc1=0;
            wtid2=0;wc2=0;
            pr1 = NULL;
            pr2 = NULL;
            pw1 = NULL;
            pw2 = NULL;
        }
};
class Nvarstate {
public:
   	Nlockstate v[4];
	uint16_t size = 4;
	uint16_t cursize = 1;
      size_t test = 1;

      Nvarstate(){
          test = 1;
	  size = 4;
	  cursize = 1;
      }
};



struct Nviolation_data {
  size_t tid;
  AccessType accessType;

  Nviolation_data(size_t tid, AccessType accessType) {
    this->tid = tid;
    this->accessType = accessType;
  }
};

struct Nviolation {
  struct Nviolation_data* a1;
  struct Nviolation_data* a2;

  Nviolation(Nviolation_data* a1, Nviolation_data* a2) {
    this->a1 = a1;
    this->a2 = a2;
  }
};

inline void Nmy_getlock(my_lock* lock) {
  size_t oldx;
  while (1) {
    while ((oldx = *lock) == 1)
      ; // std::cout << "waiting for lock to release";
    if ((*lock).compare_and_swap(1, oldx) == oldx)
      break;
  }
}

inline void Nmy_releaselock(my_lock* lock) { *lock = 0; }

typedef tbb::internal::tbb_thread_v3::id TBB_TID;

extern std::map<TBB_TID, std::stack<size_t>> Ntid_map;

extern my_lock Ntaskid_map_lock;
extern my_lock Nparent_map_lock;
extern my_lock Nlock_map_lock;
extern tbb::concurrent_hash_map<size_t, Nthreadstate*> Ntaskid_map;
extern std::map<size_t,size_t> Nlock_map;
extern std::stack<size_t> NtidToTaskIdMap [NUM_THREADS];
extern "C" {
__attribute__((noinline)) void __Nexec_begin__(unsigned long taskId);

__attribute__((noinline)) void __Nexec_end__(unsigned long taskId);

}
#ifdef DEBUG
extern NGlobalStats stats;
#endif

extern "C" void NFini();
extern "C" void NRecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void NRecordAccess(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void NCaptureLockAcquire(size_t tid, ADDRINT lock_addr);

extern "C" void NCaptureLockRelease(size_t tid, ADDRINT lock_addr);

#endif
