#include "lca_hash.H"
#include <iostream>

struct lca_hash** lca_hash::lcaHashTable;
size_t lca_hash::tableSize;

size_t lca_hash::getHTIndex (size_t arg){
  size_t ret_val = arg%tableSize;
  return ret_val;
}

struct lca_hash* lca_hash::getHTElement(size_t arg){
  size_t index = getHTIndex(arg);
  struct lca_hash* ret_val = lcaHashTable[index];
  if (ret_val != NULL && ret_val->curAddr == arg) {
    return ret_val;
  } else {
    return NULL;
  }
}

void lca_hash::setHTElement(size_t arg, struct lca_hash* entry){
  size_t index = getHTIndex(arg);
  lcaHashTable[index] = entry;
}

int lca_hash::createHashTable() {
  tableSize =  LCA_TABLE_SIZE;
  lcaHashTable = (struct lca_hash **) malloc(sizeof(struct lca_hash*) * tableSize);
  if (lcaHashTable == NULL) {
      return 0;
  } else {
    for(size_t i=0;i<tableSize;i++) {
      lcaHashTable[i] = NULL;
    }
  }
  return 1;
}

void lca_hash::updateEntry(size_t cur_addr, size_t rem_addr, bool lca_result, struct AFTask* lca)
{    
  struct lca_hash* cur = getHTElement(cur_addr);
  if (cur == NULL) {//then create
    //initialize values
    cur = new lca_hash(cur_addr, rem_addr,lca_result, lca);
    setHTElement(cur_addr, cur);
  } else {
    my_getlock(&(cur->hash_lock));
    cur->remAddr = rem_addr;
    cur->lca_result = lca_result;
    cur->lca = lca;
    my_releaselock(&(cur->hash_lock));
  }
}

ParallelStatus lca_hash::checkParallel(size_t cur_addr, size_t rem_addr) {
  struct lca_hash* cur = getHTElement(cur_addr);
  size_t remAddr;
  bool lca_result;

  if (cur != NULL) {
    my_getlock(&(cur->hash_lock));
    remAddr = cur->remAddr;
    lca_result = cur->lca_result;
    my_releaselock(&(cur->hash_lock));
    if  (remAddr == rem_addr) {
      if(lca_result == true)
	return TRUE;
      else
	return FALSE;
    } else {
      struct lca_hash* rem = getHTElement(rem_addr);
      if (rem != NULL) {
    my_getlock(&(rem->hash_lock));
    remAddr = rem->remAddr;
    lca_result = rem->lca_result;
    my_releaselock(&(rem->hash_lock));
	if  (remAddr == cur_addr) {
	  if(lca_result == true)
	    return TRUE;
	  else
	    return FALSE;	  
	} else {
	  return NA;
	}
      } else {
	return NA;
      }
    }
  } else { //cur == NULL
    struct lca_hash* rem = getHTElement(rem_addr);
    if (rem != NULL) {
    my_getlock(&(rem->hash_lock));
    remAddr = rem->remAddr;
    lca_result = rem->lca_result;
    my_releaselock(&(rem->hash_lock));
      if  (remAddr == cur_addr) {
	if(lca_result == true)
	  return TRUE;
	else
	  return FALSE;	  
      } else {
	return NA;
      }
    } else {
      return NA;
    }    
  }
}

struct AFTask* lca_hash::getLCA(size_t cur_addr, size_t rem_addr) {
  struct lca_hash* cur = getHTElement(cur_addr);
  size_t remAddr ;
  struct AFTask* lca;
  if (cur != NULL) {
    my_getlock(&(cur->hash_lock));
    remAddr = cur->remAddr;
    lca = cur->lca;
    my_releaselock(&(cur->hash_lock));
    if  (remAddr == rem_addr) {
      return lca;
    } else {
      struct lca_hash* rem = getHTElement(rem_addr);
      if (rem != NULL) {
    my_getlock(&(rem->hash_lock));
    remAddr = rem->remAddr;
    lca = rem->lca;
    my_releaselock(&(rem->hash_lock));
	if  (remAddr == cur_addr) {
	  return lca;
	} else {
	  return NULL;
	}
      } else {
	return NULL;
      }
    }
  } else { //cur == NULL
    struct lca_hash* rem = getHTElement(rem_addr);
    if (rem != NULL) {
    my_getlock(&(rem->hash_lock));
    remAddr = rem->remAddr;
    lca = rem->lca;
    my_releaselock(&(rem->hash_lock));
      if  (remAddr == cur_addr) {
	return lca;
      } else {
	return NULL;
      }
    } else {
      return NULL;
    }    
  }
}
