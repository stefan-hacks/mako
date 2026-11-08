#!/bin/bash
# Times Mako's everyday operations, command to pixels on screen.
# usage: bench-suite.sh OUT.jsonl [runs]
OUT=${1:?out.jsonl}; N=${2:-5}; H=${MAKO_DEMO:-/tmp/mako-home}
. "$(dirname "$0")/cap.sh"
rm -f "${OUT:?}"; export FLIER_BENCH_LOG=$OUT
START "$H" || exit 1
C "size:1280,800"; sleep 1
B(){ C "size:1280,800"; sleep 0.3; C "bench:$1:$2"; sleep "${3:-2}"; }
for i in $(seq 1 "$N"); do
  for v in 4 1; do  # grid, then list
    C "act:win.view-mode(uint32 $v)"; sleep 0.5
    B "open 10k files (view $v)" "go:$H/Bench/10k files" 3
    B "open 50k files (view $v)" "go:$H/Bench/50k files" 5
    C "go:$H"; sleep 1
  done
  C "go:$H/Bench/10k files"; sleep 3
  B "switch to grid (10k)" "act:win.view-mode(uint32 4)" 3
  B "switch to list (10k)" "act:win.view-mode(uint32 1)" 3
  B "switch to columns (10k)" "act:win.view-mode(uint32 3)" 3
  C "act:win.view-mode(uint32 1)"; sleep 2
  B "filter keystroke 1 (10k)" "filter:s" 1
  B "filter keystroke 2 (10k)" "filter:sa" 1
  B "filter to 1 match (10k)" "filter:sample_04242" 1
  B "clear filter (10k)" "filter:" 2
  C "go:$H/Bench/tree 20k"; sleep 1.5
  B "flat view on (20k tree)" "act:win.flat-view" 6
  C "act:win.flat-view"; sleep 1.5
  C "go:$H/Pictures/Deep Sea/Moodboard"; sleep 1.5
  B "open photo folder (grid)" "go:$H/Pictures/Deep Sea/Sharks" 1.5
  C "go:$H"; sleep 1
done
STOPAPP
