# SuRF is header-only. Clone it next to this file first:
#   git clone https://github.com/efficient/SuRF
SURF_DIR ?= SuRF
CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -mpopcnt -Wall

surf_harness: surf_harness.cpp
	$(CXX) $(CXXFLAGS) -I$(SURF_DIR)/include $< -o $@

clean:
	rm -f surf_harness

.PHONY: clean
