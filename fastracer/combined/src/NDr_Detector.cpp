#include "Nexec_calls.h"
#include <bitset>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <vector>

using namespace std;
using namespace tbb;

typedef pair<tbb::atomic<size_t>,Nvarstate*> subpair;
typedef pair<tbb::atomic<size_t>, subpair*> PAIR;
PAIR* nshadow_space;
my_lock nshadow_space_lock(1);
#ifdef DEBUG
GlobalStats stats;
#endif

#ifdef DEBUG_TIME
my_lock ndebug_lock(0);
unsigned nrecordmemt=0,recordmemi=0,rd1=0,rd2=0,wr1=0,wr2=0;
unsigned nrecordmemn = 0;
#endif
size_t nthread = 0;
const size_t SS_PRIMARY_TABLE_ENTRIES = ((size_t)1024);
const size_t SS_SEC_TABLE_ENTRIES = ((size_t)4 * (size_t)1024 * (size_t)1024);
unsigned read_1 = 0,read_2=0,write_1=0,write_2=0;

std::ofstream nreport;


std::map<ADDRINT, struct Nviolation*> nall_violations;
my_lock nviol_lock(0);

void error(ADDRINT addr, size_t ftid, AccessType ftype, size_t stid, AccessType stype) {
 Nmy_getlock(&nviol_lock);
  nall_violations.insert(make_pair(
      addr, new Nviolation(new Nviolation_data(ftid, ftype), new Nviolation_data(stid, stype))));
 Nmy_releaselock(&nviol_lock);
}

