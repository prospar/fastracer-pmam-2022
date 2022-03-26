#ifndef DR_DETECTOR_H
#define DR_DETECTOR_H

#include "exec_calls.h"
extern "C" void NTD_Activate();
extern "C" void NFini();
extern "C" void NRecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void NRecordAccess(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void NCaptureLockAcquire(size_t tid, ADDRINT lock_addr);

extern "C" void NCaptureLockRelease(size_t tid, ADDRINT lock_addr);

extern "C" void PTD_Activate();
extern "C" void PFini();
extern "C" void PRecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void PRecordAccess(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void PCaptureLockAcquire(size_t tid, ADDRINT lock_addr);

extern "C" void PCaptureLockRelease(size_t tid, ADDRINT lock_addr);

extern "C" void FTD_Activate();
extern "C" void FFini();
extern "C" void FRecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void FRecordAccess(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void FCaptureLockAcquire(size_t tid, ADDRINT lock_addr);

extern "C" void FCaptureLockRelease(size_t tid, ADDRINT lock_addr);
extern "C" void TD_Activate();
extern "C" void Fini();
extern "C" void RecordMem(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void RecordAccess(size_t tid, void* access_addr, AccessType accesstype);
extern "C" void CaptureLockAcquire(size_t tid, ADDRINT lock_addr);

extern "C" void CaptureLockRelease(size_t tid, ADDRINT lock_addr);
#endif
