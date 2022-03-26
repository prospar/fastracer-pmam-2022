#!/bin/bash

cd blackscholes
make clean
make
(time ./blackscholes 4 in_10M.txt output.txt) >../results/${1}/blackscholes.txt 2>&1
cd ..

### ConvexHull

cd convexHull/quickHull
make clean
make
(time ./hull -r 1 -o /tmp/ofile971367_438110 ../geometryData/data/2DinSphere_10M) >../../results/${1}/convexHull.txt 2>&1
cd ../..

### DelaunayRefine

cd delaunayRefine/incrementalRefine
make clean
make
(time ./refine -r 1 -o /tmp/ofile699250_954868 ../geometryData/data/2DinCubeDelaunay_2000000) >../../results/${1}/delaunayRefine.txt 2>&1
cd ../..

### DelaunayTriangulation

cd delaunayTriangulation/incrementalDelaunay
make clean
make
(time ./delaunay -r 1 -o /tmp/ofile850740_480180 ../geometryData/data/2DinCube_10M) >../../results/${1}/delaunayTriangulation.txt 2>&1
cd ../..

### Fluidanimate

cd fluidanimate
make clean
make
(time ./fluidanimate 4 5 in_300K.fluid out.fluid) >../results/${1}/fluidanimate.txt 2>&1
cd ..

### Karatsuba

cd karatsuba
make clean
make
(time ./karatsuba) >../results/${1}/karatsuba.txt 2>&1
cd ..

### Kmeans

cd kmeans
make clean
make
(time ./kmeans) >../results/${1}/kmeans.txt 2>&1
cd ..

### Nearestneighnors

cd nearestNeighbors/octTree2Neighbors
make clean
make
(time ./neighbors -d 2 -k 1 -r 1 -o /tmp/ofile677729_89710 ../geometryData/data/2DinCube_10M) >../../results/${1}/nearestNeighbors.txt
cd ../..

### Raycast

cd rayCast/kdTree
make clean
make
(time ./ray -r 1 -o /tmp/ofile136986_843068 ../geometryData/data/happyTriangles ../geometryData/data/happyRays) >../../results/${1}/rayCast.txt
cd ../..

### Sort

cd sort
make clean
make
(time ./sort) >../results/${1}/sort.txt
cd ..

### Streamcluster

cd streamcluster
make clean
make
(time ./streamcluster 10 20 128 1000000 200000 5000 none output.txt 4) >../results/${1}/streamcluster.txt
cd ..

### Swaptions

cd swaptions
make clean
make
(time ./swaptions -ns 64 -sm 40000 -nt 4) >../results/${1}/swaptions.txt
cd ..
