# db_bench filter comparison

```
date: 2026-11-19T18:08:28-06:00
host: Linux 6.18.44-fc-v653 x86_64, 2 cores, Intel(R) Xeon(R) Processor @ 2.30GHz
mem: 7 GB
db_bench version 11.8.1
NUM=10000000 WRITES=1000000 READS=200000 TRIALS=3 DISTS="10 100 1000" BITS=10
FILTERS="bloom ribbon" EXTRA=""
common flags: --num=10000000 --key_size=16 --value_size=100 --compression_type=none --threads=1 --statistics=true --cache_size=8388608 --max_background_jobs=2
```

## Build (fillrandom + waitforcompaction)

| filter | filter bits/key | filter MB | fill us/op | load s | peak RSS MB | SSTs |
|---|---:|---:|---:|---:|---:|---:|
| bloom | 10.00 | 1.22 | 2.26 | 7.4 | 112 | 3 |
| ribbon | 7.07 | 0.86 | 2.54 | 7.7 | 119 | 3 |

## Point lookups, random keys (mostly absent) (`readrandom`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| bloom | 1.191 | 18997 | 1.00% | 23011 total |
| ribbon | 1.104 | 18997 | 0.96% | 22845 total |

## Point lookups, absent keys that extend a stored key (`readmissing`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| bloom | 0.851 | 0 | 0.96% | 5150 total |
| ribbon | 0.687 | 0 | 0.95% | 5184 total |

## Bounded seeks (`seekrandom`, upper bound = key + dist, no Next)

| filter | dist | us/op | found | files skipped by filter | skip rate | data block misses |
|---|---:|---:|---:|---:|---:|---:|
| bloom | 10 | 11.831 | 19169 | 0 | 0.0% | 527472 |
| ribbon | 10 | 11.420 | 19169 | 0 | 0.0% | 527338 |
| bloom | 100 | 10.834 | 19169 | 0 | 0.0% | 528872 |
| ribbon | 100 | 10.157 | 19169 | 0 | 0.0% | 528637 |
| bloom | 1000 | 11.863 | 19169 | 0 | 0.0% | 529002 |
| ribbon | 1000 | 9.948 | 19169 | 0 | 0.0% | 528843 |

Medians over trials. "FP per SST probe" = (bloom.filter.full.positive - full.true.positive) / (useful + that). "files skipped" = (non.)last.level.seek.filtered.
