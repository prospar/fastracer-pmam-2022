// This code is part of the Problem Based Benchmark Suite (PBBS)
// Copyright (c) 2011 Guy Blelloch and the PBBS team
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights (to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Stores coordinate of event along with index to its triangle and type
// Stores type of event (START or END) in lowest bit of index
#include "t_debug_task.h"

struct event {
  float v;
  intT p;
  event(float value, intT index, bool type) 
    : v(value), p((index << 1) + type) {}
  event() {}
};
#define START 0
#define IS_START(_event) (!(_event.p & 1))
#define END 1
#define IS_END(_event) ((_event.p & 1))
#define GET_INDEX(_event) (_event.p >> 1)

struct cmpVal { bool operator() (event a, event b) {return a.v < b.v;}};

struct range {
  float min;
  float max;
  range(float _min, float _max) : min(_min), max(_max) {}
  range() {}
};

typedef range* Boxes[3];
typedef event* Events[3];
typedef range BoundingBox[3];

static std::ostream& operator<<(std::ostream& os, const BoundingBox B) {
 return os << B[0].min << ":" << B[0].max << " + " 
	   << B[1].min << ":" << B[1].max << " + " 
	   << B[2].min << ":" << B[2].max;
}

struct cutInfo {
  float cost;
  float cutOff;
  intT numLeft;
  intT numRight;
cutInfo(float _cost, float _cutOff, intT nl, intT nr) 
: cost(_cost), cutOff(_cutOff), numLeft(nl), numRight(nr) {}
  cutInfo() {}
};

struct treeNode {
  treeNode *left;
  treeNode *right;
  BoundingBox box;
  int cutDim;
  float cutOff;
  intT* triangleIndices;
  intT n;
  intT leaves;
  
  bool isLeaf() {return (triangleIndices != NULL);}

  treeNode(treeNode* L, treeNode* R, 
	   int _cutDim, float _cutOff, BoundingBox B) 
    : left(L), right(R), triangleIndices(NULL), cutDim(_cutDim), 
      cutOff(_cutOff) {
    for (int i=0; i < 3; i++) box[i] = B[i];
    n = L->n + R->n;
    leaves = L->leaves + R->leaves;
  }

  treeNode(Events E, intT _n, BoundingBox B)
    : left(NULL), right(NULL) {

    event* events = E[0];

    // extract indices from events
    triangleIndices = newA(intT, _n/2);
    intT k = 0;
    for (intT i = 0; i < _n; i++) 
      if (IS_START(events[i]))
	triangleIndices[k++] = GET_INDEX(events[i]);

    n = _n/2;
    leaves = 1;
    for (int i=0; i < 3; i++) {
      box[i] = B[i];
      free(E[i]);
    }
  }
  
  /* static void del(treeNode *T) { */
  /*   if (T->isLeaf())  free(T->triangleIndices);  */
  /*   else { */
  /*     cilk_spawn del(T->left); */
  /*     del(T->right); */
  /*     cilk_sync; */
  /*   } */
  /*   free(T); */
  /* } */
};

class Del: public tbb::t_debug_task {
 private:
  treeNode CHECK_AV * T;
 public:
 Del(treeNode *T):T(T) {}
  tbb::task* execute() {
    __exec_begin__(getTaskId());
      
    if (T->isLeaf())  free(T->triangleIndices); 
    else {
      //cilk_spawn del(T->left);
      //del(T->right);
      //cilk_sync;
      tbb::task& a = *new(tbb::task::allocate_child()) Del(T->left);
      tbb::task& b = *new(tbb::task::allocate_child()) Del(T->right);
      set_ref_count(3);
      tbb::t_debug_task::spawn(a);
      tbb::t_debug_task::spawn(b);
      tbb::t_debug_task::wait_for_all();
    }
    free(T);

    __exec_end__(getTaskId());
    return NULL;
  }
};

class BestCut: public tbb::t_debug_task {
private:
  cutInfo* CHECK_AV ret_val;
  event* CHECK_AV E;
  range CHECK_AV r;
  range CHECK_AV r1;
  range CHECK_AV r2;
  intT CHECK_AV n;
  
public:
 BestCut(cutInfo* ret_val, event* E, range r, range r1, range r2, intT n): ret_val(ret_val), E(E), r(r), r1(r1), r2(r2), n(n)
  {}
  tbb::task* execute();
};

class GenerateNode: public tbb::t_debug_task {
 private:
  treeNode** CHECK_AV ret_val;
  range** CHECK_AV boxes;
  event** CHECK_AV events;
  range* CHECK_AV B;
  intT CHECK_AV n;
  intT CHECK_AV maxDepth;
  
 public:
  GenerateNode(treeNode** ret_val, Boxes boxes, Events events, BoundingBox B,intT n, intT maxDepth) :ret_val(ret_val), boxes(boxes), events(events), B(B), n(n), maxDepth(maxDepth)
    {}
  
  tbb::task* execute();
};

typedef pair<_seq<event>, _seq<event> > eventsPair;

class SplitEvents: public tbb::t_debug_task {
 private:
  eventsPair* CHECK_AV ret_val;
  range* CHECK_AV boxes;
  event* CHECK_AV events;
  float CHECK_AV cutOff;
  intT CHECK_AV n;
  
 public:
 SplitEvents(eventsPair* ret_val, range* boxes, event* events, float cutOff, intT n) :ret_val(ret_val), boxes(boxes), events(events), cutOff(cutOff), n(n)
  {}
  
  tbb::task* execute();
};
