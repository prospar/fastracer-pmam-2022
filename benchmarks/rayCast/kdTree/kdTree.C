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

// clang-format off
#include <algorithm>
#include "parallel.h"
#include "geometry.h"
#include "gettime.h"
#include "sequence.h"
#include "float.h"
#include "sampleSort.h"
#include "ray.h"
#include "kdTree.h"
#include "rayTriangleIntersect.h"
// clang-format on

using namespace std;

int CHECK = 0; // if set checks 10 rays against brute force method
// PROSPAR: Renamed to avoid conflict
int KDSTATS = 0; // if set prints out some tree statistics

// Constants for deciding when to stop recursion in building the KDTree
float CT = 6.0;
float CL = 1.25;
float maxExpand = 1.6;
int maxRecursionDepth = 25;

// Constant for switching to sequential versions
int minParallelSize = 500000;

typedef pointT::floatT floatT;
typedef _vect3d<floatT> vectT;
typedef triangles<pointT> trianglesT;
typedef ray<pointT> rayT;

float boxSurfaceArea(BoundingBox B) {
  float r0 = B[0].max - B[0].min;
  float r1 = B[1].max - B[1].min;
  float r2 = B[2].max - B[2].min;
  return 2 * (r0 * r1 + r1 * r2 + r0 * r2);
}

float epsilon = .0000001;
range fixRange(float minv, float maxv) {
  if (minv == maxv)
    return range(minv, minv + epsilon);
  else
    return range(minv, maxv);
}

inline float inBox(pointT p, BoundingBox B) {
  return (p.x >= (B[0].min - epsilon) && p.x <= (B[0].max + epsilon) &&
          p.y >= (B[1].min - epsilon) && p.y <= (B[1].max + epsilon) &&
          p.z >= (B[2].min - epsilon) && p.z <= (B[2].max + epsilon));
}

// sequential version of best cut
cutInfo bestCutSerial(event *E, range r, range r1, range r2, intT n) {
  if (r.max - r.min == 0.0)
    return cutInfo(FLT_MAX, r.min, n, n);
  float area = 2 * (r1.max - r1.min) * (r2.max - r2.min);
  float diameter = 2 * ((r1.max - r1.min) + (r2.max - r2.min));

  // calculate cost of each possible split
  intT inLeft = 0;
  intT inRight = n / 2;
  float minCost = FLT_MAX;
  intT k = 0;
  intT rn = inLeft;
  intT ln = inRight;
  for (intT i = 0; i < n; i++) {
    float cost;
    if (IS_END(E[i]))
      inRight--;
    float leftLength = E[i].v - r.min;
    float leftArea = area + diameter * leftLength;
    float rightLength = r.max - E[i].v;
    float rightArea = area + diameter * rightLength;
    cost = (leftArea * inLeft + rightArea * inRight);
    if (cost < minCost) {
      rn = inRight;
      ln = inLeft;
      minCost = cost;
      k = i;
    }
    if (IS_START(E[i]))
      inLeft++;
  }
  return cutInfo(minCost, E[k].v, ln, rn);
}

