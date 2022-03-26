#include "kmeans.h"
#include "tbb/parallel_for.h"
#include "tbb/task_scheduler_init.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// PROSPAR: Support timers
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;

// Square root of number of intended centroids
const size_t SQRT_K = 4;
const size_t K = SQRT_K * SQRT_K;
const size_t N = 10000000;

point points[N];
point centroid[K];
cluster_id id[N];

template <typename KMeans> void Test(KMeans compute_k_means) {
  // Create a synthetic dataset of points that are clustered around integer grid point.
  srand(2);
  for (size_t i = 0; i < N; ++i) {
    point &p = points[i];
    p.x = rand() % SQRT_K + rand() % 1000 * 0.0001f;
    p.y = rand() % SQRT_K + rand() % 1000 * 0.0001f;
  }

  // Compute the K means
  compute_k_means(N, points, K, id, centroid);

  // Check that result is reasonable
  int found[SQRT_K][SQRT_K];
  std::memset(found, 0, sizeof(found));
  for (size_t i = 0; i < K; ++i) {
    const point &c = centroid[i];
    int rx = floor(c.x + 0.5);
    int ry = floor(c.y + 0.5);
    found[ry][rx]++;
    point g;
    g.x = rx;
    g.y = ry;
    float d = distance2(c, g);
    const float tolerance = 1 / std::sqrt(float(N));
    if (d > tolerance) {
      printf("warning: centroid[%d]=(%g,%g) seems surprisingly far from grid point\n", int(i), c.x,
             c.y);
    }
  }
  for (size_t y = 0; y < SQRT_K; ++y)
    for (size_t x = 0; x < SQRT_K; ++x)
      if (found[y][x] != 1)
        printf("warning: found[%d][%d]=%d\n", int(x), int(y), found[y][x]);
#if 0
    // Print the points
    for( size_t i=0; i<N; ++i ) {
        printf("%d (%g %g)\n",id[i],points[i].x,points[i].y);
    }
#endif
#if 0
    // Print the centroids
    printf("centroids = ");
    for( size_t j=0; j<K; ++j ) {
        printf("(%g %g)",centroid[j].x,centroid[j].y);
    }
    printf("\n");
#endif
}

int main() {
  printf("Testing TBB kmeans algorithm...\n");

  // PROSPAR: Track execution time
  HRTimer bench_start = HR::now();

  // taskGraph = new AFTaskGraph();
  // AV_Detector* av_detector = new AV_Detector();
  // av_detector->Activate();
  // tbb::task_scheduler_init init(16);

  // PROSPAR: Track execution time
  HRTimer roi_start = HR::now();

  TD_Activate();
  Test(tbb_example::compute_k_means);

  // PROSPAR: Track execution time
  HRTimer roi_end = HR::now();
  HRTimer bench_end = HR::now();

  auto roi_duration = duration_cast<milliseconds>(roi_end - roi_start).count();
  auto bench_duration = duration_cast<milliseconds>(bench_end - bench_start).count();
  std::cout << "PROSPAR: ROI Time: " << roi_duration << " milliseconds\n"
            << "PROSPAR: Benchmark Time: " << bench_duration << " milliseconds\n";

  Fini(); // PROSPAR: Uncommented
  // taskGraph->Fini();
  return 0;
}
