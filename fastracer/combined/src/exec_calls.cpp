using namespace std;
#include "exec_calls.h"

tbb::atomic<size_t> tid_ctr(0);
tbb::atomic<size_t> task_id_ctr(0);
my_lock tid_map_lock(0);
std::map<TBB_TID, size_t> tid_map;
extern "C" {
__attribute__((noinline)) void __exec_begin__(unsigned long taskId) {
#ifdef newalgo
  __Nexec_begin__(taskId);
#endif
#ifdef ptracer
  __Pexec_begin__(taskId);
#endif
#ifdef fasttrack
  __Fexec_begin__(taskId);
#endif
}
__attribute__((noinline)) void __exec_end__(unsigned long taskId) {
#ifdef newalgo
  __Nexec_end__(taskId);
#endif
#ifdef ptracer
  __Pexec_end__(taskId);
#endif
#ifdef fasttrack
  __Fexec_end__(taskId);
#endif
}

__attribute__((noinline)) size_t get_cur_tid() {
  TBB_TID pthd_id = tbb::this_tbb_thread::get_id();
  size_t my_tid;
  my_getlock(&tid_map_lock);
  if (tid_map.count(pthd_id) == 0) {
    my_tid = tid_ctr++;
    tid_map.insert(std::pair<TBB_TID, size_t>(pthd_id, my_tid));
    my_releaselock(&tid_map_lock);
  } else {
    my_releaselock(&tid_map_lock);
    my_tid = tid_map.at(pthd_id);
  }

  return my_tid;
}
}