tbb::task *BestCut::execute() {
  __exec_begin__(getTaskId());

  if (n < minParallelSize) {
    *ret_val = bestCutSerial(E, r, r1, r2, n);
    __exec_end__(getTaskId());
    return NULL;
  }

  if (r.max - r.min == 0.0) {
    *ret_val = cutInfo(FLT_MAX, r.min, n, n);
    __exec_end__(getTaskId());
    return NULL;
  }

  // area of two orthogonal faces
  float orthogArea = 2 * ((r1.max - r1.min) * (r2.max - r2.min));

  // length of diameter of orthogonal face
  float diameter = 2 * ((r1.max - r1.min) + (r2.max - r2.min));

  // count number that end before i
  intT *upperC = newA(intT, n);
  // parallel_for (intT i=0; i <n; i++) upperC[i] = IS_END(E[i]);
  parallel_for(tbb::blocked_range<size_t>(0, n), [=](tbb::blocked_range<size_t> r, size_t thdId) {
    for (size_t i = r.begin(); i != r.end(); ++i)
      upperC[i] = IS_END(E[i]);
  });

  intT u = sequence::plusScan(upperC, upperC, n);

  // calculate cost of each possible split location
  float *cost = newA(float, n);
  // parallel_for (intT i=0; i <n; i++) {
  //   intT inLeft = i - upperC[i];
  //   intT inRight = n/2 - (upperC[i] + IS_END(E[i]));
  //   float leftLength = E[i].v - r.min;
  //   float leftArea = orthogArea + diameter * leftLength;
  //   float rightLength = r.max - E[i].v;
  //   float rightArea = orthogArea + diameter * rightLength;
  //   cost[i] = (leftArea * inLeft + rightArea * inRight);
  // }
  parallel_for(tbb::blocked_range<size_t>(0, n), [=](tbb::blocked_range<size_t> r1, size_t thdId) {
    for (size_t i = r1.begin(); i != r1.end(); ++i) {
      intT inLeft = i - upperC[i];
      intT inRight = n / 2 - (upperC[i] + IS_END(E[i]));
      float leftLength = E[i].v - r.min;
      float leftArea = orthogArea + diameter * leftLength;
      float rightLength = r.max - E[i].v;
      float rightArea = orthogArea + diameter * rightLength;
      cost[i] = (leftArea * inLeft + rightArea * inRight);
    }
  });

  // find minimum across all (maxIndex with less is minimum)
  intT k = sequence::maxIndex(cost, n, less<float>());

  float c = cost[k];
  intT ln = k - upperC[k];
  intT rn = n / 2 - (upperC[k] + IS_END(E[k]));
  free(upperC);
  free(cost);
  *ret_val = cutInfo(c, E[k].v, ln, rn);

  __exec_end__(getTaskId());
  return NULL;
}

// // parallel version of best cut
// cutInfo bestCut(event* E, range r, range r1, range r2, intT n) {
//   if (n < minParallelSize)
//     return bestCutSerial(E, r, r1, r2, n);
//   if (r.max - r.min == 0.0) return cutInfo(FLT_MAX, r.min, n, n);

//   // area of two orthogonal faces
//   float orthogArea = 2 * ((r1.max-r1.min) * (r2.max-r2.min));

//   // length of diameter of orthogonal face
//   float diameter = 2 * ((r1.max-r1.min) + (r2.max-r2.min));

//   // count number that end before i
//   intT* upperC = newA(intT,n);
//   parallel_for (intT i=0; i <n; i++) upperC[i] = IS_END(E[i]);
//   intT u = sequence::plusScan(upperC, upperC, n);

//   // calculate cost of each possible split location
//   float* cost = newA(float,n);
//   parallel_for (intT i=0; i <n; i++) {
//     intT inLeft = i - upperC[i];
//     intT inRight = n/2 - (upperC[i] + IS_END(E[i]));
//     float leftLength = E[i].v - r.min;
//     float leftArea = orthogArea + diameter * leftLength;
//     float rightLength = r.max - E[i].v;
//     float rightArea = orthogArea + diameter * rightLength;
//     cost[i] = (leftArea * inLeft + rightArea * inRight);
//   }

//   // find minimum across all (maxIndex with less is minimum)
//   intT k = sequence::maxIndex(cost,n,less<float>());

//   float c = cost[k];
//   intT ln = k - upperC[k];
//   intT rn = n/2 - (upperC[k] + IS_END(E[k]));
//   free(upperC); free(cost);
//   return cutInfo(c, E[k].v, ln, rn);
// }

eventsPair splitEventsSerial(range *boxes, event *events, float cutOff, intT n) {
  intT l = 0;
  intT r = 0;
  event *eventsLeft = newA(event, n);
  event *eventsRight = newA(event, n);
  for (intT i = 0; i < n; i++) {
    intT b = GET_INDEX(events[i]);
    if (boxes[b].min < cutOff) {
      eventsLeft[l++] = events[i];
      if (boxes[b].max > cutOff)
        eventsRight[r++] = events[i];
    } else
      eventsRight[r++] = events[i];
  }
  return eventsPair(_seq<event>(eventsLeft, l), _seq<event>(eventsRight, r));
}

