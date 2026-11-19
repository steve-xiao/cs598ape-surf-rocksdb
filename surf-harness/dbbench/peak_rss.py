#!/usr/bin/env python3
"""Run a command and write its peak resident memory (MB) to a file.

    peak_rss.py OUTFILE CMD [ARGS...]
"""
import resource
import subprocess
import sys

rc = subprocess.call(sys.argv[2:])
peak_mb = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1024
with open(sys.argv[1], "w") as f:
    f.write(f"{peak_mb:.0f}\n")
sys.exit(rc)
