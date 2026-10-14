// Standalone SuRF test harness.
//
// Compares SuRF (github.com/efficient/SuRF) against an exact reference (a
// sorted std::vector<std::string>) and measures false positive rate, memory,
// build time and query time.
//
//   surf_harness correctness [--json]
//   surf_harness bench --keys N [--type int|string] [--queries Q] [--json]
//
// Correctness rules:
//   * A filter may say "maybe present" for an absent key or empty range
//     (false positive). That is measured, not a failure.
//   * A filter must never say "absent" for a present key or a non-empty
//     range (false negative). Every such case is a FAILED check.
//   * A deserialized filter must answer every query exactly like the
//     original filter.
#include "surf.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

using surf::SuRF;
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- reference
struct Reference {
  std::vector<std::string> keys; // sorted, unique

  bool hasKey(const std::string &k) const {
    return std::binary_search(keys.begin(), keys.end(), k);
  }
  // Is there any key in the range with the given boundary inclusivity?
  bool hasRange(const std::string &lo, bool lo_inc, const std::string &hi,
                bool hi_inc) const {
    auto it = lo_inc ? std::lower_bound(keys.begin(), keys.end(), lo)
                     : std::upper_bound(keys.begin(), keys.end(), lo);
    if (it == keys.end())
      return false;
    return hi_inc ? (*it <= hi) : (*it < hi);
  }
};

// ---------------------------------------------------------------- variants
struct Variant {
  const char *name;
  surf::SuffixType type;
  surf::level_t hash_len;
  surf::level_t real_len;
};
static const Variant kVariants[] = {
    {"SuRF-Base", surf::kNone, 0, 0},
    {"SuRF-Real8", surf::kReal, 0, 8},
};

static std::unique_ptr<SuRF> build(const std::vector<std::string> &keys,
                                   const Variant &v) {
  return std::unique_ptr<SuRF>(
      new SuRF(keys, v.type, v.hash_len, v.real_len));
}

// ---------------------------------------------------------------- key sets
static std::string u64key(uint64_t x) { // big-endian: byte order == numeric order
  std::string s(8, '\0');
  for (int i = 7; i >= 0; --i) {
    s[i] = static_cast<char>(x & 0xff);
    x >>= 8;
  }
  return s;
}

static std::vector<std::string> sortedUnique(std::vector<std::string> v) {
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}

static std::vector<std::string> genIntKeys(size_t n, std::mt19937_64 &rng) {
  std::vector<std::string> v;
  v.reserve(n + n / 8);
  while (v.size() < n + n / 16)
    v.push_back(u64key(rng()));
  v = sortedUnique(std::move(v));
  if (v.size() > n) { // drop a deterministic random subset to get exactly n
    std::shuffle(v.begin(), v.end(), rng);
    v.resize(n);
    std::sort(v.begin(), v.end());
  }
  return v;
}

static std::string randomEmail(std::mt19937_64 &rng) {
  static const char *domains[] = {"gmail.com",   "yahoo.com", "illinois.edu",
                                  "outlook.com", "cmu.edu",   "example.org",
                                  "proton.me",   "icloud.com"};
  std::uniform_int_distribution<int> len(4, 12), ch(0, 25), dom(0, 7);
  std::string s;
  int l = len(rng);
  for (int i = 0; i < l; ++i)
    s.push_back(static_cast<char>('a' + ch(rng)));
  // reversed-domain-first layout (as in the SuRF paper's email keys)
  std::string d = domains[dom(rng)];
  return d + "@" + s;
}

static std::vector<std::string> genStringKeys(size_t n, std::mt19937_64 &rng) {
  std::vector<std::string> v;
  while (v.size() < n) {
    size_t need = n - v.size();
    for (size_t i = 0; i < need + need / 8 + 16; ++i)
      v.push_back(randomEmail(rng));
    v = sortedUnique(std::move(v));
  }
  std::shuffle(v.begin(), v.end(), rng);
  v.resize(n);
  std::sort(v.begin(), v.end());
  return v;
}

