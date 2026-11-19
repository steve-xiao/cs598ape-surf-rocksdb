# SuRF standalone harness

Tests [SuRF](https://github.com/efficient/SuRF) against an exact reference (a
sorted `std::vector<std::string>`), and measures false positive rate (FPR),
memory, build time and query time. Includes a local web UI to run tests and
view results.

Variants tested: **SuRF-Base** (no suffix) and **SuRF-Real8** (8-bit real
suffix). It uses the vendored, patched SuRF in `../third-party/surf/`. For the
RocksDB integration, end-to-end test and `db_bench` comparison, see
[`../SURF_PROJECT.md`](../SURF_PROJECT.md).

## Setup and run

```bash
make                                           # builds ./surf_harness (g++ only)
./surf_harness correctness                     # correctness suite
./surf_harness correctness --skip-known-crashes
./surf_harness bench --keys 1000000 --type int # or --type string
make valgrind                                  # suite + benchmarks under valgrind
python3 web/server.py                          # UI at http://localhost:8598
```

Add `--json` for machine-readable output. Every run started from the UI is
saved in `results/` as JSON. The exit code is nonzero if any check fails or
any false negative is seen. `make SURF_DIR=/path/to/SuRF` tests another copy
of SuRF (e.g. unpatched upstream).

## What is checked

A filter is allowed to answer "maybe" for an absent key or an empty range (a
**false positive**). That is measured, not a failure. A filter must never
answer "no" for a present key or a non-empty range (a **false negative**).
Every such answer is a failed check.

Correctness suite, for each key set x each variant:

| Check | What it does |
|---|---|
| Point lookup | Every inserted key must be found |
| Range boundary | `[k,k]`, `[k,k+1)`, `(k,next]`, `[k,next)` and 50 random ranges; every non-empty range must be reported |
| Serialization (aligned) | `serialize()` then `deSerialize()` from an 8-byte-aligned buffer must answer every query exactly like the original |
| `mayContainRange` | The thread-safe `[lo, hi)` check RocksDB uses: no false negatives, and the same answer as `lookupRange` for every range |
| Serialization (misaligned, aligned copy) | Data at offsets 0-7 is copied into an aligned buffer before `deSerialize()` (what RocksDB does); answers must match |
| Known limitation (not counted) | Raw `deSerialize()` on buffers shifted by 1-7 bytes, in a child process; upstream SuRF crashes here because `align()` rounds the absolute pointer. Skipped with `--skip-known-crashes` |

Key sets: hand-written edge cases (shared prefixes, a key that is a prefix of
another), binary integer keys containing `0x00` and `0xFF` bytes (`0xFF` is
SuRF's internal terminator label), every 50th word of SuRF's `words.txt`,
2,000 random 64-bit integers, and 2,000 random email-like strings.

Benchmark (`bench`): N keys, 100,000 negative point queries and 100,000
negative range queries, all verified absent/empty against the reference, plus
100,000 positive queries for false negative checks. Ranges are
`[x, x + 2^16)` for integer keys and `[q, q + "\x7f")` (prefix-style) for
string keys. All random data is seeded, so runs are reproducible.

## Results (2-core cloud VM, Nov 19 2026, patched SuRF)

Correctness: **175,332 checks, 0 failed** (70 raw misaligned `deSerialize`
calls crash, as documented above, and are not counted). `make valgrind`:
**0 errors, 0 bytes lost** in the suite and both benchmarks.

Benchmarks (no false negatives in any run; FPR and bits/key are exact and
reproducible, times vary by roughly +/-30%):

| Keys | Type | Variant | Bits/key | Point FPR | Range FPR | Build ms | Point ns | Range ns | mayContainRange ns | Peak RSS |
|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 10K | int | Base | 16.0 | 13.14% | 13.19% | 0.5 | 27 | 67 | 133 | 22MB |
| 10K | int | Real8 | 24.0 | 0.10% | 0.05% | 0.4 | 24 | 61 | 158 | 22MB |
| 100K | int | Base | 9.5 | 33.67% | 33.85% | 7.6 | 88 | 187 | 234 | 27MB |
| 100K | int | Real8 | 17.5 | 0.27% | 0.13% | 5.8 | 76 | 137 | 213 | 27MB |
| 1M | int | Base | 10.5 | 5.63% | 5.62% | 41.5 | 114 | 186 | 255 | 94MB |
| 1M | int | Real8 | 44.5 | 0.04% | 0.02% | 50.0 | 44 | 99 | 170 | 94MB |
| 10K | string | Base | 14.9 | 34.19% | 34.30% | 1.0 | 322 | 435 | 521 | 31MB |
| 10K | string | Real8 | 22.9 | 1.35% | 1.31% | 0.9 | 326 | 451 | 535 | 31MB |
| 100K | string | Base | 13.4 | 35.97% | 35.75% | 9.0 | 366 | 544 | 601 | 42MB |
| 100K | string | Real8 | 21.4 | 1.45% | 1.42% | 9.7 | 354 | 479 | 598 | 42MB |
| 1M | string | Base | 13.1 | 21.58% | 22.12% | 113.9 | 513 | 608 | 754 | 165MB |
| 1M | string | Real8 | 21.1 | 2.76% | 3.08% | 126.3 | 395 | 519 | 611 | 165MB |

Peak RSS is for the whole benchmark process (both variants plus query sets).

## Findings

1. **Quadratic memory in upstream SuRF (fixed).** `LabelVector` allocated
   `n * (n/64 + 1)` bytes; for SuRF-Base on 1M random integers that is ~16 GB,
   wrapped to ~3.5 GB by 32-bit arithmetic. Fixed to `n + 16`: peak RSS of the
   1M-integer run 3,565 MB -> 94 MB, build 2,175 ms -> 42 ms; 1M strings
   2,760 MB -> 165 MB, 1,335 ms -> 114 ms. Same answers.
2. **Valgrind findings in upstream SuRF (fixed).** Leaked trie objects in
   `destroy()`, an off-by-one past the end of `Bitvector::bits_`, unpadded
   16-byte SIMD label reads, uninitialised padding bytes in `serialize()`
   (they ended up inside RocksDB SST files), and a shift by 64 with no suffix.
3. **Misaligned buffers.** `deSerialize()` crashes unless the buffer is
   8-byte aligned; RocksDB filter blocks have no such guarantee, so the
   RocksDB reader copies into an aligned buffer first.
4. **Thread safety.** `lookupRange()` mutates a member iterator. We added a
   `const` `mayContainRange(lo, hi)` (per-call iterator), checked here against
   `lookupRange` on every range. Constructing that iterator originally cost
   ~25 allocations; building it in place made it 1.45-1.80x faster (still
   ~1.5-2x slower than `lookupRange`, which reuses its iterator).
5. **Base FPR is not monotonic in N for random integers** (13% -> 34% ->
   6%): the trie stores the shortest distinguishing prefix, so FPR depends on
   how full the last stored byte level is (~N/65,536 keys per 2-byte prefix at
   100K vs. ~N/16.7M per 3-byte prefix at 1M).
