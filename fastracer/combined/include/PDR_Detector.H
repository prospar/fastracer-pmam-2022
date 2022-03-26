#ifndef DR_DETECTOR_H
#define DR_DETECTOR_H

#include <assert.h>
#include <iostream>
#include <fstream>
#include <chrono>

#include "PLockset_data.H"
#include "PAddress_data.H"
#include "PCommon.H"
#include "PAFTaskGraph.H"
#include <stdlib.h>
#include <cstring>
#include "tbb/task.h"
#include "tbb/tbb_thread.h"
#include "tbb/atomic.h"
using namespace tbb;

#define NUM_FIXED_ENTRIES 3
typedef tbb::atomic<size_t> my_lock;
#define DEBUG_TIME 10
inline void Pmy_getlock(my_lock* lock) {
  size_t oldx;
  // cout << *lock << "Pmy_getlock\n";
  while (1) {
    while ((oldx = *lock) == 2)
      ; // cout << "waiting for lock to release";
    if ((*lock).compare_and_swap(2, oldx) == oldx)
      break;
  }
}
inline void Pmy_releaselock(my_lock* lock) { *lock = 1; }
extern AFTaskGraph* taskGraph;

// PROSPAR: One corrupt racy step (location/site)
struct Pviolation_data {
  struct AFTask* task;
  AccessType accessType;

  Pviolation_data(struct AFTask* task, AccessType accessType) {
    this-> task = task;
    this->accessType = accessType;
  }
};

// PROSPAR: A data race violation (pair of locations/sites)
struct Pviolation {
  struct Pviolation_data* a1;
  struct Pviolation_data* a2;
  
  Pviolation(Pviolation_data* a1,
	    Pviolation_data* a2) {
    this->a1 = a1;
    this->a2 = a2;
  }
};

struct Dr_Address_Data {
  PIN_LOCK addr_lock;
  struct Address_data f_entries[NUM_FIXED_ENTRIES];
  std::vector<struct Address_data>* access_list;
};

//public functions
extern "C" void PTD_Activate();

void CaptureExecute(THREADID threadid);
void CaptureReturn(THREADID threadid);
extern "C" void PRecordMem(THREADID threadid, void * addr, AccessType accessType);
extern "C" void PRecordAccess(THREADID threadid, void * addr, ADDRINT* locks_acq, 
			     size_t locks_acq_size, ADDRINT* locks_rel, 
			     size_t locks_rel_size, AccessType accessType);
  
extern "C" void PCaptureLockAcquire(THREADID threadid, ADDRINT lock_addr);
extern "C" void PCaptureLockRelease(THREADID threadid, ADDRINT lock_addr);
extern "C" void PFini();

#endif
