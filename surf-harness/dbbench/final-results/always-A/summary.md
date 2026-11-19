# db_bench filter comparison

```
date: 2026-11-19T09:42:15-06:00
host: Linux 6.18.44-fc-v653 x86_64, 2 cores, Intel(R) Xeon(R) Processor @ 2.10GHz
mem: 7 GB
db_bench version 11.8.1
NUM=10000000 WRITES=1000000 READS=200000 TRIALS=3 DISTS="10 100 1000" BITS=10
FILTERS="nofilter bloom ribbon surf-base surf-real8 surf-hash8" EXTRA=""
common flags: --num=10000000 --key_size=16 --value_size=100 --compression_type=none --threads=1 --statistics=true --cache_size=8388608 --max_background_jobs=2
```

## Build (fillrandom + waitforcompaction)

| filter | filter bits/key | filter MB | fill us/op | load s | peak RSS MB | SSTs |
|---|---:|---:|---:|---:|---:|---:|
| nofilter | 0.00 | 0.00 | 2.18 | 7.3 | 111 | 3 |
| bloom | 10.00 | 1.22 | 2.09 | 7.2 | 112 | 3 |
| ribbon | 10.00 | 1.22 | 1.89 | 7.0 | 113 | 3 |
| surf-base | 10.28 | 1.26 | 2.04 | 7.2 | 142 | 3 |
| surf-real8 | 18.28 | 2.24 | 1.81 | 6.9 | 146 | 3 |
| surf-hash8 | 18.28 | 2.24 | 1.90 | 7.0 | 148 | 3 |

## Point lookups, random keys (mostly absent) (`readrandom`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| nofilter | 10.058 | 18997 | - | 517261 total |
| bloom | 1.113 | 18997 | 0.98% | 22946 total |
| ribbon | 1.301 | 18997 | 0.99% | 22902 total |
| surf-base | 2.457 | 18997 | 9.58% | 38020 total |
| surf-real8 | 1.879 | 18997 | 0.04% | 17928 total |
| surf-hash8 | 1.837 | 18997 | 0.04% | 18016 total |

## Point lookups, absent keys that extend a stored key (`readmissing`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| nofilter | 9.512 | 0 | - | 530091 total |
| bloom | 0.737 | 0 | 0.97% | 5243 total |
| ribbon | 0.921 | 0 | 0.95% | 5133 total |
| surf-base | 2.364 | 0 | 12.34% | 39576 total |
| surf-real8 | 1.644 | 0 | 3.29% | 18992 total |
| surf-hash8 | 1.496 | 0 | 0.05% | 284 total |

## Bounded seeks (`seekrandom`, upper bound = key + dist, no Next)

| filter | dist | us/op | found | files skipped by filter | skip rate | data block misses |
|---|---:|---:|---:|---:|---:|---:|
| nofilter | 10 | 11.413 | 19169 | 0 | 0.0% | 527372 |
| bloom | 10 | 11.377 | 19169 | 0 | 0.0% | 527161 |
| ribbon | 10 | 10.978 | 19169 | 0 | 0.0% | 527468 |
| surf-base | 10 | 8.822 | 19169 | 370105 | 61.7% | 203437 |
| surf-real8 | 10 | 7.097 | 19169 | 423970 | 70.7% | 164145 |
| surf-hash8 | 10 | 7.875 | 19169 | 370090 | 61.7% | 203341 |
| nofilter | 100 | 12.135 | 19169 | 0 | 0.0% | 528543 |
| bloom | 100 | 11.441 | 19169 | 0 | 0.0% | 528391 |
| ribbon | 100 | 11.277 | 19169 | 0 | 0.0% | 528906 |
| surf-base | 100 | 13.122 | 19169 | 54852 | 9.1% | 492015 |
| surf-real8 | 100 | 12.181 | 19169 | 94290 | 15.7% | 464328 |
| surf-hash8 | 100 | 12.748 | 19169 | 54831 | 9.1% | 492034 |
| nofilter | 1000 | 11.704 | 19169 | 0 | 0.0% | 529041 |
| bloom | 1000 | 11.203 | 19169 | 0 | 0.0% | 528804 |
| ribbon | 1000 | 11.009 | 19169 | 0 | 0.0% | 529166 |
| surf-base | 1000 | 12.916 | 19169 | 22 | 0.0% | 528942 |
| surf-real8 | 1000 | 14.036 | 19169 | 39 | 0.0% | 528577 |
| surf-hash8 | 1000 | 14.030 | 19169 | 20 | 0.0% | 528693 |

Medians over trials. "FP per SST probe" = (bloom.filter.full.positive - full.true.positive) / (useful + that). "files skipped" = (non.)last.level.seek.filtered.
