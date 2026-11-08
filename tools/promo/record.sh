#!/bin/bash
# Records raw promo clips (no captions, no fades) as time-named PNG frames,
# one folder per clip, each with markers.tsv (ms, command) for syncing overlays.
# usage: record.sh OUT_DIR [clip ...]     (no clips = all)
OUT=${1:?out dir}; shift; H=${MAKO_DEMO:-/tmp/mako-home}
P="$H/Pictures/Deep Sea"; D="$H/Documents/Expedition"
export FLIER_CAPTURE_FPS=${FLIER_CAPTURE_FPS:-30} FLIER_CAPTURE_SCALE=${FLIER_CAPTURE_SCALE:-1.5}
. "$(dirname "$0")/cap.sh"
mkdir -p "$OUT"
export FLIER_CAPTURE_RAW=1; RAW=/tmp/mako-frames
R(){ CUR=$1; rm -rf "${RAW:?}/$1"; mkdir -p "$RAW/$1"; C "size:1280,800"; sleep 0.3; C "rec:$RAW/$1"; }
S(){ C stop; sleep 0.5; "$(dirname "$0")/encode.py" "$RAW/$CUR" "$OUT/$CUR.mp4" && rm -rf "${RAW:?}/${CUR:?}"; }
fresh(){ START "${1:-$H}" || exit 1; C "size:1280,800"; sleep 1; }
s(){ sleep "$1"; }

clip_intro(){ fresh "$P/Moodboard"; C "act:win.view-mode(uint32 4)"; C "act:win.toggle-sidebar"; s 1
  R intro; s 1; C "select:mako-shark.jpg"; s 2.5; S; }

clip_quicklook_multi(){ fresh "$P/Moodboard"; C "act:win.view-mode(uint32 4)"; s 1
  C "select:mako-shark.jpg,great-white.jpg,hammerhead.png,betta-violet.jpg,jellyfish.jpg,nautilus-octopus.jpg"; s 0.5
  R quicklook-multi; s 0.6; C "key:view.preview-selection"; s 4; C "act:view.preview-selection"; s 0.6
  C "select:nautilus-octopus.jpg"; C "key:view.preview-selection"; s 3; C "act:view.preview-selection"; s 0.6; S; }

clip_quicklook_frames(){ fresh "$H/Videos/Sardine Run frames"; C "act:win.view-mode(uint32 4)"; s 1.5
  R quicklook-frames; s 0.5; C "key:view.select-all"; s 0.8; C "key:view.preview-selection"; s 4.5; C "act:view.preview-selection"; s 0.6; S; }

clip_quicklook_video(){ fresh "$H/Videos"; C "act:win.view-mode(uint32 4)"; s 1; C "select:sardine-school.mp4"; s 0.5
  R quicklook-video; s 0.5; C "key:view.preview-selection"; s 6; C "act:view.preview-selection"; s 0.6; S; }

clip_quicklook_text(){ fresh "$D"; C "act:win.view-mode(uint32 1)"; s 1; C "select:README.md"; s 0.5
  R quicklook-text; s 0.4; C "key:view.preview-selection"; s 2.5; C "act:view.preview-selection"; s 0.3
  C "select:gear.json"; C "key:view.preview-selection"; s 2.5; C "act:view.preview-selection"; s 0.3
  C "select:species.csv"; C "key:view.preview-selection"; s 2.5; C "act:view.preview-selection"; s 0.5; S; }

clip_panels(){ fresh "$P/Sharks"; C "act:win.view-mode(uint32 4)"; C "act:win.toggle-sidebar"; s 1; C "select:mako-shark.jpg"; s 0.5
  R panels; s 0.6; C "key:win.toggle-sidebar"; s 1.4; C "key:win.inspector-visible"; s 1.6
  for i in 1 2 3; do C "key:win.inspector-side-next"; s 1.3; done; C "act:win.inspector-side-next"; s 1.2
  C "key:win.inspector-visible"; s 0.8; C "key:win.toggle-sidebar"; s 1; S; }

