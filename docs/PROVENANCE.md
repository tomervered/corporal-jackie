# Provenance of code and assets we did not write from scratch

Corporal Jackie is GPL-3.0-or-later. This page lists what in the repository comes from somewhere else (or may), where it came from, under which license, what we changed, and where its notice is kept. Libraries we link or ship are in [DEPENDENCIES.md](DEPENDENCIES.md); their license files go into every package (`licenses/`, `THIRD_PARTY_NOTICES.txt`, `SOURCE.txt`).

If we cannot show that we may distribute an item, it is replaced.

## Upscaler shaders (`src/targets/shaders/`)

| File | Origin | License | What we changed | Notice |
| --- | --- | --- | --- | --- |
| `fsr1.glsl` | AMD FidelityFX Super Resolution 1.0, `ffx_fsr1.h` (32-bit path) | MIT | Ported to an mpv user shader on the luma plane; fast reciprocal tricks replaced by plain division, with guards for the zero cases | File header and `packaging/THIRD_PARTY_NOTICES.txt` |
| `cas.glsl` | AMD FidelityFX Contrast Adaptive Sharpening, `ffx_cas.h` (scaling path) | MIT | Ported to an mpv user shader on the luma plane | File header and `THIRD_PARTY_NOTICES.txt` |
| `sgsr1.glsl` | Qualcomm Snapdragon Game Super Resolution 1, `sgsr1_shader_mobile.frag` | BSD-3-Clause | Works on luma instead of green; texture gathers written as plain reads | File header and `THIRD_PARTY_NOTICES.txt` |
| `anime4k_upscale_s.glsl` | bloc97's Anime4K v3.2, Upscale CNN x2 (S), `glsl/Upscale` | MIT | Unchanged except the `@WHEN@` placeholder the app fills in | File header and `THIRD_PARTY_NOTICES.txt` |

Each file keeps its owner's copyright line and permission text, which the licenses require. The packages carry the same texts in `THIRD_PARTY_NOTICES.txt`.

## Patches to libraries we build (`packaging/windows/mpv-patches/`, `third_party/libtorrent/`)

- `0001-d3d11-dxinterop.patch` (mpv 0.41). The new file `video/out/opengl/hwdec_d3d11gldx.c` was written for Corporal Jackie. mpv has an older bridge for Direct3D 9 (`hwdec_dxva2gldx.c`), and the structure of our file follows it in places (about a quarter of the lines), so the new file carries mpv's LGPL-2.1-or-later header and stays under that license. The other two hunks are small edits to mpv's `meson.build` and `hwdec.c`. Patched files keep mpv's license; the app links the resulting library dynamically.
- libtorrent patch: one patch to libtorrent 2.1.2, which is BSD-3-Clause; the patch stays under that license.
- FFmpeg: one upstream one-line fix for CVE-2026-70628, applied by `build-mpv.sh` (`docs/SECURITY.md`). FFmpeg is built without `--enable-gpl`.

## Copied add-on manifest (`src/sources/StremioAdapter.cpp`)

`kPublicDomainManifest` follows the shape of the manifest of the Public Domain Movies add-on (`https://caching.stremio.net/publicdomainmovies.now.sh/manifest.json`, copied 2026-11-20). A manifest is the add-on's description of itself for any client to read. The add-on id, catalog id and resource names are kept as they are, because the installed copy has to match the add-on's own. The description text is our own wording. Its logo is a link to an image on the add-on author's page, fetched when shown, not copied into the app.

## Images (`src/ui/images/`)

| File | Origin | Status |
| --- | --- | --- |
| `subtitle-sample.jpg` | Hand-drawn frame from Winsor McCay's *Gertie the Dinosaur* (1914), Wikimedia Commons, marked public domain | Clear |
| `logo.png` | Added in the GUI redesign (commit `ea02a49`, 2026-11-18), which followed a prototype supplied by the project owner. The repository does not record who made the picture, with which tool, or under which terms | **Open: the project owner has to state the origin and that Corporal Jackie may distribute it** |
| `app-icon.png` | Made from `logo.png` (cropped and masked as a circle) | Follows `logo.png` |

If the origin of `logo.png` cannot be shown, the logo and the window icon are replaced before a release.

## Checked by CI

The Windows and macOS build scripts stop if FFmpeg or mpv turn out GPL (they are shipped as LGPL libraries), and each Windows run prints the exact files the package holds. Neither is a rule against GPL libraries: the project license is GPL-3.0-or-later. The aim is to know what is in a package. Still to add: a check that the printed file list matches the dependency register, and that every shipped library has its license file and source link in the package.
