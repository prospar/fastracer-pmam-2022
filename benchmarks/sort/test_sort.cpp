#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#if HAVE_OPENMP
#include <omp.h>
#endif

#if HAVE_CILKPLUS
#include <cilk/cilk.h>
#endif

#include <algorithm>
// GNU C++ 4.4.3 or later, or VS2010 or later, are known to support std::move
#if (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) >= 40403 || _MSC_VER >= 1600
#include <utility>
#define HAVE_MOVE 1
#else
namespace std {
// Declare fake "move"
template <typename T> T &move(T &x) { return x; }
template <typename T> T *move(const T *xs, const T *xe, T *ys) { return std::copy(xs, xe, ys); }
} // namespace std
#endif

#include "tbb/task_scheduler_init.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <tbb/tbb.h>

#define CHECK_STABILITY 1
#define EXPONENTIAL_DISTRIBUTION 2
#define UNIFORM_DISTRIBUTION 3
#define STRING_KEY 4

//#define MODE STRING_KEY
#define MODE UNIFORM_DISTRIBUTION
//#define MODE CHECK_STABILITY

// PROSPAR: Support timers
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;

unsigned Random() { return rand() * (RAND_MAX + 1u) + rand(); }

#if MODE == CHECK_STABILITY

// Test that checks stability of sort and checks for construct/assign/destruct errors
tbb::atomic<int> KeyCount;

const char SequenceName[] = "uniform distribution of (char,int)";

class T {
  char value;
  size_t index;
  T *self;
  T(int value_, size_t index_) : value(value_ & 0xF), index(index_), self(this) { ++KeyCount; }

public:
  friend bool operator<(const T &x, const T &y) { return x.value < y.value; }
  friend T MakeRandomT(size_t index) { return T(rand(), index); }
  friend int IndexOf(const T &x) {
    assert(x.self == &x);
    return x.index;
  }
  T() : self(this) { ++KeyCount; }
  T(const T &x) : value(x.value), index(x.index), self(this) {
    assert(x.self == &x);
    ++KeyCount;
  }
  void operator=(const T &x) {
    assert(self == this);
    assert(x.self == &x);
    value = x.value;
    index = x.index;
  }
  ~T() {
    assert(self == this);
    --KeyCount;
    self = NULL;
  }
};
#elif MODE == EXPONENTIAL_DISTRIBUTION
const char SequenceName[] = "exponential distribution of double";

typedef double T;

inline T MakeRandomT(size_t) { return -std::log(double(Random() + 1)); }
#elif MODE == UNIFORM_DISTRIBUTION
const char SequenceName[] = "uniform distribution of int";

typedef int T;

inline T MakeRandomT(size_t) { return Random(); }
#elif MODE == STRING_KEY
#include <string>
const char SequenceName[] = "strings";

typedef std::string T;

inline T MakeRandomT(size_t) {
  char buffer[20];
  sprintf(buffer, "%d", int(Random()));
  return buffer;
}
#else
#error Unknown MODE
#endif

#include "quicksort_util.h"
#include "sample_sort_util.h"

// namespace Ex0 {
//#include "serial/serial_merge.h"
//}

// namespace Ex5 {         // TBB semi-recursive quicksort using task_group
//#include "tbb/quicksort_tbb_taskgroup.h"
//}

namespace Ex6 { // TBB tail-tasking with guaranteed limit on number of tasks
#include "quicksort_tbb_task.h"
}

// clang-format off
namespace Ex7 { // TBB mergesort
#include "merge_tbb.h"
#include "merge_sort_tbb.h"
} // namespace Ex7

namespace Ex8 {      // TBB Sample Sort
using namespace Ex6; // Need quicksort from here
#include "map_keys_to_bins_tbb.h"
#include "bin_tbb.h"
#include "repack_and_subsort_tbb.h"
#include "sample_sort.h"
} // namespace Ex8
// clang-format on

// For comparing with STL sort
namespace Ref {

void stl_sort(T *first, T *last) { std::sort(first, last); }

} // namespace Ref

void TestSerialMerge(void (*mergeRoutine)(T *xs, T *xe, T *ys, T *ye, T *zs)) {
  const size_t n = 10;
  T x[2][n], z[2 * n], w[2 * n];
  size_t m[2];
  for (m[0] = 0; m[0] < n; ++m[0])
    for (m[1] = 0; m[1] < n; ++m[1]) {
      for (size_t i = 0; i < 2; ++i) {
        for (size_t j = 0; j < m[i]; ++j)
          x[i][j] = MakeRandomT(j);
        std::sort(x[i], x[i] + m[i]);
      }
      (*mergeRoutine)(x[0], x[0] + m[0], x[1], x[1] + m[1], z);
      std::merge(x[0], x[0] + m[0], x[1], x[1] + m[1], w);
      for (int k = 0; k < m[0] + m[1]; ++k) {
        assert(!(z[k] < w[k] || w[k] < z[k]));
#if MODE == CHECK_STABILITY
        assert(IndexOf(z[k]) == IndexOf(w[k]));
#endif
      }
    }
}

