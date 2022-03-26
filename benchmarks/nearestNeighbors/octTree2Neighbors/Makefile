include parallelDefs

# required files
NEAREST_NEIGHBORS = geometry.h nearestNeighbors.h blockRadixSort.h transpose.h quickSort.h
GLOBAL_REQUIRE = parallel.h sequence.h $(NEAREST_NEIGHBORS)
LOCAL_REQUIRE = octTree.h
INCLUDE = nearestNeighbors.h

PCFLAGS += -I$(TD_ROOT)/include -I$(TBBROOT)/include -std=c++11
PLFLAGS += -L$(TBBROOT)/obj -ltbb -L$(TD_ROOT)/obj -lftdebug -std=c++11

include ../common/timeRequiredFiles
include MakeBenchI
