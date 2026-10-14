#!/usr/bin/env python3
"""Local web UI for the SuRF harness (Python standard library only).

    python3 web/server.py            # then open http://localhost:8598

Runs ./surf_harness with --json, saves every run under results/, and serves
web/index.html, which shows the runs as tables and charts.
"""
import json
import os
import subprocess
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "surf_harness")
RESULTS = os.path.join(ROOT, "results")
INDEX = os.path.join(ROOT, "web", "index.html")
PORT = int(os.environ.get("PORT", "8598"))

ALLOWED_KEYS = {"10000", "100000", "1000000"}
ALLOWED_TYPES = {"int", "string"}


def run_harness(args):
    started = time.time()
    proc = subprocess.run([BIN, *args, "--json"], cwd=ROOT,
                          capture_output=True, text=True, timeout=1800)
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        data = {"error": (proc.stderr or proc.stdout)[-2000:]}
    data["args"] = args
    data["exit_code"] = proc.returncode
    data["started"] = started
    data["seconds"] = round(time.time() - started, 2)
    os.makedirs(RESULTS, exist_ok=True)
    name = time.strftime("%Y%m%d-%H%M%S") + "-" + "-".join(args) + ".json"
    with open(os.path.join(RESULTS, name), "w") as f:
        json.dump(data, f, indent=1)
    data["file"] = name
    return data


def load_results():
    if not os.path.isdir(RESULTS):
        return []
    out = []
    for name in sorted(os.listdir(RESULTS)):
        if name.endswith(".json"):
            with open(os.path.join(RESULTS, name)) as f:
                d = json.load(f)
            d["file"] = name
            out.append(d)
    return out


class Handler(BaseHTTPRequestHandler):
    def send_json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path in ("/", "/index.html"):
            with open(INDEX, "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif url.path == "/api/results":
            self.send_json(load_results())
        else:
            self.send_json({"error": "not found"}, 404)

    def do_POST(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        if url.path == "/api/correctness":
            self.send_json(run_harness(["correctness"]))
        elif url.path == "/api/bench":
            keys = q.get("keys", ["100000"])[0]
            ktype = q.get("type", ["int"])[0]
            if keys not in ALLOWED_KEYS or ktype not in ALLOWED_TYPES:
                self.send_json({"error": "bad parameters"}, 400)
                return
            self.send_json(run_harness(["bench", "--keys", keys,
                                        "--type", ktype]))
        else:
            self.send_json({"error": "not found"}, 404)

    def log_message(self, fmt, *args):
        sys.stderr.write("[web] " + fmt % args + "\n")


if __name__ == "__main__":
    if not os.path.exists(BIN):
        sys.exit("Build the harness first: make")
    print(f"SuRF harness UI: http://localhost:{PORT}")
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