tbb::task *SplitEvents::execute() {
  __exec_begin__(getTaskId());

  if (n < minParallelSize) {
    *ret_val = splitEventsSerial(boxes, events, cutOff, n);
    __exec_end__(getTaskId());
    return NULL;
  }
  bool *lower = newA(bool, n);
  bool *upper = newA(bool, n);

  // parallel_for (intT i=0; i <n; i++) {
  //   intT b = GET_INDEX(events[i]);
  //   lower[i] = boxes[b].min < cutOff;
  //   upper[i] = boxes[b].max > cutOff;
  // }
  parallel_for(tbb::blocked_range<size_t>(0, n), [=](tbb::blocked_range<size_t> r, size_t thdId) {
    for (size_t i = r.begin(); i != r.end(); ++i) {
      // RecordMem(thdId, &(events[i]), READ);
      intT b = GET_INDEX(events[i]);
      RecordMem(thdId, &boxes[b].min, READ);
      RecordMem(thdId, &(cutOff), READ);
      // RecordMem(thdId, &(lower[i]), WRITE);
      lower[i] = boxes[b].min < cutOff;
      RecordMem(thdId, &boxes[b].max, READ);
      // RecordMem(thdId, &(upper[i]), WRITE);
      RecordMem(thdId, &(cutOff), READ);
      upper[i] = boxes[b].max > cutOff;
    }
  });

  _seq<event> L = sequence::pack(events, lower, n);
  _seq<event> R = sequence::pack(events, upper, n);
  free(lower);
  free(upper);

  *ret_val = eventsPair(L, R);

  __exec_end__(getTaskId());
  return NULL;
}

// #if 0
// eventsPair splitEvents(range* boxes, event* events, float cutOff, intT n) {
//   if (n < minParallelSize)
//     return splitEventsSerial(boxes, events, cutOff, n);
//   bool* lower = newA(bool,n);
//   bool* upper = newA(bool,n);

//   parallel_for (intT i=0; i <n; i++) {
//     intT b = GET_INDEX(events[i]);
//     lower[i] = boxes[b].min < cutOff;
//     upper[i] = boxes[b].max > cutOff;
//   }

//   _seq<event> L = sequence::pack(events, lower, n);
//   _seq<event> R = sequence::pack(events, upper, n);
//   free(lower); free(upper);

//   return eventsPair(L,R);
// }

// // n is the number of events (i.e. twice the number of triangles)
// treeNode* generateNode(Boxes boxes, Events events, BoundingBox B,
// 		       intT n, intT maxDepth) {
//   //cout << "n=" << n << " maxDepth=" << maxDepth << endl;
//   if (n <= 2 || maxDepth == 0)
//     return new treeNode(events, n, B);

//   // loop over dimensions and find the best cut across all of them
//   cutInfo cuts[3];
//   for (int d = 0; d < 3; d++)
//     cuts[d] = cilk_spawn bestCut(events[d], B[d], B[(d+1)%3], B[(d+2)%3], n);
//   cilk_sync;

//   int cutDim = 0;
//   for (int d = 1; d < 3; d++)
//     if (cuts[d].cost < cuts[cutDim].cost) cutDim = d;

//   range* cutDimRanges = boxes[cutDim];
//   float cutOff = cuts[cutDim].cutOff;
//   float area = boxSurfaceArea(B);
//   float bestCost = CT + CL * cuts[cutDim].cost/area;
//   float origCost = (float) (n/2);

//   // quit recursion early if best cut is not very good
//   if (bestCost >= origCost ||
//       cuts[cutDim].numLeft + cuts[cutDim].numRight > maxExpand * n/2)
//     return new treeNode(events, n, B);

//   // declare structures for recursive calls
//   BoundingBox BBL;
//   for (int i=0; i < 3; i++) BBL[i] = B[i];
//   BBL[cutDim] = range(BBL[cutDim].min, cutOff);
//   event* leftEvents[3];
//   intT nl;

//   BoundingBox BBR;
//   for (int i=0; i < 3; i++) BBR[i] = B[i];
//   BBR[cutDim] = range(cutOff, BBR[cutDim].max);
//   event* rightEvents[3];
//   intT nr;

//   // now split each event array to the two sides
//   eventsPair X[3];
//   for (int d = 0; d < 3; d++)
//      X[d] = cilk_spawn splitEvents(cutDimRanges, events[d], cutOff, n);
//   cilk_sync;

//   for (int d = 0; d < 3; d++) {
//     leftEvents[d] = X[d].first.A;
//     rightEvents[d] = X[d].second.A;
//     if (d == 0) {
//       nl = X[d].first.n;
//       nr = X[d].second.n;
//     } else if (X[d].first.n != nl || X[d].second.n != nr) {
//       cout << "kdTree: mismatched lengths, something wrong" << endl;
//       abort();
//     }
//   }

