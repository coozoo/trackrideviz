#!/usr/bin/env python3
"""Adds what the bundled Qt and GStreamer need to the AppRun go-appimage writes, and the
system's graphics driver loaders before the bundled ones.

    python3 tools/appimage/patch-apprun.py appdir/AppRun

Fails when AppRun no longer looks as expected, so a changed go-appimage is noticed in CI
rather than by an AppImage that hangs.
"""

import sys

ANCHOR = "# Run experimental bundle that bundles everything"

ADDED = """\
############################################################################################
# Race Track Visualizer: the bundled Qt and GStreamer on their own
############################################################################################

# Qt's plugins (the display's first): go-appimage says where they are only when it bundles
# everything, and Qt looks for them where the build machine had them
if [ -z "$QT_PLUGIN_PATH" ] ; then
  PLATFORMS=$(find "${HERE}" -type d -path '*/plugins/platforms' | head -n 1)
  if [ -n "$PLATFORMS" ] ; then
    export QT_PLUGIN_PATH="$(dirname "$PLATFORMS")"
  fi
fi

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

# The loaders of the graphics card's drivers (libva for VA-API, VDPAU, OpenCL, Intel's
# libvpl and Media SDK) only work with the drivers of their own version: the system's are
# used, and those of the build machine (in graphics-fallback, a folder per library named
# after it, out of the program's way) only for a library the system does not have, so that
# the program still starts there, without that part of the graphics card.
SYSTEM_LIBRARIES=$({ /sbin/ldconfig -p 2>/dev/null || ldconfig -p 2>/dev/null; })
for FALLBACK in "${HERE}"/usr/lib/graphics-fallback/*/ ; do
  [ -d "$FALLBACK" ] || continue
  if ! printf '%s\\n' "$SYSTEM_LIBRARIES" | grep -qF "$(basename "$FALLBACK") (" ; then
    export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}${FALLBACK%/}"
  fi
done

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
print(f"{path}: Qt, GStreamer and graphics driver settings added")
