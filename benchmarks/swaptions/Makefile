ifneq (,$(shell which clang++ 2>/dev/null))
CXX=clang++ -std=c++11 # PROSPAR: Added -std
else
CXX=g++ -std=c++11 # PROSPAR: Added -std
endif # clang++

INCLUDE = -I$(TD_ROOT)/include -I$(TBBROOT)/include

EXEC = swaptions

DEF := $(DEF) -DENABLE_THREADS -DTBB_VERSION
LIBS := $(LIBS) -ltbb -lftdebug

OBJS= CumNormalInv.o MaxFunction.o RanUnif.o nr_routines.o icdf.o \
	HJM_SimPath_Forward_Blocking.o HJM.o HJM_Swaption_Blocking.o  \
	HJM_Securities.o

LDFLAGS = -L$(TBBROOT)/obj -L$(TD_ROOT)/obj

CXXFLAGS += -O3 -ftaskdebug

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(DEF) $(OBJS) $(INCLUDE) $(LIBS) -o $(EXEC)

.cpp.o:
	$(CXX) $(CXXFLAGS) $(INCLUDE) $(DEF) -c $*.cpp -o $*.o

.c.o:
	$(CXX) $(CXXFLAGS) $(INCLUDE) $(DEF) -c $*.c -o $*.o

clean:
	rm -f $(OBJS) $(EXEC) *~ *.out
