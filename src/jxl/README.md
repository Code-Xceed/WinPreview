JPEG XL runtime - libjxl 0.13.0
================================

Vendored unmodified from the official upstream release
(https://github.com/libjxl/libjxl/releases/tag/v0.13.0,
`jxl-x64-windows.zip`). WinPreview loads `jxl.dll` at runtime, so JPEG XL works
without the Microsoft Store JPEG XL extension.

- `jxl.dll` decodes still images; `jxl_cms.dll` and the brotli DLLs are its
  dependencies.
- `msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` are the Microsoft
  Visual C++ runtime that the official binaries link, deployed app-local
  (permitted redistribution with the application).
- Licenses for libjxl, highway, brotli, skcms and the other components are in
  `licenses/`.
