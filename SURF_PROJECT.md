# SuRF range filters in RocksDB (CS598 APE project)

Steven Xiao and Apurv Aditya Singhdeo.

This repository is a fork of RocksDB v11.8.1 that adds
[SuRF](https://github.com/efficient/SuRF) (Succinct Range Filter, Zhang et
al., SIGMOD 2018) as an SST filter and uses it to skip SST files on bounded
range scans. Bloom and Ribbon filters cannot do this: they only answer "is
this exact key here?".

Everything except the files listed below is unmodified upstream RocksDB.

## Hardware and software requirements

- x86-64 CPU with `POPCNT` (any CPU from the last ~12 years).
- Linux. Tested on Ubuntu 24.04, g++ 13.3, 2 cores, 8 GB RAM.
- Packages: `build-essential libgflags-dev libsnappy-dev liblz4-dev
  libzstd-dev zlib1g-dev libbz2-dev python3`; optional `python3-matplotlib`
  (charts), `valgrind`, `linux-tools-generic` (perf).
- Disk: ~1.5 GB for the build, ~1 GB for benchmark databases in `/tmp`.
- Building `db_bench` + `sst_dump` + `librocksdb.a` takes ~15 minutes on 2
  cores.

## File layout (what we added or changed)

| Path | What it is |
|---|---|
| `table/block_based/surf_filter_policy.{h,cc}` | `SurfFilterPolicy`: builder collects user keys and serializes SuRF; reader deserializes (from an aligned copy) and answers point (`MayMatch`) and range (`RangeMayMatch`) queries |
| `table/block_based/filter_policy_internal.h` | new `FilterBitsReader::RangeMayMatch` (default: "maybe") |
| `table/block_based/filter_block.h`, `full_filter_block.{h,cc}` | new `UserKeyRangeMayMatch` on filter block readers |
| `table/block_based/block_based_table_reader.{h,cc}` | `RangeFilterApplicable()` and `UserKeyRangeMayMatch()` |
| `table/block_based/block_based_table_iterator.{h,cc}` | the range check in `Seek` when `iterate_upper_bound` is set |
| `tools/db_bench_tool.cc` | `--use_surf_filter`, `--surf_suffix=none|realN|hashN` |
| `src.mk`, `CMakeLists.txt` | build the new source file |
| `third-party/surf/` | vendored SuRF with our bug fixes (see below) and `words.txt` test data |
| `surf-harness/surf_harness.cpp` | standalone SuRF correctness suite and microbenchmark |
| `surf-harness/rocksdb_filter_test.cc` | end-to-end RocksDB correctness test for every filter |
| `surf-harness/dbbench/` | `run_dbbench.sh` (filter comparison), `summarize.py`, `peak_rss.py` |
| `surf-harness/web/` | local web UI for the harness |

## Build

```bash
make -j"$(nproc)" DEBUG_LEVEL=0 db_bench sst_dump static_lib   # ~15 min
cd surf-harness
make                          # ./surf_harness (standalone, needs no RocksDB build)
make rocksdb_filter_test      # links ../librocksdb.a
```

`DEBUG_LEVEL=0` matters: the default build has assertions on and its timings
are meaningless.

## Test

```bash
cd surf-harness
./surf_harness correctness    # 175,332 checks; exit code 0 = all pass
./rocksdb_filter_test         # RocksDB vs std::map for none/Bloom/Ribbon/SuRF
make valgrind                 # harness under valgrind (expects 0 errors, 0 leaks)
```

Valgrind cannot run AVX-512 code, and the default RocksDB build uses
`-march=native`. To run the end-to-end test under valgrind, build a portable
library first (another ~15 minutes):

```bash
make clean && make -j"$(nproc)" PORTABLE=1 DEBUG_LEVEL=0 static_lib
cd surf-harness && rm -f rocksdb_filter_test && make valgrind-rocksdb
```

Our results: harness 175,332 checks and `rocksdb_filter_test` 571,550 checks
(3,714,410 with `--keys 200000 --queries 100000`), all passing; under
valgrind both report 0 errors and 0 bytes lost.

`surf_harness correctness` builds SuRF-Base and SuRF-Real8 on five key sets
(edge cases, integers with 0x00/0xFF bytes, English words, random integers,
random emails) and checks, against a sorted-vector reference: every point
lookup, every range boundary, the thread-safe `mayContainRange` used by
RocksDB, and serialization round trips from aligned and misaligned buffers.
Calling SuRF's `deSerialize` directly on a misaligned buffer still crashes;
that is an upstream limitation, reported separately and not counted. RocksDB
never does this (it copies into an aligned buffer first).

`rocksdb_filter_test` opens a real database per filter (small write buffers so
there are several LSM levels), inserts random keys plus edge cases and
deletes, and checks every `Get` and 20,000 bounded range scans
(`iterate_upper_bound`) against a `std::map`, before and after reopening the
database. It also prints how many SST files each filter skipped.

## Benchmark

```bash
surf-harness/dbbench/run_dbbench.sh                 # ~7 min on 2 cores
QUICK=1 surf-harness/dbbench/run_dbbench.sh         # ~1 min smoke test
```

For each filter (no filter, Bloom 10 bits/key, Ribbon 10 bits/key,
SuRF-Base, SuRF-Real8, SuRF-Hash8) it builds the same database
(`fillrandom` of 1M keys drawn from a 10M key space, then
`waitforcompaction`), records filter size with `sst_dump`, and runs, 3 times
each in fresh processes with fixed seeds:

