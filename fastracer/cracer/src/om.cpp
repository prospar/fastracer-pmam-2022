#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#include "Common.H"
#include "om.h"

// unsigned int g_num_malloc_calls;
// double g_malloc_begin;
#include "blist.h"

// #ifdef __cplusplus
// extern "C" {
// #endif

static inline int is_leaf(tl_node* n) {
  return n->level == MAX_LEVEL || (n->prev == NULL && n->right == NULL);
}

tl_node* tl_node_new() {
  tl_node* x = (tl_node*)malloc(1 * sizeof(tl_node));
  return x;
}

void tl_node_free_recursive(tl_node* n) {
  if (n->level < MAX_LEVEL) {
    if (n->left)
      tl_node_free_recursive(n->left);
    if (n->right)
      tl_node_free_recursive(n->right);
  }
  free(n);
}

void tl_node_free(tl_node* n) { free(n); }

// The relabel functions are separated to make the code easier to read.
// #include "om_relabel.cpp"

#define FAST_LOG2(x) (sizeof(label_t) * 8 - 1 - __builtin_clzl((label_t)(x)))
#define FAST_LOG2_CEIL(x) (((x) - (1 << FAST_LOG2(x))) ? FAST_LOG2(x) + 1 : FAST_LOG2(x))
/* #define parfor for */
/* #define spawn */
/* #define sync */
// #include <cilk/cilk.h>
// #define parfor cilk_for
// #define spawn cilk_spawn
// #define sync cilk_sync

// AA: TODO: Currently I am removing cilk code and making it sequential, later we can replace it with TBB code.

typedef bl_node node;
static size_t split(blist* self) {
  size_t num_lists = 0;
  assert(self->above->below == self);
  assert(self->head);

  node* current_node = self->head;
  tl_node* prev_tl_node = NULL;
  while (current_node) {
    tl_node* n = tl_node_new();
    n->level = MAX_LEVEL;
    n->next = NULL;
    n->prev = prev_tl_node;
    n->num_leaves = 1;
    if (prev_tl_node)
      prev_tl_node->next = n;
    else
      self->above->split_nodes = n;

    blist* list = bl_new();
    n->below = list;
    list->above = n;

    current_node->prev = NULL;
    label_t current_label = 0;
    size_t list_size = 0;
    while (current_node && list_size < SUBLIST_SIZE) {
      node* next = current_node->next;
      current_node->label = current_label;
      insert_internal(list, current_node->prev, current_node);

      current_node = next;
      current_label += NODE_INTERVAL;
      list_size++;
    }
    num_lists++;
    prev_tl_node = n;
  }

  // Free the sublist itself, but not its nodes!
  self->head = self->tail = NULL;
  free(self);

  return num_lists;
}

static inline label_t range_right(tl_node* n) { return n->label; }
static inline label_t range_left(tl_node* n) {
  // Need signed to do arithmetic shift
  if (!n->parent)
    return 0;
  long lab = (long)MAX_LABEL; // all 1s
  lab <<= (MAX_LEVEL - 1);
  lab >>= (n->level - 1);
  return ((label_t)lab) & range_right(n);
}

static inline size_t height(tl_node* n) { return MAX_LEVEL - n->level; }
static inline size_t capacity(tl_node* n) { return (n->level == 0) ? MAX_LABEL : 1 << height(n); }
static inline double density(tl_node* n) { return n->num_leaves / (double)(capacity(n)); }

int too_heavy(tl_node* n, size_t height) {
  if (!n->needs_rebalance)
    return 0;
  double threshold = 0.75 + 0.25 * (n->level / (double)height);
  return density(n) >= threshold;
}

tl_node* rebuild_recursive(tl_node* n, tl_node** array, size_t lindex, size_t rindex, label_t llab,
                           label_t rlab);

void build_array_of_leaves(tl_node* n, tl_node** array, size_t left_index, size_t right_index) {
  //  if (!n) return; ///@todo assert?
  assert(n);

  if (is_leaf(n)) {
    size_t index = left_index;
    if (n->num_leaves == 1) {
      array[index] = n;
      assert(index + 1 == right_index);
    } else {
      tl_node* current = n->split_nodes;

      while (current) {
        array[index] = current;

        if (current->next)
          assert(current->next->prev == current);
        if (current->prev)
          current->prev->next = NULL;
        current->prev = NULL;
        current = current->next;

        index++;
      }
      assert(index == right_index);
    }
  } else {
    assert(n->left);
    size_t mid = left_index + n->left->num_leaves;
    // spawn build_array_of_leaves(n->left, array, left_index, mid);
    build_array_of_leaves(n->left, array, left_index, mid);
    build_array_of_leaves(n->right, array, mid, right_index);
    // sync;
    n->left = n->right = NULL;
  }

  // We don't want to free the root of this subtree...
  assert(n->left == NULL);
  assert(n->right == NULL);
  if (n->needs_rebalance)
    tl_node_free(n);
  return;
}

