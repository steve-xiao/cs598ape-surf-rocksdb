# Project setup: SuRF in RocksDB

Everything here was run and verified on Ubuntu 24.04 (2 cores, 8 GB RAM) on
Oct 14 2026.

## 1. Which repos matter

| Repo | What it is | Use it for |
|---|---|---|
| `efficient/SuRF` | The SuRF C++ library (header-only) | **The code you integrate** |
| `efficient/SuRF-demo` | A browser visualization (SuRF compiled to JavaScript) | Understanding / slides only; nothing to build |
| `facebook/rocksdb` | RocksDB | Your fork, where SuRF becomes a filter policy |
| `efficient/rocksdb` | The SuRF authors' 2018 RocksDB fork | Reference for *where* they hooked range filtering; too old to reuse directly |

## 2. Standalone harness (done)

```bash
cd surf-harness
git clone https://github.com/efficient/SuRF
make
./surf_harness correctness
./surf_harness bench --keys 1000000 --type int
python3 web/server.py          # http://localhost:8598
```

See `README.md` for what is checked and the current results.

## 3. RocksDB build

Pin a release tag so both of you (and your results) use the same code. The
latest release as of Oct 14 2026 is **v11.8.1**.

```bash
sudo apt-get install -y build-essential libgflags-dev libsnappy-dev \
    liblz4-dev libzstd-dev zlib1g-dev libbz2-dev
git clone --branch v11.8.1 https://github.com/facebook/rocksdb
cd rocksdb
git switch -c surf                       # your work goes on this branch
make -j"$(nproc)" DEBUG_LEVEL=0 db_bench
./db_bench --version                     # db_bench version 11.8.1
```

- **`DEBUG_LEVEL=0` matters.** The default `make` builds a debug binary with
  assertions on, and every benchmark number from it is meaningless.
- Build time: **18.5 minutes on 2 cores**. Do a full build once; after that,
  `make` only recompiles changed files.
- The binary is ~330 MB; do not commit it.

## 4. Baseline: Bloom vs Ribbon (verified)

```bash
for f in bloom ribbon; do
  extra=""; [ "$f" = ribbon ] && extra="--use_ribbon_filter"
  rm -rf /tmp/db_$f
  ./db_bench --db=/tmp/db_$f --num=1000000 --reads=200000 \
    --benchmarks=fillrandom,compact,readmissing,seekrandom \
    --bloom_bits=10 $extra --max_scan_distance=100 --seek_nexts=10 \
    --statistics --compression_type=none --threads=1
done
```

- `readmissing`: point lookups of keys that do not exist. Filter FPR =
  `bloom.filter.full.positive / reads` (minus true positives, which are 0 here).
- `seekrandom` + `--max_scan_distance`: range scans with an upper bound
  (`iterate_upper_bound`). This is the workload SuRF should improve.
- Watch the `*.seek.filtered` counters: they count files skipped by a filter
  during a seek.

Results on the 2-core VM (1M keys, 10 bits/key, 200K operations):

| Filter | readmissing | Point FPR | seekrandom | Files skipped on seeks |
|---|---:|---:|---:|---:|
| Bloom | 0.58 us/op | 0.99% (1,977/200K) | 8.5 us/op | **0** |
| Ribbon | 1.06 us/op | 0.92% (1,836/200K) | 8.0 us/op | **0** |

The last column is the motivation for the project, measured: neither
built-in filter helps bounded range scans at all.

## 5. Integration plan (this weekend)

Key facts about RocksDB 11.8.1 found while setting up:

- `FilterBitsBuilder` / `FilterBitsReader` are in
  `table/block_based/filter_policy_internal.h`, an **internal** header. SuRF
  has to be compiled inside your RocksDB fork, not loaded as a plugin.
- On seeks, RocksDB only consults filters through the **prefix Bloom** path
  (`CheckPrefixMayMatch` in `table/block_based/block_based_table_iterator.cc`).
  There is no existing hook for "does this file have a key in
  [target, upper_bound)?", so range filtering needs a new check there.

Steps:

1. **Vendor SuRF** into `third-party/surf/` and mark the free functions in
   `config.hpp` `inline` (otherwise more than one `.cc` including it fails to
   link with duplicate symbols; verified).
2. **`SurfFilterPolicy`** in `table/block_based/surf_filter_policy.{h,cc}`:
   - Builder: `AddKey` collects keys (skip consecutive duplicates;
     `AddKeyAndAlt` ignores `alt` at first), `Finish` builds SuRF and
     serializes it. Also implement `EstimateEntriesAdded`,
     `ApproximateNumEntries`, `CalculateSpace`, `EstimatedFpRate`.
   - Reader: **copy the filter `Slice` into an 8-byte-aligned buffer**
     before `deSerialize()` (misaligned buffers crash; verified), then
     `MayMatch` calls `lookupKey`.
   - Make `CompatibilityName()` unique so RocksDB never reads a SuRF block
     as a Bloom block or the reverse.
3. **db_bench flag** `--use_surf_filter` (next to `use_ribbon_filter` in
   `tools/db_bench_tool.cc`, around line 5797) plus a suffix option
   (`none` / `real8`).
4. **Point-lookup milestone:** rerun section 4 with `--use_surf_filter` and
   compare FPR, filter size (`--statistics` and the SST properties) and
   `readmissing` latency.
5. **Range milestone:** in `BlockBasedTableIterator`'s seek path, when
   `iterate_upper_bound` is set and the file's filter is SuRF, call
   `lookupRange(target, true, upper_bound, false)`; if it returns false,
   skip the file. Note `lookupRange` is **not thread-safe** (non-`const`,
   shared member iterator), so use a per-call iterator or a lock.
6. **Keep the harness as the oracle:** any change to the vendored SuRF must
   still pass `./surf_harness correctness` with 0 false negatives.

## 6. Open questions to resolve early

- **Build memory:** SuRF-Base on 1M random integer keys peaked at ~3.5 GB
  RSS during construction (SuRF-Real8: ~62 MB). RocksDB builds a filter for
  every SST file during flush and compaction, so check this with realistic
  SST sizes before the large runs.
- **Key format:** db_bench keys are 16 bytes by default (`--key_size`), and
  RocksDB's internal keys have an 8-byte suffix. Filter on the **user key**
  only (what `AddKey` receives with whole-key filtering).
