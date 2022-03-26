#include "t_debug_task.h"
#include <iostream>

using namespace std;
// using namespace tbb;

#define NUM_TASKS 22
my_lock lock(1);
int CHECK_AV* shd;

 class Task1: public t_debug_task {
 public:
   task* execute() {
     __exec_begin__(getTaskId());

     // READ & WRITE


    // READ & WRITE
    shd[getTaskId()]++;


//     shd[getTaskId()]++;
//cout << shd[getTaskId()] << "------------------------\n";
     __exec_end__(getTaskId());
     return NULL;
   }
 };

 class Task2: public t_debug_task {
 public:
   task* execute() {
     __exec_begin__(getTaskId());


    // READ & WRITE
    shd[2]++;

     // READ & WRITE
//     shd[2]++;
//cout << shd[2] <<"---------------------------\n";
     __exec_end__(getTaskId());
     return NULL;
   }
 };

 class Driver : public t_debug_task {
 public:
   task* execute() {
    __exec_begin__(getTaskId());

    set_ref_count(3);
//
     task& a = *new (task::allocate_child()) Task1();
    t_debug_task::spawn(a);

    task& b = *new (task::allocate_child()) Task2();
     t_debug_task::spawn(b);


    t_debug_task::wait_for_all();

     __exec_end__(getTaskId());
     return NULL;
   }
 };

int main(int argc, const char* argv[]) {
   TD_Activate();
  cout << "This is a microbenchark\n";

  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
cout << pthd_id << "\n";
  shd = (int*)malloc(NUM_TASKS * sizeof(int));

  shd[0] = 10;
  shd[1] = 20;
shd[2]=30;
shd[3]=40;
     task& v = *new (task::allocate_root()) Driver();
     t_debug_task::spawn_root_and_wait(v);

//  cout << "Addr of shd[2] " << (size_t)&shd[0] << std::endl;
   Fini();
}
