#include <sys/mman.h>
#include <iostream>
#include "PDR_Detector.H"
#include "Pexec_calls.h"
using std::endl;

#include <bitset>

//array of stacks of lockset_data
std::stack<struct Lockset_data*> thd_lockset[NUM_THREADS];

// 2^10 entries each will be 8 bytes each
const size_t SS_PRIMARY_TABLE_ENTRIES = ((size_t) 1024);

// each secondary entry has 2^ 22 entries (64 bytes each)
const size_t SS_SEC_TABLE_ENTRIES = ((size_t) 4*(size_t) 1024 * (size_t) 1024);
typedef std::pair<tbb::atomic<size_t>,struct Dr_Address_Data*> PAIR;
PAIR* pshadow_space;
//struct Dr_Address_Data** pshadow_space;
//PIN_LOCK lock;
std::ofstream preport;
my_lock pviol_lock;
std::map<ADDRINT, struct Pviolation*> pall_violations;

#ifdef DEBUG_TIME
my_lock pdebug_lock(0);
unsigned precordmemt = 0, precordmemn = 0,recordaccesst = 0, recordaccessn = 0;
#endif

extern "C" void PTD_Activate() {
  std::cout << "Address data size = " << sizeof(struct Dr_Address_Data) << " addr_data = " << sizeof(struct Address_data) << std::endl;
  taskGraph = new AFTaskGraph();
  thd_lockset[0].push(new Lockset_data());
  size_t primary_length = (SS_PRIMARY_TABLE_ENTRIES) * sizeof(PAIR);
  pshadow_space = (PAIR*)mmap(0, primary_length, PROT_READ| PROT_WRITE,
						MMAP_FLAGS, -1, 0);
  assert(pshadow_space != (void *)-1);
}

extern "C" void PCaptureLockAcquire(THREADID threadid, ADDRINT lock_addr) {
  
  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring 7\n";

  struct Lockset_data* curLockset = thd_lockset[threadid].top();
  curLockset->addLockToLockset(lock_addr);

  //std::cout << "Releasing 7\n";
  PIN_ReleaseLock(&lock);
}

extern "C" void PCaptureLockRelease(THREADID threadid, ADDRINT lock_addr) {

  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring 8\n";

  struct Lockset_data* curLockset = thd_lockset[threadid].top();
  curLockset->removeLockFromLockset();

  //std::cout << "Releasing 8\n";
  PIN_ReleaseLock(&lock);
}

void CaptureExecute(THREADID threadid) {

  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring 9\n";

  thd_lockset[threadid].push(new Lockset_data());

  //std::cout << "Releasing 9\n";
  PIN_ReleaseLock(&lock);
}

void CaptureReturn(THREADID threadid) {

  PIN_GetLock(&lock, 0);
  //std::cout << "Acquiring a\n";

  struct Lockset_data* curLockset = thd_lockset[threadid].top();
  thd_lockset[threadid].pop();
  delete(curLockset);

  //std::cout << "Releasing a\n";
  PIN_ReleaseLock(&lock);
}

static bool exceptions (THREADID threadid, ADDRINT addr) {
  return (PtidToTaskIdMap[threadid].empty() ||
	  PtidToTaskIdMap[threadid].top() == 0 ||
	  thd_lockset[threadid].empty() ||
	  pall_violations.count(addr) != 0
	  );
}