//   // free old events and make recursive calls
//   for (int i=0; i < 3; i++) free(events[i]);
//   treeNode *L;
//   treeNode *R;
//   L = cilk_spawn generateNode(boxes, leftEvents, BBL, nl, maxDepth-1);
//   R = generateNode(boxes, rightEvents, BBR, nr, maxDepth-1);
//   cilk_sync;

//   return new treeNode(L, R, cutDim, cutOff, B);
// }
// #endif

tbb::task *GenerateNode::execute() {
  __exec_begin__(getTaskId());

  if (n <= 2 || maxDepth == 0) {
    *ret_val = new treeNode(events, n, B);
    __exec_end__(getTaskId());
    return NULL;
  }

  // loop over dimensions and find the best cut across all of them
  cutInfo cuts[3];

  set_ref_count(3 + 1);
  for (int d = 0; d < 3; d++) {
    // cuts[d] = cilk_spawn bestCut(events[d], B[d], B[(d+1)%3], B[(d+2)%3], n);
    tbb::task &a = *new (tbb::task::allocate_child())
                       BestCut(&cuts[d], events[d], B[d], B[(d + 1) % 3], B[(d + 2) % 3], n);
    tbb::t_debug_task::spawn(a);
  }
  // cilk_sync;
  tbb::t_debug_task::wait_for_all();

  int cutDim = 0;
  for (int d = 1; d < 3; d++)
    if (cuts[d].cost < cuts[cutDim].cost)
      cutDim = d;

  range *cutDimRanges = boxes[cutDim];
  float cutOff = cuts[cutDim].cutOff;
  float area = boxSurfaceArea(B);
  float bestCost = CT + CL * cuts[cutDim].cost / area;
  float origCost = (float)(n / 2);

  // quit recursion early if best cut is not very good
  if (bestCost >= origCost || cuts[cutDim].numLeft + cuts[cutDim].numRight > maxExpand * n / 2) {
    *ret_val = new treeNode(events, n, B);
    __exec_end__(getTaskId());
    return NULL;
  }

  // declare structures for recursive calls
  BoundingBox BBL;
  for (int i = 0; i < 3; i++)
    BBL[i] = B[i];
  BBL[cutDim] = range(BBL[cutDim].min, cutOff);
  event *leftEvents[3];
  intT nl;

  BoundingBox BBR;
  for (int i = 0; i < 3; i++)
    BBR[i] = B[i];
  BBR[cutDim] = range(cutOff, BBR[cutDim].max);
  event *rightEvents[3];
  intT nr;

  // now split each event array to the two sides
  eventsPair X[3];

  set_ref_count(3 + 1);
  for (int d = 0; d < 3; d++) {
    // X[d] = cilk_spawn splitEvents(cutDimRanges, events[d], cutOff, n);
    tbb::task &a =
        *new (tbb::task::allocate_child()) SplitEvents(&X[d], cutDimRanges, events[d], cutOff, n);
    tbb::t_debug_task::spawn(a);
  }
  tbb::t_debug_task::wait_for_all();
  // cilk_sync;

  for (int d = 0; d < 3; d++) {
    leftEvents[d] = X[d].first.A;
    rightEvents[d] = X[d].second.A;
    if (d == 0) {
      nl = X[d].first.n;
      nr = X[d].second.n;
    } else if (X[d].first.n != nl || X[d].second.n != nr) {
      cout << "kdTree: mismatched lengths, something wrong" << endl;
      abort();
    }
  }

  // free old events and make recursive calls
  for (int i = 0; i < 3; i++)
    free(events[i]);
  treeNode *L;
  treeNode *R;
  // L = cilk_spawn generateNode(boxes, leftEvents, BBL, nl, maxDepth-1);
  // R = generateNode(boxes, rightEvents, BBR, nr, maxDepth-1);
  // cilk_sync;

  set_ref_count(3);
  tbb::task &a =
      *new (tbb::task::allocate_child()) GenerateNode(&L, boxes, leftEvents, BBL, nl, maxDepth - 1);
  tbb::task &b = *new (tbb::task::allocate_child())
                     GenerateNode(&R, boxes, rightEvents, BBR, nr, maxDepth - 1);
  tbb::t_debug_task::spawn(a);
  tbb::t_debug_task::spawn_and_wait_for_all(b);

  *ret_val = new treeNode(L, R, cutDim, cutOff, B);

  __exec_end__(getTaskId());
  return NULL;
}

