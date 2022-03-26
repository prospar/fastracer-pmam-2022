// Copyright (c) 2007 Intel Corp.

// Black-Scholes
// Analytical method for calculating European Options
//
//
// Reference Source: Options, Futures, and Other Derivatives, 3rd Edition, Prentice
// Hall, John C. Hull,

#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_PARSEC_HOOKS
#include <hooks.h>
#endif

// Multi-threaded pthreads header
#ifdef ENABLE_THREADS
// Add the following line so that icc 9.0 is compatible with pthread lib.
#define __thread __threadp
MAIN_ENV
#undef __thread
#endif

// Multi-threaded OpenMP header
#ifdef ENABLE_OPENMP
#include <omp.h>
#endif

#ifdef ENABLE_TBB
#include "tbb/blocked_range.h"
#include "tbb/parallel_for.h"
#include "tbb/task_scheduler_init.h"
#include "tbb/tick_count.h"

using namespace std;
using namespace tbb;
#endif // ENABLE_TBB

// PROSPAR: Support timers
using namespace std::chrono;
using HR = high_resolution_clock;
using HRTimer = HR::time_point;

// Multi-threaded header for Windows
#ifdef WIN32
#pragma warning(disable : 4305)
#pragma warning(disable : 4244)
#include <windows.h>
#endif

// Precision to use for calculations
#define fptype float

#define NUM_RUNS 100

typedef struct OptionData_ {
  fptype s;        // spot price
  fptype strike;   // strike price
  fptype r;        // risk-free interest rate
  fptype divq;     // dividend rate
  fptype v;        // volatility
  fptype t;        // time to maturity or option expiration in years
                   //     (1yr = 1.0, 6mos = 0.5, 3mos = 0.25, ..., etc)
  char OptionType; // Option type.  "P"=PUT, "C"=CALL
  fptype divs;     // dividend vals (not used in this test)
  fptype DGrefval; // DerivaGem Reference Value
} OptionData;

fptype CHECK_AV *prices;
size_t CHECK_AV tmp_ctr;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Cumulative Normal Distribution Function
// See Hull, Section 11.8, P.243-244
#define inv_sqrt_2xPI 0.39894228040143270286

fptype CNDF(fptype InputX) {
  int sign;

  fptype OutputX;
  fptype xInput;
  fptype xNPrimeofX;
  fptype expValues;
  fptype xK2;
  fptype xK2_2, xK2_3;
  fptype xK2_4, xK2_5;
  fptype xLocal, xLocal_1;
  fptype xLocal_2, xLocal_3;

  // Check for negative value of InputX
  if (InputX < 0.0) {
    InputX = -InputX;
    sign = 1;
  } else
    sign = 0;

  xInput = InputX;

  // Compute NPrimeX term common to both four & six decimal accuracy calcs
  expValues = exp(-0.5f * InputX * InputX);
  xNPrimeofX = expValues;
  xNPrimeofX = xNPrimeofX * inv_sqrt_2xPI;

  xK2 = 0.2316419 * xInput;
  xK2 = 1.0 + xK2;
  xK2 = 1.0 / xK2;
  xK2_2 = xK2 * xK2;
  xK2_3 = xK2_2 * xK2;
  xK2_4 = xK2_3 * xK2;
  xK2_5 = xK2_4 * xK2;

  xLocal_1 = xK2 * 0.319381530;
  xLocal_2 = xK2_2 * (-0.356563782);
  xLocal_3 = xK2_3 * 1.781477937;
  xLocal_2 = xLocal_2 + xLocal_3;
  xLocal_3 = xK2_4 * (-1.821255978);
  xLocal_2 = xLocal_2 + xLocal_3;
  xLocal_3 = xK2_5 * 1.330274429;
  xLocal_2 = xLocal_2 + xLocal_3;

  xLocal_1 = xLocal_2 + xLocal_1;
  xLocal = xLocal_1 * xNPrimeofX;
  xLocal = 1.0 - xLocal;

  OutputX = xLocal;

  if (sign) {
    OutputX = 1.0 - OutputX;
  }

  return OutputX;
}

