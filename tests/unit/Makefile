CXX      ?= g++
CXXFLAGS  = -std=c++11 -Wall -Wextra -I../../components/st25r
SRCS      = test_crypto1.cpp ../../components/st25r/crypto1.cpp
TARGET    = test_crypto1

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
