# Define compiler
CXX=g++

# Compiler flags
CXXFLAGS=-g -std=c++11

# Target executable name
TARGET=scheduler

# Source files
SOURCES=scheduler.cpp

# Object files
OBJECTS=$(SOURCES:.cpp=.o)

# Default target
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

# Clean target
clean:
	rm -f $(TARGET) $(OBJECTS)

# Phony targets
.PHONY: all clean
