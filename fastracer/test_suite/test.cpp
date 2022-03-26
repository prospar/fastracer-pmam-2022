#include <iostream>
#include <vector>
#include <list>
#include <stdlib.h> 
#include <malloc.h> 
#include <sys/mman.h>
#include <unordered_map>
using namespace std;
#define MMAP_FLAGS (MAP_PRIVATE| MAP_ANONYMOUS| MAP_NORESERVE)

// first my_getlock(&y); CaptureLockAcquire(get_cur_tid(),(ADDRINT)&y); space
class x{
public:
list<size_t> m;
};

int main () {
   // This calls function from second my_getlock(&y); CaptureLockAcquire(get_cur_tid(),(ADDRINT)&y); space.
int primary_length = 5*sizeof(x);
x* a = (x*)mmap(0, primary_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
//x* a = (x*)new x[primary_length];
//`x* b = a + 2;
//`(b->m)[0] = 1;
//`(b->m)[1] = 2;
//`cout << (b->m)[0];
  
(a->m).push_back(2);
(a->m).push_back(4);
 return 0;
}
