#!/usr/bin/env python3
"""Turns one recorded frame folder into a clean 30 fps MP4 (no captions, no
fades). Frames are named by ms since recording began, so each is held for
as long as it really lasted. Markers become CLIP.markers.json (seconds).
usage: encode.py FRAME_DIR OUT.mp4"""
import json, os, subprocess, sys

src, out = sys.argv[1], sys.argv[2]
frames = sorted(f for f in os.listdir(src) if f.endswith((".ppm", ".png")))
times = [int(f.split(".")[0]) for f in frames]
lst = os.path.join(src, "list.txt")
with open(lst, "w") as fh:
    for i, f in enumerate(frames):
        dur = (times[i + 1] - times[i]) / 1000 if i + 1 < len(frames) else 0.1
        fh.write(f"file '{os.path.join(src, f)}'\nduration {max(dur, 0.005):.3f}\n")
    fh.write(f"file '{os.path.join(src, frames[-1])}'\n")
subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", lst,
                "-vf", "fps=30,format=yuv420p", "-c:v", "libx264", "-preset", "medium", "-crf", "14",
                "-movflags", "+faststart", out], check=True)
markers = []
mpath = os.path.join(src, "markers.tsv")
if os.path.exists(mpath):
    t0 = times[0] if times else 0
    for line in open(mpath):
        ms, _, label = line.rstrip("\n").partition("\t")
        markers.append({"t": round((int(ms) - t0) / 1000, 3), "label": label})
json.dump({"clip": os.path.basename(out), "duration": round((times[-1] - times[0]) / 1000 + 0.1, 3),
           "markers": markers}, open(out[:-4] + ".markers.json", "w"), indent=1)
print(f"{os.path.basename(out)}: {len(frames)} frames, {(times[-1]-times[0])/1000:.1f}s")
