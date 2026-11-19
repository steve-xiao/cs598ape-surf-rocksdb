SURF_DIR ?= ../third-party/surf
CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -mpopcnt -Wall
DEFS = -DSURF_WORDS=\"$(SURF_DIR)/test/words.txt\"

surf_harness: surf_harness.cpp $(wildcard $(SURF_DIR)/include/*.hpp)
	$(CXX) $(CXXFLAGS) $(DEFS) -I$(SURF_DIR)/include $< -o $@

surf_harness_debug: surf_harness.cpp $(wildcard $(SURF_DIR)/include/*.hpp)
	$(CXX) -O1 -g -std=c++17 -mpopcnt -Wall $(DEFS) -I$(SURF_DIR)/include $< -o $@

valgrind: surf_harness_debug
	valgrind --leak-check=full --error-exitcode=9 ./surf_harness_debug correctness --skip-known-crashes
	valgrind --leak-check=full --error-exitcode=9 ./surf_harness_debug bench --keys 10000 --type int --queries 10000
	valgrind --leak-check=full --error-exitcode=9 ./surf_harness_debug bench --keys 10000 --type string --queries 10000

# End-to-end RocksDB test. Needs ../librocksdb.a:
#   (cd .. && make -j DEBUG_LEVEL=0 static_lib)
ROCKSDB_ROOT ?= ..
ROCKSDB_LIBS ?= -lpthread -lrt -ldl -lsnappy -lgflags -lz -lbz2 -llz4 -lzstd

rocksdb_filter_test: rocksdb_filter_test.cc $(ROCKSDB_ROOT)/librocksdb.a
	$(CXX) -O2 -g -std=c++20 -fno-rtti -I$(ROCKSDB_ROOT) -I$(ROCKSDB_ROOT)/include $< \
	  $(ROCKSDB_ROOT)/librocksdb.a $(ROCKSDB_LIBS) -o $@

test: surf_harness rocksdb_filter_test
	./surf_harness correctness
	./rocksdb_filter_test

valgrind-rocksdb: rocksdb_filter_test
	valgrind --leak-check=full --error-exitcode=9 ./rocksdb_filter_test --keys 2000 --queries 2000

clean:
	rm -f surf_harness surf_harness_debug rocksdb_filter_test

.PHONY: clean valgrind valgrind-rocksdb test
