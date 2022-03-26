#!/bin/sh

./configure --enable-tbb --disable-threads --disable-openmp --prefix=/home/adarsh/cgo_artifact/benchmarks/bodytrack CXXFLAGS="-O3 -funroll-loops -fprefetch-loop-arrays -fpermissive -fno-exceptions -static-libgcc -Wl,--hash-style=both,--as-needed -DPARSEC_VERSION=3.0-beta-20150206 -fexceptions -DAV_ANALYSIS -I${TD_ROOT}/include -I${TBBROOT}/include" LDFLAGS="-L${TBBROOT}/obj -L${TD_ROOT}/obj" LIBS="-ltbb -lftdebug" VPATH="."

make

make install
