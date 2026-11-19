//  Copyright (c) Meta Platforms, Inc. and affiliates.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "table/block_based/surf_filter_policy.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "table/block_based/filter_policy_internal.h"

// SuRF headers define non-inline member functions, so they must be included
// from exactly one translation unit (this one).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#pragma GCC diagnostic ignored "-Wreorder"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#include "third-party/surf/include/surf.hpp"
#pragma GCC diagnostic pop

namespace ROCKSDB_NAMESPACE {

namespace {

// Filter block layout: 4-byte magic, 1-byte mode, 3 bytes padding, then the
// serialized SuRF (mode kSurf only).
constexpr char kMagic[4] = {'S', 'u', 'R', 'F'};
constexpr size_t kHeaderSize = 8;
enum Mode : uint8_t { kSurf = 0, kAlwaysMatch = 1 };

surf::SuffixType ToSurfType(SurfSuffix s) {
  switch (s) {
    case SurfSuffix::kReal:
      return surf::kReal;
    case SurfSuffix::kHash:
      return surf::kHash;
    default:
      return surf::kNone;
  }
}

class SurfBitsBuilder : public FilterBitsBuilder {
 public:
  SurfBitsBuilder(SurfSuffix suffix, uint32_t suffix_bits)
      : suffix_(suffix), suffix_bits_(suffix_bits) {}

  void AddKey(const Slice& key) override {
    ++added_;
    if (key.empty()) {
      always_match_ = true;
      return;
    }
    if (!keys_.empty()) {
      int c = key.compare(Slice(keys_.back()));
      if (c == 0) {
        return;
      }
      if (c < 0) {
        always_match_ = true;
        return;
      }
    }
    keys_.emplace_back(key.data(), key.size());
  }

  // Prefix entries cannot share a trie with whole keys; fall back.
  void AddKeyAndAlt(const Slice& key, const Slice& /*alt*/) override {
    always_match_ = true;
    AddKey(key);
  }

  size_t EstimateEntriesAdded() override {
    return added_ == 0 ? 0 : (keys_.empty() ? 1 : keys_.size());
  }

  using FilterBitsBuilder::Finish;

  Slice Finish(std::unique_ptr<const char[]>* buf) override {
    if (always_match_ || keys_.empty()) {
      return Emit(buf, kAlwaysMatch, nullptr, 0);
    }
    const surf::level_t hash_len =
        suffix_ == SurfSuffix::kHash ? suffix_bits_ : 0;
    const surf::level_t real_len =
        suffix_ == SurfSuffix::kReal ? suffix_bits_ : 0;
    std::unique_ptr<surf::SuRF> f(
        new surf::SuRF(keys_, ToSurfType(suffix_), hash_len, real_len));
    std::unique_ptr<char[]> ser(f->serialize());
    const uint64_t sz = f->serializedSize();
    f->destroy();
    keys_.clear();
    keys_.shrink_to_fit();
    return Emit(buf, kSurf, ser.get(), sz);
  }

  size_t ApproximateNumEntries(size_t bytes) override {
    return bytes * 8 / BitsPerKeyEstimate();
  }

  size_t CalculateSpace(size_t num_entries) override {
    return kHeaderSize + num_entries * BitsPerKeyEstimate() / 8 + 64;
  }

  double EstimatedFpRate(size_t /*num_entries*/, size_t /*bytes*/) override {
    return suffix_ == SurfSuffix::kNone ? 0.2 : 0.02;
  }

 private:
  size_t BitsPerKeyEstimate() const {
    return 14 + (suffix_ == SurfSuffix::kNone ? 0 : suffix_bits_);
  }

  static Slice Emit(std::unique_ptr<const char[]>* buf, Mode mode,
                    const char* data, uint64_t sz) {
    const size_t total = kHeaderSize + static_cast<size_t>(sz);
    char* out = new char[total];
    std::memset(out, 0, kHeaderSize);
    std::memcpy(out, kMagic, sizeof(kMagic));
    out[4] = static_cast<char>(mode);
    if (sz > 0) {
      std::memcpy(out + kHeaderSize, data, static_cast<size_t>(sz));
    }
    buf->reset(out);
    return Slice(out, total);
  }

