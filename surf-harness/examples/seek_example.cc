// Appendix example 2: follow individual bounded seeks through the 36-SST
// databases of benchmark scenario C, with Bloom and with SuRF-Real8.
//   seek_example DB_ROOT     (DB_ROOT/bloom and DB_ROOT/surf-real8 must exist)
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/statistics.h"
#include "rocksdb/table.h"
#include "table/block_based/surf_filter_policy.h"

using namespace ROCKSDB_NAMESPACE;

static std::string Key(uint64_t v) {
  std::string s(8, '\0');
  for (int i = 7; i >= 0; --i) {
    s[i] = static_cast<char>(v & 0xff);
    v >>= 8;
  }
  return s + "00000000";
}

static uint64_t Int(const Slice& k) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<unsigned char>(k[i]);
  return v;
}

struct Seek {
  const char* label;
  uint64_t lo, hi;
};

int main(int argc, char** argv) {
  std::string root = argc > 1 ? argv[1] : "/tmp/surf_dbbench_C";
  struct Cfg {
    const char* name;
    std::shared_ptr<const FilterPolicy> fp;
  } cfgs[] = {
      {"bloom", std::shared_ptr<const FilterPolicy>(NewBloomFilterPolicy(10))},
      {"surf-real8",
       std::shared_ptr<const FilterPolicy>(NewSurfFilterPolicy("real8"))}};

  // Pick example ranges from the data itself.
  std::vector<Seek> seeks;
  {
    Options o;
    std::unique_ptr<DB> db;
    if (!DB::OpenForReadOnly(o, root + "/nofilter", &db).ok()) {
      std::fprintf(stderr, "cannot open %s/nofilter\n", root.c_str());
      return 1;
    }
    std::set<uint64_t> present;
    std::unique_ptr<Iterator> it(db->NewIterator(ReadOptions()));
    for (it->SeekToFirst(); it->Valid(); it->Next()) present.insert(Int(it->key()));
    std::printf("database: %zu keys in [0, 10M)\n", present.size());
    uint64_t empty10 = 0, full10 = 0;
    for (uint64_t x = 5000000; !empty10 || !full10; ++x) {
      auto n = present.lower_bound(x);
      bool has = n != present.end() && *n < x + 10;
      if (!has && !empty10) empty10 = x;
      if (has && *n == x + 4 && !full10) full10 = x;
    }
    seeks = {{"empty range of 10", empty10, empty10 + 10},
             {"range of 10 holding a key", full10, full10 + 10},
             {"range of 1000", empty10, empty10 + 1000}};
  }

  for (const Seek& s : seeks) {
    std::printf("\n%s: [%llu, %llu)\n", s.label, (unsigned long long)s.lo,
                (unsigned long long)s.hi);
    for (const Cfg& c : cfgs) {
      Options o;
      o.statistics = CreateDBStatistics();
      BlockBasedTableOptions t;
      t.filter_policy = c.fp;
      o.table_factory.reset(NewBlockBasedTableFactory(t));
      std::unique_ptr<DB> db;
      if (!DB::OpenForReadOnly(o, root + "/" + c.name, &db).ok()) {
        std::fprintf(stderr, "cannot open %s/%s\n", root.c_str(), c.name);
        return 1;
      }
      std::string lo = Key(s.lo), hi = Key(s.hi);
      Slice ub(hi);
      ReadOptions ro;
      ro.iterate_upper_bound = &ub;
      // Warm up once (loads index/filter blocks), then measure one seek.
      {
        std::unique_ptr<Iterator> it(db->NewIterator(ro));
        it->Seek(lo);
      }
      o.statistics->Reset().PermitUncheckedError();
      std::unique_ptr<Iterator> it(db->NewIterator(ro));
      it->Seek(lo);
      std::string got = it->Valid() ? std::to_string(Int(it->key())) : "none";
      auto tk = [&](Tickers a, Tickers b) {
        return o.statistics->getTickerCount(a) + o.statistics->getTickerCount(b);
      };
      uint64_t skipped = tk(NON_LAST_LEVEL_SEEK_FILTERED, LAST_LEVEL_SEEK_FILTERED);
      uint64_t passed =
          tk(NON_LAST_LEVEL_SEEK_FILTER_MATCH, LAST_LEVEL_SEEK_FILTER_MATCH);
      uint64_t blocks = o.statistics->getTickerCount(BLOCK_CACHE_DATA_MISS) +
                        o.statistics->getTickerCount(BLOCK_CACHE_DATA_HIT);
      it.reset();
      std::printf(
          "  %-10s first key: %-8s files skipped by filter: %2llu  checked and "
          "kept: %2llu  data blocks read: %2llu\n",
          c.name, got.c_str(), (unsigned long long)skipped,
          (unsigned long long)passed, (unsigned long long)blocks);
    }
  }
  return 0;
}
