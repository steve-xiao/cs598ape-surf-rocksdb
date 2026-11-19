#!/usr/bin/env python3
"""Summarize a run_dbbench.sh result directory.

    summarize.py RESULT_DIR

Writes RESULT_DIR/summary.md (medians over trials) and, if matplotlib is
installed, RESULT_DIR/seek.png, point.png and size.png.
"""
import csv
import os
import statistics
import sys
from collections import defaultdict

out = sys.argv[1]
load = list(csv.DictReader(open(os.path.join(out, "load.csv"))))
reads = list(csv.DictReader(open(os.path.join(out, "reads.csv"))))
order = [r["filter"] for r in load]


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return 0.0


groups = defaultdict(list)
for r in reads:
    groups[(r["filter"], r["workload"], r["dist"])].append(r)


def med(rows, key):
    return statistics.median(num(r[key]) for r in rows)


def probe_fpr(rows):
    """False positives per filter probe on keys absent from that SST."""
    fp = med(rows, "bloom_full_positive") - med(rows, "bloom_full_true_positive")
    neg = med(rows, "bloom_useful") + fp
    return fp / neg if neg else None


def skip_rate(rows):
    f, m = med(rows, "seek_filtered"), med(rows, "seek_filter_match")
    return f / (f + m) if f + m else 0.0


L = ["# db_bench filter comparison", ""]
env = os.path.join(out, "env.txt")
if os.path.exists(env):
    L += ["```", open(env).read().strip(), "```", ""]

L += ["## Build (fillrandom + waitforcompaction)", "",
      "| filter | filter bits/key | filter MB | fill us/op | load s | peak RSS MB | SSTs |",
      "|---|---:|---:|---:|---:|---:|---:|"]
for r in load:
    L.append(f"| {r['filter']} | {num(r['filter_bits_per_key']):.2f} | "
             f"{num(r['filter_bytes'])/1e6:.2f} | {num(r['fill_us_per_op']):.2f} | "
             f"{num(r['load_seconds']):.1f} | {r['peak_rss_mb']} | {r['sst_files']} |")

for w, title in [("readrandom", "Point lookups, random keys (mostly absent)"),
                 ("readmissing", "Point lookups, absent keys that extend a stored key")]:
    L += ["", f"## {title} (`{w}`)", "",
          "| filter | us/op | found | FP per SST probe | data block misses |",
          "|---|---:|---:|---:|---:|"]
    for f in order:
        rows = groups.get((f, w, ""))
        if not rows:
            continue
        fpr = probe_fpr(rows) if f != "nofilter" else None
        fpr_s = "-" if fpr is None else f"{100 * fpr:.2f}%"
        L.append(f"| {f} | {med(rows,'us_per_op'):.3f} | {int(med(rows,'found'))} | "
                 f"{fpr_s} | {med(rows,'data_block_misses'):.0f} total |")

dists = sorted({r["dist"] for r in reads if r["workload"] == "seekrandom"}, key=num)
L += ["", "## Bounded seeks (`seekrandom`, upper bound = key + dist, no Next)", "",
      "| filter | dist | us/op | found | files skipped by filter | skip rate | data block misses |",
      "|---|---:|---:|---:|---:|---:|---:|"]
for d in dists:
    for f in order:
        rows = groups.get((f, "seekrandom", d))
        if not rows:
            continue
        L.append(f"| {f} | {d} | {med(rows,'us_per_op'):.3f} | {int(med(rows,'found'))} | "
                 f"{int(med(rows,'seek_filtered'))} | {100*skip_rate(rows):.1f}% | "
                 f"{int(med(rows,'data_block_misses'))} |")

L += ["", "Medians over trials. \"FP per SST probe\" = (bloom.filter.full.positive"
      " - full.true.positive) / (useful + that). \"files skipped\" ="
      " (non.)last.level.seek.filtered."]
open(os.path.join(out, "summary.md"), "w").write("\n".join(L) + "\n")
print("\n".join(L))

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit(0)

plt.rcParams.update({"font.size": 8, "axes.spines.top": False, "axes.spines.right": False})
palette = {"nofilter": "#a8a7a2", "bloom": "#7f7f7f", "ribbon": "#52514e",
           "surf-base": "#9ec5f2", "surf-real8": "#2a78d6", "surf-hash8": "#1d4f91"}

fig, axs = plt.subplots(1, 2, figsize=(7, 2.6))
w = 0.8 / max(1, len(order))
for i, f in enumerate(order):
    xs, lat, skip = [], [], []
    for j, d in enumerate(dists):
        rows = groups.get((f, "seekrandom", d))
        if rows:
            xs.append(j + i * w)
            lat.append(med(rows, "us_per_op"))
            skip.append(100 * skip_rate(rows))
    axs[0].bar(xs, lat, w, label=f, color=palette.get(f))
    axs[1].bar(xs, skip, w, label=f, color=palette.get(f))
for ax, t in zip(axs, ["seek latency (us/op)", "files skipped by filter (%)"]):
    ax.set_xticks([j + 0.4 - w / 2 for j in range(len(dists))])
    ax.set_xticklabels([f"dist {d}" for d in dists])
    ax.set_title(t)
axs[1].legend(frameon=False, fontsize=7)
fig.tight_layout()
fig.savefig(os.path.join(out, "seek.png"), dpi=200)

fig, axs = plt.subplots(1, 3, figsize=(7, 2.4))
fs = [f for f in order if f != "nofilter"]
for ax, w_, t in [(axs[0], "readrandom", "readrandom FP/probe (%)"),
                  (axs[1], "readmissing", "readmissing FP/probe (%)")]:
    vals = []
    for f in fs:
        rows = groups.get((f, w_, ""))
        v = probe_fpr(rows) if rows else None
        vals.append(100 * v if v is not None else 0)
    ax.bar(fs, vals, color=[palette.get(f) for f in fs])
    ax.set_title(t)
    ax.tick_params(axis="x", rotation=45)
bpk = {r["filter"]: num(r["filter_bits_per_key"]) for r in load}
axs[2].bar(fs, [bpk.get(f, 0) for f in fs], color=[palette.get(f) for f in fs])
axs[2].set_title("filter bits/key")
axs[2].tick_params(axis="x", rotation=45)
fig.tight_layout()
fig.savefig(os.path.join(out, "point.png"), dpi=200)
print("wrote seek.png, point.png")
