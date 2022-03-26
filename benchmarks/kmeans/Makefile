ifneq (,$(shell which clang++ 2>/dev/null))
CXX=clang++ -std=c++11 # PROSPAR: Added c++11
else
CXX=g++ -std=c++11 # PROSPAR: Added c++11
endif # clang++

INCLUDE = -I$(TD_ROOT)/include -I$(TBBROOT)/include

TARGET=kmeans
OBJS=test_kmeans.o kmeans_tbb.o repair_empty_clusters.o

CXXFLAGS := $(CXXFLAGS) -O0 -ftaskdebug
LIBS := $(LIBS) -ltbb -lftdebug

LDFLAGS = -L$(TBBROOT)/obj -L$(TD_ROOT)/obj

all: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(INCLUDE) $(LIBS) -o $(TARGET)

%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $<

clean:
	rm -f *.o $(TARGET) *~ *.out