extern "C" void PRecordAccess(THREADID threadid, void * access_addr, ADDRINT* locks_acq,
			     size_t locks_acq_size, ADDRINT* locks_rel,
			     size_t locks_rel_size, AccessType accessType) {
  //PIN_GetLock(&lock, 0);
  //return;

#ifdef DEBUG_TIME
 Pmy_getlock(&pdebug_lock);
	recordaccessn++;
 Pmy_releaselock(&pdebug_lock);
 unsigned  time0 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 unsigned time1;
#endif


  // Exceptions
  ADDRINT addr = (ADDRINT) access_addr;
  if(exceptions(threadid, addr)){
    //PIN_ReleaseLock(&lock);
#ifdef DEBUG_TIME
	time1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Pmy_getlock(&pdebug_lock);
	recordaccesst += time1-time0;
 Pmy_releaselock(&pdebug_lock);
#endif
    return;
  }

  //get lockset and current step node
  size_t curLockset = (thd_lockset[threadid].top())->createLockset();

  /////////// extra for adding lockset /////////////////////////

  for (size_t i = 0; i < locks_acq_size; i++) {
    curLockset = curLockset & Lockset_data::getLockId(locks_acq[i]);
  }

  for (size_t i = 0; i < locks_rel_size; i++) {
    curLockset = curLockset | ~(Lockset_data::getLockId(locks_rel[i]));
  }

  /////////////////////////////////////////////////////////////

  struct AFTask* curStepNode = taskGraph->getCurTask(threadid);

  /////////////////////////////////////////////////////
  // check for access pattern and update shadow space

  size_t primary_index = (addr >> 22) & 0x3ff;
  PAIR* x = pshadow_space + primary_index;
  (x->first).compare_and_swap(1, 0);
  Pmy_getlock(&(x->first));
//  struct Dr_Address_Data* primary_entry = pshadow_space[primary_index];

  if (x->second == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(struct Dr_Address_Data);
  struct Dr_Address_Data*  primary_entry = (struct Dr_Address_Data*)mmap(0, sec_length, PROT_READ| PROT_WRITE,
						     MMAP_FLAGS, -1, 0);
    x->second = primary_entry;

    //initialize all locksets to 0xffffffff
    // for (size_t i = 0; i< SS_SEC_TABLE_ENTRIES;i++) {
    //   struct Dr_Address_Data& dr_address_data = primary_entry[i];
    //   for (int j = 0 ; j < NUM_FIXED_ENTRIES ; j++) {
    // 	(dr_address_data.f_entries[j]).lockset = 0xffffffff;
    //   }
    // }
  }

  Pmy_releaselock(&(x->first));
  struct Dr_Address_Data* primary_entry = x->second;
  size_t offset = (addr) & 0x3fffff;
  struct Dr_Address_Data* dr_address_data = primary_entry + offset;

  PIN_GetLock(&dr_address_data->addr_lock, 0);

  if (dr_address_data == NULL) {
    // first access to the location. add access history to shadow space
    if (accessType == READ) {
      (dr_address_data->f_entries[0]).lockset = curLockset;
      (dr_address_data->f_entries[0]).r1_task = curStepNode;
    } else {
      (dr_address_data->f_entries[0]).lockset = curLockset;
      (dr_address_data->f_entries[0]).w1_task = curStepNode;
    }
  } else {
    // check for data race with each access history entry

    bool race_detected = false;
    int f_insert_index = -1;

    for(int i = 0 ; i < NUM_FIXED_ENTRIES; i++) {
      if(race_detected && f_insert_index != -1) break;

      struct Address_data& f_entry = dr_address_data->f_entries[i];

      if (f_insert_index == -1 && f_entry.lockset == 0) {
	f_insert_index = i;
	break;
      }

      if ((f_entry.lockset != 0) && (((~curLockset) & (~f_entry.lockset)) == 0)) {
	//check for data race
	if (f_entry.w1_task != NULL &&
	    taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), f_entry.w1_task, threadid)) {
  Pmy_getlock(&pviol_lock);
            pall_violations.insert( std::pair<ADDRINT,
				 struct Pviolation* >(addr,
						     new Pviolation(new Pviolation_data(curStepNode, accessType),
								   new Pviolation_data(f_entry.w1_task, WRITE))) );
  Pmy_releaselock(&pviol_lock);
	  race_detected = true;
	  break;
	}
	if (f_entry.w2_task != NULL &&
	    taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), f_entry.w2_task, threadid)) {
  Pmy_getlock(&pviol_lock);
	  pall_violations.insert( std::pair<ADDRINT,
				 struct Pviolation* >(addr,
						     new Pviolation(new Pviolation_data(curStepNode, accessType),
								   new Pviolation_data(f_entry.w2_task, WRITE))) );
  Pmy_releaselock(&pviol_lock);
	  race_detected = true;
	  break;
	}
	if (accessType == WRITE) {
	  if (f_entry.r1_task != NULL && taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), f_entry.r1_task, threadid)) {
  Pmy_getlock(&pviol_lock);
	    pall_violations.insert( std::pair<ADDRINT,
				   struct Pviolation* >(addr,
						       new Pviolation(new Pviolation_data(curStepNode, accessType),
								     new Pviolation_data(f_entry.r1_task, READ))) );
  Pmy_releaselock(&pviol_lock);
	    race_detected = true;
	    break;
	  }
	  if (f_entry.r2_task != NULL && taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), f_entry.r2_task, threadid)) {
  Pmy_getlock(&pviol_lock);
	    pall_violations.insert( std::pair<ADDRINT,
				   struct Pviolation* >(addr,
						       new Pviolation(new Pviolation_data(curStepNode, accessType),
								     new Pviolation_data(f_entry.r2_task, READ))) );
  Pmy_releaselock(&pviol_lock);
	    race_detected = true;
	    break;
	  }
	}
      }
      if (f_entry.lockset != 0 && curLockset == f_entry.lockset){
	f_insert_index = i;
      }

    }

    if (!race_detected) {
      int insert_index = -1;

      std::vector<struct Address_data>* access_list = dr_address_data->access_list;

      if (access_list != NULL) {
	for (std::vector<struct Address_data>::iterator it=access_list->begin();
	     it!=access_list->end(); ++it) {
	  struct Address_data& add_data = *it;
	  //check if intersection of lockset is empty
	  if (((~curLockset) & (~add_data.lockset)) == 0) {
	    //check for data race
	    if (add_data.w1_task != NULL &&
		taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), add_data.w1_task, threadid)) {

  Pmy_getlock(&pviol_lock);
	      pall_violations.insert( std::pair<ADDRINT,
				     struct Pviolation* >(addr,
							 new Pviolation(new Pviolation_data(curStepNode, accessType),
								       new Pviolation_data(add_data.w1_task, WRITE))) );
  Pmy_releaselock(&pviol_lock);
	      race_detected = true;
	      break;
	    }
	    if (add_data.w2_task != NULL &&
		taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), add_data.w2_task, threadid)) {

  Pmy_getlock(&pviol_lock);
	      pall_violations.insert( std::pair<ADDRINT,
				     struct Pviolation* >(addr,
							 new Pviolation(new Pviolation_data(curStepNode, accessType),
								       new Pviolation_data(add_data.w2_task, WRITE))) );
  Pmy_releaselock(&pviol_lock);
	      race_detected = true;
	      break;
	    }
	    if (accessType == WRITE) {
	      if (add_data.r1_task != NULL && taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), add_data.r1_task, threadid)) {
  Pmy_getlock(&pviol_lock);
		pall_violations.insert( std::pair<ADDRINT,
				       struct Pviolation* >(addr,
							   new Pviolation(new Pviolation_data(curStepNode, accessType),
									 new Pviolation_data(add_data.r1_task, READ))) );
  Pmy_releaselock(&pviol_lock);
		race_detected = true;
		break;
	      }
	      if (add_data.r2_task != NULL && taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), add_data.r2_task, threadid)) {
  Pmy_getlock(&pviol_lock);
		pall_violations.insert( std::pair<ADDRINT,
				       struct Pviolation* >(addr,
							   new Pviolation(new Pviolation_data(curStepNode, accessType),
									 new Pviolation_data(add_data.r2_task, READ))) );
  Pmy_releaselock(&pviol_lock);
		race_detected = true;
		break;
	      }
	    }
	  }
	  if (curLockset == add_data.lockset){
	    insert_index = it - access_list->begin();
	  }
	}
      }

      if(!race_detected) {
	if(f_insert_index != -1) {
	  struct Address_data& f_entry = dr_address_data->f_entries[f_insert_index];
	  if (f_entry.lockset == 0) {
	    f_entry.lockset = curLockset;
	    if (accessType == READ) {
	      f_entry.r1_task = curStepNode;
	    } else {
	      f_entry.w1_task = curStepNode;;
	    }
	  } else {
	    if (accessType == WRITE) {
	      //f_entry.wr_task = taskGraph->rightmostNode(curStepNode, f_entry.wr_task);
	      bool par_w1 = false, par_w2 = false;
	      if (f_entry.w1_task)
		par_w1 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),f_entry.w1_task, threadid);
	      if (f_entry.w2_task)
		par_w2 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),f_entry.w2_task, threadid);

	      if ((f_entry.w1_task == NULL && f_entry.w2_task == NULL) ||
		  (f_entry.w1_task == NULL && !par_w2) ||
		  (f_entry.w2_task == NULL && !par_w1) ||
		  (!par_w1 && !par_w2)) {
		f_entry.w1_task = curStepNode;
		f_entry.w2_task = NULL;
	      } else if (par_w1 && par_w2) {
		struct AFTask* lca12 = taskGraph->LCA(f_entry.w1_task, f_entry.w2_task);
		struct AFTask* lca1s = taskGraph->LCA(f_entry.w1_task, curStepNode);
		//struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
		if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
		  f_entry.w1_task = curStepNode;
	      } else if (f_entry.w2_task == NULL && par_w1) {
		f_entry.w2_task = curStepNode;
	      }
	    } else { //accessType == READ
	      bool par_r1 = false, par_r2 = false;
	      if (f_entry.r1_task)
		par_r1 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),f_entry.r1_task, threadid);
	      if (f_entry.r2_task)
		par_r2 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),f_entry.r2_task, threadid);

	      if ((f_entry.r1_task == NULL && f_entry.r2_task == NULL) ||
		  (f_entry.r1_task == NULL && !par_r2) ||
		  (f_entry.r2_task == NULL && !par_r1) ||
		  (!par_r1 && !par_r2)) {
		f_entry.r1_task = curStepNode;
		f_entry.r2_task = NULL;
	      } else if (par_r1 && par_r2) {
		struct AFTask* lca12 = taskGraph->LCA(f_entry.r1_task, f_entry.r2_task);
		struct AFTask* lca1s = taskGraph->LCA(f_entry.r1_task, curStepNode);
		//struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
		if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
		  f_entry.r1_task = curStepNode;
	      } else if (f_entry.r2_task == NULL && par_r1) {
		f_entry.r2_task = curStepNode;
	      }
	    }
	  }
	} else {
	  if (insert_index == -1) {
	    std::vector<struct Address_data>* access_list = dr_address_data->access_list;

	    if (access_list == NULL) {
	      dr_address_data->access_list = new std::vector<struct Address_data>();
	      access_list = dr_address_data->access_list;
	    }
	    // form address data
	    struct Address_data address_data;
	    address_data.lockset = curLockset;
	    if (accessType == READ) {
	      address_data.r1_task = curStepNode;
	    } else {
	      address_data.w1_task = curStepNode;
	    }
	    // add to access history
	    access_list->push_back(address_data);
	  } else {
	    // update access history
	    struct Address_data& update_data = access_list->at(insert_index);

	    if (accessType == WRITE) {
	      //update_data.wr_task = taskGraph->rightmostNode(curStepNode, update_data.wr_task);
	      bool par_w1 = false, par_w2 = false;
	      if (update_data.w1_task)
		par_w1 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),update_data.w1_task, threadid);
	      if (update_data.w2_task)
		par_w2 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),update_data.w2_task, threadid);

	      if ((update_data.w1_task == NULL && update_data.w2_task == NULL) ||
		  (update_data.w1_task == NULL && !par_w2) ||
		  (update_data.w2_task == NULL && !par_w1) ||
		  (!par_w1 && !par_w2)) {
		update_data.w1_task = curStepNode;
		update_data.w2_task = NULL;
	      } else if (par_w1 && par_w2) {
		struct AFTask* lca12 = taskGraph->LCA(update_data.w1_task, update_data.w2_task);
		struct AFTask* lca1s = taskGraph->LCA(update_data.w1_task, curStepNode);
		//struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
		if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
		  update_data.w1_task = curStepNode;
	      } else if (update_data.w2_task == NULL && par_w1) {
		update_data.w2_task = curStepNode;
	      }
	    } else { //accessType == READ
	      bool par_r1 = false, par_r2 = false;
	      if (update_data.r1_task)
		par_r1 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),update_data.r1_task, threadid);
	      if (update_data.r2_task)
		par_r2 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),update_data.r2_task, threadid);

	      if ((update_data.r1_task == NULL && update_data.r2_task == NULL) ||
		  (update_data.r1_task == NULL && !par_r2) ||
		  (update_data.r2_task == NULL && !par_r1) ||
		  (!par_r1 && !par_r2)) {
		update_data.r1_task = curStepNode;
		update_data.r2_task = NULL;
	      } else if (par_r1 && par_r2) {
		struct AFTask* lca12 = taskGraph->LCA(update_data.r1_task, update_data.r2_task);
		struct AFTask* lca1s = taskGraph->LCA(update_data.r1_task, curStepNode);
		//struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
		if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
		  update_data.r1_task = curStepNode;
	      } else if (update_data.r2_task == NULL && par_r1) {
		update_data.r2_task = curStepNode;
	      }
	    }
	  }
	}
      }
    }
  }
  /////////////////////////////////////////////////////

  PIN_ReleaseLock(&dr_address_data->addr_lock);
  //PIN_ReleaseLock(&lock);
 
