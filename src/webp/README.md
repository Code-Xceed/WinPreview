libwebp 1.5.0 - decoder subset
================================

Vendored from the official release tarball
(https://github.com/webmproject/libwebp/releases/tag/v1.5.0), unmodified, so
WinPreview can decode WebP even when the machine has no WebP codec installed.

Only the decoder paths are compiled (see src/build.bat): src/dec, the decoder
parts of src/dsp (C plus the SSE2 variants), and src/utils. Encoders, muxers and
demuxers are present in the tree but not built.

License: BSD 3-Clause (COPYING) plus the additional IP rights grant in PATENTS.
