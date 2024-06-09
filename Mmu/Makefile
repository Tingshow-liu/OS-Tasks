# Compiler and compiler flags
CXX = g++
CXXFLAGS = -std=c++11

# Define the target executable
TARGET = mmu_
SRC = mmu_.cpp

# Default rule to build the program
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# Clean rule for cleaning up generated files
clean:
	rm -f $(TARGET)

.PHONY: clean
