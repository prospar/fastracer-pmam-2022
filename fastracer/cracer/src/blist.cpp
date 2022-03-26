/** Lower-level linked-list for order-maintenance. */
#include <assert.h>
#include <stdlib.h> // malloc

#include "Common.H"
#include "blist.h"
#include "exec_calls.h"

// #ifdef __cplusplus
// extern "C" {
// #endif

struct blist_s;
struct tl_node_s;

typedef bl_node node;

static label_t get_new_label(blist* self, node* base) {
  if (base->next)
    assert(base->next->label > base->label);
  label_t next_label = (base->next) ? base->next->label : MAX_LABEL;
  label_t lab = (base->label >> 1) + (next_label >> 1);

  // Correction for average two odd integers (integer division rounding)
  if ((base->label & next_label & 0x1) == 0x1)
    lab++;

  // Correction for adding to the end.
  if (!base->next)
    lab++;

  /// I think it could only match the base's label...
  lab = (base->label == lab) ? 0 : lab;
  assert(lab == 0 || lab > base->label);
  return lab;
}

/// Node allocated, have ptr to base node, already have label
// static void insert_internal(blist* self, node* base, node* n)
// {
//   if (!base) {
//     self->tail = self->head = n;
//     n->next = n->prev = NULL;
//     n->label = 0;
//   } else {
//     assert(n->label > base->label);
//     if (base == self->tail) { // i.e. insert at end
//       self->tail = n;
//       n->next = NULL;
//     } else {
//       n->next = base->next;
//       base->next->prev = n;
//     }
//     base->next = n;
//     n->prev = base;
//   }

//   // Finalize
//   n->list = self;
// }

/** Allocate a new, uninitialized node. */
static inline node* node_new() {
  node* n = (node*)malloc(sizeof(node));
  n->in_use = 42;
  n->active_insert = 0;
  n->last_insert_id = -1;
  return n;
}

/** # Public methods */

blist* bl_new() {
  blist* self = (blist*)malloc(sizeof(struct blist_s));
  bl_create(self);
  return self;
}

void bl_create(blist* self) {
  self->head = self->tail = NULL;
  self->above = NULL;
  self->heavy = 0;
}

void bl_destroy(blist* self) {
  if (!self->head)
    return;
  for (node* it = self->head->next; it != NULL; it = it->next) {
    it->prev->in_use = 69;
    free(it->prev);
  }
  free(self->tail);
}

void bl_free(blist* self) {
  if (self->head)
    bl_destroy(self);
  free(self);
}

node* bl_insert_initial(blist* self) {
  // std::cout<<"INSERT INITIAL"<<std::endl;
  assert(!self->head);
  node* n = node_new();
  insert_internal(self, NULL, n);
  return n;
}

node* bl_insert(blist* self, node* base) {
  assert(base);

  asm volatile("" : : : "memory");
  __sync_synchronize();
  size_t threadId = get_cur_tid();
  if (base->active_insert != 0 || base->last_insert_id != -1) {
    printf("Error: workers %i and %i inserting at %p!\n", base->last_insert_id, threadId, base);
    assert(0);
  }
  assert(base->active_insert == 0);
  __sync_fetch_and_add(&base->active_insert, 1);
  assert(base->active_insert == 1);
  base->last_insert_id = threadId;
  asm volatile("" : : : "memory");
  __sync_synchronize();

  label_t lab = get_new_label(self, base);
  node* n = NULL;
  if (lab > 0) { // Only the initial insert has label 0.
    n = node_new();
    n->label = lab;
    insert_internal(self, base, n);
  }

  asm volatile("" : : : "memory");
  __sync_synchronize();
  __sync_fetch_and_add(&base->active_insert, -1);
  assert(base->active_insert == 0);
  base->last_insert_id = -1;
  asm volatile("" : : : "memory");
  __sync_synchronize();

  return n;
}

bool bl_precedes(const node* x, const node* y) {
  assert(x->list == y->list);
  return x->label < y->label;
}

size_t bl_size(const blist* self) {
#ifdef BL_HAS_SIZE_FIELD
  return self->size;
#else
  size_t count = 0;
  node* current = self->head;
  while (current) {
    count++;
    current = current->next;
  }
  return count;
#endif // BL_HAS_SIZE_FIELD
}

size_t bl_memsize(const blist* self) { return (bl_size(self) * sizeof(bl_node)) + sizeof(blist); }

int bl_verify(const blist* self) {
  node* n = self->head;
  if (!n) {
    assert(self->tail == NULL);
    assert(self->heavy == 0);
    return 0;
  }
  assert(n->prev == NULL);

  while (n->next) {
    assert(n->in_use == 42);
    assert(n->active_insert == 0);
    assert(n->last_insert_id == -1);
    assert(n->next != n);
    assert(n->label < n->next->label);
    assert(n->list == self);
    assert(n->next->prev == n);
    n = n->next;
  }
  assert(n->in_use == 42);
  assert(n->active_insert == 0);
  assert(n->last_insert_id == -1);

  assert(n->list == self);
  assert(n->next == NULL);
  assert(self->tail == n);
  return 0;
}

void bl_fprint(const blist* self, FILE* out) {
  node* current = self->head;
  size_t size = 0;
  fprintf(out, "Blist at %p\n", self);
  while (current) {
    size++;
    fprintf(out, "->%zu", current->label);
    current = current->next;
  }
  fprintf(out, "\nSize: %zu\n", size);
}

void bl_print(const blist* self) { bl_fprint(self, stdout); }

// #ifdef __cplusplus
// } // extern "C"
// #endif