// Rebuild a subtree rooted at node n.
void rebuild(tl_node* n) {
  size_t array_size = n->num_leaves;
  int was_leaf = (n->left == NULL && n->right == NULL);
  tl_node** array = (tl_node**)malloc(array_size * sizeof(tl_node*));

  // will remove extra scaffolding
  build_array_of_leaves(n, array, 0, array_size);

  if (was_leaf) { // Change this node to be an internal node.
    n->below = NULL;
    if (n->parent) {
      if (n->parent->right == n) { // is right child
        n->label = n->parent->label;
      } else { // is left child
        n->label = (range_right(n->parent) - range_left(n->parent)) / 2;
        n->label += range_left(n->parent);
      }
      n->level = n->parent->level + 1;
    } else { // root (only) node
      n->level = 0;
      n->label = MAX_LABEL;
    }
  }
  n->left = n->right = NULL;

  label_t llab = range_left(n);
  label_t rlab = n->label;
  assert(rlab % 2 == 1);

  size_t mindex = array_size - array_size / 2;
  label_t mlab = llab + ((rlab - llab) / 2 + 1) - 1;

  // n->left = spawn rebuild_recursive(n, array, 0, mindex,
  //                                   llab, mlab);
  n->left = rebuild_recursive(n, array, 0, mindex, llab, mlab);
  n->right = rebuild_recursive(n, array, mindex, array_size, mlab + 1, rlab);
  // sync;
  free(array);
}

tl_node* rebuild_recursive(tl_node* parent, tl_node** array, size_t lindex, size_t rindex,
                           label_t llab, label_t rlab) {
  size_t size = rindex - lindex;
  tl_node* n;

  n = (size == 1) ? array[lindex] : tl_node_new();
  n->parent = parent;
  n->needs_rebalance = 0;

  assert(llab <= rlab);

  if (size == 1) {
    assert(array[lindex] == n);
    n->label = llab;
    n->level = MAX_LEVEL;
    n->left = n->right = NULL;
    n->num_leaves = 1;
  } else {
    n->level = n->parent->level + 1;

    size_t mindex = lindex + (size - size / 2);
    label_t mlab = llab + ((rlab - llab) / 2 + 1) - 1;

    // n->left = spawn rebuild_recursive(n, array, lindex, mindex, llab, mlab);
    n->left = rebuild_recursive(n, array, lindex, mindex, llab, mlab);
    n->right = rebuild_recursive(n, array, mindex, rindex, mlab + 1, rlab);
    // sync;

    n->label = rlab;
    n->num_leaves = size;
    assert(rlab % 2 == 1);
  }

  return n;
}

void rebalance(tl_node* n, size_t height) {
  if (!n || !n->needs_rebalance)
    return; // stop here
  n->needs_rebalance = 0;

  if (is_leaf(n))
    return rebuild(n);
  assert(capacity(n) >=
         n->num_leaves); // The children might be out of room, but this node cannot be.
  if (too_heavy(n->left, height) || (n->right && too_heavy(n->right, height))) {
    return rebuild(n);
  }

  // spawn rebalance(n->left, height);
  rebalance(n->left, height);
  rebalance(n->right, height);
  // sync;
}

static inline void om_rebalance(om* self) { rebalance(self->root, self->height); }

void om_relabel(om* self, tl_node** heavy_lists, size_t num_heavy_lists) {
  size_t old_size = self->root->num_leaves; // to check for overflow
  // parfor (int i = 0; i < num_heavy_lists; ++i) {
  for (int i = 0; i < num_heavy_lists; ++i) {
    tl_node* current = heavy_lists[i];

    assert(current->level == MAX_LEVEL);
    assert(current->parent || current == self->root);
    assert(current->left == NULL && current->right == NULL);

    // Split into several sublists.
    size_t num_split_lists = split(current->below);
    //    tl_node* test = current->split_nodes;
    /* while (test) { */
    /*   bl_verify(test->below); */
    /*   assert(test == test->below->above); */
    /*   test = test->next; */
    /* } */

    if (num_split_lists > 1) {
      //      current->level = current->parent->level + 1;
      current->left = current->right = NULL;

      // Update sizes up the tree, while also calculating the height
      // from the current node.

      // -1 because we increment it below for the current node
      size_t height = FAST_LOG2_CEIL(num_split_lists) - 1;
      while (current) {
        __sync_fetch_and_add(&current->num_leaves, num_split_lists - 1);
        current->needs_rebalance = 1;
        height++;
        current = current->parent;
      }

      // Update the height, if necessary.
      size_t old_height = self->height;
      while (height > old_height) {
        old_height = __sync_val_compare_and_swap(&self->height, old_height, height);
      }
    } else { // split into just 1 list! This occasionally happens
             // if SUBLIST_SIZE is not exact
      current->needs_rebalance = 0;
      blist* list = current->split_nodes->below;
      current->split_nodes->level = MAX_LEVEL;
      tl_node_free(current->split_nodes);
      current->below = list;
      list->above = current;
    }
  }

  size_t new_size = self->root->num_leaves;
  if (new_size < old_size // overflow
      || ((MAX_LABEL + 1 != 0) && new_size > MAX_LABEL + 1)) {
    fprintf(stderr, "OM data structure is full!\n");
    exit(1);
  } else {
    om_rebalance(self);
  }
  //  om_verify(self);
}

