#!/usr/bin/env bash
# Builds a playback-only FFmpeg, libplacebo and libmpv for the macOS package,
# the same versions and the same LGPL licensing as packaging/windows/build-mpv.sh.
# Homebrew's mpv and FFmpeg are GPL builds (x264, x265 and other GPL parts),
# so the package uses these instead.
#
# Needs from Homebrew: meson ninja nasm pkgconf dav1d libass lcms2 uchardet
# zimg. (Homebrew has no libdovi, so Dolby Vision metadata handling is left out on macOS.)
#
# Usage: build-mpv.sh <prefix>   (installs into <prefix>; skips if done)
set -euo pipefail

PREFIX=$1
FFMPEG_VERSION=n7.1.5
LIBPLACEBO_VERSION=v7.349.0
MPV_VERSION=v0.41.0
STAMP="$PREFIX/.built-$FFMPEG_VERSION-$LIBPLACEBO_VERSION-$MPV_VERSION-1"

if [ -f "$STAMP" ]; then
    echo "Using cached build in $PREFIX"
    exit 0
fi

SRC=$(mktemp -d)
JOBS=$(sysctl -n hw.ncpu)
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export PATH="$PREFIX/bin:$PATH"

echo "::group::FFmpeg $FFMPEG_VERSION"
git clone --depth 1 --branch "$FFMPEG_VERSION" https://github.com/FFmpeg/FFmpeg.git "$SRC/ffmpeg"
cd "$SRC/ffmpeg"
# The same fix as the Windows build (CVE-2026-70628, DVB subtitle parser).
DVBSUB_OLD='if (buf_size - buf_pos + pc->packet_index > PARSE_BUF_SIZE)'
DVBSUB_NEW='if (buf_size - buf_pos > PARSE_BUF_SIZE - pc->packet_index)'
grep -qF "$DVBSUB_OLD" libavcodec/dvbsub_parser.c \
    || { echo "dvbsub_parser.c changed: check whether CVE-2026-70628 is fixed upstream" >&2; exit 1; }
sed -i '' "s/$DVBSUB_OLD/$DVBSUB_NEW/" libavcodec/dvbsub_parser.c
grep -qF "$DVBSUB_NEW" libavcodec/dvbsub_parser.c
# LGPL: no --enable-gpl, no --enable-version3. VideoToolbox does the hardware
# decoding, Secure Transport (Apple's TLS) the HTTPS; OpenSSL would need version3.
./configure --prefix="$PREFIX" \
    --enable-shared --disable-static \
    --disable-programs --disable-doc --disable-debug \
    --disable-avdevice --disable-postproc \
    --disable-encoders --enable-encoder=png,mjpeg,bmp \
    --disable-muxers \
    --disable-autodetect \
    --enable-videotoolbox --enable-securetransport \
    --enable-zlib --enable-libdav1d
make -j"$JOBS"
make install
grep -q '^#define CONFIG_GPL 0$' config.h && grep -q '^#define CONFIG_VERSION3 0$' config.h \
    || { echo "FFmpeg was configured as GPL; the package must be LGPL" >&2; exit 1; }
echo "::endgroup::"

echo "::group::libplacebo $LIBPLACEBO_VERSION"
git clone --depth 1 --branch "$LIBPLACEBO_VERSION" --recurse-submodules --shallow-submodules \
    https://github.com/haasn/libplacebo.git "$SRC/libplacebo"
cd "$SRC/libplacebo"
meson setup build --prefix="$PREFIX" --buildtype=release \
    -Dvulkan=disabled -Dshaderc=disabled -Dglslang=disabled \
    -Dopengl=enabled -Dd3d11=disabled \
    -Dlcms=enabled -Dlibdovi=auto \
    -Ddemos=false -Dtests=false
meson compile -C build
meson install -C build
echo "::endgroup::"

echo "::group::mpv $MPV_VERSION"
git clone --depth 1 --branch "$MPV_VERSION" https://github.com/mpv-player/mpv.git "$SRC/mpv"
cd "$SRC/mpv"
meson setup build --prefix="$PREFIX" --buildtype=release \
    -Dlibmpv=true -Dcplayer=false -Dtests=false -Dgpl=false \
    -Dlua=disabled -Djavascript=disabled -Dlibarchive=disabled \
    -Drubberband=disabled -Dlibbluray=disabled -Ddvdnav=disabled -Dcdda=disabled \
    -Dvulkan=disabled -Dshaderc=disabled -Dspirv-cross=disabled \
    -Dsdl2-audio=disabled -Dsdl2-video=disabled -Dsdl2-gamepad=disabled -Dopenal=disabled \
    -Dlibavdevice=disabled -Djpeg=disabled -Dvapoursynth=disabled \
    -Dlcms2=enabled -Duchardet=enabled -Dzimg=enabled -Diconv=enabled
meson compile -C build
meson install -C build
grep -q '^#define HAVE_GPL 0$' build/config.h \
    || { echo "mpv was built as GPL; the package must be LGPL" >&2; exit 1; }
echo "::endgroup::"

touch "$STAMP"