int numOptions;
OptionData CHECK_AV *data;
fptype CHECK_AV *sptprice;
int CHECK_AV *otype;
fptype CHECK_AV *strike;
fptype CHECK_AV *rate;
fptype CHECK_AV *volatility;
fptype CHECK_AV *otime;
int numError = 0;
int CHECK_AV nThreads;

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
fptype BlkSchlsEqEuroNoDiv(fptype sptprice, fptype strike, fptype rate, fptype volatility,
                           fptype time, int otype, float timet) {
  fptype OptionPrice;

  // local private working variables for the calculation
  fptype xStockPrice;
  fptype xStrikePrice;
  fptype xRiskFreeRate;
  fptype xVolatility;
  fptype xTime;
  fptype xSqrtTime;

  fptype logValues;
  fptype xLogTerm;
  fptype xD1;
  fptype xD2;
  fptype xPowerTerm;
  fptype xDen;
  fptype d1;
  fptype d2;
  fptype FutureValueX;
  fptype NofXd1;
  fptype NofXd2;
  fptype NegNofXd1;
  fptype NegNofXd2;

  xStockPrice = sptprice;
  xStrikePrice = strike;
  xRiskFreeRate = rate;
  xVolatility = volatility;

  xTime = time;
  xSqrtTime = sqrt(xTime);

  logValues = log(sptprice / strike);

  xLogTerm = logValues;

  xPowerTerm = xVolatility * xVolatility;
  xPowerTerm = xPowerTerm * 0.5;

  xD1 = xRiskFreeRate + xPowerTerm;
  xD1 = xD1 * xTime;
  xD1 = xD1 + xLogTerm;

  xDen = xVolatility * xSqrtTime;
  xD1 = xD1 / xDen;
  xD2 = xD1 - xDen;

  d1 = xD1;
  d2 = xD2;

  NofXd1 = CNDF(d1);
  NofXd2 = CNDF(d2);

  FutureValueX = strike * (exp(-(rate) * (time)));
  if (otype == 0) {
    OptionPrice = (sptprice * NofXd1) - (FutureValueX * NofXd2);
  } else {
    NegNofXd1 = (1.0 - NofXd1);
    NegNofXd2 = (1.0 - NofXd2);
    OptionPrice = (FutureValueX * NegNofXd2) - (sptprice * NegNofXd1);
  }

  return OptionPrice;
}