intT tcount = 0;
intT ccount = 0;

// Given an a ray, a bounding box, and a sequence of triangles, returns the
// index of the first triangle the ray intersects inside the box.
// The triangles are given by n indices I into the triangle array Tri.
// -1 is returned if there is no intersection
intT findRay(rayT r, intT *I, intT n, triangles<pointT> Tri, BoundingBox B) {
  if (KDSTATS) {
    tcount += n;
    ccount += 1;
  }
  pointT *P = Tri.P;
  floatT tMin = FLT_MAX;
  intT k = -1;
  for (intT i = 0; i < n; i++) {
    intT j = I[i];
    triangle *tr = Tri.T + j;
    pointT m[3] = {P[tr->C[0]], P[tr->C[1]], P[tr->C[2]]};
    floatT t = rayTriangleIntersect(r, m);
    if (t > 0.0 && t < tMin && inBox(r.o + r.d * t, B)) {
      tMin = t;
      k = j;
    }
  }
  return k;
}

// Given a ray and a tree node find the index of the first triangle the
// ray intersects inside the box represented by that node.
// -1 is returned if there is no intersection
intT findRay(rayT r, treeNode *TN, trianglesT Tri) {
  // cout << "TN->n=" << TN->n << endl;
  if (TN->isLeaf())
    return findRay(r, TN->triangleIndices, TN->n, Tri, TN->box);
  pointT o = r.o;
  vectT d = r.d;

  floatT oo[3] = {o.x, o.y, o.z};
  floatT dd[3] = {d.x, d.y, d.z};

  // intersect ray with splitting plane
  int k0 = TN->cutDim;
  int k1 = (k0 == 2) ? 0 : k0 + 1;
  int k2 = (k0 == 0) ? 2 : k0 - 1;
  point2d o_p(oo[k1], oo[k2]);
  vect2d d_p(dd[k1], dd[k2]);
  // does not yet deal with dd[k0] == 0
  floatT scale = (TN->cutOff - oo[k0]) / dd[k0];
  point2d p_i = o_p + d_p * scale;

  range rx = TN->box[k1];
  range ry = TN->box[k2];
  floatT d_0 = dd[k0];

  // decide which of the two child boxes the ray intersects
  enum { LEFT, RIGHT, BOTH };
  int recurseTo = LEFT;
  if (p_i.x < rx.min) {
    if (d_p.x * d_0 > 0)
      recurseTo = RIGHT;
  } else if (p_i.x > rx.max) {
    if (d_p.x * d_0 < 0)
      recurseTo = RIGHT;
  } else if (p_i.y < ry.min) {
    if (d_p.y * d_0 > 0)
      recurseTo = RIGHT;
  } else if (p_i.y > ry.max) {
    if (d_p.y * d_0 < 0)
      recurseTo = RIGHT;
  } else
    recurseTo = BOTH;

  if (recurseTo == RIGHT)
    return findRay(r, TN->right, Tri);
  else if (recurseTo == LEFT)
    return findRay(r, TN->left, Tri);
  else if (d_0 > 0) {
    intT t = findRay(r, TN->left, Tri);
    if (t >= 0)
      return t;
    else
      return findRay(r, TN->right, Tri);
  } else {
    intT t = findRay(r, TN->right, Tri);
    if (t >= 0)
      return t;
    else
      return findRay(r, TN->left, Tri);
  }
}

void processRays(trianglesT Tri, rayT *rays, intT numRays, treeNode *R, intT *results) {
  // parallel_for (intT i= 0; i < numRays; i++)
  // results[i] = findRay(rays[i], R, Tri);
  parallel_for(tbb::blocked_range<size_t>(0, numRays),
               [=](tbb::blocked_range<size_t> r, size_t thdId) {
                 for (size_t i = r.begin(); i != r.end(); ++i)
                   results[i] = findRay(rays[i], R, Tri);
               });
}

