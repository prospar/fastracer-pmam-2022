#include "PAFTaskGraph.H"

std::ofstream graph;
size_t AFTaskGraph::create_node(NodeType node_type, size_t task_id){

  int index = ++last_allocated_node;
  //assert(last_allocated_node < NUM_GRAPH_NODES);
  initialize_task(index, node_type, task_id); 

  return index;
}

void AFTaskGraph::initialize_task (size_t index,
			NodeType node_type, 
			size_t val){

  tgraph_nodes[index].cur_step = 0;
  tgraph_nodes[index].parent = 0;
  tgraph_nodes[index].taskId = val;
  tgraph_nodes[index].type = node_type;

  tgraph_nodes[index].depth = 0;
  tgraph_nodes[index].seq_num = 0;
  tgraph_nodes[index].num_children = 0;
  tgraph_nodes[index].young_ns_child = NO_TYPE;
}

void AFTaskGraph::PFini() {
  //std::cout << "Total nodes = " << AFTask::num_nodes << std::endl;
  //std::cout << "Size of node = " << sizeof(struct AFTask) << std::endl;
  //std::cout << "Total LCA = " << total_lca << std::endl;// << " succ = " << success << " failure = " << failure << std::endl;
#if 0
  FILE* op_file = fopen("Task_hist.txt", "w");
  for (std::map<size_t, size_t>::iterator it=jump_map.begin(); it!=jump_map.end(); ++it) {
    //std::cout << it->first << ":" << it->second << '\n';
    fprintf(op_file,"%ld:%ld\n", it->first, it->second);
  }
#endif
}

static void print_type(NodeType type){
  if(type == FINISH) graph << "F";
  else if(type == ASYNC) graph << "A";
  else if(type == STEP) graph << "S";
  else graph << "N";
}

void AFTaskGraph::print_taskgraph(){
  graph.open("taskgraph.dot");
  graph << "digraph program {\n";
   for(int i=0;i<=last_allocated_node;i++){
        int pindex = tgraph_nodes[i].parent;
       NodeType ptype = tgraph_nodes[pindex].type;
      NodeType curtype = tgraph_nodes[i].type;
       
      print_type(ptype);
      graph << pindex << "_tid_" << tgraph_nodes[pindex].taskId  << " -> ";
      print_type(curtype);
      graph << i << "_tid_" << tgraph_nodes[i].taskId  << "\n";
   }
   graph << "}";
   graph.close();
}

struct AFTask* AFTaskGraph::getCurTask(THREADID threadid) {
  //assert(tgraph_nodes[cur[threadid].top()].cur_step != 0);
  return &tgraph_nodes[tgraph_nodes[cur[threadid].top()].cur_step];
}

void AFTaskGraph::printGraph(struct AFTask* node) {
  //printRecurse(node, 0);
}

struct AFTask* AFTaskGraph::LCA(struct AFTask* cur_task, struct AFTask* remote_task) {
  struct AFTask* cached_lca = lca_hash::getLCA((size_t)cur_task, (size_t)remote_task);
  if (cached_lca)
    return cached_lca;

  struct AFTask* new_cur_task = cur_task;
  struct AFTask* new_remote_task = remote_task;

  if (new_cur_task->depth > new_remote_task->depth) {
    while(new_cur_task->depth != new_remote_task->depth) {
      new_cur_task = &tgraph_nodes[new_cur_task->parent];
    }
  } else if (new_remote_task->depth > new_cur_task->depth) {
    while(new_remote_task->depth != new_cur_task->depth) {
      new_remote_task = &tgraph_nodes[new_remote_task->parent];
    }
  }

  while(new_remote_task != new_cur_task) {
    new_remote_task = &tgraph_nodes[new_remote_task->parent];
    new_cur_task = &tgraph_nodes[new_cur_task->parent];
  }

  return new_cur_task;
}

struct AFTask* AFTaskGraph::rightmostNode(struct AFTask* cur_task, struct AFTask* remote_task) {

