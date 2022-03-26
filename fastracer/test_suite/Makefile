ifneq (,$(shell which clang++ 2>/dev/null))
CXX=clang++ -std=c++11
CXXFLAGS = -I$(TD_ROOT)/include -I$(TBBROOT)/include -D__STRICT_ANSI__ -ftaskdebug -g
else
CXX=g++ -std=c++11
CXXFLAGS = -I$(TD_ROOT)/include -I$(TBBROOT)/include -O3
endif # clang++

LIBS = -L$(TBBROOT)/obj -ltbb -L$(TD_ROOT)/obj -lftdebug
#LIBS = -ltbb -L$(TD_ROOT)/obj -lprospar

SRCS = $(wildcard dr*.cpp)

PROGS = $(patsubst %.cpp,%,$(SRCS))

all: $(PROGS)

%: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBS)

clean:
	rm -rf $(PROGS) *~ *.out util.pyc traces
