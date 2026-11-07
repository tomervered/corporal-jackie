#!/usr/bin/env bash
# Builds a playback-only FFmpeg, libplacebo and libmpv for the Windows package,
# inside MSYS2 UCRT64. MSYS2's own mpv links every encoder and graphics
# library FFmpeg can use (x265, AV1 encoders, SVG, JPEG XL, Vulkan shader
# compilers...), about 70 MB the player never loads. This build keeps every
# decoder, demuxer and parser, the image encoders screenshots use, Direct3D
# 11 hardware decoding, HTTPS through Windows' own TLS (schannel), and nothing for encoding video.
#
# Usage: build-mpv.sh <prefix>   (installs into <prefix>; skips if done)
set -euo pipefail

PREFIX=$1
FFMPEG_VERSION=n7.1.1
LIBPLACEBO_VERSION=v7.349.0
MPV_VERSION=v0.39.0
STAMP="$PREFIX/.built-$FFMPEG_VERSION-$LIBPLACEBO_VERSION-$MPV_VERSION-1"

if [ -f "$STAMP" ]; then
    echo "Using cached build in $PREFIX"
    exit 0
fi

SRC=$(mktemp -d)
JOBS=$(nproc)
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export PATH="$PREFIX/bin:$PATH"

echo "::group::FFmpeg $FFMPEG_VERSION"
git clone --depth 1 --branch "$FFMPEG_VERSION" https://github.com/FFmpeg/FFmpeg.git "$SRC/ffmpeg"
cd "$SRC/ffmpeg"
./configure --prefix="$PREFIX" \
    --enable-shared --disable-static \
    --enable-gpl --enable-version3 \
    --disable-programs --disable-doc --disable-debug \
    --disable-avdevice --disable-postproc \
    --disable-encoders --enable-encoder=png,mjpeg,bmp \
    --disable-muxers \
    --disable-autodetect \
    --enable-d3d11va --enable-dxva2 --enable-w32threads \
    --enable-zlib --enable-libdav1d --enable-schannel
make -j"$JOBS"
make install
echo "::endgroup::"

echo "::group::libplacebo $LIBPLACEBO_VERSION"
git clone --depth 1 --branch "$LIBPLACEBO_VERSION" --recurse-submodules --shallow-submodules \
    https://github.com/haasn/libplacebo.git "$SRC/libplacebo"
cd "$SRC/libplacebo"
meson setup build --prefix="$PREFIX" --buildtype=release \
    -Dvulkan=disabled -Dshaderc=disabled -Dglslang=disabled \
    -Dopengl=enabled -Dd3d11=disabled \
    -Dlcms=enabled -Dlibdovi=enabled \
    -Ddemos=false -Dtests=false
meson compile -C build
meson install -C build
echo "::endgroup::"

echo "::group::mpv $MPV_VERSION"
git clone --depth 1 --branch "$MPV_VERSION" https://github.com/mpv-player/mpv.git "$SRC/mpv"
cd "$SRC/mpv"
meson setup build --prefix="$PREFIX" --buildtype=release \
    -Dlibmpv=true -Dcplayer=false -Dtests=false \
    -Dlua=disabled -Djavascript=disabled -Dlibarchive=disabled \
    -Drubberband=disabled -Dlibbluray=disabled -Ddvdnav=disabled -Dcdda=disabled \
    -Dvulkan=disabled -Dshaderc=disabled -Dspirv-cross=disabled \
    -Dsdl2-audio=disabled -Dsdl2-video=disabled -Dsdl2-gamepad=disabled -Dopenal=disabled \
    -Dlibavdevice=disabled -Djpeg=disabled -Dvapoursynth=disabled \
    -Degl-angle=disabled -Degl-angle-lib=disabled -Degl-angle-win32=disabled \
    -Dgl=enabled -Dgl-win32=enabled -Dd3d11=enabled -Dd3d-hwaccel=enabled \
    -Dwasapi=enabled -Dlcms2=enabled -Duchardet=enabled -Dzimg=enabled -Diconv=enabled
meson compile -C build
meson install -C build
echo "::endgroup::"

touch "$STAMP"