clip_inspector(){ fresh "$P/Reef"; C "act:win.view-mode(uint32 4)"; s 1; C "select:nautilus-octopus.jpg"; s 0.4
  R inspector; s 0.5; C "key:win.inspector-visible"; s 2
  C "select:jellyfish.jpg"; s 1.5; C "select:coral.jpg"; s 1.5; C "go:$H/Videos"; s 0.8; C "select:sardine-school.mp4"; s 2
  C "go:$D"; s 0.8; C "select:README.md"; s 1.8; C "select:Dives"; s 1.8
  C "select:budget.txt,gear.json,species.csv,README.md"; s 1.8
  C "key:win.inspector-side-next"; s 1.6; C "key:win.inspector-side-next"; s 1.6; C "act:win.inspector-side-next"; s 0.3; C "act:win.inspector-side-next"; s 1; S; }

clip_views(){ fresh "$D/Dives"; C "act:win.view-mode(uint32 4)"; s 1; C "select:Mariana Trench"; s 0.5
  R views; s 0.6; for v in 1 2 3 4; do C "key:win.view-mode(uint32 $v)"; s 1.6; done; S; }

clip_columns(){ fresh "$H"; C "act:win.view-mode(uint32 3)"; s 1
  R columns; s 0.5; C "go:$H/Pictures"; s 1; C "go:$P"; s 1; C "go:$P/Sharks"; s 1; C "select:mako-shark.jpg"; s 1.5
  C "go:$D"; s 1; C "go:$D/Dives"; s 1; C "go:$D/Dives/Kelp Forest"; s 1; C "select:log-1.txt"; s 1.5; S; }

clip_tiles(){ fresh "$P/Sharks"; C "act:win.view-mode(uint32 4)"; s 1
  R tiles; s 0.6
  C "key:win.split-right"; s 1; C "go:$P/Bettas"; C "act:win.view-mode(uint32 4)"; s 1.2
  C "key:win.split-down"; s 1; C "go:$P/Reef"; C "act:win.view-mode(uint32 4)"; s 1.2
  C "key:win.focus-next-tile"; s 0.8; C "key:win.split-down"; s 1; C "go:$D/Dives"; C "act:win.view-mode(uint32 4)"; s 1.2
  C "key:win.new-tab"; s 0.8; C "go:$H/Videos"; C "act:win.view-mode(uint32 4)"; s 1.2
  C "key:win.new-tab"; s 0.8; C "go:$D"; C "act:win.view-mode(uint32 1)"; s 1.2
  for i in 1 2 3 4; do C "key:win.focus-next-tile"; s 0.7; done; s 1; S; }

clip_tiles_grid(){ fresh "$P/Sharks"; C "act:win.view-mode(uint32 4)"; C "act:win.toggle-sidebar"; s 1
  C "act:win.split-right"; s 0.6; C "go:$P/Bettas"; C "act:win.view-mode(uint32 4)"; s 0.8
  C "act:win.new-tab"; s 0.6; C "go:$H/Videos"; C "act:win.view-mode(uint32 4)"; s 0.8
  C "act:win.split-down"; s 0.6; C "go:$P/Reef"; C "act:win.view-mode(uint32 4)"; s 0.8
  C "act:win.focus-next-tile"; s 0.5; C "act:win.split-down"; s 0.6; C "go:$D/Dives"; C "act:win.view-mode(uint32 4)"; s 0.8
  C "act:win.new-tab"; s 0.6; C "go:$D"; C "act:win.view-mode(uint32 1)"; s 1
  R tiles-grid; s 0.5; for i in 1 2 3 4 5; do C "key:win.focus-next-tile"; s 0.8; done; s 0.5; S
  C "shot:$OUT/tiles-grid.png"; s 0.5; }

clip_rainbow(){ fresh "$D/Dives"; C "act:win.view-mode(uint32 4)"; s 1; C "select:Abyssal Plain"; s 0.4
  R rainbow; s 0.6; C "act:view.folder-color(4)"; s 1.2; C "key:view.select-all"; s 0.8; C "act:view.folder-color(100)"; s 1.6
  for sc in ocean earth mono vibrant; do C "set:folder-color-scheme='$sc'"; s 1.3; done; S; }

clip_marks(){ fresh "$P/Bettas"; C "act:win.view-mode(uint32 4)"; s 1; C "select:betta-red.jpg"; s 0.4
  R marks; s 0.5; C "key:view.rate(uint32 4)"; s 1.2; C "select:betta-lilac.jpg"; C "key:view.rate(uint32 2)"; s 1.2
  C "select:betta-closeup.jpg"; C "key:view.rate(uint32 5)"; s 1.2
  C "act:win.show-tag('Favorites')"; s 2.5; C "act:win.show-tag('Print')"; s 2; C "act:win.show-tag('Research')"; s 2; S; }

