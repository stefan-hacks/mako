#!/usr/bin/env python3
"""Launch-to-window time, Mako vs the system's GNOME Files.

Both run in the same demo home with plugins off and throwaway settings. The
clock stops when the app exports its first window on D-Bus. usage:
bench-startup.py OUT.json [runs]"""
import json, os, statistics, subprocess, sys, time

runs = int(sys.argv[2]) if len(sys.argv) > 2 else 7
home = os.environ.get("MAKO_DEMO", "/tmp/mako-home")
repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
apps = {
    "mako": ([os.path.join(repo, "_build/src/nautilus")], "org.gnome.Nautilus.Devel", "/org/gnome/Nautilus/Devel/window/1",
             {"GSETTINGS_SCHEMA_DIR": os.path.expanduser("~/.local/nautilus-ff/share/glib-2.0/schemas")}),
    "gnome-files": (["/usr/bin/nautilus"], "org.gnome.Nautilus", "/org/gnome/Nautilus/window/1", {}),
}

def has_window(name, path):
    r = subprocess.run(["gdbus", "introspect", "--session", "-d", name, "-o", path],
                       capture_output=True)
    return r.returncode == 0 and b"org.gtk.Actions" in r.stdout

results = {}
for app, (cmd, name, path, extra) in apps.items():
    env = dict(os.environ, HOME=home, XDG_CONFIG_HOME=home + "/.config",
               XDG_DATA_HOME=home + "/.local/share", XDG_CACHE_HOME=home + "/.cache",
               GSETTINGS_BACKEND="memory", NAUTILUS_DISABLE_PLUGINS="TRUE", **extra)
    times = []
    for i in range(runs + 1):  # first run warms the disk cache and is dropped
        t0 = time.monotonic()
        p = subprocess.Popen(cmd + [home], env=env, cwd=home,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        while not has_window(name, path):
            if time.monotonic() - t0 > 20:
                break
            time.sleep(0.002)
        ms = (time.monotonic() - t0) * 1000
        p.terminate(); p.wait()
        time.sleep(0.8)
        if i:
            times.append(round(ms, 1))
    results[app] = {"runs_ms": times, "median_ms": statistics.median(times)}
    print(app, results[app])
json.dump(results, open(sys.argv[1], "w"), indent=2)
