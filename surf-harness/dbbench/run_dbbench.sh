#!/usr/bin/env bash
# Compare filters inside RocksDB with db_bench, on identical data and queries.
#
#   surf-harness/dbbench/run_dbbench.sh            # full run (defaults below)
#   QUICK=1 surf-harness/dbbench/run_dbbench.sh    # small smoke test
#
# Every setting can be overridden from the environment, e.g.
#   FILTERS="bloom surf-real8" TRIALS=5 DISTS="10 100" run_dbbench.sh
#
# For each filter it builds one database (fillrandom + waitforcompaction),
# measures the filter's size with sst_dump, then runs each read workload in
# its own db_bench process, TRIALS times. Output: $OUT/load.csv,
# $OUT/reads.csv and every raw log. Summarize with summarize.py.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DB_BENCH="${DB_BENCH:-$ROOT/db_bench}"
SST_DUMP="${SST_DUMP:-$ROOT/sst_dump}"

if [ "${QUICK:-0}" = 1 ]; then
  : "${NUM:=1000000}" "${WRITES:=100000}" "${READS:=20000}" "${TRIALS:=1}"
fi
NUM="${NUM:-10000000}"        # key space: keys are drawn from [0, NUM)
WRITES="${WRITES:-1000000}"   # keys written (sparse: WRITES/NUM of the space)
READS="${READS:-200000}"      # operations per read workload
TRIALS="${TRIALS:-3}"
DISTS="${DISTS:-10 100 1000}" # seek upper bound = seek key + DIST
FILTERS="${FILTERS:-nofilter bloom ribbon surf-base surf-real8 surf-hash8}"
BITS="${BITS:-10}"            # Bloom/Ribbon bits per key
DBROOT="${DBROOT:-/tmp/surf_dbbench}"
OUT="${OUT:-$HERE/results/$(date +%Y%m%d-%H%M%S)}"
EXTRA="${EXTRA:-}"            # e.g. EXTRA="--use_direct_reads=true"

for b in "$DB_BENCH" "$SST_DUMP"; do
  [ -x "$b" ] || { echo "missing $b; run: make -j DEBUG_LEVEL=0 db_bench sst_dump" >&2; exit 1; }
done
mkdir -p "$OUT/logs" "$DBROOT"

filter_flags() {
  case "$1" in
    nofilter)   echo "--bloom_bits=0" ;;
    bloom)      echo "--bloom_bits=$BITS" ;;
    ribbon)     echo "--bloom_bits=$BITS --use_ribbon_filter=true" ;;
    surf-base)  echo "--use_surf_filter=true --surf_suffix=none" ;;
    surf-real8) echo "--use_surf_filter=true --surf_suffix=real8" ;;
    surf-hash8) echo "--use_surf_filter=true --surf_suffix=hash8" ;;
    *) echo "unknown filter $1" >&2; exit 1 ;;
  esac
}

COMMON="--num=$NUM --key_size=16 --value_size=100 --compression_type=none \
--threads=1 --statistics=true --cache_size=8388608 \
--max_background_jobs=2 $EXTRA"

{
  echo "date: $(date -Iseconds)"
  echo "host: $(uname -srm), $(nproc) cores, $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | xargs)"
  echo "mem: $(free -g | awk '/Mem:/{print $2" GB"}')"
  "$DB_BENCH" --version 2>/dev/null | head -1 || true
  echo "NUM=$NUM WRITES=$WRITES READS=$READS TRIALS=$TRIALS DISTS=\"$DISTS\" BITS=$BITS"
  echo "FILTERS=\"$FILTERS\" EXTRA=\"$EXTRA\""
  echo "common flags: $COMMON"
} > "$OUT/env.txt"
cat "$OUT/env.txt"

echo "filter,fill_us_per_op,load_seconds,peak_rss_mb,sst_files,entries,filter_bytes,data_bytes,filter_bits_per_key" > "$OUT/load.csv"
echo "filter,workload,dist,trial,us_per_op,ops_per_sec,found,seek_filtered,seek_filter_match,bloom_useful,bloom_full_positive,bloom_full_true_positive,data_block_misses,sst_reads" > "$OUT/reads.csv"

