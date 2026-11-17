#!/bin/bash
# Builds the Windows installer from the program's folder (the one the zip is made of):
#   windows/make-installer.sh <program folder> <version> <setup.exe>
# makensis is taken from $MAKENSIS, else from PATH. Runs in MSYS2 and on Linux alike.
set -euo pipefail

SOURCE="$1"
VERSION="$2"
OUTFILE="$3"
MAKENSIS="${MAKENSIS:-makensis}"
HERE="$(cd "$(dirname "$0")" && pwd)"

[ -f "$SOURCE/trackrideviz.exe" ] || { echo "no trackrideviz.exe in $SOURCE" >&2; exit 1; }

# Windows wants four numbers for the file's version: 0.01 -> 0.1.0.0
IFS=. read -r major minor _ <<< "$VERSION."
FILEVERSION="$((10#${major:-0})).$((10#${minor:-0})).0.0"

# The uninstaller takes away what is installed and nothing else: each file, then each folder,
# the deepest first
LIST="$(mktemp --suffix=.nsh)"
trap 'rm -f "$LIST"' EXIT
(
    cd "$SOURCE"
    find . -type f | sed 's|^\./||' | sort | while read -r file; do
        printf 'Delete "$INSTDIR\\%s"\n' "${file//\//\\}"
    done
    find . -mindepth 1 -type d | sed 's|^\./||' | awk '{ print gsub("/", "/") "\t" $0 }' | sort -rn | cut -f2- \
        | while read -r dir; do
            printf 'RMDir "$INSTDIR\\%s"\n' "${dir//\//\\}"
        done
) > "$LIST"

to_native() {
    # makensis of MSYS2 is a Windows program: it wants C:\... paths
    if command -v cygpath > /dev/null; then cygpath -w "$1"; else echo "$1"; fi
}

OUTDIR="$(cd "$(dirname "$OUTFILE")" && pwd)"
cd "$HERE"
"$MAKENSIS" -V2 \
    -DVERSION="$VERSION" -DFILEVERSION="$FILEVERSION" \
    -DSOURCE="$(to_native "$(cd "$SOURCE" && pwd)")" \
    -DUNINSTALL_LIST="$(to_native "$LIST")" \
    -DOUTFILE="$(to_native "$OUTDIR/$(basename "$OUTFILE")")" \
    trackrideviz.nsi
echo "made $OUTFILE"
