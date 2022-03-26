// A wrapper around my OM data structure, made specifically with the
// extra features necessary for race detection.
// #include "omrd.h"
// #include <internal/abi.h> // for __cilkrts_get_nworkers();
// #include <cilk/batcher.h> // for cilK_batchify
#include "omrd.h"
#include <cstdio> // temporary
// #include "om.h"
// #include "stack.h"
#include "exec_calls.h"
// #include "Common.H"

// #define HALF_BITS ((sizeof(label_t) * 8) / 2)
//#define HALF_BITS 8
// #define DEFAULT_HEAVY_THRESHOLD HALF_BITS
// static label_t g_heavy_threshold = DEFAULT_HEAVY_THRESHOLD;

volatile size_t relabel_time = 0;
volatile size_t total_num_relabel = 0;

// static volatile int g_batch_in_progress = 0;
volatile size_t g_relabel_id = 0;
size_t g_num_relabel_lock_tries = 0;

my_lock global_relable_lock;
my_lock thread_local_lock[NUM_THREADS];

// // Brian Kernighan's algorithm
// size_t count_set_bits(label_t label)
// {
//   size_t count = 0;
//   while (label) {
//     label &= (label - 1);
//     count++;
//   }
//   return count;
// }

// //int is_heavy(om_node* n) { return count_set_bits >= g_heavy_threshold; }
// int is_heavy(om_node* n)
// {
//   label_t next_lab = (n->next) ? n->next->label : MAX_LABEL;
//   return (next_lab - n->label) < (1L << (64 - g_heavy_threshold));
// }

// #define CAS(loc,old,nval) __sync_bool_compare_and_swap(loc,old,nval)
// void batch_relabel(void* _ds, void* data, size_t size, void* results);
void batch_relabel();

/// @todo break this into a FPSPController class (or something similar)
// #include "rd.h" // for local_mut definition
// #include <../runtime/worker_mutex.h>

// #define PADDING char pad[(64 - sizeof(pthread_spinlock_t))]
// typedef struct local_mut_s {
//   pthread_spinlock_t mut;
//   PADDING;
// } local_mut;
local_mut* g_worker_mutexes;
// mutex g_relabel_mutex;
#define LOCK_SUCCESS 1

pthread_spinlock_t g_relabel_mutex;
//__cilkrts_worker *g_relabel_owner;
//#define LOCK_SUCCESS 0

int g_batch_owner_id = -1;
__thread int insert_failed = 0;
__thread int failed_at_relabel = -1;
//__thread int t_in_batch = 0;

int tool_om_try_lock_all() {
  // std::cout<<"*********** lock_all ************" << std::endl;
  // If we haven't failed an insert, we came here because someone had
  // our lock when we wanted to insert. Don't don't even try to
  // contend on the lock.
  if (!insert_failed)
    return 0;
  if ((g_relabel_id & 0x1) == 1)
    return 0;

  // fprintf(stderr, "Worker %d trying to start batch %zu\n", w->self, g_num_relabels);
  // assert(failed_at_relabel < ((int)g_num_relabels));
  // failed_at_relabel = g_num_relabels;

  //  __sync_fetch_and_add(&g_num_relabel_lock_tries, 1);
  // RDTOOL_INTERVAL_BEGIN(RELABEL_LOCK);
  //int result = pthread_spin_trylock(&g_relabel_mutex);
  // int result = __cilkrts_mutex_trylock(w, &g_relabel_mutex);

  // int result = my_trylock(&global_relable_lock); *****
  // RDTOOL_INTERVAL_END(RELABEL_LOCK);
  my_getlock(&global_relable_lock);
  // if (!result) return 0; *****
  //  fprintf(stderr, "Relabel lock acquired by %d\n", g_relabel_mutex.owner->self);
  //  assert(t_in_batch == 1);

  // cilk_set_next_batch_owner();
  // int p = __cilkrts_get_nworkers();
  // std::cout<<"*********** global lock ************" << std::endl;
  // size_t threadId = get_cur_tid();
  for (int i = 0; i < NUM_THREADS; ++i) {
    // pthread_spin_lock(&g_worker_mutexes[i].mut);
    // if(i == threadId) continue; ******
    my_getlock(&thread_local_lock[i]);
    // std::cout<<"*********** local lock ************" << std::endl;
  }
  g_batch_in_progress = 1;
  assert(g_batch_owner_id == -1);
  g_batch_owner_id = get_cur_tid();
  //assert(self == __cilkrts_get_tls_worker()->self);
  return 1;
}

class omrd_t;

void join_batch(omrd_t* ds) {
  // std::cout<<"*********** join batch ************" << std::endl;
  // DBG_TRACE(DEBUG_BACKTRACE, "Worker %i calling batchify.\n", w->self);
  //  assert(self == __cilkrts_get_tls_worker()->self);
  //  t_in_batch++;
  if (tool_om_try_lock_all()) {
    // std::cout<<"*********** lock success ************" << std::endl;
    batch_relabel();
  }
  // TODO: IMP **************************************
  // ?? what is this instruction doing??
  // cilk_batchify(batch_relabel, (void*)ds, 0, sizeof(int));
  // *************************************

  //  assert(self == __cilkrts_get_tls_worker()->self);
  //  assert(t_in_batch == 1);
  size_t threadId = get_cur_tid();
  if (threadId == g_batch_owner_id) {
    g_batch_in_progress = 0;
    g_batch_owner_id = -1;
    for (int i = 0; i < NUM_THREADS; ++i) {
      // pthread_spin_unlock(&thread_local_lock[i]);
      // if(i == threadId) continue;
      my_releaselock(&thread_local_lock[i]);
    }
    // pthread_spin_unlock(&g_relabel_mutex);
    my_releaselock(&global_relable_lock);
    // __cilkrts_mutex_unlock(t_worker, &g_relabel_mutex);
  }
  //  t_in_batch--;
  //  assert(t_in_batch == 0);
  //  assert(self == __cilkrts_get_tls_worker()->self);
}

