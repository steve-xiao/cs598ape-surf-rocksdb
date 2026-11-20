// Appendix example 1: what SuRF answers on five db_bench-style keys.
//   g++ -O2 -std=c++17 -mpopcnt -I../../third-party/surf/include trie_example.cpp
#include <cstdio>
#include <string>
#include <vector>

#include "surf.hpp"

using namespace surf;

// db_bench key format (key_size 16): 8-byte big-endian integer + "00000000".
static std::string Key(uint64_t v, int pad = 8) {
  std::string s(8, '\0');
  for (int i = 7; i >= 0; --i) {
    s[i] = static_cast<char>(v & 0xff);
    v >>= 8;
  }
  return s + std::string(pad, '0');
}

int main() {
  const std::vector<uint64_t> ints = {12, 17, 23, 71, 75};
  std::vector<std::string> keys;
  for (uint64_t v : ints) keys.push_back(Key(v));

  struct V {
    const char* name;
    SuffixType t;
    level_t hash, real;
  } vs[] = {{"Base", kNone, 0, 0}, {"Real8", kReal, 0, 8}, {"Hash8", kHash, 8, 0}};

  struct Q {
    const char* what;
    std::string a, b;
    bool range;
    bool truth;
  } qs[] = {
      {"point 17 (present)", Key(17), "", false, true},
      {"point 43 (absent)", Key(43), "", false, false},
      {"point 17+'0' (readmissing-style)", Key(17, 9), "", false, false},
      {"range [40,50)", Key(40), Key(50), true, false},
      {"range [70,80)", Key(70), Key(80), true, true},
      {"range [24,71)", Key(24), Key(71), true, false},
      {"range [76,1000)", Key(76), Key(1000), true, false},
      {"range [0,1000)", Key(0), Key(1000), true, true},
  };

  std::printf("%-34s %-6s", "query", "truth");
  for (auto& v : vs) std::printf(" %-6s", v.name);
  std::printf("\n");
  std::vector<SuRF*> fs;
  for (auto& v : vs) fs.push_back(new SuRF(keys, v.t, v.hash, v.real));
  for (auto& q : qs) {
    std::printf("%-34s %-6s", q.what, q.truth ? "yes" : "no");
    for (SuRF* f : fs) {
      bool r = q.range ? f->mayContainRange(q.a, q.b) : f->lookupKey(q.a);
      std::printf(" %-6s", r ? "maybe" : "no");
    }
    std::printf("\n");
  }
  std::printf("%-34s %-6s", "serialized bytes", "");
  for (SuRF* f : fs) std::printf(" %-6lu", (unsigned long)f->serializedSize());
  std::printf("\n");
  for (SuRF* f : fs) {
    f->destroy();
    delete f;
  }
  return 0;
}
