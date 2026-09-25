CXX      ?= g++
CXXFLAGS  = -std=c++11 -Wall -Wextra -I../../components/st25r
TARGETS   = test_crypto1 test_st25r3916_frame test_scan_step

.PHONY: all run clean

all: $(TARGETS)

test_crypto1: test_crypto1.cpp ../../components/st25r/crypto1.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test_st25r3916_frame: test_st25r3916_frame.cpp ../../components/st25r/st25r3916_frame.h
	$(CXX) $(CXXFLAGS) $< -o $@

test_scan_step: test_scan_step.cpp ../../components/st25r/scan_step.h
	$(CXX) $(CXXFLAGS) $< -o $@

run: $(TARGETS)
	./test_crypto1
	./test_st25r3916_frame
	./test_scan_step

clean:
	rm -f $(TARGETS)