intT *rayCast(triangles<pointT> Tri, ray<pointT> *rays, intT numRays) {
  startTime();

  // Extract triangles into a separate array for each dimension with
  // the lower and upper bound for each triangle in that dimension.
  Boxes boxes;
  intT n = Tri.numTriangles;
  for (int d = 0; d < 3; d++)
    boxes[d] = newA(range, n);
  pointT *P = Tri.P;
  // parallel_for (intT i=0; i < n; i++) {
  //   pointT p0 = P[Tri.T[i].C[0]];
  //   pointT p1 = P[Tri.T[i].C[1]];
  //   pointT p2 = P[Tri.T[i].C[2]];
  //   boxes[0][i] = fixRange(min(p0.x,min(p1.x,p2.x)),max(p0.x,max(p1.x,p2.x)));
  //   boxes[1][i] = fixRange(min(p0.y,min(p1.y,p2.y)),max(p0.y,max(p1.y,p2.y)));
  //   boxes[2][i] = fixRange(min(p0.z,min(p1.z,p2.z)),max(p0.z,max(p1.z,p2.z)));
  // }
  parallel_for(tbb::blocked_range<size_t>(0, n), [=](tbb::blocked_range<size_t> r, size_t thdId) {
    for (size_t i = r.begin(); i != r.end(); ++i) {
      pointT p0 = P[Tri.T[i].C[0]];
      pointT p1 = P[Tri.T[i].C[1]];
      pointT p2 = P[Tri.T[i].C[2]];
      boxes[0][i] = fixRange(min(p0.x, min(p1.x, p2.x)), max(p0.x, max(p1.x, p2.x)));
      boxes[1][i] = fixRange(min(p0.y, min(p1.y, p2.y)), max(p0.y, max(p1.y, p2.y)));
      boxes[2][i] = fixRange(min(p0.z, min(p1.z, p2.z)), max(p0.z, max(p1.z, p2.z)));
    }
  });

  // Loop over the dimensions creating an array of events for each
  // dimension, sorting each one, and extracting the bounding box
  // from the first and last elements in the sorted events in each dim.
  Events events;
  BoundingBox boundingBox;
  for (int d = 0; d < 3; d++) {
    events[d] = newA(event, 2 * n); // freed while generating tree
    // parallel_for (intT i=0; i <n; i++) {
    //   events[d][2*i] = event(boxes[d][i].min, i, START);
    //   events[d][2*i+1] = event(boxes[d][i].max, i, END);
    // }

    parallel_for(tbb::blocked_range<size_t>(0, n), [=](tbb::blocked_range<size_t> r, size_t thdId) {
      for (size_t i = r.begin(); i != r.end(); ++i) {
        events[d][2 * i] = event(boxes[d][i].min, i, START);
        events[d][2 * i + 1] = event(boxes[d][i].max, i, END);
      }
    });

    compSort(events[d], n * 2, cmpVal());
    boundingBox[d] = range(events[d][0].v, events[d][2 * n - 1].v);
  }
  nextTime("generate and sort events");

  // build the tree
  intT recursionDepth = min(maxRecursionDepth, utils::log2Up(n) - 1);
  // treeNode* R = generateNode(boxes, events, boundingBox, n*2,
  //			     recursionDepth);

  treeNode *R;
  tbb::task &main_task = *new (tbb::task::allocate_root())
                             GenerateNode(&R, boxes, events, boundingBox, n * 2, recursionDepth);
  tbb::t_debug_task::spawn_root_and_wait(main_task);
  nextTime("build tree");

  if (KDSTATS)
    cout << "Triangles across all leaves = " << R->n << " Leaves = " << R->leaves << endl;
  for (int d = 0; d < 3; d++)
    free(boxes[d]);

  // get the intersections
  intT *results = newA(intT, numRays);
  processRays(Tri, rays, numRays, R, results);
  nextTime("intersect rays");
  // treeNode::del(R);
  tbb::task &main_task1 = *new (tbb::task::allocate_root()) Del(R);
  tbb::t_debug_task::spawn_root_and_wait(main_task1);

  nextTime("delete tree");

  if (CHECK) {
    int nr = 10;
    intT *indx = newA(intT, n);
    /*parallel_*/ for (intT i = 0; i < n; i++)
      indx[i] = i;
    for (int i = 0; i < nr; i++) {
      cout << results[i] << endl;
      if (findRay(rays[i], indx, n, Tri, boundingBox) != results[i]) {
        cout << "bad intersect in checking ray intersection" << endl;
        abort();
      }
    }
  }

  if (KDSTATS)
    cout << "tcount=" << tcount << " ccount=" << ccount << endl;
  return results;
}
