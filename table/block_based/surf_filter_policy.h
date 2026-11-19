//  Copyright (c) Meta Platforms, Inc. and affiliates.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// SuRF (Succinct Range Filter, Zhang et al., SIGMOD 2018) as a RocksDB full
// filter. Point lookups use SuRF::lookupKey. Bounded seeks
// (ReadOptions::iterate_upper_bound) use SuRF's range check through
// FilterBitsReader::RangeMayMatch, so a file with no key in
// [seek target, upper bound) can be skipped.
//
// Requirements: bytewise-ordered user keys, whole_key_filtering, no
// prefix_extractor. Anything else falls back to an always-match filter or
// disables the range check, so results stay correct.

#pragma once

#include <cstdint>

#include "rocksdb/filter_policy.h"

namespace ROCKSDB_NAMESPACE {

enum class SurfSuffix : uint8_t { kNone = 0, kReal = 1, kHash = 2 };

class SurfFilterPolicy : public FilterPolicy {
 public:
  explicit SurfFilterPolicy(SurfSuffix suffix = SurfSuffix::kNone,
                            uint32_t suffix_bits = 0);

  static const char* kClassName() { return "rocksdb.SurfFilter"; }
  const char* Name() const override { return kClassName(); }
  const char* CompatibilityName() const override { return kClassName(); }
  std::string GetId() const override;

  FilterBitsBuilder* GetBuilderWithContext(
      const FilterBuildingContext& context) const override;
  FilterBitsReader* GetFilterBitsReader(const Slice& contents) const override;

  SurfSuffix suffix() const { return suffix_; }
  uint32_t suffix_bits() const { return suffix_bits_; }

 private:
  SurfSuffix suffix_;
  uint32_t suffix_bits_;
};

// "none", "real8", "hash8", ... (the number is the suffix length in bits).
// Returns nullptr for an unrecognized spec.
const FilterPolicy* NewSurfFilterPolicy(const std::string& spec);

bool IsSurfFilterPolicy(const FilterPolicy* policy);

}  // namespace ROCKSDB_NAMESPACE
