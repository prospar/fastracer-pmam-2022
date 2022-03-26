include parallelDefs

# required files
NEAREST_NEIGHBORS = geometry.h nearestNeighbors.h octTree.h blockRadixSort.h transpose.h
DELAUNAY = topology.h delaunayDefs.h delaunay.C
GLOBAL_REQUIRE = parallel.h sequence.h $(NEAREST_NEIGHBORS) $(DELAUNAY)
LOCAL_REQUIRE =
OBJS = delaunay.o

PCFLAGS += -I$(TD_ROOT)/include -I$(TBBROOT)/include -std=c++11 -ftaskdebug
PLFLAGS += -L$(TBBROOT)/obj -ltbb -L$(TD_ROOT)/obj -lftdebug -std=c++11

include ../common/timeRequiredFiles
include MakeBench