  SurfSuffix suffix_;
  uint32_t suffix_bits_;
  std::vector<std::string> keys_;
  size_t added_ = 0;
  bool always_match_ = false;
};

class AlwaysMatchReader : public FilterBitsReader {
 public:
  using FilterBitsReader::MayMatch;
  bool MayMatch(const Slice& /*entry*/) override { return true; }
};

class SurfBitsReader : public FilterBitsReader {
 public:
  explicit SurfBitsReader(surf::SuRF* f) : f_(f) {}

  ~SurfBitsReader() override {
    f_->destroy();
    delete f_;
  }

  using FilterBitsReader::MayMatch;

  bool MayMatch(const Slice& entry) override {
    return f_->lookupKey(entry.ToString());
  }

  bool RangeMayMatch(const Slice& lo, const Slice& hi_exclusive,
                     bool* supported) override {
    *supported = true;
    return f_->mayContainRange(lo.ToString(), hi_exclusive.ToString());
  }

 private:
  surf::SuRF* f_;
};

}  // namespace

SurfFilterPolicy::SurfFilterPolicy(SurfSuffix suffix, uint32_t suffix_bits)
    : suffix_(suffix),
      suffix_bits_(suffix == SurfSuffix::kNone ? 0 : suffix_bits) {}

std::string SurfFilterPolicy::GetId() const {
  std::string id = kClassName();
  if (suffix_ == SurfSuffix::kReal) {
    id += ":real" + std::to_string(suffix_bits_);
  } else if (suffix_ == SurfSuffix::kHash) {
    id += ":hash" + std::to_string(suffix_bits_);
  } else {
    id += ":none";
  }
  return id;
}

FilterBitsBuilder* SurfFilterPolicy::GetBuilderWithContext(
    const FilterBuildingContext& /*context*/) const {
  return new SurfBitsBuilder(suffix_, suffix_bits_);
}

FilterBitsReader* SurfFilterPolicy::GetFilterBitsReader(
    const Slice& contents) const {
  if (contents.size() < kHeaderSize ||
      std::memcmp(contents.data(), kMagic, sizeof(kMagic)) != 0 ||
      static_cast<uint8_t>(contents.data()[4]) != kSurf) {
    return new AlwaysMatchReader();
  }
  // SuRF's deSerialize requires an 8-byte-aligned buffer; RocksDB does not
  // guarantee alignment, so copy first. deSerialize copies the data it keeps.
  const size_t sz = contents.size() - kHeaderSize;
  std::vector<uint64_t> aligned((sz + 7) / 8);
  std::memcpy(aligned.data(), contents.data() + kHeaderSize, sz);
  return new SurfBitsReader(
      surf::SuRF::deSerialize(reinterpret_cast<char*>(aligned.data())));
}

const FilterPolicy* NewSurfFilterPolicy(const std::string& spec) {
  if (spec.empty() || spec == "none" || spec == "base") {
    return new SurfFilterPolicy(SurfSuffix::kNone, 0);
  }
  SurfSuffix kind;
  std::string rest;
  if (spec.rfind("real", 0) == 0) {
    kind = SurfSuffix::kReal;
    rest = spec.substr(4);
  } else if (spec.rfind("hash", 0) == 0) {
    kind = SurfSuffix::kHash;
    rest = spec.substr(4);
  } else {
    return nullptr;
  }
  if (rest.empty() || rest.size() > 2 ||
      rest.find_first_not_of("0123456789") != std::string::npos) {
    return nullptr;
  }
  int bits = std::stoi(rest);
  if (bits < 1 || bits > 64) {
    return nullptr;
  }
  return new SurfFilterPolicy(kind, static_cast<uint32_t>(bits));
}

bool IsSurfFilterPolicy(const FilterPolicy* policy) {
  return policy != nullptr &&
         std::strcmp(policy->CompatibilityName(),
                     SurfFilterPolicy::kClassName()) == 0;
}

}  // namespace ROCKSDB_NAMESPACE