  if (!remote_task) return cur_task;
  if (cur_task == remote_task) return cur_task;

  //assert(cur_task->type == STEP && remote_task->type == STEP);
  struct AFTask* new_cur_task = cur_task;
  struct AFTask* new_remote_task = remote_task;
  struct AFTask* prev_remote_task = NULL;
  struct AFTask* prev_cur_task = NULL;

  if (cur_task->depth > remote_task->depth) {
    while(new_cur_task->depth != new_remote_task->depth) {
      prev_cur_task = new_cur_task;
      new_cur_task = &tgraph_nodes[new_cur_task->parent];
    }
  } else if (remote_task->depth > cur_task->depth) {
    while(new_remote_task->depth != new_cur_task->depth) {
      prev_remote_task = new_remote_task;
      new_remote_task = &tgraph_nodes[new_remote_task->parent];
    }
  }

  //assert(new_remote_task != new_cur_task);
  while(new_remote_task != new_cur_task) {
    prev_remote_task = new_remote_task;
    new_remote_task = &tgraph_nodes[new_remote_task->parent];

    prev_cur_task = new_cur_task;
    new_cur_task = &tgraph_nodes[new_cur_task->parent];
  }

  size_t cur_index = prev_cur_task->seq_num; 
  size_t rem_index = prev_remote_task->seq_num;
  //assert(rem_index != cur_index);
  if (cur_index > rem_index)
    return cur_task;
  else
    return remote_task;
}

bool AFTaskGraph::parallel(struct AFTask* cur_task, struct AFTask* remote_task) {
  //use depth to find LCA

  ParallelStatus par_status = lca_hash::checkParallel((size_t)cur_task, (size_t)remote_task);
  if (par_status == TRUE) {
    return true;
  } else if (par_status == FALSE) {
    return false;
  }

  struct AFTask* new_cur_task = cur_task;
  struct AFTask* new_remote_task = remote_task;
  struct AFTask* prev_remote_task = NULL;
  struct AFTask* prev_cur_task = NULL;

  //std::cout << "Remote id = " << new_remote_task->taskId << " Cur id = " << new_cur_task->taskId << std::endl;
  if (cur_task->depth > remote_task->depth) {
    while(new_cur_task->depth != new_remote_task->depth) {
      prev_cur_task = new_cur_task;
      new_cur_task = &tgraph_nodes[new_cur_task->parent];
    }
  } else if (remote_task->depth > cur_task->depth) {
    while(new_remote_task->depth != new_cur_task->depth) {
      prev_remote_task = new_remote_task;
      new_remote_task = &tgraph_nodes[new_remote_task->parent];
    }
  }

  //assert(new_remote_task != new_cur_task);
  while(new_remote_task != new_cur_task) {
    prev_remote_task = new_remote_task;
    new_remote_task = &tgraph_nodes[new_remote_task->parent];

    prev_cur_task = new_cur_task;
    new_cur_task = &tgraph_nodes[new_cur_task->parent];
  }

  size_t cur_index = prev_cur_task->seq_num; 
  size_t rem_index = prev_remote_task->seq_num;
  //assert(rem_index != cur_index);

  bool ret_val = false;
  if (rem_index < cur_index) {
    if (prev_remote_task != NULL && prev_remote_task->type == ASYNC)
      ret_val = true;
  } else {
    if (prev_cur_task != NULL && prev_cur_task->type == ASYNC)
      ret_val = true;
  }

  lca_hash::updateEntry((size_t)cur_task, (size_t)remote_task, ret_val, new_cur_task);
  lca_hash::updateEntry((size_t)remote_task,(size_t)cur_task, ret_val, new_cur_task);

  return ret_val;
}

