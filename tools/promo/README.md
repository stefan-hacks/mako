# Promo recordings and speed tests

Scripts that made the README media and the promo clips. They run the build
from `_build` in a throwaway demo home with in-memory settings, driven over
D-Bus through the `FLIER_CAPTURE` hook in `src/nautilus-window.c`.

| Script | What it does |
|---|---|
| `make-home.sh SRC [HOME]` | Builds the demo home (default `/tmp/mako-home`) from a folder of images and one video: photo folders, ratings, tags, docs, and 10k/50k/20k-tree bench folders. |
| `record.sh OUT [clip…]` | Records raw clips (no captions or fades) and encodes each to a 30 fps MP4 plus `.markers.json` (when each action happens). |
| `encode.py DIR OUT.mp4` | Frame folder to MP4; frames are named by ms, so timing is real. |
| `bench-suite.sh OUT.jsonl [runs]` | Times folder loads, view switches, filter and flat view, command to frame painted. |
| `bench-startup.py OUT.json [runs]` | Launch-to-window, Mako vs the system's GNOME Files, plugins off for both. |
| `summarize.py SUITE STARTUP OUT` | Medians; drops runs throttled by the compositor. |
| `headless.sh CMD…` | Runs CMD against a private headless GNOME Shell, so no other window can cover or throttle the test window. |

Example: `./headless.sh ./bench-suite.sh /tmp/bench.jsonl 7`

`record.sh` and `bench-suite.sh` stop any running Mako first.
