#ifndef _OMRD_H
#define _OMRD_H

#include "Common.H"
#include "stack.h"

extern volatile size_t relabel_time;
extern volatile size_t total_num_relabel;

static volatile int g_batch_in_progress = 0;
extern volatile size_t g_relabel_id;
extern size_t g_num_relabel_lock_tries;
// extern size_t count_set_bits(label_t label);
// extern int is_heavy(om_node* n);
extern pthread_spinlock_t g_relabel_mutex;
extern int g_batch_owner_id;
extern __thread int insert_failed;
extern __thread int failed_at_relabel;

extern my_lock global_relable_lock;
extern my_lock thread_local_lock[NUM_THREADS];

class omrd_t;
extern void join_batch(omrd_t* ds);

#define HALF_BITS ((sizeof(label_t) * 8) / 2)
#define DEFAULT_HEAVY_THRESHOLD HALF_BITS
static label_t g_heavy_threshold = DEFAULT_HEAVY_THRESHOLD;

#define CAS(loc, old, nval) __sync_bool_compare_and_swap(loc, old, nval)

#define PADDING char pad[(64 - sizeof(pthread_spinlock_t))]
typedef struct local_mut_s {
  pthread_spinlock_t mut;
  PADDING;
} local_mut;
extern local_mut* g_worker_mutexes;

// Brian Kernighan's algorithm
static size_t count_set_bits(label_t label) {
  size_t count = 0;
  while (label) {
    label &= (label - 1);
    count++;
  }
  return count;
}

//int is_heavy(om_node* n) { return count_set_bits >= g_heavy_threshold; }
static int is_heavy(om_node* n) {
  label_t next_lab = (n->next) ? n->next->label : MAX_LABEL;
  return (next_lab - n->label) < (1L << (64 - g_heavy_threshold));
}

class omrd_t {
private:
  AtomicStack_t<tl_node*> m_heavy_nodes;
  om* m_ds;
  om_node* m_base;

  void add_heavy(om_node* base) {
    assert(g_batch_in_progress == 0);
    m_heavy_nodes.add(om_get_tl(base));
    assert(base->list->above->level == MAX_LEVEL);
  }

  om_node* try_insert(om_node* base = NULL) {
    assert(g_batch_in_progress == 0);
    om_node* n;
    if (base == NULL)
      n = om_insert_initial(m_ds);
    else {
      //      RDTOOL_INTERVAL_BEGIN(INSERT);
      n = om_insert(m_ds, base);
      //      RDTOOL_INTERVAL_END(INSERT);
    }

    if (base)
      assert(base->list->above);
    if (n)
      assert(n->list->above);
    return n;
  }

  om_node* slow_path(om_node* base);

public:
  omrd_t() {
    m_ds = om_new();
    m_base = try_insert();
  }

  ~omrd_t() { om_free(m_ds); }

  om_node* get_base() { return m_base; }
  AtomicStack_t<tl_node*>* get_heavy_nodes() { return &m_heavy_nodes; }
  om* get_ds() { return m_ds; }

  om_node* insert(om_node* base);

  label_t set_heavy_threshold(label_t new_threshold) {
    label_t old = g_heavy_threshold;
    g_heavy_threshold = new_threshold;
    return old;
  }

  size_t memsize() {
    size_t ds_size = om_memsize(m_ds);
    size_t hnode_size = m_heavy_nodes.memsize();
    return ds_size + hnode_size + sizeof(void*) + sizeof(omrd_t);
  }
};

extern omrd_t* g_english;
extern omrd_t* g_hebrew;
extern void relabel(omrd_t* _ds);
extern void batch_relabel(void* _ds, void* data, size_t size, void* results);

#endif
