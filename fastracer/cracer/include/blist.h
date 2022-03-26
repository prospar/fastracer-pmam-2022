/** Lower-level linked-list for order-maintenance. */
#ifndef _BLIST_H
#define _BLIST_H

#include <stdbool.h> // bool
#include <stdio.h>
#include "Common.H"

struct blist_s;
struct blist_node_s {
  label_t label;
  struct blist_node_s* next;
  struct blist_node_s* prev;
  struct blist_s* list; // Needed for node comparison
  volatile int in_use; // debug
  volatile int active_insert;
  volatile int last_insert_id;
};

typedef struct blist_node_s bl_node;

struct blist_s {
  bl_node* head;
  bl_node* tail;

  // Needed for interacting with top list.
  struct tl_node_s* above;
  unsigned char heavy;
};
typedef struct blist_s blist;


/** Initialize a pre-allocated list. */
extern void bl_create(blist* self);

/** Allocate a list from the heap and initialize.
  * @return An empty list.
*/
extern blist* bl_new();

/** Destroy, but do not de-allocate, list. */
extern void bl_destroy(blist* self);

/** Destroy and deallocate list. */
extern void bl_free(blist* self);

/** Insert a new item into the order.
 *  @param base Node after which to insert.
 *  @return A pointer to the new node.
 */
extern bl_node* bl_insert(blist* self, bl_node* base);
extern bl_node* bl_insert_initial(blist* self);

/** Returns true if x precedes y in the order, false otherwise. */
extern bool bl_precedes(const bl_node* x, const bl_node* y);

/** Verify all labels are in the correct order. */
extern int bl_verify(const blist* self);

extern size_t bl_size(const blist* self);

/** Print list to a file. */
extern void bl_fprint(const blist* self, FILE* out);

/** Print list to standard output. */
extern void bl_print(const blist* self);

extern size_t bl_memsize(const blist* self);

// extern void insert_internal(blist* self, bl_node* base, bl_node* n);
static void insert_internal(blist* self, bl_node* base, bl_node* n)
{
  if (!base) {
    self->tail = self->head = n;
    n->next = n->prev = NULL;
    n->label = 0;
  } else {
    assert(n->label > base->label);
    if (base == self->tail) { // i.e. insert at end
      self->tail = n;
      n->next = NULL;
    } else {
      n->next = base->next;
      base->next->prev = n;
    }
    base->next = n;
    n->prev = base;
  }

  // Finalize
  n->list = self;
}

#endif // ifndef _BLIST_H