om_node* omrd_t::insert(om_node* base) {
  // std::cout<<"*********** insert ************" << std::endl;
  //    RDTOOL_INTERVAL_BEGIN(FAST_PATH);
  size_t threadId = get_cur_tid();
  // pthread_spinlock_t* mut = &thread_local_lock[threadId];
  auto mut = &thread_local_lock[threadId];
  // while (pthread_spin_trylock(mut) != 0) {
  // while (my_getlock(mut) != 0) {
  my_getlock(mut);
  //fprintf(stderr, "Worker %d joining batch %zu from fast path\n", w->self, g_num_relabels);

  // join_batch( this);
  // }
  om_node* n = try_insert(base);
  //    RDTOOL_INTERVAL_END(FAST_PATH);
  if (!n) {
    n = slow_path(base);
    assert(!is_heavy(n));
  }
  assert(n->list == base->list);
  if (!n->list->heavy && is_heavy(n)) {
    if (CAS(&base->list->heavy, 0, 1)) {
      // DBG_TRACE(DEBUG_BACKTRACE,
      //           "List marked as heavy with %zu items.\n", n->list->size);
      add_heavy(base);
    }
  }
  // pthread_spin_unlock(mut);
  my_releaselock(mut);
  //    RD_STATS(__sync_fetch_and_add(&g_num_inserts, 1));
  return n;
}

om_node* omrd_t::slow_path(om_node* base) {
  // std::cout<<"*********** slow path ************" << std::endl;
  size_t threadId = get_cur_tid();
  //    RDTOOL_INTERVAL_BEGIN(SLOW_PATH);
  om_node* n = NULL;
  auto mut = &thread_local_lock[threadId];

  /// Assert: mut is owned by self
  while (!n) {
    insert_failed = 1;
    if (!base->list->heavy && CAS(&base->list->heavy, 0, 1)) {
      // DBG_TRACE(DEBUG_BACKTRACE,
      //           "Could not fit into list of size %zu, not heavy.\n",
      //           base->list->size);
      add_heavy(base);
    }
    // TODO: IMP ****************
    // __cilkrts_set_batch_id(w);
    // ******************************
    // pthread_spin_unlock(mut);
    my_releaselock(mut);
    //fprintf(stderr, "Worker %d trying to start batch %zu\n", w->self, g_num_relabels);
    join_batch(this); // try to start

    insert_failed = 0; // Don't start a batch, just join if one has started.
                       // while (pthread_spin_trylock(mut) != 0) {
                       // while (my_getlock(mut) != 0) {
    my_getlock(mut);
    //fprintf(stderr, "Worker %d joining batch %zu from slow_path\n", w->self, g_num_relabels);
    // join_batch( this);
    // }
    n = try_insert(base);
    //      assert(n);
  }
  assert(insert_failed == 0);
  //    RDTOOL_INTERVAL_END(SLOW_PATH);
  return n;
}

/// These are pointers so that we can control when we get
/// freed. Sometimes I want to access them after main(), in a special
/// program destructor.
omrd_t* g_english;
omrd_t* g_hebrew;

void relabel(omrd_t* _ds) {
  om* ds = _ds->get_ds();
  //  om_verify(ds);
  AtomicStack_t<tl_node*>* heavy_nodes = _ds->get_heavy_nodes();
  if (!heavy_nodes->empty()) {
    om_relabel(ds, heavy_nodes->at(0), heavy_nodes->size());

    // Sequential only!
    // #if STATS > 0
    //     g_heavy_node_info[g_num_relabels] = heavy_nodes->size();
    //     g_num_relabels++;
    // #endif
    //    RD_STATS(__sync_fetch_and_add(&g_num_relabels, 1));
    //    RD_STATS(g_relabel_size += heavy_nodes->size());

    heavy_nodes->reset();
  }
  // else {
  //   //    RD_STATS(__sync_fetch_and_add(&g_num_empty_relabels, 1));
  // }
  //  om_verify(ds);
}

// We actually only need the DS.
/// @todo It would be great define batch functions as class methods...
// void batch_relabel(void* _ds, void* data, size_t size, void* results)
void batch_relabel() {
  using namespace std::chrono;
  using HR = high_resolution_clock;
  using HRTimer = HR::time_point;
  HRTimer relabel_time_start = HR::now();

  // std::cout<<"*********** RELABEL **********"<<std::endl;
  //  RD_STATS(DBG_TRACE(DEBUG_BACKTRACE, "Begin relabel %zu.\n", g_num_relabels));
  g_relabel_id++;
  asm volatile("" : : : "memory");
  // fprintf(stderr, "Worker %d starting relabeling phase %zu\n", self, g_num_relabels);

  // omrd_t* ds = (omrd_t*)_ds;
  // relabel(ds);

  // You might as well try to relabel both structures...or should you?
  //  RDTOOL_INTERVAL_BEGIN(RELABEL);

  // TODO: run both relables operations in parallel with tbb spawn/sync.

  relabel(g_english);
  relabel(g_hebrew);
  g_relabel_id++;
  //  RDTOOL_INTERVAL_END(RELABEL);
  // fprintf(stderr, "Ending relabeling phase %zu\n", g_num_relabels-1);

  total_num_relabel++;
  HRTimer relabel_time_end = HR::now();
  relabel_time += duration_cast<nanoseconds>(relabel_time_end - relabel_time_start).count();
}
