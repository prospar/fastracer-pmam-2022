ifneq (,$(shell which clang++ 2>/dev/null))
CXX=clang++ -std=c++11
else
CXX=g++ -std=c++11
endif # clang++

CSRC    = $(SRC)
TARGET  = blackscholes
M4_BASE = .
MACROS  = c.m4.pthreads
LDFLAGS = -L$(TBBROOT)/obj -L$(TD_ROOT)/obj
LIBS    = -ltbb -ltbbmalloc_proxy -ltbbmalloc -ldl -lrt -lpthread -lftdebug
CXXFLAGS += -O0 -I$(TD_ROOT)/include -I$(TBBROOT)/include -ftaskdebug -fno-inline

SRC	= blackscholes.c
MT        = -DENABLE_TBB

# Default build single precision version
NCO     = -DNCO=4

CXXFLAGS += $(MT) $(NCO) $(FUNC) $(ERR) $(CSRC)

all: $(TARGET)

$(TARGET): clean $(CSRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $(TARGET)

$(M4_SRC): $(SRC)
	$(M4) $(M4_BASE)/$(MACROS) $< > $@

clean:
	rm -f $(TARGET) $(M4_SRC) *~ *.out prices.txt
