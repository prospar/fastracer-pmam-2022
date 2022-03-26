#include<iostream>
#include "t_debug_task.h"
//
using namespace std;
using namespace tbb;

//////#define NUM_TASKS 30

//int  CHECK_AV *shd;
int  CHECK_AV *shd;
class Task1: public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());

    // READ & WRITE
  //  shd[3]++;

    __exec_end__(getTaskId());
    return NULL;
  }
};

class Task2: public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());

    // READ & WRITE
 //   shd[4]++;

    __exec_end__(getTaskId());
    return NULL;
  }
};


class Driver1: public t_debug_task{
  public:
    task* execute(){
      __exec_begin__(getTaskId());
      
      set_ref_count(3);
    task& a = *new(task::allocate_child()) Task1();
    t_debug_task::spawn(a);

    task& b = *new(task::allocate_child()) Task2();
    t_debug_task::spawn(b);

    shd[2]++;
//    t_debug_task::wait_for_all();
      __exec_end__(getTaskId());
      return NULL;
    }
};

class Driver: public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());

    set_ref_count(2);
  task& v1 = *new(task::allocate_child()) Driver1();
  t_debug_task::spawn(v1);

  t_debug_task::wait_for_all();
  shd[2]++;
    __exec_end__(getTaskId());
    return NULL;
  }
};

int main( int argc, const char *argv[] ) {
  TD_Activate();

  shd = (int*)malloc(NUM_TASKS * sizeof(int));

  task& v = *new(task::allocate_root()) Driver();
  t_debug_task::spawn_root_and_wait(v);

 cout << "Addr of shd[2] " << (size_t)&shd[2] << std::endl;
  Fini();
}
