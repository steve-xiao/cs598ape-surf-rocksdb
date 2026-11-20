# db_bench filter comparison

```
date: 2026-11-19T09:50:48-06:00
host: Linux 6.18.44-fc-v653 x86_64, 2 cores, Intel(R) Xeon(R) Processor @ 2.10GHz
mem: 7 GB
db_bench version 11.8.1
NUM=10000000 WRITES=1000000 READS=200000 TRIALS=3 DISTS="10 100 1000" BITS=10
FILTERS="nofilter bloom ribbon surf-base surf-real8 surf-hash8" EXTRA=""
common flags: --num=10000000 --key_size=16 --value_size=100 --compression_type=none --threads=1 --statistics=true --cache_size=8388608 --max_background_jobs=2 
NOTE: ribbon rows replaced by a rerun with --ribbon_bloom_before_level=-1 (true Ribbon everywhere); see ribbon-rerun folder.
```

## Build (fillrandom + waitforcompaction)

| filter | filter bits/key | filter MB | fill us/op | load s | peak RSS MB | SSTs |
|---|---:|---:|---:|---:|---:|---:|
| nofilter | 0.00 | 0.00 | 2.09 | 7.2 | 107 | 3 |
| bloom | 10.00 | 1.22 | 2.10 | 7.2 | 110 | 3 |
| ribbon | 7.07 | 0.86 | 2.54 | 7.7 | 119 | 3 |
| surf-base | 10.27 | 1.26 | 1.93 | 7.1 | 145 | 3 |
| surf-real8 | 18.28 | 2.24 | 1.96 | 7.1 | 147 | 3 |
| surf-hash8 | 18.28 | 2.24 | 2.01 | 7.1 | 146 | 3 |

## Point lookups, random keys (mostly absent) (`readrandom`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| nofilter | 10.871 | 18997 | - | 517068 total |
| bloom | 1.123 | 18997 | 0.98% | 22963 total |
| ribbon | 1.104 | 18997 | 0.96% | 22845 total |
| surf-base | 2.246 | 18997 | 9.58% | 37943 total |
| surf-real8 | 1.640 | 18997 | 0.04% | 17914 total |
| surf-hash8 | 1.625 | 18997 | 0.04% | 17998 total |

## Point lookups, absent keys that extend a stored key (`readmissing`)

| filter | us/op | found | FP per SST probe | data block misses |
|---|---:|---:|---:|---:|
| nofilter | 10.384 | 0 | - | 529830 total |
| bloom | 0.770 | 0 | 0.97% | 5249 total |
| ribbon | 0.687 | 0 | 0.95% | 5184 total |
| surf-base | 2.345 | 0 | 12.34% | 39541 total |
| surf-real8 | 1.867 | 0 | 3.29% | 18956 total |
| surf-hash8 | 1.210 | 0 | 0.05% | 281 total |

## Bounded seeks (`seekrandom`, upper bound = key + dist, no Next)

| filter | dist | us/op | found | files skipped by filter | skip rate | data block misses |
|---|---:|---:|---:|---:|---:|---:|
| nofilter | 10 | 11.313 | 19169 | 0 | 0.0% | 527248 |
| bloom | 10 | 11.101 | 19169 | 0 | 0.0% | 527101 |
| ribbon | 10 | 11.420 | 19169 | 0 | 0.0% | 527338 |
| surf-base | 10 | 7.884 | 19169 | 370087 | 61.7% | 203336 |
| surf-real8 | 10 | 6.989 | 19169 | 423965 | 70.7% | 164195 |
| surf-hash8 | 10 | 7.543 | 19169 | 370069 | 61.7% | 203374 |
| nofilter | 100 | 11.131 | 19169 | 0 | 0.0% | 528358 |
| bloom | 100 | 11.555 | 19169 | 0 | 0.0% | 528474 |
| ribbon | 100 | 10.157 | 19169 | 0 | 0.0% | 528637 |
| surf-base | 100 | 12.177 | 19169 | 51267 | 22.8% | 495512 |
| surf-real8 | 100 | 11.567 | 19169 | 90742 | 40.3% | 467717 |
| surf-hash8 | 100 | 11.708 | 19169 | 51291 | 22.8% | 495664 |
| nofilter | 1000 | 11.054 | 19169 | 0 | 0.0% | 528736 |
| bloom | 1000 | 11.112 | 19169 | 0 | 0.0% | 528691 |
| ribbon | 1000 | 9.948 | 19169 | 0 | 0.0% | 528843 |
| surf-base | 1000 | 11.722 | 19169 | 2 | 0.0% | 528932 |
| surf-real8 | 1000 | 11.997 | 19169 | 3 | 0.0% | 528900 |
| surf-hash8 | 1000 | 11.469 | 19169 | 2 | 0.0% | 528828 |

Medians over trials. "FP per SST probe" = (bloom.filter.full.positive - full.true.positive) / (useful + that). "files skipped" = (non.)last.level.seek.filtered.