fptype BlkSchlsEqEuroNoDiv(fptype sptprice, fptype strike, fptype rate, fptype volatility,
                           fptype time, int otype, float timet, size_t taskId) {
  fptype OptionPrice;

  // local private working variables for the calculation
  fptype CHECK_AV xStockPrice;
  fptype CHECK_AV xStrikePrice;
  fptype xRiskFreeRate;
  fptype xVolatility;
  fptype xTime;
  fptype xSqrtTime;

  fptype logValues;
  fptype xLogTerm;
  fptype xD1;
  fptype xD2;
  fptype xPowerTerm;
  fptype xDen;
  fptype d1;
  fptype d2;
  fptype FutureValueX;
  fptype NofXd1;
  fptype NofXd2;
  fptype NegNofXd1;
  fptype NegNofXd2;

  // RecordMem(taskId, &sptprice, READ);
  // RecordMem(taskId, &xStockPrice, WRITE);
  xStockPrice = sptprice;

  // RecordMem(taskId, &strike, READ);
  // RecordMem(taskId, &xStrikePrice, WRITE);
  xStrikePrice = strike;

  // RecordMem(taskId, &rate, READ);
  // RecordMem(taskId, &xRiskFreeRate, WRITE);
  xRiskFreeRate = rate;

  // RecordMem(taskId, &volatility, READ);
  // RecordMem(taskId, &xVolatility, WRITE);
  xVolatility = volatility;

  // RecordMem(taskId, &time, READ);
  // RecordMem(taskId, &xTime, WRITE);
  xTime = time;

  // RecordMem(taskId, &xTime, READ);
  // RecordMem(taskId, &xSqrtTime, WRITE);
  xSqrtTime = sqrt(xTime);

  // RecordMem(taskId, &strike, READ);
  // RecordMem(taskId, &sptprice, READ);
  // RecordMem(taskId, &logValues, WRITE);
  logValues = log(sptprice / strike);

  // RecordMem(taskId, &logValues, READ);
  // RecordMem(taskId, &xLogTerm, WRITE);
  xLogTerm = logValues;

  // RecordMem(taskId, &xVolatility, READ);
  // RecordMem(taskId, &xVolatility, READ);
  // RecordMem(taskId, &xPowerTerm, WRITE);
  xPowerTerm = xVolatility * xVolatility;

  // RecordMem(taskId, &xPowerTerm, READ);
  // RecordMem(taskId, &xPowerTerm, WRITE);
  xPowerTerm = xPowerTerm * 0.5;

  // RecordMem(taskId, &xRiskFreeRate, READ);
  // RecordMem(taskId, &xPowerTerm, READ);
  // RecordMem(taskId, &xD1, WRITE);
  xD1 = xRiskFreeRate + xPowerTerm;

  // RecordMem(taskId, &xTime, READ);
  // RecordMem(taskId, &xD1, READ);
  // RecordMem(taskId, &xD1, WRITE);
  xD1 = xD1 * xTime;

  // RecordMem(taskId, &xD1, READ);
  // RecordMem(taskId, &xLogTerm, READ);
  // RecordMem(taskId, &xD1, WRITE);
  xD1 = xD1 + xLogTerm;

  // RecordMem(taskId, &xVolatility, READ);
  // RecordMem(taskId, &xSqrtTime, READ);
  // RecordMem(taskId, &xDen, WRITE);
  xDen = xVolatility * xSqrtTime;

  // RecordMem(taskId, &xD1, READ);
  // RecordMem(taskId, &xDen, READ);
  // RecordMem(taskId, &xD1, WRITE);
  xD1 = xD1 / xDen;

  // RecordMem(taskId, &xD1, READ);
  // RecordMem(taskId, &xDen, READ);
  // RecordMem(taskId, &xD2, WRITE);
  xD2 = xD1 - xDen;

  // RecordMem(taskId, &xD1, READ);
  // RecordMem(taskId, &d1, WRITE);
  d1 = xD1;

  // RecordMem(taskId, &xD2, READ);
  // RecordMem(taskId, &d2, WRITE);
  d2 = xD2;

  // RecordMem(taskId, &NofXd1, READ);
  // RecordMem(taskId, &d1, WRITE);
  NofXd1 = CNDF(d1);

  // RecordMem(taskId, &NofXd2, READ);
  // RecordMem(taskId, &d2, WRITE);
  NofXd2 = CNDF(d2);

  // RecordMem(taskId, &time, READ);
  // RecordMem(taskId, &rate, READ);
  // RecordMem(taskId, &strike, READ);
  // RecordMem(taskId, &FutureValueX, WRITE);
  FutureValueX = strike * (exp(-(rate) * (time)));

  // RecordMem(taskId, &otype, READ);
  if (otype == 0) {
    // RecordMem(taskId, &NofXd2, READ);
    // RecordMem(taskId, &FutureValueX, READ);
    // RecordMem(taskId, &NofXd1, READ);
    // RecordMem(taskId, &sptprice, READ);
    // RecordMem(taskId, &OptionPrice, WRITE);
    OptionPrice = (sptprice * NofXd1) - (FutureValueX * NofXd2);
  } else {
    // RecordMem(taskId, &NofXd1, READ);
    // RecordMem(taskId, &NegNofXd1, WRITE);
    NegNofXd1 = (1.0 - NofXd1);

    // RecordMem(taskId, &NofXd2, READ);
    // RecordMem(taskId, &NegNofXd2, WRITE);
    NegNofXd2 = (1.0 - NofXd2);

    // RecordMem(taskId, &NegNofXd1, READ);
    // RecordMem(taskId, &sptprice, READ);
    // RecordMem(taskId, &NegNofXd2, READ);
    // RecordMem(taskId, &FutureValueX, READ);
    // RecordMem(taskId, &OptionPrice, WRITE);
    OptionPrice = (FutureValueX * NegNofXd2) - (sptprice * NegNofXd1);
  }

  // RecordMem(taskId, &OptionPrice, READ);
  return OptionPrice;
}

