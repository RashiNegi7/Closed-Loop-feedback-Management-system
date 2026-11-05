# Closed Loop Feedback Management System
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INC       = -Isrc -Ithird_party
CROWINC   = -DASIO_STANDALONE -Ithird_party/crow -Ithird_party/asio
SERVER_LIBS = -lpthread
ifeq ($(OS),Windows_NT)
SERVER_LIBS += -lws2_32 -lmswsock
endif
CORE_SRC  = src/service.cpp src/store.cpp src/text.cpp src/util.cpp src/types.cpp
CORE_OBJ  = $(CORE_SRC:src/%.cpp=build/%.o)

all: feedback_server feedback_console run_tests

build/%.o: src/%.cpp $(wildcard src/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INC) -c $< -o $@

build/server.o: src/server.cpp $(wildcard src/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INC) $(CROWINC) -c $< -o $@

feedback_server: build/server.o $(CORE_OBJ)
	$(CXX) $^ -o $@ $(SERVER_LIBS)

feedback_console: build/console.o $(CORE_OBJ)
	$(CXX) $^ -o $@

run_tests: build/test_all.o $(CORE_OBJ)
	$(CXX) $^ -o $@

build/test_all.o: tests/test_all.cpp $(wildcard src/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INC) -c $< -o $@

test: run_tests
	./run_tests

demo: feedback_server
	./feedback_server --demo

clean:
	rm -rf build feedback_server feedback_console run_tests

.PHONY: all test demo clean
