# db_bench filter comparison

```
date: 2026-11-19T09:53:55-06:00
host: Linux 6.18.44-fc-v653 x86_64, 2 cores, Intel(R) Xeon(R) Processor @ 2.10GHz
mem: 7 GB
db_bench version 11.8.1
NUM=10000000 WRITES=1000000 READS=200000 TRIALS=3 DISTS="10 100 1000" BITS=10
FILTERS="nofilter bloom ribbon surf-base surf-real8 surf-hash8" EXTRA="--disable_auto_compactions=true --write_buffer_size=4194304 --level0_slowdown_writes_trigger=10000 --level0_stop_writes_trigger=10000"
common flags: --num=10000000 --key_size=16 --value_size=100 --compression_type=none --threads=1 --statistics=true --cache_size=8388608 --max_background_jobs=2 --disable_auto_compactions=true --write_buffer_size=4194304 --level0_slowdown_writes_trigger=10000 --level0_stop_writes_trigger=10000
NOTE: ribbon rows replaced by a rerun with --ribbon_bloom_before_level=-1 (true Ribbon everywhere); see ribbon-rerun folder.
```

## Build (fillrandom + waitforcompaction)

| filter | filter bits/key | filter MB | fill us/op | load s | peak RSS MB | SSTs |
|---|---:|---:|---:|---:|---:|---:|
| nofilter | 0.00 | 0.00 | 1.03 | 6.1 | 26 | 36 |
| bloom | 10.01 | 1.25 | 1.07 | 6.1 | 28 | 36 |
| ribbon | 6.99 | 0.87 | 0.99 | 6.1 | 28 | 36 |
| surf-base | 12.61 | 1.57 | 1.12 | 6.2 | 30 | 36 |
| surf-real8 | 16.47 | 2.06 | 1.23 | 6.3 | 32 | 36 |
| surf-hash8 | 16.47 | 2.06 | 1.10 | 6.2 | 32 | 36 |

## Point lookups, random keys (mostly absent) (`readrandom`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| nofilter | 104.566 | 18997 | - | 6450260 total |
| bloom | 6.406 | 18997 | 0.96% | 79421 total |
| ribbon | 6.869 | 18997 | 0.95% | 78845 total |
| surf-base | 56.214 | 18997 | 34.97% | 2281058 total |
| surf-real8 | 9.962 | 18997 | 0.14% | 24094 total |
| surf-hash8 | 9.916 | 18997 | 0.14% | 26590 total |

## Point lookups, absent keys that extend a stored key (`readmissing`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| nofilter | 110.495 | 0 | - | 6769729 total |
| bloom | 7.257 | 0 | 0.96% | 65025 total |
| ribbon | 6.504 | 0 | 0.95% | 64181 total |
| surf-base | 60.163 | 0 | 35.16% | 2394260 total |
| surf-real8 | 10.452 | 0 | 0.41% | 25913 total |
| surf-hash8 | 9.737 | 0 | 0.14% | 9348 total |

## Bounded seeks (`seekrandom`, upper bound = key + dist, no Next)

| filter | dist | us/op | found | files skipped by filter | skip rate | data block misses |
|---|---:|---:|---:|---:|---:|---:|
| nofilter | 10 | 121.746 | 19169 | 0 | 0.0% | 6724324 |
| bloom | 10 | 124.759 | 19169 | 0 | 0.0% | 6723810 |
| ribbon | 10 | 121.390 | 19169 | 0 | 0.0% | 6724431 |
| surf-base | 10 | 94.915 | 19169 | 4507597 | 62.6% | 2512546 |
| surf-real8 | 10 | 37.192 | 19169 | 6983226 | 97.0% | 202754 |
| surf-hash8 | 10 | 75.874 | 19169 | 4507762 | 62.6% | 2512859 |
| nofilter | 100 | 122.373 | 19169 | 0 | 0.0% | 6751014 |
| bloom | 100 | 124.588 | 19169 | 0 | 0.0% | 6750454 |
| ribbon | 100 | 112.655 | 19169 | 0 | 0.0% | 6750126 |
| surf-base | 100 | 105.642 | 19169 | 3364163 | 46.7% | 3606350 |
| surf-real8 | 100 | 68.272 | 19169 | 5438554 | 75.5% | 1651019 |
| surf-hash8 | 100 | 93.062 | 19169 | 3364181 | 46.7% | 3605726 |
| nofilter | 1000 | 124.704 | 19169 | 0 | 0.0% | 6767259 |
| bloom | 1000 | 129.950 | 19169 | 0 | 0.0% | 6766556 |
| ribbon | 1000 | 107.740 | 19169 | 0 | 0.0% | 6766191 |
| surf-base | 1000 | 143.267 | 19169 | 17107 | 3.8% | 6750624 |
| surf-real8 | 1000 | 134.252 | 19169 | 55031 | 8.6% | 6715939 |
| surf-hash8 | 1000 | 132.206 | 19169 | 17086 | 3.8% | 6750705 |

Medians over trials. "FP per SST probe" = (bloom.filter.full.positive - full.true.positive) / (useful + that). "files skipped" = (non.)last.level.seek.filtered.
