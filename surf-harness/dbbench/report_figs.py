#!/usr/bin/env python3
"""Report figures from run_dbbench.sh result directories.

    report_figs.py OUTDIR LABEL=RESULT_DIR [LABEL=RESULT_DIR ...]

For each result directory: OUTDIR/seek-LABEL.pdf (seek latency and share of
file probes skipped, per range width). From the first directory:
OUTDIR/point.pdf (filter size vs. point false-positive rate).
"""
import csv
import os
import statistics
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 7.5, "font.family": "serif", "pdf.fonttype": 42,
                     "axes.spines.top": False, "axes.spines.right": False,
                     "axes.edgecolor": "#52514e", "xtick.color": "#52514e",
                     "ytick.color": "#52514e"})
COLORS = {"nofilter": "#d9d8d3", "bloom": "#a8a7a2", "ribbon": "#6f6e69",
          "surf-base": "#9ec5f2", "surf-real8": "#2a78d6", "surf-hash8": "#173f73"}
NAMES = {"nofilter": "No filter", "bloom": "Bloom", "ribbon": "Ribbon",
         "surf-base": "SuRF-Base", "surf-real8": "SuRF-Real8", "surf-hash8": "SuRF-Hash8"}

outdir = sys.argv[1]
runs = [a.split("=", 1) for a in sys.argv[2:]]
os.makedirs(outdir, exist_ok=True)


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return 0.0


def load(d):
    lo = list(csv.DictReader(open(os.path.join(d, "load.csv"))))
    g = defaultdict(list)
    for r in csv.DictReader(open(os.path.join(d, "reads.csv"))):
        g[(r["filter"], r["workload"], r["dist"])].append(r)
    return lo, g


def med(rows, k):
    return statistics.median(num(r[k]) for r in rows)


for label, d in runs:
    lo, g = load(d)
    order = [r["filter"] for r in lo]
    dists = sorted({k[2] for k in g if k[1] == "seekrandom"}, key=num)
    fig, axs = plt.subplots(1, 2, figsize=(3.4, 1.7), gridspec_kw={"width_ratios": [1.25, 1]})
    w = 0.8 / len(order)
    for i, f in enumerate(order):
        xs = [j + (i - (len(order) - 1) / 2) * w for j in range(len(dists))]
        lat = [med(g[(f, "seekrandom", dd)], "us_per_op") for dd in dists]
        sk = []
        for dd in dists:
            rows = g[(f, "seekrandom", dd)]
            filt, match = med(rows, "seek_filtered"), med(rows, "seek_filter_match")
            sk.append(100 * filt / (filt + match) if filt + match else 0)
        axs[0].bar(xs, lat, w, color=COLORS.get(f), label=NAMES.get(f, f),
                   edgecolor="white", linewidth=0.4)
        if f.startswith("surf"):
            axs[1].bar(xs, sk, w, color=COLORS.get(f), edgecolor="white", linewidth=0.4)
    for ax in axs:
        ax.set_xticks(range(len(dists)))
        ax.set_xticklabels(dists)
        ax.set_xlabel("range width (keys)")
        ax.grid(axis="y", color="#e6e5e0", linewidth=0.5)
        ax.set_axisbelow(True)
    axs[0].set_ylabel("seek latency (µs)")
    axs[1].set_ylabel("probes skipped (%)")
    axs[1].set_ylim(0, 100)
    fig.tight_layout(rect=(0, 0, 1, 0.8))
    fig.legend(*axs[0].get_legend_handles_labels(), loc="upper center", ncol=3,
               frameon=False, fontsize=6.5, bbox_to_anchor=(0.5, 1.0),
               columnspacing=1.0, handlelength=1.2)
    fig.savefig(os.path.join(outdir, f"seek-{label}.pdf"), bbox_inches="tight")
    fig.savefig(os.path.join(outdir, f"seek-{label}.png"), dpi=200, bbox_inches="tight")
    plt.close(fig)

lo, g = load(runs[0][1])
fs = [r["filter"] for r in lo if r["filter"] != "nofilter"]
bpk = {r["filter"]: num(r["filter_bits_per_key"]) for r in lo}
fig, axs = plt.subplots(1, 2, figsize=(3.4, 1.6), sharey=True)
for ax, wl, title in [(axs[0], "readrandom", "random absent keys"),
                      (axs[1], "readmissing", "keys extending a stored key")]:
    pts = {}
    for f in fs:
        rows = g[(f, wl, "")]
        fp = med(rows, "bloom_full_positive") - med(rows, "bloom_full_true_positive")
        neg = med(rows, "bloom_useful") + fp
        pts[f] = (bpk[f], 100 * fp / neg)
        ax.scatter([pts[f][0]], [pts[f][1]], s=22, color=COLORS[f], zorder=3,
                   edgecolor="white", linewidth=0.5)
    labels = {f: NAMES[f] for f in fs}
    if "bloom" in pts and "ribbon" in pts:
        labels["bloom"], labels["ribbon"] = "Bloom, Ribbon", None
    for f in fs:
        if labels[f] is None:
            continue
        dy = 2
        if f == "surf-real8" and "surf-hash8" in pts and pts["surf-hash8"][1] < pts[f][1] * 1.6:
            dy = 6
        if f == "surf-hash8" and "surf-real8" in pts and pts["surf-real8"][1] < pts[f][1] * 1.6:
            dy = -9
        ax.annotate(labels[f], pts[f], xytext=(4, dy), textcoords="offset points",
                    fontsize=6)
    ax.set_yscale("log")
    ax.set_title(title, fontsize=7.5)
    ax.set_xlabel("filter bits / key")
    ax.set_xlim(6, 24)
    ax.grid(color="#e6e5e0", linewidth=0.5)
    ax.set_axisbelow(True)
axs[0].set_ylabel("false positives per probe (%)")
fig.tight_layout()
fig.savefig(os.path.join(outdir, "point.pdf"), bbox_inches="tight")
fig.savefig(os.path.join(outdir, "point.png"), dpi=200, bbox_inches="tight")
print("figures in", outdir)