#ifdef DEBUG_TIME
	time1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Pmy_getlock(&pdebug_lock);
	precordmemt += time1-time0;
 Pmy_releaselock(&pdebug_lock);
#endif

}

extern "C" void PRecordMem(THREADID threadid, void * access_addr, AccessType accessType) {
#ifdef DEBUG_TIME
 Pmy_getlock(&pdebug_lock);
	precordmemn++;
 Pmy_releaselock(&pdebug_lock);
 unsigned  time0 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 unsigned time1;
#endif

  // Exceptions
  ADDRINT addr = (ADDRINT) access_addr;
  if(exceptions(threadid, addr)){
    //PIN_ReleaseLock(&lock);
#ifdef DEBUG_TIME
	time1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Pmy_getlock(&pdebug_lock);
	precordmemt += time1-time0;
 Pmy_releaselock(&pdebug_lock);
#endif
    return;
  }

  //get lockset and current step node
  size_t curLockset = (thd_lockset[threadid].top())->createLockset();
  struct AFTask* curStepNode = taskGraph->getCurTask(threadid);

  /////////////////////////////////////////////////////
  // check for access pattern and update shadow space

  size_t primary_index = (addr >> 22) & 0x3ff;
  PAIR* x = pshadow_space + primary_index;
  (x->first).compare_and_swap(1, 0);
  Pmy_getlock(&(x->first));
//  struct Dr_Address_Data* primary_entry = pshadow_space[primary_index];

  if (x->second == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(struct Dr_Address_Data);
  struct Dr_Address_Data*  primary_entry = (struct Dr_Address_Data*)mmap(0, sec_length, PROT_READ| PROT_WRITE,
						     MMAP_FLAGS, -1, 0);
    x->second = primary_entry;

    //initialize all locksets to 0xffffffff
    // for (size_t i = 0; i< SS_SEC_TABLE_ENTRIES;i++) {
    //   struct Dr_Address_Data& dr_address_data = primary_entry[i];
    //   for (int j = 0 ; j < NUM_FIXED_ENTRIES ; j++) {
    // 	(dr_address_data.f_entries[j]).lockset = 0xffffffff;
    //   }
    // }
  }

  Pmy_releaselock(&(x->first));
  struct Dr_Address_Data* primary_entry = x->second;
  //size_t primary_index = (addr >> 22) & 0x3ff;
  //struct Dr_Address_Data* primary_entry = pshadow_space[primary_index];

  //if (primary_entry == NULL) {
  //  size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(struct Dr_Address_Data);
  //  primary_entry = (struct Dr_Address_Data*)mmap(0, sec_length, PROT_READ| PROT_WRITE,
  //      					     MMAP_FLAGS, -1, 0);
  //  pshadow_space[primary_index] = primary_entry;

  //  //initialize all locksets to 0xffffffff
  //  // for (size_t i = 0; i< SS_SEC_TABLE_ENTRIES;i++) {
  //  //   struct Dr_Address_Data& dr_address_data = primary_entry[i];
  //  //   for (int j = 0 ; j < NUM_FIXED_ENTRIES ; j++) {
  //  // 	(dr_address_data.f_entries[j]).lockset = 0xffffffff;
  //  //   }
  //  //}
  //}

  size_t offset = (addr) & 0x3fffff;
  struct Dr_Address_Data* dr_address_data = primary_entry + offset;

  PIN_GetLock(&dr_address_data->addr_lock, 0);

  if (dr_address_data == NULL) {
    // first access to the location. add access history to shadow space
    std::cout << "FIRST ACCESS\n";
    if (accessType == READ) {
      (dr_address_data->f_entries[0]).lockset = curLockset;
      (dr_address_data->f_entries[0]).r1_task = curStepNode;
    } else {
      (dr_address_data->f_entries[0]).lockset = curLockset;
      (dr_address_data->f_entries[0]).w1_task = curStepNode;
    }
  } else {
    // check for data race with each access history entry

    bool race_detected = false;
    int f_insert_index = -1;

    for(int i = 0 ; i < NUM_FIXED_ENTRIES; i++) {
      if(race_detected && f_insert_index != -1) break;

      struct Address_data& f_entry = dr_address_data->f_entries[i];

      if (f_insert_index == -1 && f_entry.lockset == 0) {
	f_insert_index = i;
	break;
      }

      //std::bitset<64> x(curLockset);
      //std::bitset<64> y(f_entry.lockset);
      //std::cout << "Cur = " << x << " fentry = " << y << std::endl;

      // if (addr == /*6325048*/6329176) {
      // 	std::cout << "ADDR INTEREST - ACC TYPE = " << accessType << "tid = " << PtidToTaskIdMap[threadid].top() << std::endl;
      // 	//std::cout << "lockset = " << curLockset;
      // 	std::bitset<64> x(curLockset);
      // 	std::bitset<64> y(f_entry.lockset);
      // 	std::cout << "Cur = " << x << " fentry = " << y << std::endl;
      // }

      if ((f_entry.lockset != 0) && (((~curLockset) & (~f_entry.lockset)) == 0)) {
	//check for data race
	if (f_entry.w1_task != NULL &&
	    taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), f_entry.w1_task, threadid)) {
  Pmy_getlock(&pviol_lock);
	  pall_violations.insert( std::pair<ADDRINT,
				 struct Pviolation* >(addr,
						     new Pviolation(new Pviolation_data(curStepNode, accessType),
								   new Pviolation_data(f_entry.w1_task, WRITE))) );
  Pmy_releaselock(&pviol_lock);
	  race_detected = true;
	  break;
	}
	if (f_entry.w2_task != NULL &&
	    taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), f_entry.w2_task, threadid)) {
  Pmy_getlock(&pviol_lock);
	  pall_violations.insert( std::pair<ADDRINT,
				 struct Pviolation* >(addr,
						     new Pviolation(new Pviolation_data(curStepNode, accessType),
								   new Pviolation_data(f_entry.w2_task, WRITE))) );
  Pmy_releaselock(&pviol_lock);
	  race_detected = true;
	  break;
	}

	if (accessType == WRITE) {
	  if (f_entry.r1_task != NULL && taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), f_entry.r1_task, threadid)) {
  Pmy_getlock(&pviol_lock);
	    pall_violations.insert( std::pair<ADDRINT,
				   struct Pviolation* >(addr,
						       new Pviolation(new Pviolation_data(curStepNode, accessType),
								     new Pviolation_data(f_entry.r1_task, READ))) );
  Pmy_releaselock(&pviol_lock);
	    race_detected = true;
	    break;
	  }
	  if (f_entry.r2_task != NULL && taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), f_entry.r2_task, threadid)) {
  Pmy_getlock(&pviol_lock);
	    pall_violations.insert( std::pair<ADDRINT,
				   struct Pviolation* >(addr,
						       new Pviolation(new Pviolation_data(curStepNode, accessType),
								     new Pviolation_data(f_entry.r2_task, READ))) );
  Pmy_releaselock(&pviol_lock);
	    race_detected = true;
	    break;
	  }
	}
      }
      if (f_entry.lockset != 0 && curLockset == f_entry.lockset){
	f_insert_index = i;
      }

    }

    if (!race_detected) {
      int insert_index = -1;

      std::vector<struct Address_data>* access_list = dr_address_data->access_list;

      if (access_list != NULL) {
	for (std::vector<struct Address_data>::iterator it=access_list->begin();
	     it!=access_list->end(); ++it) {
	  struct Address_data& add_data = *it;
	  //check if intersection of lockset is empty
	  if (((~curLockset) & (~add_data.lockset)) == 0) {
	    //check for data race
	    if (add_data.w1_task != NULL &&
		taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), add_data.w1_task, threadid)) {

  Pmy_getlock(&pviol_lock);
	      pall_violations.insert( std::pair<ADDRINT,
				     struct Pviolation* >(addr,
							 new Pviolation(new Pviolation_data(curStepNode, accessType),
								       new Pviolation_data(add_data.w1_task, WRITE))) );
  Pmy_releaselock(&pviol_lock);
	      race_detected = true;
	      break;
	    }
	    if (add_data.w2_task != NULL &&
		taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), add_data.w2_task, threadid)) {

  Pmy_getlock(&pviol_lock);
	      pall_violations.insert( std::pair<ADDRINT,
				     struct Pviolation* >(addr,
							 new Pviolation(new Pviolation_data(curStepNode, accessType),
								       new Pviolation_data(add_data.w2_task, WRITE))) );
  Pmy_releaselock(&pviol_lock);
	      race_detected = true;
	      break;
	    }
	    if (accessType == WRITE) {
	      if (add_data.r1_task != NULL && taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), add_data.r1_task, threadid)) {
  Pmy_getlock(&pviol_lock);
		pall_violations.insert( std::pair<ADDRINT,
				       struct Pviolation* >(addr,
							   new Pviolation(new Pviolation_data(curStepNode, accessType),
									 new Pviolation_data(add_data.r1_task, READ))) );
  Pmy_releaselock(&pviol_lock);
		race_detected = true;
		break;
	      }
	      if (add_data.r2_task != NULL && taskGraph->areParallel(PtidToTaskIdMap[threadid].top(), add_data.r2_task, threadid)) {
  Pmy_getlock(&pviol_lock);
		pall_violations.insert( std::pair<ADDRINT,
				       struct Pviolation* >(addr,
							   new Pviolation(new Pviolation_data(curStepNode, accessType),
									 new Pviolation_data(add_data.r2_task, READ))) );
  Pmy_releaselock(&pviol_lock);
		race_detected = true;
		break;
	      }
	    }
	  }
	  if (curLockset == add_data.lockset){
	    insert_index = it - access_list->begin();
	  }
	}
      }

      if(!race_detected) {
	if(f_insert_index != -1) {
	  struct Address_data& f_entry = dr_address_data->f_entries[f_insert_index];
	  if (f_entry.lockset == 0) {
	    f_entry.lockset = curLockset;
	    if (accessType == READ) {
	      f_entry.r1_task = curStepNode;
	    } else {
	      f_entry.w1_task = curStepNode;;
	    }
	  } else {
	    if (accessType == WRITE) {
	      //f_entry.wr_task = taskGraph->rightmostNode(curStepNode, f_entry.wr_task);
	      bool par_w1 = false, par_w2 = false;
	      if (f_entry.w1_task)
		par_w1 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),f_entry.w1_task, threadid);
	      if (f_entry.w2_task)
		par_w2 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),f_entry.w2_task, threadid);

	      if ((f_entry.w1_task == NULL && f_entry.w2_task == NULL) ||
		  (f_entry.w1_task == NULL && !par_w2) ||
		  (f_entry.w2_task == NULL && !par_w1) ||
		  (!par_w1 && !par_w2)) {
		f_entry.w1_task = curStepNode;
		f_entry.w2_task = NULL;
	      } else if (par_w1 && par_w2) {
		struct AFTask* lca12 = taskGraph->LCA(f_entry.w1_task, f_entry.w2_task);
		struct AFTask* lca1s = taskGraph->LCA(f_entry.w1_task, curStepNode);
		//struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
		if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
		  f_entry.w1_task = curStepNode;
	      } else if (f_entry.w2_task == NULL && par_w1) {
		f_entry.w2_task = curStepNode;
	      }
	    } else { //accessType == READ
	      bool par_r1 = false, par_r2 = false;
	      if (f_entry.r1_task)
		par_r1 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),f_entry.r1_task, threadid);
	      if (f_entry.r2_task)
		par_r2 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),f_entry.r2_task, threadid);

	      if ((f_entry.r1_task == NULL && f_entry.r2_task == NULL) ||
		  (f_entry.r1_task == NULL && !par_r2) ||
		  (f_entry.r2_task == NULL && !par_r1) ||
		  (!par_r1 && !par_r2)) {
		f_entry.r1_task = curStepNode;
		f_entry.r2_task = NULL;
	      } else if (par_r1 && par_r2) {
		struct AFTask* lca12 = taskGraph->LCA(f_entry.r1_task, f_entry.r2_task);
		struct AFTask* lca1s = taskGraph->LCA(f_entry.r1_task, curStepNode);
		//struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
		if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
		  f_entry.r1_task = curStepNode;
	      } else if (f_entry.r2_task == NULL && par_r1) {
		f_entry.r2_task = curStepNode;
	      }
	    }
	  }
	} else {
	  if (insert_index == -1) {
	    std::vector<struct Address_data>* access_list = dr_address_data->access_list;

	    if (access_list == NULL) {
	      dr_address_data->access_list = new std::vector<struct Address_data>();
	      access_list = dr_address_data->access_list;
	    }
	    // form address data
	    struct Address_data address_data;
	    address_data.lockset = curLockset;
	    if (accessType == READ) {
	      address_data.r1_task = curStepNode;
	    } else {
	      address_data.w1_task = curStepNode;
	    }
	    // add to access history
	    access_list->push_back(address_data);
	  } else {
	    // update access history
	    struct Address_data& update_data = access_list->at(insert_index);

	    if (accessType == WRITE) {
	      //update_data.wr_task = taskGraph->rightmostNode(curStepNode, update_data.wr_task);
	      bool par_w1 = false, par_w2 = false;
	      if (update_data.w1_task)
		par_w1 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),update_data.w1_task, threadid);
	      if (update_data.w2_task)
		par_w2 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),update_data.w2_task, threadid);

	      if ((update_data.w1_task == NULL && update_data.w2_task == NULL) ||
		  (update_data.w1_task == NULL && !par_w2) ||
		  (update_data.w2_task == NULL && !par_w1) ||
		  (!par_w1 && !par_w2)) {
		update_data.w1_task = curStepNode;
		update_data.w2_task = NULL;
	      } else if (par_w1 && par_w2) {
		struct AFTask* lca12 = taskGraph->LCA(update_data.w1_task, update_data.w2_task);
		struct AFTask* lca1s = taskGraph->LCA(update_data.w1_task, curStepNode);
		//struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
		if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
		  update_data.w1_task = curStepNode;
	      } else if (update_data.w2_task == NULL && par_w1) {
		update_data.w2_task = curStepNode;
	      }
	    } else { //accessType == READ
	      bool par_r1 = false, par_r2 = false;
	      if (update_data.r1_task)
		par_r1 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),update_data.r1_task, threadid);
	      if (update_data.r2_task)
		par_r2 = taskGraph->areParallel(PtidToTaskIdMap[threadid].top(),update_data.r2_task, threadid);

	      if ((update_data.r1_task == NULL && update_data.r2_task == NULL) ||
		  (update_data.r1_task == NULL && !par_r2) ||
		  (update_data.r2_task == NULL && !par_r1) ||
		  (!par_r1 && !par_r2)) {
		update_data.r1_task = curStepNode;
		update_data.r2_task = NULL;
	      } else if (par_r1 && par_r2) {
		struct AFTask* lca12 = taskGraph->LCA(update_data.r1_task, update_data.r2_task);
		struct AFTask* lca1s = taskGraph->LCA(update_data.r1_task, curStepNode);
		//struct AFTask* lca2s = static_cast<struct AFTask*>(taskGraph->LCA(addr_vec->r2_task, curTask));
		if (lca1s->depth < lca12->depth /*|| lca2s->depth < lca12->depth*/)
		  update_data.r1_task = curStepNode;
	      } else if (update_data.r2_task == NULL && par_r1) {
		update_data.r2_task = curStepNode;
	      }
	    }
	  }
	}
      }
    }
  }

  /////////////////////////////////////////////////////
  PIN_ReleaseLock(&dr_address_data->addr_lock);
  //PIN_ReleaseLock(&lock);