#ifdef ENABLE_TBB
// AV_Detector* av_detector;
// size_t iter_count[1000];

class mainWork {
public:
  mainWork() {}
  mainWork(mainWork &w, tbb::split) {}

  void operator()(const tbb::blocked_range<int> &range, size_t thdId) const {
    fptype CHECK_AV price;
    int CHECK_AV begin;
    begin = range.begin();
    int CHECK_AV end;
    end = range.end();

    // printf("Taskid = %lu; Begin = %d(%lu); End = %d(%lu)\n", taskId, begin,
    // (size_t)&prices[begin], end, (size_t)&prices[end]);
    for (int i = begin; i != end; i++) {
      /* Calling main function to calculate option value based on
       * Black & Scholes's equation.
       */
      // iter_count[taskId]++;

      price = BlkSchlsEqEuroNoDiv(sptprice[i], strike[i], rate[i], volatility[i], otime[i],
                                  otype[i], 0, thdId);

      // RecordMem(thdId, &prices, READ);
      // RecordMem(thdId, &prices[i], WRITE);
      prices[i] = price;

      // RecordMem(thdId, &tmp_ctr, READ);
      // RecordMem(thdId, &tmp_ctr, WRITE);
      tmp_ctr++;
#ifdef ERR_CHK
      fptype priceDelta = data[i].DGrefval - price;
      if (fabs(priceDelta) >= 1e-5) {
        fprintf(stderr, "Error on %d. Computed=%.5f, Ref=%.5f, Delta=%.5f\n", i, price,
                data[i].DGrefval, priceDelta);
        numError++;
      }
#endif
    }
  }
};

#endif // ENABLE_TBB

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

#ifdef ENABLE_TBB
int bs_thread(void *tid_ptr) {
  int j;
  tbb::affinity_partitioner a;

  // mainWork doall;
  // for (j=0; j<NUM_RUNS; j++) {
  // for (j=0; j<2; j++) {
  // tbb::parallel_for(tbb::blocked_range<int>(0, numOptions), mainWork(), a);
  tbb::parallel_for(tbb::blocked_range<int>(0, numOptions), mainWork());
  //}

  return 0;
}
#else // !ENABLE_TBB

