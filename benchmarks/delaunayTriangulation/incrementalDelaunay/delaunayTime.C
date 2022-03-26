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

#include "delaunay.h"
#include "geometry.h"
#include "geometryIO.h"
#include "gettime.h"
#include "parallel.h"
#include "parseCommandLine.h"
#include "tbb/task_scheduler_init.h"
#include "utils.h"
#include <algorithm>
#include <chrono>
#include <iostream>

using namespace std;
using namespace benchIO;

// PROSPAR: Support timers
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;

// *************************************************************
//  TIMING
// *************************************************************

void timeDelaunay(point2d *pts, intT n, int rounds, char *outFile) {
  triangles<point2d> R;
  for (int i = 0; i < rounds; i++) {
    if (i != 0)
      R.del();
    startTime();
    R = delaunay(pts, n);
    nextTimeN();
  }
  cout << endl;

  if (outFile != NULL)
    writeTrianglesToFile(R, outFile);
  R.del();
}

int parallel_main(int argc, char *argv[]) {
  // PROSPAR: Track execution time
  HRTimer bench_start = HR::now();

  // tbb::task_scheduler_init init(16);
  TD_Activate();
  commandLine P(argc, argv, "[-o <outFile>] [-r <rounds>] <inFile>");
  char *iFile = P.getArgument(0);
  char *oFile = P.getOptionValue("-o");
  int rounds = P.getOptionIntValue("-r", 1);

  _seq<point2d> PIn = readPointsFromFile<point2d>(iFile);

  HRTimer roi_start = HR::now();

  timeDelaunay(PIn.A, PIn.n, rounds, oFile);

  HRTimer roi_end = HR::now();
  HRTimer bench_end = HR::now();
  auto roi_duration = duration_cast<milliseconds>(roi_end - roi_start).count();
  auto bench_duration = duration_cast<milliseconds>(bench_end - bench_start).count();
  std::cout << "PROSPAR: ROI Time: " << roi_duration << " milliseconds\n"
            << "PROSPAR: Benchmark Time: " << bench_duration << " milliseconds\n";

  Fini();
  // taskGraph->Fini();
}
