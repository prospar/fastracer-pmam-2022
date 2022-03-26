#include <iostream>
#include <tbb/mutex.h>
#include <tbb/tbb.h>

using namespace std;
using namespace tbb;

tbb::mutex printLock;
int CHECK_AV* shd;
#define NUM_TASKS 30
class Task1 : public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
    printLock.lock();
    cout << "Task 1 start" << std::endl;
    printLock.unlock();

    printLock.lock();
    cout << "Task 1 end" << std::endl;
    printLock.unlock();

    __exec_end__(getTaskId());
    return NULL;
  }
};

class Task2 : public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
    printLock.lock();
    cout << "Task 2 start" << std::endl;
    printLock.unlock();

    printLock.lock();
    cout << "Task 2 end" << std::endl;
    printLock.unlock();

    __exec_end__(getTaskId());
    return NULL;
  }
};

class Task3 : public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
    printLock.lock();
    cout << "Task 3 start" << std::endl;
    printLock.unlock();

//    shd[2]++;
    printLock.lock();
    cout << "Task 3 end" << std::endl;
    printLock.unlock();

    __exec_end__(getTaskId());
    return NULL;
  }
};

class Task6 : public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
    printLock.lock();
    std::cout << "Task 6 start" << std::endl;
    printLock.unlock();

    // std::cout<<"T6:Sleep start"<<std::endl;
    // sleep(30);
    // std::cout<<"T6:Sleep end"<<std::endl;

    printLock.lock();
    std::cout << "Task 6 end" << std::endl;
    printLock.unlock();

    __exec_end__(getTaskId());
    return NULL;
  }
};

class Task7 : public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
    printLock.lock();
    std::cout << "Task 7 start" << std::endl;
    printLock.unlock();

    printLock.lock();
    std::cout << "T7:Sleep start" << std::endl;
    printLock.unlock();
    sleep(30);
    
    printLock.lock();
    std::cout << "T7:Sleep end" << std::endl;
    printLock.unlock();

    printLock.lock();
    std::cout << "Task 7 end" << std::endl;
    printLock.unlock();

    __exec_end__(getTaskId());
    return NULL;
  }
};

class Task4 : public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
    printLock.lock();
    std::cout << "Task 4 start" << std::endl;
    printLock.unlock();



   set_ref_count(2); // no waiting

   task& u = *new (task::allocate_child()) Task7();
    t_debug_task::spawn(u);

//    t_debug_task::wait_for_all();

	shd[2]++;
    printLock.lock();
    std::cout << "Task 4 end" << std::endl;
    printLock.unlock();

    __exec_end__(getTaskId());
    return NULL;
  }
};

class Task5 : public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
    printLock.lock();
    std::cout << "Task 5 start" << std::endl;
    printLock.unlock();

    set_ref_count(2); // no waiting

    task& u = *new (task::allocate_child()) Task4();
    t_debug_task::spawn(u);

    t_debug_task::wait_for_all();
    printLock.lock();
    std::cout << "Task 5 end" << std::endl;
    printLock.unlock();

    __exec_end__(getTaskId());
    return NULL;
  }
};

class Root2 : public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
    printLock.lock();
    std::cout << "Root2 start" << std::endl;
    printLock.unlock();

    set_ref_count(4); // Waiting for child tasks+1

    task& s = *new (task::allocate_child()) Task3();
    t_debug_task::spawn(s);

    task& t = *new (task::allocate_child()) Task5();
    t_debug_task::spawn(t);

    task& u = *new (task::allocate_child()) Task6();
    t_debug_task::spawn(u);

    t_debug_task::wait_for_all();

//    sleep(10);
    printLock.lock();
    std::cout << "Root2 end" << std::endl;
    printLock.unlock();

    __exec_end__(getTaskId());
    return NULL;
  }
};

class Root1 : public t_debug_task {
public:
  task* execute() {
    __exec_begin__(getTaskId());
    printLock.lock();
    std::cout << "Root1 start" << std::endl;
    printLock.unlock();

    set_ref_count(4); // No wait for all

    task& a = *new (task::allocate_child()) Task1();
    t_debug_task::spawn(a);

    task& v = *new (task::allocate_child()) Root2();
    t_debug_task::spawn(v);

    task& b = *new (task::allocate_child()) Task2();
    t_debug_task::spawn(b);

        t_debug_task::wait_for_all();

    shd[2]++;
    printLock.lock();
    std::cout << "Root1 end" << std::endl;
    printLock.unlock();

    __exec_end__(getTaskId());
    return NULL;
  }
};

int main() {
  TD_Activate();

  shd = (int*)malloc(NUM_TASKS * sizeof(int));
  task& v = *new (task::allocate_root()) Root1();
  t_debug_task::spawn_root_and_wait(v);
  cout << "Addr of shd[2] " << (size_t)&shd[2] << std::endl;
  Fini();
  return EXIT_SUCCESS;
}
