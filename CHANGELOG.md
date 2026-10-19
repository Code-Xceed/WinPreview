# Changelog

All notable changes to **WinPreview** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-10-19

### Added
- **Native Direct2D / D3D11 Engine**: Ultra-fast pure C++20 rendering pipeline achieving <35ms cold launches and <20MB idle memory.
- **Universal Folder Navigation**:
  - Left / Right arrow navigation (`←` / `→`) to switch between adjacent items in the same folder seamlessly.
  - Interactive toolbar pill capsule with Prev/Next buttons (`[ ◀ | ▶ ]`) and exact index indicator e.g. `(3/14)`.
- **Quick Open with Default App**:
  - `Enter` key shortcut and dedicated toolbar button (`[ ↗ ]`) to open the currently previewed file with its default system application.
- **Universal File Type Support**:
  - **Images**: PNG, JPG, JPEG, WEBP, GIF, TIFF, BMP, HEIC, ICO via hardware-accelerated WIC.
  - **Documents**: High-fidelity PDF rendering via Chromium PDFium engine with thumbnail sidebar support.
  - **Audio & Video**: Universal playback HUD with play/pause, scrub slider, and volume control.
  - **Code & Text**: Built-in DirectWrite text engine supporting syntax highlighting for 50+ languages, line numbers, search, and editable mini VS Code capabilities.
  - **Spreadsheets**: Automatic grid layout parser for CSV, TSV, TAB, and PSV pipe-delimited files.
  - **Vector Graphics**: Native Direct2D SVG rasterizer and DirectWrite font previewer (`.ttf`, `.otf`).
  - **System & Archives**: ZIP file inspection, Windows Registry script viewer, certificate viewer (`.cer`), and binary hex inspection.
- **Explorer Spacebar Hook**: Responsive background low-level hook with 30ms debouncing for instant previews from Windows Explorer and Desktop.
- **Windows Integration**:
  - Comprehensive Inno Setup installer script (`WinPreview.iss`).
  - Windows Explorer right-click context menu integration ("Preview with WinPreview").
  - `App Paths` registration allowing instant launch via `Win + R` or command line.
  - High-DPI auto-scaling and Windows 11 dark mode aesthetic.
