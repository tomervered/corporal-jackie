# Patches to mpv for the Windows build

`build-mpv.sh` applies every `.patch` here, in name order, to the mpv source before building it. The patched files keep mpv's licence (LGPL-2.1-or-later for these).

- `0001-d3d11-dxinterop.patch` adds a hardware-decoding bridge, `d3d11-dxinterop`, that puts Direct3D 11 frames on a normal (WGL) OpenGL context through `WGL_NV_DX_interop2`. NVIDIA, AMD and Intel drivers support that extension. mpv already has such a bridge for Direct3D 9, but not for Direct3D 11. Without it, mpv has no Direct3D 11 device when the app draws through mpv's OpenGL render API, and `vf=d3d11vpp` (which runs NVIDIA RTX and Intel Video Super Resolution) cannot load. Each frame is converted to one 8-bit RGB texture by the Direct3D 11 video processor, because the interop cannot share NV12 or P010 textures. The Direct3D 11 device is made on the graphics card the OpenGL context runs on (matched by vendor), because on a laptop with two cards the default one is often the other card, and the interop only works on one card.

The app only uses this bridge while NVIDIA or Intel VSR is picked in Settings. Otherwise it asks mpv for copy-back decoding (`hwdec=auto-copy-safe`), the same path as before the patch.
