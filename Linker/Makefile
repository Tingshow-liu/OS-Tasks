# Makefile for compiling linker.cpp
CXX = g++

CXXFLAGS = -std=c++11

TARGET = linker

SOURCE = linker.cpp

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(TARGET)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET) $(FILE)
