# Data Race Detection for Task Parallel Programs

- TODO
  - Optimize Same Epoch cases both for read and write (performance)
  - Merge the read epoch and the read vector in a single word (memory and performance)
  - Should we include the above two optimizations in FT as well?

## Executing microbenchmarks

For executing on microbenchmarks (and for benchmarks as well),

```Bash
source build_fasttrack.sh
cd test_suite
make clean
make
python run1_tests.py -d fasttrack
```

Note: For seeing the stats for microbenchmarks, one can execute binaries one by one. Like `./dr_113_yes`, `./dr_lock_119_yes`, etc.

## Executing Benchmarks

For executing benchmarks, first all the big input files earlier removed need to be copied into the corresponding sub-directories. Also, pull once in the benchmarks directories to update the latest changes.

Note: For some of the inputs, the execution might give segmentation fault, on which one can try either reducing input size or increasing the `NUM_TASKS` and `NUM_TASK_BITS` defined in `fasttrack/include/exec_calls.h` file (line#22 and line#23). Also for larger values, code will slow down. Each of the benchmarks thereafter can be executed as follows:

### Blackscholes

```Bash
cd blackscholes
make clean
make
./blackscholes 4 in_10M.txt out.txt
```

Note: One can also try for smaller input files(pushed in repo) like `./blackscholes 4 in_1000.txt out.txt`, and `./blackscholes 4 in_10000.txt out.txt`.

### ConvexHull

```Bash
cd convexHull/quickHull
make clean
make
./testInputs
```

### DelaunayRefine

```Bash
cd delaunayRefine/incrementalRefine
make clean
make
./testInputs
```

### DelaunayTriangulation

```Bash
cd delaunayTriangulation/incrementalDelaunay
make clean
make
./testInputs
```

### Fluidanimate

```Bash
cd fluidanimate
make clean
make
./fluidanimate 4 5 in_300K.fluid out.fluid
```

### Karatsuba

```Bash
cd karatsuba
make clean
make
./karatsuba
```

### Kmeans

```Bash
cd kmeans
make clean
make
./kmeans
```

### Nearestneighnors

```Bash
cd nearestNeighbors/octTree2Neighbors
make clean
make
./testInputs
```

### Raycast

```Bash
cd rayCast/kdTree
make clean
make
./testInputs
```

### Sort

```Bash
cd sort
make clean
make
./sort
```

### Streamcluster

```Bash
cd streamcluster
make clean
make
./streamcluster 10 20 128 1000000 200000 5000 none output.txt 4
```

Note: Here also execution can be checked for smaller inputs (numbers above)

### Swaptions

```Bash
cd swaptions
make clean
make
./swaptions -ns 64 -sm 40000 -nt 4
```

## TODOs

- Oct 16 2019

  - Blackscholes dies with large inputs for PTracer
  - streamcluster, raycast, fluidanimate, convexHull - FastTrack runs for a long time
  - Formalize the new algorithm Shivam has, including the exact conditions when it will work
  - Think what will it take to implement SLIMFast on top of new-FastTrack
  - Predictive interval-based data race detection idea of Shivam with new examples

- Oct 3 2019
  - Do performance test for native execution, SPD3, PTracer, and FastTrack (10 trials). Write a script to automate, SB can help.
  - Add stats for counting total shared variables in an application
  - Print heap size and memory usage periodically in the STATS mode
  - Compute stats about the VC and print for a few shared variables randomly (at say every 10000th access)
  - Run the debug configuration for FastTrack for each benchmark (1 time with small inputs, 1 time with larger inputs)
  - RADISH paper
  - Incorporate SlimFast strategy to improve FastTrack

You might want to create a new directory for the next optimizations.

- At wait operations, the parent task can remove entries for the child tasks which have ended
- Use a map data structure as VC per task. And we can statically estimate the initial size of the map based on the maximum number of concurrent tasks.

- Fix the PTracer bug in initializing the shadow memory -- 70% DONE (There seems to be more issues related to synchronization)
- Remove annotations from PTracer(CHECK_AV) -- Done (except in case of ENABLE_BITS)

## Miscellaneous

SB: PTracer code seems to have been built on Quala from Adrian Sampson. Check the following links.

`git clone --recurse-submodules https://github.com/sampsyo/quala.git`

`https://www.cs.cornell.edu/~asampson/blog/quala-codegen.html`

Do a code diff to see the changes between Quala and PTracer.

One can also do a diff between the PTracer LLVM+Clang code with LLVM+Clang 3.7 but the changes will be more difficult to read.

---

```Bash
clang++ -I/home/shivam/Documents/summer_project/my_project/tdebug-lib/include -I/home/shivam/Documents/summer_project/my_project/tbb-lib/include -D__STRICT_ANSI__ -ftaskdebug -g -o prospar-microbenchmark prospar-microbenchmark.cpp -ltbb -L/home/shivam/Documents/summer_project/my_project/tdebug-lib/obj -ltdebug

clang++ -I/home/shivam/Documents/summer_project/my_project/fasttrack/include -I/home/shivam/Documents/summer_project/my_project/tbb-lib/include -D__STRICT_ANSI__ -ftaskdebug -g -o prospar-microbenchmark prospar-microbenchmark.cpp -ltbb -L/home/shivam/Documents/summer_project/my_project/fasttrack/obj -ltdebug
```

---

```Bash
clang++ -I/home/swarnendu/prospar-workspace/ptracer/tdebug-lib/include -I/home/swarnendu/prospar-workspace/ptracer/tbb-lib/include -D__STRICT_ANSI__ -ftaskdebug -g -o prospar-microbenchmark prospar-microbenchmark.cpp -ltbb -L/home/swarnendu/prospar-workspace/ptracer/tdebug-lib/obj -ltdebug

clang++ -I/home/swarnendu/prospar-workspace/ptracer/fasttrack/include -I/home/swarnendu/prospar-workspace/ptracer/tbb-lib/include -D__STRICT_ANSI__ -ftaskdebug -g -ltbb -L/home/swarnendu/prospar-workspace/ptracer/fasttrack/obj -ltdebug -o dr_12_Yes_ft dr_12_Yes

clang++ -std=c++11 -I/home/swarnendu/prospar-workspace/ptracer-prospar-review/fasttrack/include -I/home/swarnendu/prospar-workspace/ptracer-prospar-review/tbb-lib/include -D__STRICT_ANSI__ -ftaskdebug -g -ltbb -L/home/swarnendu/prospar-workspace/ptracer-prospar-review/fasttrack/obj -lftdebug -o dr_110_Yes dr_110_Yes.cpp

clang++ -I/home/swarnendu/prospar-workspace/ptracer-fasttrack/tdebug-lib/include -I/home/swarnendu/prospar-workspace/ptracer-fasttrack/tbb-lib/include -D__STRICT_ANSI__ -ftaskdebug -g -o prospar-microbenchmark prospar-microbenchmark.cpp -ltbb -L/home/swarnendu/prospar-workspace/ptracer-fasttrack/tdebug-lib/obj -ltdebug
```

---

NOTE: To compile prospar-microbenchmark.cpp or dr_12_Yes.cpp file with spawning operations, uncomment all lines excluding lines with "**exec_begin" , "**exec_end" and "Fini()".

---

- `prospar-microbenchmark.cpp`

```Bash
clang++ -I/home/swarnendu/prospar-workspace/ptracer-prosper/fasttrack/include -I/home/swarnendu/prospar-workspace/ptracer-prospar/tbb-lib/include -D__STRICT_ANSI__ -fprospar -g -o prospar-microbenchmark prospar-microbenchmark.cpp -ltbb -L/home/swarnendu/prospar-workspace/ptracer-prospar/fasttrack/obj -lprospar
```

- `mem-accesses.cpp`

```Bash
clang++ -O0 -emit-llvm mem-accesses.cpp -c -o mem-accesses.bc
opt -load ../tdebug-llvm/build/built/lib/LLVMProsparso.so -prospar-mem-accesses < mem-accesses.bc > /dev/null
clang++ tmp.bc -o mem-accesses; ./mem-accesses
```

```Bash
cd test_suite; clang++ -D__STRICT_ANSI__ -fprospar -g -o mem-accesses mem-accesses.cpp
./mem-accesses
```

```Bash
source build_fasttrack_without_llvm.sh; cd test_suite; clang++ -std=c++11 -I/home/swarnendu/prospar-workspace/ptracer-prospar-review/fasttrack/include -I/home/swarnendu/prospar-workspace/ptracer-prospar-review/tbb-lib/include -D__STRICT_ANSI__ -ftaskdebug -g -ltbb -L/home/swarnendu/prospar-workspace/ptracer-prospar-review/fasttrack/obj -lftdebug -o dr_110_Yes dr_110_Yes.cpp; ./dr_110_Yes; cd ..
```

```Bash
source build_fasttrack_without_llvm.sh; cd test_suite; clang++ -std=c++11 -I/home/swarnendu/prospar-workspace/ptracer-prospar-review/fasttrack/include -I/home/swarnendu/prospar-workspace/ptracer-prospar-review/tbb-lib/include -D__STRICT_ANSI__ -ftaskdebug -g -ltbb -L/home/swarnendu/prospar-workspace/ptracer-prospar-review/fasttrack/obj -lftdebug -o dr_113_Yes dr_113_Yes.cpp; ./dr_113_Yes; cd ..
```

```Bash
source build_fasttrack_without_llvm.sh; cd test_suite; clang++ -std=c++11 -I/home/swarnendu/prospar-workspace/ptracer-prospar-fasttrack/fasttrack/include -I/home/swarnendu/prospar-workspace/ptracer-prospar-fasttrack/tbb-lib/include -D__STRICT_ANSI__ -ftaskdebug -g -ltbb -L/home/swarnendu/prospar-workspace/ptracer-prospar-fasttrack/fasttrack/obj -lftdebug -o dr_110_Yes dr_110_Yes.cpp ; ./dr_110_Yes; cd ..
```

`fastpar-exp --tasks build_bench,run --trials 1 --bench blackscholes --verbose 0 --tool fasttrack --workloadSize debug --printOnly False`