#ifdef DEBUG_TIME
	time1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Pmy_getlock(&pdebug_lock);
	precordmemt += time1-time0;
 Pmy_releaselock(&pdebug_lock);
#endif

}

static void preport_access(struct Pviolation_data* a) {
  preport << a->task->taskId << "          ";
  if (a->accessType == READ)
    preport << "READ\n";
  else
    preport << "WRITE\n";
}

static void preport_DR(ADDRINT addr, struct Pviolation_data* a1, struct Pviolation_data* a2) {
  preport << "** Data Race Detected at " << addr << " **\n";
  preport << "Accesses:\n";
  preport << "TaskId    AccessType\n";
  preport_access(a1);
  preport_access(a2);
  preport << "*******************************\n";
}

extern "C" void PFini()
{
  preport.open("Pviolations.out");

  for (std::map<ADDRINT,struct Pviolation*>::iterator it=pall_violations.begin();
       it!=pall_violations.end(); ++it) {
    struct Pviolation* viol = it->second;
    preport_DR(it->first, viol->a1, viol->a2);
  }
  preport.close();
  std::cout << "\n\nNumber of violations = " << pall_violations.size() << std::endl;
#ifdef DEBUG_TIME
  std::cout << "DEBUG_TIME Mode in Ptracer\n";
  std::cout << "Number of tasks spawned " << Ptask_id_ctr << endl; 
  std::cout << "Total time in recordmem function " << precordmemt << " milliseconds" << endl;
  std::cout << "Total time in recordaccess function " << recordaccesst << " milliseconds" << endl;
  std::cout << "Total time in recordmem + recordaccess function " << recordaccesst + precordmemt << " milliseconds" << endl;
  std::cout << "No of times recordmem is called is " << precordmemn  << endl;
  std::cout << "No of times recordaccess is called is " << recordaccessn  << endl;
  std::cout << "No of times recordmem + recordaccess is called is " << recordaccessn + precordmemn << endl;
  taskGraph->print_taskgraph();
#endif

}