clip_grouping(){ fresh "$P/Moodboard"; C "act:win.view-mode(uint32 1)"; s 1
  R grouping; s 0.5; for g in rating kind name none; do C "act:view.group-by('$g')"; s 1.8; done; S; }

clip_sidebar(){ fresh "$P"; C "act:win.view-mode(uint32 4)"; C "set:sidebar-groups=true"; s 1
  R sidebar; s 0.5; for i in 1 2 3 4 5 6; do C "key:win.sidebar-step(1)"; s 0.6; done
  C "key:win.sidebar-fold(true)"; s 0.8; C "key:win.sidebar-step(1)"; s 0.6; C "key:win.sidebar-fold(true)"; s 0.8
  C "key:win.sidebar-fold(false)"; s 0.8
  C "set:folder-color-scheme='vibrant'"; C "set:sidebar-colors={'file://$D': 1}"; s 0.8; C "set:sidebar-colors={'file://$D': 1, 'file://${P// /%20}': 4}"; s 1.5; S; }

clip_filter(){ fresh "$H/Bench/50k files"; C "act:win.view-mode(uint32 1)"; s 4
  R filter; s 0.6; t=""; for ch in s a m p l e _ 4 2 4 2; do t="$t$ch"; C "filter:$t"; s 0.25; done; s 1.2
  C "filter:"; s 1.5; S; }

clip_flat(){ fresh "$H/Bench/tree 20k"; C "act:win.view-mode(uint32 1)"; s 1
  R flat; s 0.6; C "key:win.flat-view"; s 3; C "act:win.flat-view"; s 1; S; }

clip_palette(){ fresh "$P/Sharks"; C "act:win.view-mode(uint32 4)"; s 1
  R palette; s 0.5; C "key:win.keyboard-shortcuts"; s 1.5; for i in $(seq 1 12); do C "scroll:80"; s 0.15; done; s 1.5; S; }

clip_settings(){ fresh "$D/Dives"; C "act:win.view-mode(uint32 4)"; s 1; C "act:view.select-all"; C "act:view.folder-color(100)"; s 0.6
  R settings; s 0.4; C "key:app.preferences"; s 1.2; C "page:Organize"; s 1.2
  for i in $(seq 1 10); do C "scroll:60"; s 0.12; done; s 0.6
  C "set:folder-color-scheme='earth'"; s 1.3; C "set:folder-color-scheme='vibrant'"; s 1.2
  for i in $(seq 1 16); do C "scroll:60"; s 0.12; done; s 1.5; S; C "act:app.preferences"; }

# speed runs: each go/act is timed too (bench log next to the frames)
clip_speed_50k(){ fresh "$H/Bench"; C "act:win.view-mode(uint32 1)"; s 1
  R speed-50k; s 0.6; C "bench:open 50k:go:$H/Bench/50k files"; s 2; C "go:$H/Bench"; s 0.8
  C "bench:open 10k:go:$H/Bench/10k files"; s 1.5; S; }
clip_speed_views(){ fresh "$H/Bench/10k files"; C "act:win.view-mode(uint32 1)"; s 3
  R speed-views; s 0.5; for v in 4 3 2 1; do C "bench:view $v:key:win.view-mode(uint32 $v)"; s 1.4; done; S; }
clip_speed_photos(){ fresh "$P"; C "act:win.view-mode(uint32 4)"; s 1
  R speed-photos; s 0.5; for f in Sharks Bettas Reef Moodboard; do C "bench:open $f:go:$P/$f"; s 1.2; done; S; }

# env inheritance: FLIER_BENCH_LOG is read by the app, so speed clips set it before START
all="intro inspector quicklook_multi quicklook_frames quicklook_video quicklook_text panels views columns tiles tiles_grid rainbow marks grouping sidebar filter flat palette settings speed_50k speed_views speed_photos"
for c in ${@:-$all}; do
  case $c in speed_*) export FLIER_BENCH_LOG="$OUT/${c//_/-}.bench.jsonl"; rm -f "$FLIER_BENCH_LOG";; *) unset FLIER_BENCH_LOG;; esac
  echo "== $c"; clip_$c
done
STOPAPP