bool AFTaskGraph::areParallel(size_t cur_task, struct AFTask* remote_task, THREADID threadid) {
  //std::cout << "FIRST Remote id = " << remote_task->taskId << " Cur id = " << cur_task << std::endl;
  if (cur_task == remote_task->taskId)
    return false;
  struct AFTask* cur_task_ptr = &tgraph_nodes[cur[threadid].top()];

  if (cur_task_ptr->taskId == remote_task->taskId)
    return false;
  
  //std::cout << "Cur task id = " << cur_task_ptr->taskId << std::endl;
  //assert(tgraph_nodes[cur_task_ptr->cur_step].type == STEP);
  //assert(remote_task->type == STEP);

  return parallel(&tgraph_nodes[cur_task_ptr->cur_step], remote_task);
}

bool AFTaskGraph::checkForStep (struct AFTask* cur_node) {
  return (cur_node->young_ns_child == ASYNC);
}

void AFTaskGraph::CaptureSpawnOnly(THREADID threadid, size_t taskId)
{
  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring 1\n";
  //std::cout << "CaptureSpawnOnly\n";
  struct AFTask* cur_node = &tgraph_nodes[cur[threadid].top()];

  //if current node rightmost child is Async or not
  //check for STEP and prev of STEP == ASYNC
  struct AFTask* newNode;
  size_t newNode_index;
  if (checkForStep(cur_node)) {
    newNode_index = create_node(ASYNC, taskId);
    newNode = &tgraph_nodes[newNode_index];
    newNode->parent = cur[threadid].top();

    newNode->seq_num = cur_node->num_children;
    cur_node->num_children++;
    cur_node->young_ns_child = ASYNC;
  } else {
    size_t newFinish_index = create_node(FINISH, cur_node->taskId);
    struct AFTask* newFinish = &tgraph_nodes[newFinish_index];
    newFinish->parent = cur[threadid].top();
    newFinish->depth = cur_node->depth + 1;
    newFinish->seq_num = cur_node->num_children;
    cur_node->num_children++;
    cur_node->young_ns_child = FINISH;

    newNode_index = create_node(ASYNC, taskId);
    newNode = &tgraph_nodes[newNode_index];
    newNode->parent = newFinish_index;
    newNode->seq_num = newFinish->num_children;
    newFinish->num_children++;
    newFinish->young_ns_child = ASYNC;
    assert(tgraph_nodes[newFinish_index].type != STEP);
    cur[threadid].push(newFinish_index);
    cur_node = newFinish;
  }

  // add newNode->children STEP and cur_node->children STEP
  newNode->depth = cur_node->depth + 1;

  size_t newStep_index =create_node(STEP, newNode->taskId);
  struct AFTask* newStep = &tgraph_nodes[newStep_index];
  newStep->parent = newNode_index;
  newStep->depth = newNode->depth + 1;
  newStep->seq_num = newNode->num_children;
  newNode->num_children++;
  newNode->cur_step = newStep_index;

  newStep_index = create_node(STEP, cur_node->taskId);
  newStep = &tgraph_nodes[newStep_index];
  newStep->parent = cur[threadid].top();
  newStep->depth = cur_node->depth + 1;
  newStep->seq_num = cur_node->num_children;
  cur_node->num_children++;
  cur_node->cur_step = newStep_index;

  //assert(temp_cur_map.count(taskId) == 0);
  temp_cur_map.insert(std::pair<size_t, size_t>(taskId, newNode_index));

  //std::cout << "Releasing 1\n";
  PIN_ReleaseLock(&lock);
}

void AFTaskGraph::CaptureExecute(THREADID threadid, size_t taskId)
{

  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring 2\n";
  //assert(temp_cur_map.count(taskId) != 0);
  //std::cout << temp_cur_map.size() << std::endl;
  size_t temp_cur = temp_cur_map[taskId];
  //std::cout << "Releasing 2_1\n";
  temp_cur_map.erase(taskId);
  //std::cout << "Releasing 2_2\n";
  //assert(temp_cur != 0);
 // assert(tgraph_nodes[temp_cur].type != STEP);
  cur[threadid].push(temp_cur);
  //std::cout << "Releasing 2_3\n";
  PtidToTaskIdMap[threadid].push(tgraph_nodes[temp_cur].taskId);
  //std::cout << "Releasing 2\n";
  PIN_ReleaseLock(&lock);
}

