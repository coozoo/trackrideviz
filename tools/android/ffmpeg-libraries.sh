#!/bin/bash
# FFmpeg's libraries for the Android build: the program's own video engine, which exports there.
# Qt's multimedia brings an FFmpeg of its own, but without the filters the engine needs, so this
# one is built beside it, its libraries named with _rtv (libavcodec_rtv.so) so that the two
# never meet. With Java (JNI) and MediaCodec: the phone's own encoders and decoders, and files
# Android hands over by address (content://).
#
#   tools/android/ffmpeg-libraries.sh <ndk> <prefix>
#
# The same release as the AppImage's. CI keeps the result between runs, keyed on this file, so a
# change here builds it again.
set -euo pipefail

VERSION=8.1.2
SHA256=464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c
# the oldest Android the program installs on (ANDROID_MIN_SDK_VERSION)
API=28

NDK="$1"
PREFIX="$2"
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

curl -fsSL -o ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
echo "$SHA256  ffmpeg.tar.xz" | sha256sum -c -
tar xf ffmpeg.tar.xz

cd "ffmpeg-$VERSION"
# pages of 16 KB: what Android 15 and later want of a library
./configure --prefix="$PREFIX" \
    --target-os=android --arch=aarch64 --cpu=armv8-a --enable-cross-compile \
    --cc="$TOOLCHAIN/bin/aarch64-linux-android$API-clang" \
    --cxx="$TOOLCHAIN/bin/aarch64-linux-android$API-clang++" \
    --ar="$TOOLCHAIN/bin/llvm-ar" --nm="$TOOLCHAIN/bin/llvm-nm" \
    --ranlib="$TOOLCHAIN/bin/llvm-ranlib" --strip="$TOOLCHAIN/bin/llvm-strip" \
    --sysroot="$TOOLCHAIN/sysroot" --pkg-config=false \
    --enable-shared --disable-static --build-suffix=_rtv --enable-pic \
    --disable-programs --disable-doc --disable-avdevice --disable-debug --disable-vulkan \
    --enable-jni --enable-mediacodec \
    --extra-ldflags="-Wl,-z,max-page-size=16384" > /dev/null
# without the phone's encoders the build is of no use for exporting
for wanted in H264_MEDIACODEC_ENCODER HEVC_MEDIACODEC_ENCODER H264_MEDIACODEC_DECODER AAC_ENCODER; do
    grep -q "#define CONFIG_$wanted 1" config_components.h || { echo "FFmpeg configured without $wanted" >&2; exit 1; }
done
make -j"$(nproc)" > /dev/null
make install > /dev/null
