#include<iostream>
#include "t_debug_task.h"
//
using namespace std;
using namespace tbb;

int  CHECK_AV *shd;
class Task1: public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
//cout << "Thread id in Task1: " << get_cur_tid() << "\n";
    shd[2]++;
for(int i=0;i<10000000;i++);
    __exec_end__(getTaskId());
    return NULL;
  }
};

class Task2: public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
//cout << "Thread id in Task2: " << get_cur_tid() << "\n";
for(int i=0;i<10000000;i++);
    __exec_end__(getTaskId());
    return NULL;
  }
};


class Driver1: public t_debug_task{
  public:
    task* execute(){
      __exec_begin__(getTaskId());
//cout << "Thread id in Driver1: " << get_cur_tid() << "\n";
      set_ref_count(2);
    task& a = *new(task::allocate_child()) Task1();
    t_debug_task::spawn(a);
 //   t_debug_task::wait_for_all();
      __exec_end__(getTaskId());
      return NULL;
    }
};

class Driver2: public t_debug_task{
  public:
    task* execute(){
      __exec_begin__(getTaskId());
//cout << "Thread id in Driver2: " << get_cur_tid() << "\n";
      set_ref_count(2);
    task& a = *new(task::allocate_child()) Task2();
    t_debug_task::spawn(a);
    t_debug_task::wait_for_all();
      __exec_end__(getTaskId());
      return NULL;
    }
};
class Driver: public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
//cout << "Thread id in Driver: " << get_cur_tid() << "\n";
    set_ref_count(3);
  task& v1 = *new(task::allocate_child()) Driver1();
  t_debug_task::spawn(v1);

  task& v2 = *new(task::allocate_child()) Driver2();
  t_debug_task::spawn(v2);

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
