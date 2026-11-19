#!/bin/bash
# FFmpeg's libraries for the AppImage, which is built on Ubuntu 22.04: its own FFmpeg (4.4) is
# older than the program's video engine is written for (6.1), so a current release is built
# from source, shared libraries only, and bundled with the program.
#
#   tools/appimage/ffmpeg-libraries.sh <prefix>     (e.g. /opt/ffmpeg; must be writable)
#
# The codec libraries come from Ubuntu (see the workflow's apt list); NVIDIA's encoder
# needs only FFmpeg's own headers for it, loaded from the driver at run time. Intel's Quick
# Sync needs libvpl, which 22.04 has too old: its loader is built here, and it loads the
# system's Intel driver at run time. dav1d is left
# out: 22.04's is older than FFmpeg wants. CI keeps the result between runs, keyed on this
# file, so a change here builds it again.
set -euo pipefail

VERSION=8.1.2
SHA256=464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c
# 12.0: NVIDIA drivers from 520 on
NV_HEADERS=n12.0.16.1
LIBVPL=v2.16.0

PREFIX="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

curl -fsSL -o ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
echo "$SHA256  ffmpeg.tar.xz" | sha256sum -c -
tar xf ffmpeg.tar.xz

git clone --quiet --depth 1 --branch "$NV_HEADERS" https://github.com/FFmpeg/nv-codec-headers.git
make -C nv-codec-headers PREFIX="$PREFIX" install
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

git clone --quiet --depth 1 --branch "$LIBVPL" https://github.com/intel/libvpl.git
cmake -S libvpl -B libvpl/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DINSTALL_EXAMPLES=OFF
cmake --build libvpl/build -j"$(nproc)"
cmake --install libvpl/build

cd "ffmpeg-$VERSION"
./configure --prefix="$PREFIX" \
    --enable-shared --disable-static --disable-programs --disable-doc \
    --enable-gpl \
    --enable-libx264 --enable-libx265 --enable-libvpx --enable-libaom --enable-libsvtav1 \
    --enable-libopus --enable-libvorbis --enable-libmp3lame \
    --enable-vaapi --enable-libdrm --enable-ffnvcodec --enable-nvenc --enable-libvpl \
    || { tail -40 ffbuild/config.log; exit 1; }
make -j"$(nproc)"
make install
echo "FFmpeg $VERSION libraries in $PREFIX/lib"
