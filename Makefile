# Detect platform

UNAME_S := $(shell uname -s)

# Compiler selection
ifeq ($(UNAME_S),Darwin)
	CXX = clang++
else
	CXX = g++
endif

# Common flags
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -g -Werror

# SDL flags
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

# Directories
SRC_DIR = src
BIN_DIR = bin

# Source and target
SOURCE = $(SRC_DIR)/ges.cpp
TARGET = $(BIN_DIR)/ges

# Default target
all: $(TARGET)

# Build
$(TARGET): $(SOURCE) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $< -o $@ $(SDL_LIBS)

# Create bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Clean
clean:
	rm -rf $(BIN_DIR)

# Debug build
debug: CXXFLAGS += -DDEBUG -g3 -O0
debug: clean all

.PHONY: all clean debug
