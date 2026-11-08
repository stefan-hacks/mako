#!/usr/bin/env python3
"""Medians of bench-suite.sh output, plus the startup comparison.
Runs whose paint came >250 ms after the work was done were throttled by the
compositor (window covered) and are dropped. usage: summarize.py SUITE.jsonl STARTUP.json OUT.json"""
import collections, json, statistics, sys
runs = collections.defaultdict(list); dropped = collections.Counter()
for line in open(sys.argv[1]):
    j = json.loads(line)
    if j["painted_ms"] - j["ready_ms"] > 250:
        dropped[j["name"]] += 1; continue
    runs[j["name"]].append(j["painted_ms"])
ops = {k: {"median_ms": round(statistics.median(v), 1), "min_ms": round(min(v), 1),
           "max_ms": round(max(v), 1), "runs": len(v), "dropped": dropped[k]} for k, v in runs.items()}
out = {"what": "Command to pixels on screen (frame painted), Mako -O2 build, GTK 4.22",
       "operations": ops, "startup": json.load(open(sys.argv[2]))}
json.dump(out, open(sys.argv[3], "w"), indent=2)
for k, v in ops.items():
    print(f"{k:30s} {v['median_ms']:7.1f} ms  (min {v['min_ms']}, max {v['max_ms']}, n={v['runs']}, dropped {v['dropped']})")