void AFTaskGraph::CaptureSpawnAndWait(THREADID threadid, size_t taskId)
{

  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring 3\n";
  //std::cout << "CaptureSpawnAndWait\n";
  size_t cur_index = cur[threadid].top();
  struct AFTask* cur_node = &tgraph_nodes[cur_index];
  size_t newFinish_index = create_node(FINISH, taskId);
  struct AFTask* newFinish = &tgraph_nodes[newFinish_index];
  newFinish->parent = cur_index;
  newFinish->seq_num = cur_node->num_children;
  cur_node->num_children++;
  cur_node->young_ns_child = FINISH;
  newFinish->depth = cur_node->depth + 1;

  size_t newStep_index = create_node(STEP, newFinish->taskId);
  struct AFTask* newStep = &tgraph_nodes[newStep_index];
  newStep->parent = newFinish_index;
  newStep->depth = newFinish->depth + 1;
  newStep->seq_num = newFinish->num_children;
  newFinish->num_children++;
  newFinish->cur_step = newStep_index;

  //assert(temp_cur_map.count(taskId) == 0);
  temp_cur_map.insert(std::pair<size_t, size_t>(taskId,newFinish_index));
  
  //std::cout << "Releasing 3\n";
  PIN_ReleaseLock(&lock);
}

void AFTaskGraph::CaptureWait(THREADID threadid)
{

  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring 4\n";
  //std::cout << "CaptureWait\n";
  size_t cur_index = cur[threadid].top();
  struct AFTask* cur_node = &tgraph_nodes[cur_index];
  size_t newStep_index = create_node(STEP, cur_node->taskId);
  struct AFTask* newStep = &tgraph_nodes[newStep_index];
  newStep->parent = cur_index;
  newStep->depth = cur_node->depth + 1;
  newStep->seq_num = cur_node->num_children;
  cur_node->num_children++;
  cur_node->cur_step = newStep_index;
  
  //std::cout << "Releasing 4\n";
  PIN_ReleaseLock(&lock);
}

void AFTaskGraph::CaptureReturn(THREADID threadid)
{

  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring 5\n";

  PtidToTaskIdMap[threadid].pop();
  assert(!cur[threadid].empty());
  cur[threadid].pop();

  //std::cout << "Releasing 5\n";
  PIN_ReleaseLock(&lock);
}

void AFTaskGraph::CaptureWaitOnly(THREADID threadid)
{

  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring 6\n";
  //std::cout << "CaptureWaitOnly\n";
  assert(!cur[threadid].empty());
  size_t cur_index = cur[threadid].top();
  struct AFTask* cur_node = &tgraph_nodes[cur_index];
  cur[threadid].pop();
  assert(!cur[threadid].empty());
  cur_index = cur[threadid].top();
  cur_node = &tgraph_nodes[cur_index];
  assert(cur_node->type != STEP);
  size_t newStep_index = create_node(STEP, cur_node->taskId);
  struct AFTask* newStep = &tgraph_nodes[newStep_index];
  newStep->parent = cur_index;
  newStep->depth = cur_node->depth + 1;;
  newStep->seq_num = cur_node->num_children;
  cur_node->num_children++;
  cur_node->cur_step = newStep_index;
  
  //std::cout << "Releasing 6\n";
  PIN_ReleaseLock(&lock);
}

void AFTaskGraph::CaptureSetTaskId(THREADID threadid, size_t taskId, int sp_only)
{
  if (sp_only) {
    CaptureSpawnOnly(threadid, taskId);
  } else {
    CaptureSpawnAndWait(threadid, taskId);
  }
}
