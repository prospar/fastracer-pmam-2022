#ifndef EXEC_CALLS_H
#define EXEC_CALLS_H

#pragma GCC push_options
#pragma GCC optimize("O0")

#include "Common.H"
#include "tbb/task.h"
#include <fstream>
#include <iostream>

extern "C" void TD_Activate();

extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId);

__attribute__((noinline)) void __exec_end__(unsigned long taskId);

size_t __TBB_EXPORTED_METHOD get_cur_tid();
}

extern "C" void Fini();

#endif
