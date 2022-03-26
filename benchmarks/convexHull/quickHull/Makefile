include parallelDefs

# required files
GLOBAL_REQUIRE = parallel.h sequence.h geometry.h
LOCAL_REQUIRE =
OBJS = hull.o
PCFLAGS += -I$(TD_ROOT)/include -I$(TBBROOT)/include -std=c++11 -ftaskdebug
PLFLAGS += -L$(TBBROOT)/obj -ltbb -L$(TD_ROOT)/obj -lftdebug -std=c++11

include ../common/timeRequiredFiles
include MakeBench
