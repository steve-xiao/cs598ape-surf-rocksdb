// End-to-end correctness test: RocksDB with no filter, Bloom, Ribbon,
// SuRF-Base and SuRF-Real8 must return exactly what a std::map returns, for
// point lookups and for bounded range scans (iterate_upper_bound).
//
//   rocksdb_filter_test [--keys N] [--queries Q] [--dir /tmp/path]
//
// Exit code is nonzero on any mismatch.
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/options.h"
#include "rocksdb/statistics.h"
#include "rocksdb/table.h"
#include "table/block_based/surf_filter_policy.h"

using namespace ROCKSDB_NAMESPACE;

namespace {

struct FilterCase {
  const char* name;
  std::shared_ptr<const FilterPolicy> policy;
};

std::string BigEndian(uint64_t x, int bytes) {
  std::string s(bytes, '\0');
  for (int i = bytes - 1; i >= 0; --i) {
    s[i] = static_cast<char>(x & 0xff);
    x >>= 8;
  }
  return s;
}

// Sparse 6-byte keys plus a random 0-2 byte tail, so keys have shared
// prefixes, different lengths and contain 0x00 and 0xFF bytes.
std::string RandomKey(std::mt19937_64& rng) {
  std::string k = BigEndian(rng() % (uint64_t(1) << 40), 6);
  int tail = static_cast<int>(rng() % 3);
  for (int i = 0; i < tail; ++i) {
    k.push_back(static_cast<char>(rng() & 0xff));
  }
  return k;
}

std::string RandomUpper(const std::string& lo, std::mt19937_64& rng) {
  // Increment byte 3, 4 or 5 of lo: spans of 2^16, 2^8 or 1 in key space.
  std::string hi = lo.substr(0, 6);
  int pos = 3 + static_cast<int>(rng() % 3);
  if (pos >= static_cast<int>(hi.size())) {
    pos = static_cast<int>(hi.size()) - 1;
  }
  for (int i = pos; i >= 0; --i) {
    if (static_cast<unsigned char>(hi[i]) != 0xff) {
      hi[i] = static_cast<char>(hi[i] + 1);
      hi.resize(i + 1);
      return hi;
    }
  }
  return std::string(7, '\xff');
}

struct Result {
  long checks = 0, failed = 0;
  uint64_t seek_filtered = 0, seek_filter_match = 0, bloom_useful = 0;
};

#define CHECK_EQ_OR_FAIL(cond, ...)       \
  do {                                    \
    ++r.checks;                           \
    if (!(cond)) {                        \
      if (r.failed++ < 5) {               \
        std::fprintf(stderr, __VA_ARGS__); \
      }                                   \
    }                                     \
  } while (0)

Result RunCase(const FilterCase& fc, const std::string& dir, size_t nkeys,
               size_t nq) {
  Result r;
  Options opt;
  opt.create_if_missing = true;
  opt.statistics = CreateDBStatistics();
  opt.write_buffer_size = 64 << 10;
  opt.target_file_size_base = 64 << 10;
  opt.max_bytes_for_level_base = 256 << 10;
  opt.level0_file_num_compaction_trigger = 2;
  opt.compression = kNoCompression;
  BlockBasedTableOptions t;
  t.filter_policy = fc.policy;
  t.block_size = 1024;
  opt.table_factory.reset(NewBlockBasedTableFactory(t));

  std::string path = dir + "/" + fc.name;
  DestroyDB(path, opt);
  std::unique_ptr<DB> db;
  Status s = DB::Open(opt, path, &db);
  if (!s.ok()) {
    std::fprintf(stderr, "open %s: %s\n", fc.name, s.ToString().c_str());
    r.failed = 1;
    return r;
  }

  std::mt19937_64 rng(598);
  std::map<std::string, std::string> ref;
  const std::vector<std::string> edge = {
      std::string("\x00", 1), std::string("\x00\x00", 2), "\xff",
      "\xff\xff\xff", "a", "ab", "abc", "abd", "b", "far", "fast", "fat"};
  for (const auto& k : edge) {
    ref[k] = "edge";
    db->Put(WriteOptions(), k, "edge");
  }
  for (size_t i = 0; i < nkeys; ++i) {
    std::string k = RandomKey(rng), v = std::to_string(i);
    ref[k] = v;
    db->Put(WriteOptions(), k, v);
    if (i % 7 == 3) {
      auto it = ref.lower_bound(RandomKey(rng));
      if (it != ref.end()) {
        db->Delete(WriteOptions(), it->first);
        ref.erase(it);
      }
    }
  }
  db->Flush(FlushOptions());
  db->WaitForCompact(WaitForCompactOptions());

  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 1) {
      db.reset();
      s = DB::Open(opt, path, &db);
      if (!s.ok()) {
        r.failed++;
        return r;
      }
    }
    std::string v;
    for (const auto& kv : ref) {
      Status g = db->Get(ReadOptions(), kv.first, &v);
      CHECK_EQ_OR_FAIL(g.ok() && v == kv.second, "%s: Get(present) wrong\n",
                       fc.name);
    }
    for (size_t i = 0; i < nq; ++i) {
      std::string k = RandomKey(rng);
      if (ref.count(k)) {
        continue;
      }
      Status g = db->Get(ReadOptions(), k, &v);
      CHECK_EQ_OR_FAIL(g.IsNotFound(), "%s: Get(absent) found a key\n",
                       fc.name);
    }
    for (size_t i = 0; i < nq; ++i) {
      std::string lo = RandomKey(rng);
      if (i % 4 == 0) {
        auto it = ref.lower_bound(lo);
        if (it != ref.end()) {
          lo = it->first;
        }
      }
      std::string hi = RandomUpper(lo, rng);
      Slice ub(hi);
      ReadOptions ro;
      ro.iterate_upper_bound = &ub;
      std::unique_ptr<Iterator> it(db->NewIterator(ro));
      std::vector<std::string> got, want;
      for (it->Seek(lo); it->Valid() && got.size() < 64; it->Next()) {
        got.push_back(it->key().ToString());
      }
      for (auto e = ref.lower_bound(lo);
           e != ref.end() && e->first < hi && want.size() < 64; ++e) {
        want.push_back(e->first);
      }
      CHECK_EQ_OR_FAIL(it->status().ok() && got == want,
                       "%s: range scan returned %zu keys, expected %zu\n",
                       fc.name, got.size(), want.size());
    }
  }
  r.seek_filtered =
      opt.statistics->getTickerCount(NON_LAST_LEVEL_SEEK_FILTERED) +
      opt.statistics->getTickerCount(LAST_LEVEL_SEEK_FILTERED);
  r.seek_filter_match =
      opt.statistics->getTickerCount(NON_LAST_LEVEL_SEEK_FILTER_MATCH) +
      opt.statistics->getTickerCount(LAST_LEVEL_SEEK_FILTER_MATCH);
  r.bloom_useful = opt.statistics->getTickerCount(BLOOM_FILTER_USEFUL);
  db.reset();
  DestroyDB(path, opt);
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  size_t nkeys = 20000, nq = 20000;
  std::string dir = "/tmp/surf_rocksdb_test";
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--keys")) {
      nkeys = std::stoull(argv[i + 1]);
    } else if (!std::strcmp(argv[i], "--queries")) {
      nq = std::stoull(argv[i + 1]);
    } else if (!std::strcmp(argv[i], "--dir")) {
      dir = argv[i + 1];
    }
  }
  Env::Default()->CreateDirIfMissing(dir).PermitUncheckedError();
  std::vector<FilterCase> cases = {
      {"none", nullptr},
      {"bloom10", std::shared_ptr<const FilterPolicy>(NewBloomFilterPolicy(10))},
      {"ribbon10",
       std::shared_ptr<const FilterPolicy>(NewRibbonFilterPolicy(10))},
      {"surf-base", std::shared_ptr<const FilterPolicy>(NewSurfFilterPolicy("none"))},
      {"surf-real8",
       std::shared_ptr<const FilterPolicy>(NewSurfFilterPolicy("real8"))},
  };
  long total = 0, failed = 0;
  std::printf("%-11s %8s %7s %14s %17s %13s\n", "filter", "checks", "failed",
              "seek.filtered", "seek.filter.match", "bloom.useful");
  for (const auto& fc : cases) {
    Result r = RunCase(fc, dir, nkeys, nq);
    total += r.checks;
    failed += r.failed;
    std::printf("%-11s %8ld %7ld %14" PRIu64 " %17" PRIu64 " %13" PRIu64 "\n",
                fc.name, r.checks, r.failed, r.seek_filtered,
                r.seek_filter_match, r.bloom_useful);
  }
  std::printf("\nTOTAL: %ld checks, %ld failed\n", total, failed);
  return failed ? 1 : 0;
}
