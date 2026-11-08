#!/bin/bash
# Runs the build in the demo home with throwaway settings and the capture hook.
DEMO=${MAKO_DEMO:-/tmp/mako-home}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SCHEMAS=${GSETTINGS_SCHEMA_DIR:-$HOME/.local/nautilus-ff/share/glib-2.0/schemas}
export HOME=$DEMO XDG_CONFIG_HOME=$DEMO/.config XDG_DATA_HOME=$DEMO/.local/share XDG_CACHE_HOME=$DEMO/.cache
export GSETTINGS_BACKEND=memory GSETTINGS_SCHEMA_DIR=$SCHEMAS
export FLIER_CAPTURE=1 FLIER_CAPTURE_PRESET=1
cd "$DEMO"
exec "$REPO/_build/src/nautilus" "$@"