// ---------------------------------------------------------------- results
struct CheckGroup {
  std::string keyset, variant, category;
  long checks = 0, failed = 0;
  std::vector<std::string> examples; // first few failure descriptions
  void check(bool ok, const std::function<std::string()> &describe) {
    ++checks;
    if (!ok) {
      ++failed;
      if (examples.size() < 3)
        examples.push_back(describe());
    }
  }
};

static std::string hex(const std::string &s) {
  bool printable = std::all_of(s.begin(), s.end(), [](char c) {
    return c >= 32 && c < 127 && c != '"' && c != '\\';
  });
  if (printable)
    return s;
  static const char *d = "0123456789abcdef";
  std::string o = "0x";
  for (unsigned char c : s) {
    o.push_back(d[c >> 4]);
    o.push_back(d[c & 15]);
  }
  return o;
}

static std::string jsonEsc(const std::string &s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
      o.push_back(c);
    } else if (static_cast<unsigned char>(c) < 32) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04x", c);
      o += buf;
    } else {
      o.push_back(c);
    }
  }
  return o;
}

// ---------------------------------------------------------------- correctness
struct NamedKeys {
  std::string name;
  std::vector<std::string> keys;
};

static std::vector<NamedKeys> correctnessKeysets(const std::string &words) {
  std::vector<NamedKeys> sets;
  // Hand-written edge cases: shared prefixes, a key that is a prefix of
  // another, single characters, and adjacent strings.
  sets.push_back({"edge-cases",
                  sortedUnique({"a", "ab", "abc", "abd", "b", "f", "far",
                                "fast", "fat", "s", "top", "toy", "trie",
                                "trip", "z", "zz", "zzz"})});
  // Binary integer keys that deliberately contain 0x00 and 0xFF bytes.
  // 0xFF is SuRF's internal terminator label, so these are the risky ones.
  {
    std::vector<std::string> v;
    for (uint64_t x : {0x0ull, 0x1ull, 0xffull, 0x100ull, 0xff00ull,
                       0xffffull, 0x00ff00ff00ff00ffull, 0x7fffffffffffffffull,
                       0x8000000000000000ull, 0xfffffffffffffffeull,
                       0xffffffffffffffffull})
      v.push_back(u64key(x));
    sets.push_back({"int-ff-bytes", sortedUnique(v)});
  }
  // Real English words from the SuRF repo's test data (every 50th word).
  {
    std::ifstream in(words);
    std::vector<std::string> v;
    std::string w;
    for (size_t i = 0; std::getline(in, w); ++i)
      if (!w.empty() && i % 50 == 0)
        v.push_back(w);
    if (!v.empty())
      sets.push_back({"words", sortedUnique(v)});
  }
  std::mt19937_64 rng(598);
  sets.push_back({"random-int", genIntKeys(2000, rng)});
  sets.push_back({"random-email", genStringKeys(2000, rng)});
  return sets;
}

// Queries replayed against the original and deserialized filter.
struct Query {
  bool range;
  std::string lo, hi;
  bool lo_inc, hi_inc;
};

static bool ask(SuRF &f, const Query &q) {
  return q.range ? f.lookupRange(q.lo, q.lo_inc, q.hi, q.hi_inc)
                 : f.lookupKey(q.lo);
}

