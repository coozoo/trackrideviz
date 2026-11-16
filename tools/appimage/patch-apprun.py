#!/usr/bin/env python3
"""Adds what the bundled GStreamer needs to the AppRun go-appimage writes.

    python3 tools/appimage/patch-apprun.py appdir/AppRun

Fails when AppRun no longer looks as expected, so a changed go-appimage is noticed in CI
rather than by an AppImage that hangs.
"""

import sys

ANCHOR = "# Run experimental bundle that bundles everything"

ADDED = """\
############################################################################################
# Race Track Visualizer: the bundled GStreamer on its own
############################################################################################

if [ -n "$GST_PLUGIN_PATH" ] ; then
  # the plugins are checked in the program: the helper cannot be started in this bundle
  export GST_REGISTRY_FORK=no
  # a registry of its own: the system's is of another GStreamer
  export GST_REGISTRY="${XDG_CACHE_HOME:-$HOME/.cache}/trackrideviz/gstreamer-registry.bin"
  mkdir -p "$(dirname "$GST_REGISTRY")"
fi
# nothing of the system's audio and video effects, built for its own libraries
export LADSPA_PATH=
export LV2_PATH=
export FREI0R_PATH=

"""

path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    lines = f.read().split("\n")
at = next((i for i, line in enumerate(lines) if line.startswith(ANCHOR)), None)
if at is None or at == 0 or not lines[at - 1].startswith("####"):
    sys.exit(f"{path}: no '{ANCHOR}' section; the AppRun of go-appimage has changed")
# before the banner of that section
lines[at - 1:at - 1] = ADDED.split("\n")
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
print(f"{path}: GStreamer settings added")