- `readrandom`: random point lookups (about 90% absent),
- `readmissing`: absent keys that extend a stored key (adversarial for SuRF),
- `seekrandom --max_scan_distance=D --seek_nexts=0` for D in 10, 100, 1000:
  bounded seeks, the workload a range filter can help.

Results go to `surf-harness/dbbench/results/<timestamp>/` (`summary.md`,
`load.csv`, `reads.csv`, charts, raw logs). Settings can be changed from the
environment (`NUM`, `WRITES`, `READS`, `TRIALS`, `DISTS`, `FILTERS`, `EXTRA`);
e.g. a deeper LSM:

```bash
EXTRA="--write_buffer_size=4194304 --target_file_size_base=4194304 \
--max_bytes_for_level_base=16777216" surf-harness/dbbench/run_dbbench.sh
```

Single runs also work, e.g.:

```bash
./db_bench --db=/tmp/surf --benchmarks=fillrandom,waitforcompaction \
  --num=10000000 --writes=1000000 --use_surf_filter --surf_suffix=real8 --statistics
./db_bench --db=/tmp/surf --use_existing_db --benchmarks=seekrandom --reads=200000 \
  --num=10000000 --seek_nexts=0 --max_scan_distance=10 --use_surf_filter \
  --surf_suffix=real8 --statistics | grep -E "seekrandom|seek.filter"
```

Files skipped by the range filter are counted by the existing tickers
`rocksdb.(non.)last.level.seek.filtered`; probes it could not rule out by
`rocksdb.(non.)last.level.seek.filter.match`.

## Results summary

Final numbers are in `surf-harness/dbbench/final-results/` (see its README).
Bounded seeks, SuRF-Real8 vs. Bloom (10 bits/key), median us/op:

| LSM shape | range 10 keys | range 100 | range 1000 |
|---|---|---|---|
| A: leveled, 3 SSTs | 7.0 vs 11.1 (1.6x faster) | 11.6 vs 11.6 | 12.0 vs 11.1 (8% slower) |
| C: 36 overlapping SSTs | 37.2 vs 124.8 (3.4x faster) | 68.3 vs 124.6 (1.8x faster) | 134.3 vs 130.0 (3% slower) |

Point lookups (shape A): Bloom 0.98% false positives per SST probe at 10
bits/key and 1.1 us/op; SuRF-Real8 0.04% at 18.3 bits/key but 1.6 us/op;
SuRF-Base 9.6% at 10.3 bits/key. On `readmissing` (absent keys that extend a
stored key) SuRF-Hash8 stays at 0.05% while Base/Real8 rise to 12% / 3.3%.

## How the integration works

1. **Build.** `SurfBitsBuilder::AddKey` receives user keys in sorted order
   during flush and compaction, drops consecutive duplicates, and `Finish`
   builds and serializes a SuRF behind an 8-byte header (magic + mode).
   Unsorted or empty keys, or prefix entries (`AddKeyAndAlt`), make it emit
   an always-match filter instead, so results stay correct.
2. **Point lookups** go through the normal full-filter path
   (`FilterBitsReader::MayMatch` -> `SuRF::lookupKey`).
3. **Bounded seeks.** In `BlockBasedTableIterator::SeekImpl`, if the read has
   `iterate_upper_bound` and the table has a SuRF filter on whole bytewise
   user keys (no prefix extractor, no timestamps), we ask
   `SuRF::mayContainRange(user_key(target), upper_bound)`. If it says no, the
   iterator becomes invalid and reports out-of-bound, so `LevelIterator` also
   stops instead of moving to the next file (correct, because that file's
   keys are all at or past the upper bound).
4. **Adaptive checking.** Each SST file's filter reader counts its range
   checks and how many ruled the file out. After 64 checks, if fewer than 1
   in 8 helped (wide ranges, dense data), it only checks every 16th seek; the
   counts decay by half every 4,096 checks so it adapts if the workload
   changes. Skipping a check only means "maybe", so correctness never depends
   on it (`full_filter_block.{h,cc}`).
5. `CompatibilityName()` is `rocksdb.SurfFilter`, so RocksDB never reads a
   SuRF block as Bloom or the reverse.

## Fixes to the vendored SuRF

All found by our harness or by valgrind; each keeps results identical.

| Problem | Effect | Fix |
|---|---|---|
| `LabelVector` allocated `n * (n/64 + 1)` bytes (quadratic, overflowing 32 bits) | 3.5 GB peak and 2 s to build SuRF-Base on 1M integer keys | allocate `n + 16` |
| Label search reads 16 bytes at a time with no padding | possible out-of-bounds read | pad by 16 bytes |
| `destroy()` freed arrays but never the objects holding them | memory leak per filter | delete them |
| `Bitvector::concatenateBitvectors` stepped one word past the end | out-of-bounds read/write (valgrind) | guard the last word |
| `storeSuffix` shifted by 64 with no suffix | undefined behavior | return early |
| non-`inline` functions in `config.hpp` | duplicate symbols when included twice | `inline` |
| `lookupRange` reuses a member iterator | not thread-safe | new `const` `mayContainRange` |
| `SuRF::Iter` built sub-iterators with `push_back` and then copied them | ~25 allocations per range check | `assign` + direct construction: range check 1.45-1.80x faster |
| `serialize()` left alignment padding uninitialised | garbage bytes written into SST files (valgrind) | zero-initialise the buffer |
