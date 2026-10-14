# SuRF standalone harness

Tests [SuRF](https://github.com/efficient/SuRF) against an exact reference (a
sorted `std::vector<std::string>`), and measures false positive rate (FPR),
memory, build time and query time. Includes a local web UI to run tests and
view results.

Variants tested: **SuRF-Base** (no suffix) and **SuRF-Real8** (8-bit real
suffix).

## Setup

```bash
# Ubuntu 24.04 / the course VM: g++ and python3 are all you need
git clone https://github.com/efficient/SuRF     # header-only, no build step
make                                            # builds ./surf_harness
```

Use `make SURF_DIR=/path/to/SuRF` if SuRF is cloned somewhere else.

## Run

```bash
./surf_harness correctness                     # correctness suite
./surf_harness bench --keys 1000000 --type int # or --type string
python3 web/server.py                          # UI at http://localhost:8598
```

Add `--json` for machine-readable output. Every run started from the UI is
saved in `results/` as JSON. The exit code is nonzero if any check fails or
any false negative is seen.

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
| Serialization (misaligned) | Same, from buffers shifted by 1 to 7 bytes (RocksDB does not guarantee filter data is 8-byte aligned). Runs in a child process so a crash is recorded as a failure |

Key sets: hand-written edge cases (shared prefixes, a key that is a prefix of
another), binary integer keys containing `0x00` and `0xFF` bytes (`0xFF` is
SuRF's internal terminator label), every 50th word of SuRF's `words.txt`,
2,000 random 64-bit integers, and 2,000 random email-like strings.

Benchmark (`bench`): N keys, 100,000 negative point queries and 100,000
negative range queries, all verified absent/empty against the reference, plus
100,000 positive queries for false negative checks. Ranges are
`[x, x + 2^16)` for integer keys and `[q, q + "\x7f")` (prefix-style) for
string keys. All random data is seeded, so runs are reproducible.

## Results (2-core cloud VM, Oct 14 2026)

Correctness: **87,692 checks, 70 failed.**

- Point lookup and range boundary: **87,622 checks, 0 false negatives**.
- Aligned serialization round trip: 10/10 passed.
- Misaligned serialization: **70/70 failed**. `deSerialize()` crashes
  (SIGFPE/SIGSEGV) when the buffer is not 8-byte aligned, because
  `align()` in `config.hpp` rounds the absolute pointer address, not an
  offset from the start of the buffer.

Benchmarks (no false negatives in any run):

| Keys | Type | Variant | Bits/key | Point FPR | Range FPR | Point ns | Range ns |
|---:|---|---|---:|---:|---:|---:|---:|
| 10K | int | Base | 16.0 | 13.1% | 13.2% | 27 | 90 |
| 10K | int | Real8 | 24.0 | 0.10% | 0.05% | 31 | 89 |
| 100K | int | Base | 9.5 | 33.7% | 33.9% | 81 | 148 |
| 100K | int | Real8 | 17.5 | 0.27% | 0.13% | 86 | 181 |
| 1M | int | Base | 10.5 | 5.6% | 5.6% | 149 | 264 |
| 1M | int | Real8 | 44.5 | 0.04% | 0.02% | 51 | 124 |
| 10K | string | Base | 14.9 | 34.2% | 34.3% | 482 | 682 |
| 10K | string | Real8 | 22.9 | 1.35% | 1.31% | 484 | 715 |
| 100K | string | Base | 13.4 | 36.0% | 35.8% | 586 | 792 |
| 100K | string | Real8 | 21.4 | 1.45% | 1.42% | 615 | 756 |
| 1M | string | Base | 13.1 | 21.6% | 22.1% | 436 | 549 |
| 1M | string | Real8 | 21.1 | 2.76% | 3.08% | 539 | 664 |

Query times vary roughly +/-50% between runs on this VM; FPR and memory are
exact and reproducible.

## Findings to carry into the RocksDB integration

1. **Copy filter data to an aligned buffer before `deSerialize()`.** RocksDB
   passes the filter block as a `Slice` with no alignment guarantee.
2. **`lookupRange()` is not thread-safe**: it is non-`const` and reuses a
   member iterator. RocksDB readers run concurrently, so use a per-call
   iterator (`moveToKeyGreaterThan`) or a lock.
3. **SuRF's headers define non-`inline` functions** (e.g. `align()` in
   `config.hpp`). Including `surf.hpp` from more than one `.cc` file in
   RocksDB will cause duplicate-symbol link errors; include it from exactly
   one file, or mark those functions `inline`.
4. **Base FPR is not monotonic in N for random integers** (13% -> 34% ->
   6%). That is expected: the trie stores the shortest distinguishing
   prefix, so FPR depends on how full the last stored byte level is
   (~N/65,536 keys per 2-byte prefix at 100K vs. ~N/16.7M per 3-byte prefix
   at 1M). Worth one sentence in the report.
5. **Build memory spike (not yet explained):** building SuRF-Base on 1M
   random integer keys peaked at ~3.5 GB of RSS and took ~2.3 s, while
   SuRF-Real8 on the same keys used ~62 MB and ~0.09 s (it chose one more
   dense trie level, hence 44 bits/key). RocksDB builds one filter per SST
   file during flush and compaction, so this needs investigating before the
   large-scale runs.
