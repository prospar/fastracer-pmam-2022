ifneq (,$(shell which clang++ 2>/dev/null))
CXX=clang++ -std=c++11  # PROSPAR: Added c++11
else
CXX=g++ -std=c++11  # PROSPAR: Added c++11
endif # clang++

TARGET   = fluidanimate
OBJS     = tbb.cpp cellpool.o
LIBS	 += -ltbb -lftdebug
CXXFLAGS += -O3 -D_GNU_SOURCE -D__XOPEN_SOURCE=600 -ftaskdebug
LDFLAGS = -L$(TBBROOT)/obj -L$(TD_ROOT)/obj
INCLUDE += -I$(TD_ROOT)/include -I$(TBBROOT)/include

all: tbb fluidcmp

tbb: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(INCLUDE) $(LIBS) -o $(TARGET)

%.o : %.cpp
	$(CXX) $(CXXFLAGS) -c $<

fluidcmp: fluidcmp.cpp
	rm -rf fluidcmp
	$(CXX) $(CXXFLAGS) fluidcmp.cpp -o fluidcmp

clean:
	rm -rf $(TARGET)
	rm -rf fluidcmp *~ *.out *.o
