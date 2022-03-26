# PROSPAR README

## Blackscholes

```Bash
cd blackscholes
make clean
make
./blackscholes 5 in_100.txt output.txt
```

## Bodytrack

```Bash
cd bodytrack/TrackingBenchmark
./configure --enable-tbb --disable-threads --disable-openmp --prefix=/home/swarnendu/prospar-workspace/ptracer-benchmarks-github/bodytrack CXXFLAGS="-O3 -funroll-loops -fprefetch-loop-arrays -fpermissive -fno-exceptions -static-libgcc -Wl,--hash-style=both,--as-needed -DPARSEC_VERSION=3.0-beta-20150206 -fexceptions -I/home/swarnendu/prospar-workspace/ptracer-prospar-swarnendu/tbb-lib/include -I/home/swarnendu/prospar-workspace/ptracer-prospar-swarnendu/newfasttrack/include" LDFLAGS="-L/home/swarnendu/prospar-workspace/ptracer-prospar-swarnendu/tbb-lib/obj -L/home/swarnendu/prospar-workspace/ptracer-prospar-swarnendu/newfasttrack/obj" LIBS="-ltbb -lftdebug" VPATH="."
make
./bodytrack ../inputs/sequenceB_261 4 261 4000 5 0 4
```

## Convex Hull

```Bash
cd convexHull/quickHull
make clean
make
./testInputs
```

## Delaunay Refinement

```Bash
cd delaunayRefine/incrementalRefine
make clean
make
./testInputs
```

## Delaunay Triangulation

```Bash
cd delaunayTriangulation/incrementalDelaunay
make clean
make
./testInputs
```

## Fluidanimate

```Bash
cd fluidanimate
make clean
make
./fluidanimate 4 5 in_300K.fluid out.fluid
```

## Karatsuba

```Bash
cd karatsuba
make clean
make
./karatsuba
```

## kMeans

```Bash
cd kmeans
make clean
make
./kmeans
```

## Nearest Neighbors

```Bash
cd nearestNeighbors/octTree2Neighbors
make clean
make
./testInputs
```

## RayCast

```Bash
cd rayCast/kdTree
make clean
make
./testInputs
```

## Sort

```Bash
cd sort
make clean
make
./sort
```

## Streamcluster

```Bash
cd streamcluster
make clean
make
./streamcluster 10 20 128 1000000 200000 5000 none output.txt 4
```

## Swaptions

```Bash
cd swaptions
make clean
make
./swaptions -ns 64 -sm 40000 -nt 4
```