tick() { awk -v k="$2" '$1==k && $2=="COUNT" {print $4; f=1} END {if(!f) print 0}' "$1"; }
hist_count() { awk -v k="$2" '$1==k {for(i=1;i<=NF;i++) if($i=="COUNT") {print $(i+2); f=1}} END {if(!f) print 0}' "$1"; }

parse_read() { # log filter workload dist trial
  local log=$1 line us ops found
  line=$(grep -E "^$2 +:" "$log" | tail -1 || true)
  [ -n "$line" ] || line=$(grep -E "^(readrandom|readmissing|seekrandom) +:" "$log" | tail -1)
  us=$(echo "$line" | awk '{print $3}')
  ops=$(echo "$line" | awk '{print $5}')
  found=$(echo "$line" | sed -n 's/.*(\([0-9]*\) of [0-9]* found).*/\1/p')
  local sf=$(( $(tick "$log" rocksdb.non.last.level.seek.filtered) + $(tick "$log" rocksdb.last.level.seek.filtered) ))
  local sm=$(( $(tick "$log" rocksdb.non.last.level.seek.filter.match) + $(tick "$log" rocksdb.last.level.seek.filter.match) ))
  echo "$5,$2,$3,$4,$us,$ops,${found:-},$sf,$sm,$(tick "$log" rocksdb.bloom.filter.useful),$(tick "$log" rocksdb.bloom.filter.full.positive),$(tick "$log" rocksdb.bloom.filter.full.true.positive),$(tick "$log" rocksdb.block.cache.data.miss),$(hist_count "$log" rocksdb.sst.read.micros)"
}

for f in $FILTERS; do
  FF=$(filter_flags "$f")
  DB="$DBROOT/$f"
  rm -rf "$DB"
  echo "== $f: load"
  log="$OUT/logs/$f-load.log"
  start=$(date +%s.%N)
  python3 -I "$HERE/peak_rss.py" "$OUT/logs/$f-load.rss" "$DB_BENCH" $COMMON $FF --db="$DB" \
    --benchmarks=fillrandom,waitforcompaction --writes="$WRITES" --disable_wal=true --seed=598 > "$log" 2>&1
  end=$(date +%s.%N)
  fill_us=$(grep -E "^fillrandom +:" "$log" | awk '{print $3}')
  "$SST_DUMP" --file="$DB" --show_properties > "$OUT/logs/$f-sst.txt" 2>&1 || true
  read -r files entries fbytes dbytes < <(awk -F: '
    {sub(/^[ \t]+/, "", $1)}
    $1=="# entries" {e+=$2} $1=="filter block size" {fb+=$2} $1=="data block size" {db+=$2} /^Process .*\.sst/ {n++}
    END {printf "%d %d %d %d\n", n, e, fb, db}' "$OUT/logs/$f-sst.txt")
  bpk=$(awk -v fb="$fbytes" -v e="$entries" 'BEGIN{printf "%.2f", e ? 8*fb/e : 0}')
  rss=$(cat "$OUT/logs/$f-load.rss")
  secs=$(awk -v a="$start" -v b="$end" 'BEGIN{printf "%.1f", b-a}')
  echo "$f,$fill_us,$secs,$rss,$files,$entries,$fbytes,$dbytes,$bpk" >> "$OUT/load.csv"
  echo "   fill ${fill_us} us/op, ${secs}s, peak ${rss} MB, $files SSTs, filter ${bpk} bits/key"

  for t in $(seq 1 "$TRIALS"); do
    for w in readrandom readmissing; do
      log="$OUT/logs/$f-$w-t$t.log"
      "$DB_BENCH" $COMMON $FF --db="$DB" --use_existing_db=true \
        --benchmarks=$w --reads="$READS" --seed=$((1000 + t)) > "$log" 2>&1
      parse_read "$log" "$w" "" "$t" "$f" >> "$OUT/reads.csv"
    done
    for d in $DISTS; do
      log="$OUT/logs/$f-seek$d-t$t.log"
      "$DB_BENCH" $COMMON $FF --db="$DB" --use_existing_db=true \
        --benchmarks=seekrandom --reads="$READS" --seek_nexts=0 --seed=$((2000 + t)) \
        --max_scan_distance="$d" > "$log" 2>&1
      parse_read "$log" seekrandom "$d" "$t" "$f" >> "$OUT/reads.csv"
    done
    echo "   trial $t done"
  done
done

echo "done: $OUT"
python3 "$HERE/summarize.py" "$OUT" || true