void om_create(om* self) {
  tl_node* root = tl_node_new();
  root->below = bl_new();
  root->below->above = root;

  root->level = MAX_LEVEL;
  root->num_leaves = 1;
  root->label = 0;

  root->needs_rebalance = 0;
  root->parent = root->left = root->right = NULL;

  self->root = root;
  self->head = self->tail = root;
  self->height = 0;
}

om* om_new() {
  om* self = (om*)malloc(sizeof(*self));
  om_create(self);
  assert(self);
  return self;
}

void om_free(om* self) {
  om_destroy(self);
  free(self);
}

/// Internal, recursive destroy
void destroy(tl_node* n) {
  if (!n)
    return;
  if (n->level < MAX_LEVEL) {
    destroy(n->left);
    destroy(n->right);
  } else {
    bl_free(n->below);
  }
  free(n);
}

tl_node* om_get_tl(node* n) {
  assert(n->list->above->level == MAX_LEVEL);
  return n->list->above;
}

void om_destroy(om* self) { destroy(self->root); }

node* om_insert_initial(om* self) {
  assert(self->root->num_leaves == 1);
  assert(self->root->below);
  node* n = bl_insert_initial(self->root->below);
  assert(n->list == self->root->below);
  assert(n);
  return n;
}

node* om_insert(om* self, node* base) {
  assert(base->list->above->level == MAX_LEVEL);
  assert(base->list->above->below == base->list);
  node* n = bl_insert(base->list, base);
  if (n)
    assert(n->label > base->label);
  return n;
}

bool om_precedes(om_node* x, om_node* y) {
  // std::cout<<"*********** om_precedes ************" << std::endl;
  if (x == NULL)
    return true;
  if (y == NULL)
    return false;
  assert(x->list);
  assert(y->list);
  if (x->list == y->list)
    return (x->label < y->label);
  assert(x->list->above);
  assert(y->list->above);
  // assert(x->list->above->label);
  // assert(y->list->above->label);
  return (x->list->above->label < y->list->above->label);
  // return false;
}

int verify(tl_node* n) {
  if (!n)
    return 0;
  assert(n->level <= MAX_LEVEL);
  int left = verify(n->left);
  int right = verify(n->right);
  if (n->left)
    assert(n->left->parent == n);
  if (n->right)
    assert(n->right->parent == n);

  if (n->level == MAX_LEVEL) { // leaf
    assert(n->num_leaves == 1);
    assert(n->left == NULL && n->right == NULL);
    bl_verify(n->below);
    assert(n->below->above == n);
  } else { // not leaf
    assert(n->num_leaves == left + right);
  }
  return n->num_leaves;
}

int om_verify(const om* self) {
  assert(self->root->parent == NULL);
  verify(self->root);
  return 0;
}

// void om_fprint(const om* self, FILE* out)
// {
//   if (!om_verify(self)) fprintf(out, "Warning: OM is not valid!\n");

//   size_t num_leaves = 1 << MAX_LEVEL;

//   tl_node** future = (tl_node**) calloc(num_leaves, sizeof(tl_node*));
//   tl_node** current = (tl_node**) calloc(num_leaves, sizeof(tl_node*));
//   future[0] = self->root;

//   for (int level = 0; level < MAX_LEVEL; ++level) {

//     tl_node** tmp = current;
//     current = future;
//     future = tmp;

//     size_t num_nodes = 1 << level;
//     size_t spacing = 1 << (MAX_LEVEL - level);
//     for (int i = 0; i < num_nodes; ++i) {
//       tl_node* n = current[i];

//       for (int j = 0; j < spacing; ++j) fprintf(out, "\t");
//       if (!n || n->level != level) fprintf(out, "-");
//       else fprintf(out, "%lu", n->label);
//       for (int j = 0; j < spacing; ++j) fprintf(out, "\t");

//       tl_node *left, *right;
//       if (!n) {
//         left = right = NULL;
//       } else if (n->level == MAX_LEVEL) {
//         left = n;
//         right = NULL;
//       } else {
//         left = n->left;
//         right = n->right;
//       }
//       future[i * 2] = left;
//       future[i * 2 + 1] = right;
//     }
//     fprintf(out, "\n");
//   }
//   for (int i = 0; i < num_leaves; ++i) { // leaves
//     tl_node* leaf = future[i];
//     fprintf(out, "\t");
//     if (leaf) fprintf(out, "%lu", leaf->label);
//     else fprintf(out, "-");
//     fprintf(out, "\t");
//   }
//   fprintf(out, "\n");
//   free(future);
//   free(current);
// }

// void om_print(const om* self) { om_fprint(self, stdout); }

size_t node_memsize(tl_node* n) {
  if (!n)
    return 0;
  size_t size = sizeof(*n);
  if (is_leaf(n))
    size += bl_memsize(n->below);
  else
    size += node_memsize(n->left) + node_memsize(n->right);
  return size;
}

size_t om_memsize(const om* self) { return node_memsize(self->root) + sizeof(om); }

// #ifdef __cplusplus
// } // extern C
// #endif
