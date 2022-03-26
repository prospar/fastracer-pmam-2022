#include <iostream>

extern "C" { // Prevent my_getlock(&y); CaptureLockAcquire(get_cur_tid(),(ADDRINT)&y); mangling
void __record_mem_access_addr(void* addr) {
  std::cout << "Memory operation: addr: " << addr << "\n";
}

void __record_mem_access_type(int type) {
  if (type) {
    std::cout << "Read memory operation"
              << "\n";
  } else {
    std::cout << "Write memory operation"
              << "\n";
  }
}

void __record_mem_access_addr_type(void* addr, int type) {
  if (type) {
    std::cout << "Read memory operation: addr: " << addr << "\n";
  } else {
    std::cout << "Write memory operation: addr " << addr << "\n";
  }
}
}

int main() {
  // int *shared;
  // shared = new int[20];
  // std::cout << "After allocation\n";
  int a = 1, b = 10;
  int c = 20;
  c = a + b;
  // shared[5] = c;
  int d;
  d = b + c;
  return 0;
}