#ifdef WIN32
DWORD WINAPI bs_thread(LPVOID tid_ptr) {
#else
int bs_thread(void *tid_ptr) {
#endif
  int i, j;
  fptype price;
  fptype priceDelta;
  int tid = *(int *)tid_ptr;
  int start = tid * (numOptions / nThreads);
  int end = start + (numOptions / nThreads);

  for (j = 0; j < NUM_RUNS; j++) {
#ifdef ENABLE_OPENMP
#pragma omp parallel for private(i, price, priceDelta)
    for (i = 0; i < numOptions; i++) {
#else  // ENABLE_OPENMP
    for (i = start; i < end; i++) {
#endif // ENABLE_OPENMP
      /* Calling main function to calculate option value based on
       * Black & Scholes's equation.
       */
      price = BlkSchlsEqEuroNoDiv(sptprice[i], strike[i], rate[i], volatility[i], otime[i],
                                  otype[i], 0);
      prices[i] = price;

#ifdef ERR_CHK
      priceDelta = data[i].DGrefval - price;
      if (fabs(priceDelta) >= 1e-4) {
        printf("Error on %d. Computed=%.5f, Ref=%.5f, Delta=%.5f\n", i, price, data[i].DGrefval,
               priceDelta);
        numError++;
      }
#endif
    }
  }

  return 0;
}
#endif // ENABLE_TBB

int main(int argc, char **argv) {
  FILE *file;
  int i;
  int loopnum;
  fptype *buffer;
  int *buffer2;
  int rv;

#ifdef PARSEC_VERSION
#define __PARSEC_STRING(x) #x
#define __PARSEC_XSTRING(x) __PARSEC_STRING(x)
  printf("PARSEC Benchmark Suite Version "__PARSEC_XSTRING(PARSEC_VERSION) "\n");
  fflush(NULL);
#else
  printf("PARSEC Benchmark Suite\n");
  fflush(NULL);
#endif // PARSEC_VERSION

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_bench_begin(__parsec_blackscholes);
#endif

  // PROSPAR: Track execution time
  HRTimer bench_start = HR::now();

  if (argc != 4) {
    printf("Usage:\n\t%s <nthreads> <inputFile> <outputFile>\n", argv[0]);
    exit(1);
  }
  nThreads = atoi(argv[1]);
  char *inputFile = argv[2];
  char *outputFile = argv[3];

  // Read input data from file
  file = fopen(inputFile, "r");
  if (file == NULL) {
    printf("ERROR: Unable to open file `%s'.\n", inputFile);
    exit(1);
  }
  rv = fscanf(file, "%i", &numOptions);
  if (rv != 1) {
    printf("ERROR: Unable to read from file `%s'.\n", inputFile);
    fclose(file);
    exit(1);
  }
  if (nThreads > numOptions) {
    printf("WARNING: Not enough work, reducing number of threads to match number of options.\n");
    nThreads = numOptions;
  }

#if !defined(ENABLE_THREADS) && !defined(ENABLE_OPENMP) && !defined(ENABLE_TBB)
  if (nThreads != 1) {
    printf("Error: <nthreads> must be 1 (serial version)\n");
    exit(1);
  }
#endif

  // alloc spaces for the option data
  data = (OptionData *)malloc(numOptions * sizeof(OptionData));
  prices = (fptype *)malloc(numOptions * sizeof(fptype));
  for (loopnum = 0; loopnum < numOptions; ++loopnum) {
    rv = fscanf(file, "%f %f %f %f %f %f %c %f %f", &data[loopnum].s, &data[loopnum].strike,
                &data[loopnum].r, &data[loopnum].divq, &data[loopnum].v, &data[loopnum].t,
                &data[loopnum].OptionType, &data[loopnum].divs, &data[loopnum].DGrefval);
    if (rv != 9) {
      printf("ERROR: Unable to read from file `%s'.\n", inputFile);
      fclose(file);
      exit(1);
    }
  }
  rv = fclose(file);
  if (rv != 0) {
    printf("ERROR: Unable to close file `%s'.\n", inputFile);
    exit(1);
  }

#ifdef ENABLE_THREADS
  MAIN_INITENV(, 8000000, nThreads);
#endif
  printf("Num of Options: %d\n", numOptions);
  printf("Num of Runs: %d\n", NUM_RUNS);

#define PAD 256
#define LINESIZE 64

  buffer = (fptype *)malloc(5 * numOptions * sizeof(fptype) + PAD);
  sptprice = (fptype *)(((unsigned long long)buffer + PAD) & ~(LINESIZE - 1));
  strike = sptprice + numOptions;
  rate = strike + numOptions;
  volatility = rate + numOptions;
  otime = volatility + numOptions;

  buffer2 = (int *)malloc(numOptions * sizeof(fptype) + PAD);
  otype = (int *)(((unsigned long long)buffer2 + PAD) & ~(LINESIZE - 1));

  for (i = 0; i < numOptions; i++) {
    otype[i] = (data[i].OptionType == 'P') ? 1 : 0;
    sptprice[i] = data[i].s;
    strike[i] = data[i].strike;
    rate[i] = data[i].r;
    volatility[i] = data[i].v;
    otime[i] = data[i].t;
  }

  printf("Size of data: %d\n", numOptions * (sizeof(OptionData) + sizeof(int)));

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_begin();
#endif

  // PROSPAR: Track ROI time
  HRTimer roi_start = HR::now();

#ifdef ENABLE_THREADS
#ifdef WIN32
  HANDLE *threads;
  int *nums;
  threads = (HANDLE *)malloc(nThreads * sizeof(HANDLE));
  nums = (int *)malloc(nThreads * sizeof(int));

  for (i = 0; i < nThreads; i++) {
    nums[i] = i;
    threads[i] = CreateThread(0, 0, bs_thread, &nums[i], 0, 0);
  }
  WaitForMultipleObjects(nThreads, threads, TRUE, INFINITE);
  free(threads);
  free(nums);
#else
  int *tids;
  tids = (int *)malloc(nThreads * sizeof(int));

  for (i = 0; i < nThreads; i++) {
    tids[i] = i;
    CREATE_WITH_ARG(bs_thread, &tids[i]);
  }
  WAIT_FOR_END(nThreads);
  free(tids);
#endif // WIN32
#else  // ENABLE_THREADS
#ifdef ENABLE_OPENMP
  {
    int tid = 0;
    omp_set_num_threads(nThreads);
    bs_thread(&tid);
  }
#else // ENABLE_OPENMP
#ifdef ENABLE_TBB
  tbb::task_scheduler_init init(nThreads);
  // taskGraph = new AFTaskGraph();
  // av_detector = new AV_Detector();
  // av_detector->Activate();
  TD_Activate();
  int tid = 0;
  bs_thread(&tid);
#else  // ENABLE_TBB
  // serial version
  int tid = 0;
  bs_thread(&tid);
#endif // ENABLE_TBB
#endif // ENABLE_OPENMP
#endif // ENABLE_THREADS

  HRTimer roi_end = HR::now();

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_end();
#endif

  // Write prices to output file
  file = fopen(outputFile, "w");
  if (file == NULL) {
    printf("ERROR: Unable to open file `%s'.\n", outputFile);
    exit(1);
  }
  rv = fprintf(file, "%i\n", numOptions);
  if (rv < 0) {
    printf("ERROR: Unable to write to file `%s'.\n", outputFile);
    fclose(file);
    exit(1);
  }
  for (i = 0; i < numOptions; i++) {
    rv = fprintf(file, "%.18f\n", prices[i]);
    if (rv < 0) {
      printf("ERROR: Unable to write to file `%s'.\n", outputFile);
      fclose(file);
      exit(1);
    }
  }
  rv = fclose(file);
  if (rv != 0) {
    printf("ERROR: Unable to close file `%s'.\n", outputFile);
    exit(1);
  }

#ifdef ERR_CHK
  printf("Num Errors: %d\n", numError);
#endif
  free(data);
  free(prices);

  HRTimer bench_end = HR::now();

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_bench_end();
#endif

  // printf("Counter = %lu Addr = %lu\n", tmp_ctr, (size_t)&tmp_ctr);

  // PROSPAR: Changes
  auto roi_duration = duration_cast<milliseconds>(roi_end - roi_start).count();
  auto bench_duration = duration_cast<milliseconds>(bench_end - bench_start).count();
  std::cout << "PROSPAR: ROI Time: " << roi_duration << " milliseconds\n"
            << "PROSPAR: Benchmark Time: " << bench_duration << " milliseconds\n";

  Fini();
  // taskGraph->Fini();

#if 0
    FILE *op_file;
    op_file = fopen("Task_hist.txt", "w");
    std::map<float, int> task_hist;
    for (int i = 0; i < 1000 ; i++) {
      if (iter_count[i]) {
	float percent = ((float)iter_count[i]/(float)numOptions)*100.00;
	fprintf(op_file,"%d:%f\n", i, percent);
	if (task_hist.count(percent) == 0) {
	  task_hist.insert(std::pair<float, int>(percent, 1));
	} else {
	  task_hist.at(percent)++;
	}
      }
    }
    fclose(op_file);
    op_file = fopen("hist_percent.txt", "w");
    int task_count = 0;
    for (std::map<float,int>::iterator it=task_hist.begin(); it!=task_hist.end(); ++it) {
      task_count += it->second;
      fprintf(op_file,"%f:%d\n", it->first, it->second);
    }
    printf("Total tasks = %d\n", task_count);
#endif

  return 0;
}