void NTD_Activate() {
  size_t primary_length = (SS_PRIMARY_TABLE_ENTRIES) * sizeof(PAIR);
  nshadow_space = (PAIR*)mmap(0, primary_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
  //  assert(nshadow_space != (void *)-1);
}

void read(ADDRINT addr,Nvarstate* var,Nthreadstate* t) {
#ifdef DEBUG_TIME
	unsigned  time1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
  uint32_t curclock = t->clock;
  uint32_t curtid = t->tid;
 Nlockstate x;
  size_t curlockset = t->lockset;
 size_t tid;
 int cursize = var->cursize;
 Nlockstate* curlockstate = NULL;
for(int i =0; i < cursize; ++i ){
    x = (var->v)[i];
    if(x.lockset == curlockset)
    {
        curlockstate = &x;
    }
    	
    if((x.lockset) & (curlockset))
        continue;

  if ( ((x.rtid1)==curtid && (x.rc1)==curclock) || ((x.rtid2)==curtid && (x.rc2)==curclock) ) { //same epoch
    continue;
  }
  
  if ( (x.wc1) > (t->C[x.wtid1]) )
  {var->test = 0; error(addr, x.wtid1 , WRITE, curtid, READ);
#ifdef DEBUG_TIME	 
	unsigned  time2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Nmy_getlock(&ndebug_lock);
	rd1 += time2-time1;
 Nmy_releaselock(&ndebug_lock);
#endif
	  return;} // detecting write-read race

  if ( (x.wc2) > (t->C[x.wtid2]) )
  {var->test = 0; error(addr, x.wtid2 , WRITE, curtid, READ);
#ifdef DEBUG_TIME	 
	unsigned  time2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Nmy_getlock(&ndebug_lock);
	rd1 += time2-time1;
 Nmy_releaselock(&ndebug_lock);
#endif
	  return;} // detecting write-read race

}

#ifdef DEBUG_TIME
	unsigned  time2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Nmy_getlock(&ndebug_lock);
	rd1 += time2-time1;
 Nmy_releaselock(&ndebug_lock);
#endif
//return; //*****************************6

if(curlockstate){

  if ( (curlockstate->rc1) <= (t->C[curlockstate->rtid1]) )
  {curlockstate->rtid1 = curtid; curlockstate->rc1 = curclock; curlockstate->pr1 = t;
	  goto end;
  }

  if ( (curlockstate->rc2) <= (t->C[curlockstate->rtid2]) )
  {curlockstate->rtid2 = curtid; curlockstate->rc2 = curclock; curlockstate->pr2 = t; 
	  goto end;
  }

  size_t *order1 = curlockstate->pr1->order;
  size_t* order2 = curlockstate->pr2->order;
  size_t* cur = t->order;
  size_t order1_max = curlockstate->pr1->order_cur, order2_max=curlockstate->pr2->order_cur, cur_max=t->order_cur;
  size_t min = std::min( std::min(order1_max,order2_max), cur_max);
  int i=0;
  while(i++ != min){
	  if(order1[i] == order2[i] && order1[i] == cur[i])
		  continue;
	  break;
  }

if(i == cur_max || ((cur[i] != order1[i]) && (cur[i] != order2[i]))){
	if(order1_max >= order2_max){
		curlockstate->pr2=t;
		curlockstate->rc2 = curclock;
		curlockstate->rtid2 = curtid;
	}
       else{
               curlockstate->pr1 = t;
               curlockstate->rc1 = curclock;
	       curlockstate->rtid1 = curtid;
       }
        goto end;
}

if(i == order1_max || ((cur[i] != order1[i]) && (order1[i] != order2[i]))){
	if(cur_max >= order2_max){
		curlockstate->pr2=t;
		curlockstate->rc2 = curclock;
		curlockstate->rtid2 = curtid;
	}
	goto end;
}

if(cur_max >= order1_max){
        curlockstate->pr1 = t;
        curlockstate->rc1 = curclock;
	curlockstate->rtid1 = curtid;
}


 goto end;
}
else
{
	std::cout << "Coming in else block of read\n";
//    curlockstate = new Nlockstate();
//    curlockstate->rc1 = curclock;
//    curlockstate->rtid1 = curtid;
//    curlockstate->pr1 = t;
//    curlockstate->lockset = curlockset;
//    (var->v).push_back(curlockstate);
}


end:

#ifdef DEBUG_TIME
	unsigned  time3 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Nmy_getlock(&ndebug_lock);
	rd2 += time3-time2;
 Nmy_releaselock(&ndebug_lock);
#endif
	return;
}
void write(ADDRINT addr,Nvarstate* var,Nthreadstate* t) {
#ifdef DEBUG_TIME
	unsigned  time1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
  uint32_t curclock = t->clock;
  uint32_t curtid = t->tid;

 Nlockstate x;
  size_t curlockset = t->lockset;
 Nlockstate* curlockstate = NULL;
  int check = 0;
  int cursize = var->cursize;
for(int i = 0; i < cursize; ++i){
    x = (var->v)[i];
    if(x.lockset == curlockset)
    {
        curlockstate = &x;
    }
    	
    if((x.lockset) & (curlockset))
        continue;

  if ( ((x.wtid1)==curtid && (x.wc1)==curclock) || ((x.wtid2)==curtid && (x.wc2)==curclock) ) { //same epoch
    continue;
  }
  
  if ( (x.wc1) > (t->C[x.wtid1]) )
  {var->test = 0; error(addr, x.wtid1 , WRITE, curtid, WRITE); check = 1; goto end1;} // detecting write-write race

  if ( (x.wc2) > (t->C[x.wtid2]) )
  {var->test = 0; error(addr, x.wtid2 , WRITE, curtid, WRITE); check = 1; goto end1;} // detecting write-write race
 
  if ( (x.rc1) > (t->C[x.rtid1]) )
  {var->test = 0; error(addr, x.rtid1 , READ, curtid, WRITE); check = 1; goto end1;} // detecting read-write race

  if ( (x.rc2) > (t->C[x.rtid2]) )
  {var->test = 0; error(addr, x.rtid2 , READ, curtid, READ); check = 1; goto end1;} // detecting read-write race
}
end1:
#ifdef DEBUG_TIME
	unsigned  time2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Nmy_getlock(&ndebug_lock);
	wr1 += time2-time1;
 Nmy_releaselock(&ndebug_lock);
#endif
	if(check)
	return;
//return; //**********************4
if(curlockstate){

  if ( (curlockstate->wc1) <= (t->C[curlockstate->wtid1]) )
  {curlockstate->wtid1 = curtid; curlockstate->wc1 = curclock; curlockstate->pw1 = t; goto end2;}

  if ( (curlockstate->wc2) <= (t->C[curlockstate->wtid2]) )
  {curlockstate->wtid2 = curtid; curlockstate->wc2 = curclock; curlockstate->pw2 = t; goto end2;}



  size_t *order1 = curlockstate->pw1->order;
  size_t* order2 = curlockstate->pw2->order;
  size_t* cur = t->order;
  size_t order1_max = curlockstate->pw1->order_cur, order2_max=curlockstate->pw2->order_cur, cur_max=t->order_cur;
  size_t min = std::min( std::min(order1_max,order2_max), cur_max);
  int i=0;
  while(i++ != min){
	  if(order1[i] == order2[i] && order1[i] == cur[i])
		  continue;
	  break;
  }

if(i == cur_max || ((cur[i] != order1[i]) && (cur[i] != order2[i]))){
	if(order1_max >= order2_max){
		curlockstate->pw2=t;
		curlockstate->wc2 = curclock;
		curlockstate->wtid2 = curtid;
	}
       else{
               curlockstate->pw1 = t;
               curlockstate->wc1 = curclock;
	       curlockstate->wtid1 = curtid;
       }
        goto end2;
}

if(i == order1_max || ((cur[i] != order1[i]) && (order1[i] != order2[i]))){
	if(cur_max >= order2_max){
		curlockstate->pw2=t;
		curlockstate->wc2 = curclock;
		curlockstate->wtid2 = curtid;
	}
	goto end2;
}

if(cur_max >= order1_max){
        curlockstate->pw1 = t;
        curlockstate->wc1 = curclock;
	curlockstate->wtid1 = curtid;
}


 goto end2;
}
else
{
	std::cout << "Coming in else block of write\n";

//
//    curlockstate = newNlockstate();
//    curlockstate->wc1 = curclock;
//    curlockstate->wtid1 = curtid;
//    curlockstate->pw1 = t;
//    curlockstate->lockset = curlockset;
//    (var->v).push_back(curlockstate);
}
end2:
#ifdef DEBUG_TIME
	unsigned  time3 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Nmy_getlock(&ndebug_lock);
	wr2 += time3-time2;
 Nmy_releaselock(&ndebug_lock);
#endif
return;
}

extern "C" void NRecordMem(size_t threadid, void* access_addr, AccessType accesstype) {
#ifdef DEBUG_TIME
 Nmy_getlock(&ndebug_lock);
	++nrecordmemn;
 Nmy_releaselock(&ndebug_lock);
// 	std::cout << "in recordmem\n";
	unsigned  time0 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	unsigned time2=0,time3=0,timee=0;
#endif
//  if(NtidToTaskIdMap[threadid].empty())
//    return;
  size_t tid = NtidToTaskIdMap[threadid].top();
  ADDRINT addr = (ADDRINT)access_addr;
  //std::cout << "In NRecordMem, TaskID: " << tid << "Address: " << addr << "AccessType: " << accesstype << endl;
//  if(nall_violations.count(addr) != 0)
//	  return;
 Nthreadstate* thread;
  concurrent_hash_map<size_t,Nthreadstate*>::accessor ac;
  if (Ntaskid_map.find(ac, tid))
    thread = ac->second;
  else {
	  ac.release();
	  return;
//    thread = new Nthreadstate();
//    thread->tid = tid;
//
//   Ntaskid_map.insert(ac, tid);
//    ac->second = thread;
  }
  ac.release();
  uint32_t curclock = thread ->clock;
  uint32_t curtid = thread->tid;


  size_t primary_index = (addr >> 22) & 0x3ff;
  PAIR* x = nshadow_space + primary_index;
 Nmy_getlock(&(x->first));
  if (x->second == NULL) {
    size_t sec_length = (SS_SEC_TABLE_ENTRIES) * sizeof(subpair);
    subpair* primary_entry =
        (subpair*)mmap(0, sec_length, PROT_READ | PROT_WRITE, MMAP_FLAGS, -1, 0);
    x->second = primary_entry;
  }
 Nmy_releaselock(&(x->first));
  subpair* primary_entry = x->second;
  size_t offset = (addr & 0x3fffff);
  subpair* addrpair = primary_entry + offset;
 Nmy_getlock(&(addrpair->first));
 Nvarstate* var_state = addrpair->second;
  if (var_state == NULL) {
    var_state = new Nvarstate();
    if (accesstype == READ) {
	Nlockstate* l = (var_state->v);
        l->lockset = thread->lockset;
       l->rc1 = curclock;
       l->rtid1 = curtid;
     l->pr1 = thread;
    } else {
       Nlockstate* l = var_state->v;
        l->lockset = thread->lockset;
        l->wc1 = curclock;
	l->wtid1 = curtid;
        l->pw1 = thread;
    }
    addrpair->second = var_state;
  } else {
          if(var_state->test  == 0) {Nmy_releaselock(&(addrpair->first));goto end;} //Accessing the variable having data race
    if (accesstype == READ) {
#ifdef DEBUG_TIME
	time2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
      read(addr, var_state, thread);
#ifdef DEBUG_TIME
	time3 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
    } else {
#ifdef DEBUG_TIME
	time2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
      write(addr, var_state, thread);
#ifdef DEBUG_TIME
	time3 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
#endif
    }
  }

 Nmy_releaselock(&(addrpair->first));
end:
#ifdef DEBUG_TIME
	timee = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 Nmy_getlock(&ndebug_lock);
	nrecordmemt += timee-time0;
	recordmemi += (timee-time0) - (time3-time2);
 Nmy_releaselock(&ndebug_lock);
#endif
	return;
}

extern "C" void NRecordAccess(size_t threadid, void* access_addr, AccessType accesstype) {
  NRecordMem(threadid, access_addr, accesstype);
}

void NCaptureLockAcquire(size_t threadid, ADDRINT lock_addr) {
  if(NtidToTaskIdMap[threadid].empty())
    return;
  size_t tid = NtidToTaskIdMap[threadid].top();

  concurrent_hash_map<size_t,Nthreadstate*>::accessor ac;
 Ntaskid_map.find(ac, tid);
 Nthreadstate* t = ac->second;
  ac.release();

  size_t lockId;
 Nmy_getlock(&Nlock_map_lock);
  if (Nlock_map.count(lock_addr) == 0) {
    lockId = ((size_t)1 << Nlock_ticker++);
   Nlock_map.insert(std::pair<ADDRINT, size_t>(lock_addr, lockId));
  } else {
    lockId =Nlock_map.at(lock_addr);
  }
   Nmy_releaselock(&Nlock_map_lock);
    t->lockset = t->lockset | lockId ;
}

void NCaptureLockRelease(size_t threadid, ADDRINT lock_addr) {
    if(NtidToTaskIdMap[threadid].empty())
    return;
  size_t tid = NtidToTaskIdMap[threadid].top();

  concurrent_hash_map<size_t,Nthreadstate*>::accessor ac;
 Ntaskid_map.find(ac, tid);
 Nthreadstate* t = ac->second;
  ac.release();

  size_t lockId;
 Nmy_getlock(&Nlock_map_lock);
  if (Nlock_map.count(lock_addr) == 0) {
    lockId = ((size_t)1 << Nlock_ticker++);
   Nlock_map.insert(std::pair<ADDRINT, size_t>(lock_addr, lockId));
  } else {
    lockId =Nlock_map.at(lock_addr);
  }
   Nmy_releaselock(&Nlock_map_lock);
    t->lockset = t->lockset & (~(lockId));
}

void paccesstype(AccessType accesstype) {
  if (accesstype == READ)
    nreport << "READ\n";
  if (accesstype == WRITE)
    nreport << "WRITE\n";
}

extern "C" void NFini() {
  nreport.open("Nviolations.out");
  for (std::map<ADDRINT, struct Nviolation*>::iterator i = nall_violations.begin();
       i != nall_violations.end(); ++i) {
    struct Nviolation* viol = i->second;
    nreport << "** Data Race Detected**\n";
    nreport << " Address is :";
    nreport << i->first;
    nreport << "\n";
    nreport << viol->a1->tid;
    paccesstype(viol->a1->accessType);
    nreport << viol->a2->tid;
    paccesstype(viol->a2->accessType);
    nreport << "**************************************\n";
  }
#ifdef DEBUG
  stats.dump();
  std::cout << "DEBUG Mode\n";
#endif
  std::cout << "\n\nNumber of violations = " << nall_violations.size() << std::endl;
#ifdef DEBUG_TIME
  std::cout << "DEBUG_TIME Mode in new_algo\n";
   std::cout << "Number of tasks spawned " << Ntask_id_ctr << endl; 
 std::cout << "Total time in recordmem function " << nrecordmemt << " milliseconds" << endl;
  std::cout << "Total time in recordmem function without read and write  " << recordmemi << "milliseconds" <<  endl;
  std::cout << "Total time in recordmem function without read and write in percentage" << (recordmemi*100.0)/nrecordmemt <<  endl;
  std::cout << "No of times recordmem is called is " << nrecordmemn  << endl;
  std::cout << "Time in Read first part " << (rd1*100.0)/nrecordmemt << endl;
  std::cout << "Time in Read Second part " << (rd2*100.0)/nrecordmemt << endl;
  std::cout << "Time in Write first part " << (wr1*100.0)/nrecordmemt << endl;
  std::cout << "Time in Write Second part " << (wr2*100.0)/nrecordmemt << endl;
  std::cout << "Value of wr2 is " << wr2 << endl;
#endif
  nreport.close();
}
