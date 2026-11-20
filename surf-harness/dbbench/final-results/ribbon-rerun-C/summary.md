# db_bench filter comparison

```
date: 2026-11-19T18:09:28-06:00
host: Linux 6.18.44-fc-v653 x86_64, 2 cores, Intel(R) Xeon(R) Processor @ 2.30GHz
mem: 7 GB
db_bench version 11.8.1
NUM=10000000 WRITES=1000000 READS=200000 TRIALS=3 DISTS="10 100 1000" BITS=10
FILTERS="bloom ribbon" EXTRA="--disable_auto_compactions=true --write_buffer_size=4194304 --level0_slowdown_writes_trigger=10000 --level0_stop_writes_trigger=10000"
common flags: --num=10000000 --key_size=16 --value_size=100 --compression_type=none --threads=1 --statistics=true --cache_size=8388608 --max_background_jobs=2 --disable_auto_compactions=true --write_buffer_size=4194304 --level0_slowdown_writes_trigger=10000 --level0_stop_writes_trigger=10000
```

## Build (fillrandom + waitforcompaction)

| filter | filter bits/key | filter MB | fill us/op | load s | peak RSS MB | SSTs |
|---|---:|---:|---:|---:|---:|---:|
| bloom | 10.01 | 1.25 | 0.79 | 5.8 | 27 | 36 |
| ribbon | 6.99 | 0.87 | 0.99 | 6.1 | 28 | 36 |

## Point lookups, random keys (mostly absent) (`readrandom`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| bloom | 6.408 | 18997 | 0.96% | 79451 total |
| ribbon | 6.869 | 18997 | 0.95% | 78845 total |

## Point lookups, absent keys that extend a stored key (`readmissing`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| bloom | 5.721 | 0 | 0.96% | 64932 total |
| ribbon | 6.504 | 0 | 0.95% | 64181 total |

## Bounded seeks (`seekrandom`, upper bound = key + dist, no Next)

| filter | dist | us/op | found | files skipped by filter | skip rate | data block misses |
|---|---:|---:|---:|---:|---:|---:|
| bloom | 10 | 112.079 | 19169 | 0 | 0.0% | 6724838 |
| ribbon | 10 | 121.390 | 19169 | 0 | 0.0% | 6724431 |
| bloom | 100 | 111.699 | 19169 | 0 | 0.0% | 6750728 |
| ribbon | 100 | 112.655 | 19169 | 0 | 0.0% | 6750126 |
| bloom | 1000 | 113.201 | 19169 | 0 | 0.0% | 6766984 |
| ribbon | 1000 | 107.740 | 19169 | 0 | 0.0% | 6766191 |

Medians over trials. "FP per SST probe" = (bloom.filter.full.positive - full.true.positive) / (useful + that). "files skipped" = (non.)last.level.seek.filtered.