static std::vector<CheckGroup> runCorrectness(const std::string &words) {
  std::vector<CheckGroup> out;
  for (const auto &ks : correctnessKeysets(words)) {
    Reference ref{ks.keys};
    const auto &K = ks.keys;
    std::mt19937_64 rng(42);

    // Query set: every key, absent neighbours, and boundary ranges.
    std::vector<Query> qs;
    for (size_t i = 0; i < K.size(); ++i) {
      const std::string &k = K[i];
      qs.push_back({false, k, "", true, true});           // present key
      qs.push_back({false, k + "a", "", true, true});     // absent extension
      qs.push_back({true, k, k, true, true});             // [k, k]
      qs.push_back({true, k, k + "\x01", true, false});   // [k, k+1)
      if (i + 1 < K.size()) {
        const std::string &nx = K[i + 1];
        qs.push_back({true, k, nx, false, false});        // (k, next) empty
        qs.push_back({true, k, nx, false, true});         // (k, next] = {next}
        qs.push_back({true, k, nx, true, false});         // [k, next) = {k}
      }
    }
    // Random ranges between random endpoints (mostly non-empty).
    std::uniform_int_distribution<size_t> pick(0, K.size() - 1);
    for (int j = 0; j < 50; ++j) {
      std::string a = K[pick(rng)], b = K[pick(rng)];
      if (b < a)
        std::swap(a, b);
      qs.push_back({true, a, b, j % 2 == 0, j % 3 == 0});
    }

    for (const auto &v : kVariants) {
      auto f = build(K, v);
      CheckGroup point{ks.name, v.name, "point lookup (no false negatives)"};
      CheckGroup range{ks.name, v.name, "range boundary (no false negatives)"};
      std::vector<bool> answers;
      for (const auto &q : qs) {
        bool got = ask(*f, q);
        answers.push_back(got);
        bool truth = q.range ? ref.hasRange(q.lo, q.lo_inc, q.hi, q.hi_inc)
                             : ref.hasKey(q.lo);
        if (!truth)
          continue; // absent: any answer is allowed (FP is not a failure)
        auto describe = [&q] {
          return q.range ? std::string(q.lo_inc ? "[" : "(") + hex(q.lo) +
                               ", " + hex(q.hi) + (q.hi_inc ? "]" : ")")
                         : "key " + hex(q.lo);
        };
        (q.range ? range : point).check(got, describe);
      }
      out.push_back(point);
      out.push_back(range);

      // Serialization round trip, from an aligned buffer and from buffers
      // shifted by 1..7 bytes (RocksDB filter blocks need not be 8-aligned).
      char *ser = f->serialize();
      uint64_t sz = f->serializedSize();
      CheckGroup ser_ok{ks.name, v.name, "serialization round trip (aligned)"};
      CheckGroup ser_mis{ks.name, v.name,
                         "serialization round trip (misaligned buffer)"};
      for (int off = 0; off < 8; ++off) {
        std::vector<char> buf(sz + 16);
        char *base = reinterpret_cast<char *>(
            (reinterpret_cast<uintptr_t>(buf.data()) + 7) & ~uintptr_t(7));
        std::memcpy(base + off, ser, sz);
        // Run in a child process: a bad buffer can crash SuRF (e.g. SIGFPE),
        // and that should be reported as a failed check, not kill the run.
        std::fflush(nullptr);
        pid_t pid = fork();
        if (pid == 0) {
          SuRF *g = SuRF::deSerialize(base + off);
          size_t mismatches = 0;
          for (size_t i = 0; i < qs.size(); ++i)
            mismatches += ask(*g, qs[i]) != answers[i];
          _exit(mismatches == 0 ? 0 : 1);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
        (off == 0 ? ser_ok : ser_mis).check(ok, [&] {
          if (WIFSIGNALED(st))
            return "offset " + std::to_string(off) + ": deSerialize crashed (" +
                   strsignal(WTERMSIG(st)) + ")";
          return "offset " + std::to_string(off) +
                 ": deserialized filter answered differently";
        });
      }
      delete[] ser;
      out.push_back(ser_ok);
      out.push_back(ser_mis);
      f->destroy();
    }
  }
  return out;
}

// ---------------------------------------------------------------- bench
struct BenchRow {
  std::string variant;
  size_t keys = 0, queries = 0;
  double build_ms = 0, point_ns = 0, range_ns = 0;
  uint64_t mem_bytes = 0, ser_bytes = 0;
  double point_fpr = 0, range_fpr = 0;
  long point_fn = 0, range_fn = 0, point_neg = 0, range_neg = 0;
};

static double ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

static std::vector<BenchRow> runBench(size_t n, const std::string &type,
                                      size_t nq) {
  std::mt19937_64 rng(2026);
  auto keys = type == "int" ? genIntKeys(n, rng) : genStringKeys(n, rng);
  Reference ref{keys};

  // Negative point queries: random keys of the same kind, verified absent.
  std::vector<std::string> neg_pts;
  while (neg_pts.size() < nq) {
    std::string k = type == "int" ? u64key(rng()) : randomEmail(rng);
    if (!ref.hasKey(k))
      neg_pts.push_back(k);
  }
  // Negative ranges, verified empty against the reference:
  //   int:    [x, x + 2^16)
  //   string: [q, q + "\x7f")  (keys sharing prefix q, excluding q itself)
  std::vector<std::pair<std::string, std::string>> neg_rng;
  while (neg_rng.size() < nq) {
    std::string lo, hi;
    if (type == "int") {
      uint64_t x = rng() & ~uint64_t(0xffff);
      if (x > ~uint64_t(0) - 0x10000)
        continue;
      lo = u64key(x);
      hi = u64key(x + 0x10000);
    } else {
      lo = randomEmail(rng);
      hi = lo + "\x7f";
    }
    if (!ref.hasRange(lo, true, hi, false))
      neg_rng.emplace_back(lo, hi);
  }
  // Positive queries (for false negative checks).
  std::vector<std::string> pos_pts;
  std::uniform_int_distribution<size_t> pick(0, keys.size() - 1);
  for (size_t i = 0; i < nq; ++i)
    pos_pts.push_back(keys[pick(rng)]);

  std::vector<BenchRow> rows;
  for (const auto &v : kVariants) {
    BenchRow r;
    r.variant = v.name;
    r.keys = keys.size();
    r.queries = nq;

    auto t0 = Clock::now();
    auto f = build(keys, v);
    r.build_ms = ms(Clock::now() - t0);
    r.mem_bytes = f->getMemoryUsage();
    r.ser_bytes = f->serializedSize();

    long fp = 0;
    t0 = Clock::now();
    for (const auto &k : neg_pts)
      fp += f->lookupKey(k);
    r.point_ns = ms(Clock::now() - t0) * 1e6 / nq;
    r.point_neg = nq;
    r.point_fpr = double(fp) / nq;

    long rfp = 0;
    t0 = Clock::now();
    for (const auto &q : neg_rng)
      rfp += f->lookupRange(q.first, true, q.second, false);
    r.range_ns = ms(Clock::now() - t0) * 1e6 / nq;
    r.range_neg = nq;
    r.range_fpr = double(rfp) / nq;

    for (const auto &k : pos_pts)
      r.point_fn += !f->lookupKey(k);
    for (const auto &k : pos_pts) // [k, k + "\x01") always contains k
      r.range_fn += !f->lookupRange(k, true, k + "\x01", false);

    f->destroy();
    rows.push_back(r);
  }
  return rows;
}

// ---------------------------------------------------------------- main
int main(int argc, char **argv) {
  std::string mode = argc > 1 ? argv[1] : "";
  bool json = false;
  size_t n = 100000, nq = 100000;
  std::string type = "int", words = "SuRF/test/words.txt";
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--json")
      json = true;
    else if (a == "--keys" && i + 1 < argc)
      n = std::stoull(argv[++i]);
    else if (a == "--queries" && i + 1 < argc)
      nq = std::stoull(argv[++i]);
    else if (a == "--type" && i + 1 < argc)
      type = argv[++i];
    else if (a == "--words" && i + 1 < argc)
      words = argv[++i];
  }

  if (mode == "correctness") {
    auto groups = runCorrectness(words);
    long total = 0, failed = 0;
    for (auto &g : groups) {
      total += g.checks;
      failed += g.failed;
    }
    if (json) {
      std::cout << "{\"mode\":\"correctness\",\"total\":" << total
                << ",\"failed\":" << failed << ",\"groups\":[";
      for (size_t i = 0; i < groups.size(); ++i) {
        auto &g = groups[i];
        std::cout << (i ? "," : "") << "{\"keyset\":\"" << g.keyset
                  << "\",\"variant\":\"" << g.variant << "\",\"category\":\""
                  << g.category << "\",\"checks\":" << g.checks
                  << ",\"failed\":" << g.failed << ",\"examples\":[";
        for (size_t j = 0; j < g.examples.size(); ++j)
          std::cout << (j ? "," : "") << "\"" << jsonEsc(g.examples[j])
                    << "\"";
        std::cout << "]}";
      }
      std::cout << "]}\n";
    } else {
      for (auto &g : groups) {
        std::printf("%-6s %-13s %-11s %-46s %6ld checks, %ld failed\n",
                    g.failed ? "FAIL" : "ok", g.keyset.c_str(),
                    g.variant.c_str(), g.category.c_str(), g.checks, g.failed);
        for (auto &e : g.examples)
          std::printf("         e.g. %s\n", e.c_str());
      }
      std::printf("\nTOTAL: %ld checks, %ld failed\n", total, failed);
    }
    return failed ? 1 : 0;
  }

  if (mode == "bench") {
    if (type != "int" && type != "string") {
      std::cerr << "--type must be int or string\n";
      return 2;
    }
    auto rows = runBench(n, type, nq);
    if (json) {
      std::cout << "{\"mode\":\"bench\",\"type\":\"" << type
                << "\",\"rows\":[";
      for (size_t i = 0; i < rows.size(); ++i) {
        auto &r = rows[i];
        std::cout << (i ? "," : "") << "{\"variant\":\"" << r.variant
                  << "\",\"keys\":" << r.keys << ",\"queries\":" << r.queries
                  << ",\"build_ms\":" << r.build_ms
                  << ",\"point_ns\":" << r.point_ns
                  << ",\"range_ns\":" << r.range_ns
                  << ",\"mem_bytes\":" << r.mem_bytes
                  << ",\"bits_per_key\":" << 8.0 * r.mem_bytes / r.keys
                  << ",\"ser_bytes\":" << r.ser_bytes
                  << ",\"point_fpr\":" << r.point_fpr
                  << ",\"range_fpr\":" << r.range_fpr
                  << ",\"point_fn\":" << r.point_fn
                  << ",\"range_fn\":" << r.range_fn << "}";
      }
      std::cout << "]}\n";
    } else {
      std::printf("%-11s %9s %9s %9s %9s %10s %9s %9s %5s %5s\n", "variant",
                  "keys", "build ms", "pt ns", "rng ns", "bits/key",
                  "pt FPR", "rng FPR", "ptFN", "rgFN");
      for (auto &r : rows)
        std::printf("%-11s %9zu %9.1f %9.1f %9.1f %10.2f %8.3f%% %8.3f%% "
                    "%5ld %5ld\n",
                    r.variant.c_str(), r.keys, r.build_ms, r.point_ns,
                    r.range_ns, 8.0 * r.mem_bytes / r.keys,
                    100 * r.point_fpr, 100 * r.range_fpr, r.point_fn,
                    r.range_fn);
    }
    long fn = 0;
    for (auto &r : rows)
      fn += r.point_fn + r.range_fn;
    return fn ? 1 : 0;
  }

  std::cerr << "usage:\n  " << argv[0] << " correctness [--json]\n  "
            << argv[0]
            << " bench --keys N [--type int|string] [--queries Q] [--json]\n";
  return 2;
}
