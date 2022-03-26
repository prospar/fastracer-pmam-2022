# Makefile for streamcluster
ifneq (,$(shell which clang++ 2>/dev/null))
CXX=clang++ -std=c++11 # PROSPAR: Added -std.
else
CXX=g++ -std=c++11 # PROSPAR: Added -std.
endif # clang++

INCLUDE = -I$(TD_ROOT)/include -I$(TBBROOT)/include

TARGET=streamcluster
OBJS=streamcluster.o

CXXFLAGS := $(CXXFLAGS) -DTBB_VERSION -O2 -ftaskdebug
LIBS := $(LIBS) -ltbb -lftdebug

LDFLAGS = -L$(TBBROOT)/obj -L$(TD_ROOT)/obj

all: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(INCLUDE) $(LIBS) -o $(TARGET)

%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $<

clean:
	rm -f *.o $(TARGET) *~ *.out
