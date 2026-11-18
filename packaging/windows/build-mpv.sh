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
FFMPEG_VERSION=n7.1.5
LIBPLACEBO_VERSION=v7.349.0
MPV_VERSION=v0.41.0
PATCHES="$(cd "$(dirname "$0")" && pwd)/mpv-patches"
STAMP="$PREFIX/.built-$FFMPEG_VERSION-$LIBPLACEBO_VERSION-$MPV_VERSION-4"

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
# CVE-2026-70628: the DVB subtitle parser checks the room left in its
# 64 KiB buffer with a sum that can overflow, so a crafted subtitle stream in
# a torrent writes past the buffer. FFmpeg fixed it upstream after n7.1.5;
# this applies the same one-line fix (subtract instead of add). Remove it
# when FFMPEG_VERSION moves past the fix. The build stops if the line is not
# there, so a version bump cannot drop the fix without anyone noticing.
DVBSUB_OLD='if (buf_size - buf_pos + pc->packet_index > PARSE_BUF_SIZE)'
DVBSUB_NEW='if (buf_size - buf_pos > PARSE_BUF_SIZE - pc->packet_index)'
grep -qF "$DVBSUB_OLD" libavcodec/dvbsub_parser.c \
    || { echo "dvbsub_parser.c changed: check whether CVE-2026-70628 is fixed upstream" >&2; exit 1; }
# Neither string holds a character sed treats as special.
sed -i "s/$DVBSUB_OLD/$DVBSUB_NEW/" libavcodec/dvbsub_parser.c
grep -qF "$DVBSUB_NEW" libavcodec/dvbsub_parser.c
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
# Our patches (see mpv-patches/README.md): a D3D11 hwdec for the OpenGL
# render API, which NVIDIA/Intel Video Super Resolution needs.
for patch in "$PATCHES"/*.patch; do
    git apply --verbose "$patch"
done
meson setup build --prefix="$PREFIX" --buildtype=release \
    -Dlibmpv=true -Dcplayer=false -Dtests=false \
    -Dlua=disabled -Djavascript=disabled -Dlibarchive=disabled \
    -Drubberband=disabled -Dlibbluray=disabled -Ddvdnav=disabled -Dcdda=disabled \
    -Dvulkan=disabled -Dshaderc=disabled -Dspirv-cross=disabled \
    -Dsdl2-audio=disabled -Dsdl2-video=disabled -Dsdl2-gamepad=disabled -Dopenal=disabled \
    -Dlibavdevice=disabled -Djpeg=disabled -Dvapoursynth=disabled \
    -Dvaapi=disabled -Dcuda-hwaccel=disabled -Dcuda-interop=disabled \
    -Degl-angle=disabled -Degl-angle-lib=disabled -Degl-angle-win32=disabled \
    -Dgl=enabled -Dgl-win32=enabled -Dd3d11=disabled -Dd3d-hwaccel=enabled \
    -Dwasapi=enabled -Dlcms2=enabled -Duchardet=enabled -Dzimg=enabled -Diconv=enabled
meson compile -C build
meson install -C build
echo "::endgroup::"

touch "$STAMP"
