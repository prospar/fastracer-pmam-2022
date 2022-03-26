include parallelDefs

# required files
TOPOLOGY = geometry.h topology.h topologyFromTri.C
HASH = deterministicHash.h
GLOBAL_REQUIRE = parallel.h sequence.h $(HASH) $(TOPOLOGY)
LOCAL_REQUIRE =
OBJS = refine.o topologyFromTri.o

PCFLAGS += -I$(TD_ROOT)/include -I$(TBBROOT)/include -std=c++11 -ftaskdebug
PLFLAGS += -L$(TBBROOT)/obj -ltbb -L$(TD_ROOT)/obj -lftdebug -std=c++11

include ../common/timeRequiredFiles
include MakeBench
