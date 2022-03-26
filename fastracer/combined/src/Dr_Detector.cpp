#include <iostream>
using namespace std;
#include "exec_calls.h"
#include "Dr_Detector.h"

extern "C" void TD_Activate() {
#ifdef newalgo
  NTD_Activate();
#endif
#ifdef ptracer
  PTD_Activate();
#endif
#ifdef fasttrack
  FTD_Activate();
#endif
}

extern "C" void RecordMem(size_t tid, void* access_addr, AccessType accesstype) {
  size_t addr = (size_t)access_addr;
  if(addr == 6857576)
    std::cout << "recordmem at addr 6857576\n";

#ifdef newalgo
//  std::cout << "going in NRecordMem\n";
  NRecordMem(tid,access_addr,accesstype);
#endif
#ifdef ptracer
  PRecordMem(tid,access_addr,accesstype);
#endif
#ifdef fasttrack
  FRecordMem(tid,access_addr,accesstype);
#endif
}

extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype) {
#ifdef newalgo
  NRecordAccess(tid,access_addr,accesstype);
#endif
#ifdef ptracer
  PRecordAccess(tid,access_addr,accesstype);
#endif
#ifdef fasttrack
  FRecordMem(tid,access_addr,accesstype);
#endif
}

void CaptureLockAcquire(size_t tid, ADDRINT lock_addr) {
#ifdef newalgo
  NCaptureLockAcquire(tid,lock_addr);
#endif
#ifdef ptracer
  PCaptureLockAcquire(tid,lock_addr);
#endif
#ifdef fasttrack
  FCaptureLockAcquire(tid,lock_addr);
#endif
}

void CaptureLockRelease(size_t tid, ADDRINT lock_addr) {
#ifdef newalgo
  NCaptureLockRelease(tid,lock_addr);
#endif
#ifdef ptracer
  PCaptureLockRelease(tid,lock_addr);
#endif
#ifdef fasttrack
  FCaptureLockRelease(tid,lock_addr);
#endif
}


extern "C" void Fini() {
#ifdef newalgo
 NFini();
#endif
#ifdef ptracer
 PFini();
#endif
#ifdef fasttrack
 FFini();
#endif
}
