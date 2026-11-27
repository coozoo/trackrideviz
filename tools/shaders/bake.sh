#!/bin/sh
# Bakes the simplified interface's shaders (src/simple/shaders/*.vert, *.frag) into the shader
# packs Qt reads (.qsb), kept in the repository beside them: the builds then need no shader
# tools. Readable from Qt 6.4 on (the oldest format qsb writes); run after changing a shader.
#
#     tools/shaders/bake.sh [path/to/qsb]
set -e
qsb=${1:-$(command -v qsb6 || command -v qsb || echo /usr/lib64/qt6/bin/qsb)}
cd "$(dirname "$0")/../../src/simple/shaders"
for shader in *.vert *.frag; do
    "$qsb" --qt6 --qsbversion 64 -o "$shader.qsb" "$shader"
    echo "$shader.qsb"
done
