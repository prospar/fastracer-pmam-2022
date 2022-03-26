include parallelDefs

#PCFLAGS := $(PCFLAGS) -pg
#PLFLAGS := $(PLFLAGS) -pg

# required files
SORT = sampleSort.h quickSort.h transpose.h
GLOBAL_REQUIRE = parallel.h sequence.h geometry.h dataGen.h $(SORT)
BENCH_REQUIRE = ray.h
LOCAL_REQUIRE = kdTree.C kdTree.h rayTriangleIntersect.h
OBJS = kdTree.o

PCFLAGS += -I$(TD_ROOT)/include -I$(TBBROOT)/include -std=c++11 -ftaskdebug
PLFLAGS += -L$(TBBROOT)/obj -ltbb -L$(TD_ROOT)/obj -lftdebug -std=c++11

include ../common/timeRequiredFiles
include MakeBench
