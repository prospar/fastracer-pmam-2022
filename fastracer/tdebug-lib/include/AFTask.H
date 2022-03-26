#ifndef AFTASK_H
#define AFTASK_H

#include <cstddef>

enum NodeType { // PROSPAR: DPST node type
  NO_TYPE = 0,
  ASYNC = 1,
  FINISH = 2,
  STEP = 3
};

// PROSPAR: One DPST node
struct AFTask {
  size_t taskId;
  size_t parent;
  size_t cur_step;
  NodeType type;
  size_t depth;
  size_t seq_num;
  size_t num_children;
  NodeType young_ns_child; //NodeType of youngest non-step child
  bool sp_pending;
  bool sp_root_n_wt_flag;
};

#endif
