# db_bench filter comparison

```
date: 2026-11-19T09:45:28-06:00
host: Linux 6.18.44-fc-v653 x86_64, 2 cores, Intel(R) Xeon(R) Processor @ 2.10GHz
mem: 7 GB
db_bench version 11.8.1
NUM=10000000 WRITES=1000000 READS=200000 TRIALS=3 DISTS="10 100 1000" BITS=10
FILTERS="nofilter bloom ribbon surf-base surf-real8 surf-hash8" EXTRA="--write_buffer_size=4194304 --target_file_size_base=4194304 --max_bytes_for_level_base=16777216"
common flags: --num=10000000 --key_size=16 --value_size=100 --compression_type=none --threads=1 --statistics=true --cache_size=8388608 --max_background_jobs=2 --write_buffer_size=4194304 --target_file_size_base=4194304 --max_bytes_for_level_base=16777216
```

## Build (fillrandom + waitforcompaction)

| filter | filter bits/key | filter MB | fill us/op | load s | peak RSS MB | SSTs |
|---|---:|---:|---:|---:|---:|---:|
| nofilter | 0.00 | 0.00 | 1.34 | 6.4 | 30 | 31 |
| bloom | 10.01 | 1.20 | 1.16 | 6.2 | 32 | 32 |
| ribbon | 7.07 | 0.85 | 1.23 | 6.3 | 32 | 35 |
| surf-base | 10.81 | 1.30 | 1.24 | 6.3 | 42 | 31 |
| surf-real8 | 18.44 | 2.21 | 1.24 | 6.3 | 45 | 30 |
| surf-hash8 | 19.34 | 2.33 | 1.27 | 6.3 | 45 | 26 |

## Point lookups, random keys (mostly absent) (`readrandom`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| nofilter | 9.238 | 18997 | - | 392753 total |
| bloom | 1.147 | 18997 | 0.95% | 21605 total |
| ribbon | 1.153 | 18997 | 0.96% | 21317 total |
| surf-base | 2.191 | 18997 | 13.36% | 18768 total |
| surf-real8 | 1.720 | 18997 | 0.04% | 17888 total |
| surf-hash8 | 1.808 | 18997 | 0.05% | 18053 total |

## Point lookups, absent keys that extend a stored key (`readmissing`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| nofilter | 8.832 | 0 | - | 398602 total |
| bloom | 0.850 | 0 | 0.95% | 3257 total |
| ribbon | 1.175 | 0 | 0.94% | 2913 total |
| surf-base | 2.219 | 0 | 16.74% | 19497 total |
| surf-real8 | 1.758 | 0 | 3.97% | 18622 total |
| surf-hash8 | 1.369 | 0 | 0.06% | 276 total |

## Bounded seeks (`seekrandom`, upper bound = key + dist, no Next)

| filter | dist | us/op | found | files skipped by filter | skip rate | data block misses |
|---|---:|---:|---:|---:|---:|---:|
| nofilter | 10 | 11.593 | 19169 | 0 | 0.0% | 396848 |
| bloom | 10 | 9.703 | 19169 | 0 | 0.0% | 308715 |
| ribbon | 10 | 8.885 | 19169 | 0 | 0.0% | 272837 |
| surf-base | 10 | 7.266 | 19169 | 285305 | 57.8% | 154970 |
| surf-real8 | 10 | 6.828 | 19169 | 372089 | 71.6% | 137198 |
| surf-hash8 | 10 | 7.481 | 19169 | 321498 | 59.8% | 163315 |
| nofilter | 100 | 11.455 | 19169 | 0 | 0.0% | 397454 |
| bloom | 100 | 10.231 | 19169 | 0 | 0.0% | 309056 |
| ribbon | 100 | 9.250 | 19169 | 0 | 0.0% | 273115 |
| surf-base | 100 | 10.622 | 19169 | 130830 | 26.5% | 290624 |
| surf-real8 | 100 | 9.925 | 19169 | 204613 | 39.4% | 279665 |
| surf-hash8 | 100 | 10.906 | 19169 | 128400 | 23.9% | 334447 |
| nofilter | 1000 | 11.422 | 19169 | 0 | 0.0% | 397631 |
| bloom | 1000 | 9.558 | 19169 | 0 | 0.0% | 309026 |
| ribbon | 1000 | 9.016 | 19169 | 0 | 0.0% | 273078 |
| surf-base | 1000 | 11.113 | 19169 | 32700 | 6.6% | 293093 |
| surf-real8 | 1000 | 10.741 | 19169 | 59768 | 11.5% | 301369 |
| surf-hash8 | 1000 | 11.495 | 19169 | 19230 | 3.6% | 351895 |

Medians over trials. "FP per SST probe" = (bloom.filter.full.positive - full.true.positive) / (useful + that). "files skipped" = (non.)last.level.seek.filtered.