#if 0
// Small defaults for debugging
size_t M = 1;
size_t N = 100000;
#else
// Big defaults for timing
size_t M = 10;
size_t N = 1000000;
#endif
T *Unsorted, *Expected, *Actual;

void InitializeTestData() {
  Unsorted = new T[M * N];
  Expected = new T[M * N];
  Actual = new T[M * N];
  for (size_t i = 0; i < M; ++i) {
    for (size_t j = 0; j < N; ++j) {
      Unsorted[i * N + j] = MakeRandomT(j);
    }
    std::copy(Unsorted + i * N, Unsorted + (i + 1) * N, Expected + i * N);
    std::stable_sort(Expected + i * N, Expected + (i + 1) * N);
  }
}

template <typename S>
void TestSort(S sortToBeTested, const char *what, bool shouldBeStable = false) {
  std::copy(Unsorted, Unsorted + M * N, Actual);
  // Warm up run-time
  sortToBeTested(Actual, Actual + N);
  tbb::tick_count t0 = tbb::tick_count::now();
  for (int i = 1; i < M; ++i) {
#if MODE == CHECK_STABILITY
    KeyCount = 0;
#endif
    sortToBeTested(Actual + i * N, Actual + (i + 1) * N);
#if MODE == CHECK_STABILITY
    assert(KeyCount == 0);
#endif
  }
  tbb::tick_count t1 = tbb::tick_count::now();
  for (size_t k = 0; k < M * N; ++k) {
    if (Actual[k] < Expected[k] || Expected[k] < Actual[k]) {
      printf("Error for %s\n", what);
      return;
    }
#if MODE == CHECK_STABILITY
    if (shouldBeStable) {
      if (IndexOf(Actual[k]) != IndexOf(Expected[k])) {
        printf("Stability error for %s\n", what);
        return;
      }
    }
#endif /* MODE==CHECK_STABILITY */
  }
  printf("%30s\t%5.2f\n", what, (t1 - t0).seconds());
}

template <void (*F)(T *, T *, T *, bool)> void call_parallel_merge_sort(T *xs, T *xe) {
  T *zs = new T[xe - xs];
  (*F)(xs, xe, zs, true);
  delete[] zs;
}

int main(int argc, char *argv[]) {
  // PROSPAR: Track execution time
  HRTimer bench_start = HR::now();

  if (argc > 1)
    M = strtol(argv[1], 0, 0);
  ++M; // Add one for the warmup sort
  if (argc > 2)
    N = strtol(argv[2], 0, 0);
  srand(2);

  // taskGraph = new AFTaskGraph();
  // AV_Detector* av_detector = new AV_Detector();
  // av_detector->Activate();
  // tbb::task_scheduler_init init(16);

  // PROSPAR: Track execution time
  HRTimer roi_start = HR::now();

  TD_Activate();

  // Serial merge routine is not used elsewhere, so it needs to be tested separately.
  // TestSerialMerge( Ex0::serial_merge );

  InitializeTestData();
  std::printf("Testing for %d sorts of length %d for %s\n", int(M - 1), int(N), SequenceName);

  // Test serial sort
  TestSort(Ref::stl_sort, "STL sort");

  // Test TBB sorts
  // TestSort(Ex5::parallel_quicksort,"TBB quicksort taskgroup");
  TestSort(Ex6::parallel_quicksort, "TBB quicksort continuation");
  TestSort(call_parallel_merge_sort<Ex7::parallel_merge_sort>, "TBB mergesort", true);
  TestSort(Ex8::parallel_sample_sort, "TBB samplesort");

  // PROSPAR: Track execution time
  HRTimer roi_end = HR::now();
  HRTimer bench_end = HR::now();

  std::printf("Done\n");

  // PROSPAR: Changes
  auto roi_duration = duration_cast<milliseconds>(roi_end - roi_start).count();
  auto bench_duration = duration_cast<milliseconds>(bench_end - bench_start).count();
  std::cout << "PROSPAR: ROI Time: " << roi_duration << " milliseconds\n"
            << "PROSPAR: Benchmark Time: " << bench_duration << " milliseconds\n";

  Fini();
  // taskGraph->Fini(); // PROSPAR: Is this not required?
}
