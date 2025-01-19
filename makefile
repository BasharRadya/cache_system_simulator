CXX = g++
CXXFLAGS = -std=c++11

# Targets and dependencies
TARGET = cacheSim
OBJS = cacheSim.o cache_system.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

cacheSim.o: cacheSim.cpp
	$(CXX) $(CXXFLAGS) -c cacheSim.cpp

cache_system.o: cache_system.cpp
	$(CXX) $(CXXFLAGS) -c cache_system.cpp

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)
