#ifndef DR_DETECTOR_H
#define DR_DETECTOR_H

#include <assert.h>
#include <chrono>
#include <fstream>
#include <iostream>

#include "AFTaskGraph.H"
#include "Address_data.H"
#include "Common.H"
#include "Lockset_data.H"
#include "stats.h"
#include "tbb/atomic.h"
#include "tbb/task.h"
#include "tbb/tbb_thread.h"
#include <cstring>
#include <stdlib.h>

using namespace tbb;

#define NUM_FIXED_ENTRIES 3

typedef tbb::atomic<size_t> my_lock;


extern AFTaskGraph* taskGraph;

// PROSPAR: One corrupt racy step (location/site)
struct violation_data {
  struct AFTask* task;
  AccessType accessType;
#if LINE_NO_PASS
  int line_no;
#endif

#if LINE_NO_PASS
  violation_data(struct AFTask* task, AccessType accessType, int line_no) {
#else
  violation_data(struct AFTask* task, AccessType accessType) {
#endif
    this->task = task;
    this->accessType = accessType;
#if LINE_NO_PASS
    this->line_no = line_no;
#endif
  }
};

// PROSPAR: A data race violation (pair of locations/sites)
struct violation {
  struct violation_data* a1;
  struct violation_data* a2;

  violation(violation_data* a1, violation_data* a2) {
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
extern "C" void TD_Activate();

void CaptureExecute(THREADID threadid);
void CaptureReturn(THREADID threadid);

#if LINE_NO_PASS
extern "C" void RecordMem(THREADID threadid, void* addr, AccessType accessType,int line_no = -1);
extern "C" void RecordAccess(THREADID threadid, void* addr, ADDRINT* locks_acq,
                             size_t locks_acq_size, ADDRINT* locks_rel, size_t locks_rel_size,
                             AccessType accessType, int line_no = -1);
#else
extern "C" void RecordMem(THREADID threadid, void* addr, AccessType accessType);
extern "C" void RecordAccess(THREADID threadid, void* addr, ADDRINT* locks_acq,
                             size_t locks_acq_size, ADDRINT* locks_rel, size_t locks_rel_size,
                             AccessType accessType);
#endif

extern "C" void CaptureLockAcquire(THREADID threadid, ADDRINT lock_addr);
extern "C" void CaptureLockRelease(THREADID threadid, ADDRINT lock_addr);
extern "C" void Fini();

#endif
