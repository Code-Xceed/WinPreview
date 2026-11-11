# Third-Party Notices

WinPreview's own source code is licensed under the MIT License (see `LICENSE`).
The release package also contains the native components below. Their licenses
apply to those files, not to WinPreview's source.

| Component | File(s) | License | Upstream |
| :--- | :--- | :--- | :--- |
| PDFium | `pdfium.dll` | BSD-3-Clause | https://pdfium.googlesource.com/pdfium |
| libheif | `libheif-*.dll` | LGPL-3.0 | https://github.com/strukturag/libheif |
| libde265 (HEVC decode) | `libde265-*.dll` | LGPL-3.0 | https://github.com/strukturag/libde265 |
| x265 (HEVC encode) | `libx265-*.dll` | GPL-2.0 (or commercial x265 license) | https://www.x265.org |
| libstdc++ / libgcc / libwinpthread (LLVM-MinGW runtime) | `libstdc++-*.dll`, `libgcc_s_*.dll`, `libwinpthread-*.dll` | GPL-3.0 with GCC Runtime Library Exception | https://github.com/mstorsjo/llvm-mingw |
| Lucide icon geometry | compiled into the executable | ISC | https://lucide.dev |

## Important note about x265

The prebuilt LibHEIF DLL used by WinPreview links against `libx265`, the GPL-2.0
HEVC encoder. HEIC/HEIF decoding itself is performed by libde265; x265 is present
only because this particular LibHEIF build imports it unconditionally.

If you redistribute WinPreview (including the installer or portable ZIP), review
the GPL-2.0 obligations that come with shipping x265, or replace `libheif.dll`
with a build configured with `WITH_X265=OFF` (decode-only, LGPL) and remove
`libx265-*.dll`. A decode-only build is the recommended long-term distribution.

VLC/libVLC is **not** bundled. Media playback uses an existing VLC 3.x
installation when available; see https://www.videolan.org/legal.html.
