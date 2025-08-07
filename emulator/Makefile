# makefile for gamespy emulator

# configuration steps
# sudo apt-get install build-essential g++ libsqlite3-dev libboost-all-dev libssl-dev
# cd .. && git submodule init && git submodule update
# i had to do sudo apt full-upgrade

CXX = g++
CXXFLAGS = -std=c++26 -O3 -Wall
DFLAGS = -D_UNIX
IFLAGS = -I../dependencies/boost -I../dependencies/boost-mysql/include -I../dependencies/GameSpy/src -I../dependencies/nlohmann-json/single_include
LFLAGS = -L../dependencies/boost/stage/lib -lboost_serialization -lboost_charconv -lsqlite3 -lssl -lcrypto -lquadmath

SRC_FILES = $(wildcard ./*.cpp)
OBJ_FILES = $(SRC_FILES:./%.cpp=obj/%.o)

# Executable name
TARGET = ./emulator

# Targets
.PHONY: all clean

# Default target
all: $(TARGET)

# Build the target executable
$(TARGET): $(OBJ_FILES)
	@echo "Linking..."
	$(CXX) $(OBJ_FILES) -static -o $@ $(LFLAGS)

# Compile the source files into object files
obj/%.o: ./%.cpp
	@echo "Compiling $<..."
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) $(DFLAGS) $(IFLAGS) -c $< -o $@

# Clean build files
clean:
	@echo "Cleaning up..."
	rm -rf obj bin
