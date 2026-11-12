# Changelog

All notable changes to **WinPreview** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.3.2] - 2026-11-11

### Design system & iconography
- Replaced every hand-drawn toolbar glyph with a single Lucide-derived vector set (24×24 grid, shared stroke weight, round caps) rendered from cached geometry.
- One icon language now covers navigation, zoom, rotate, crop, search, file info, settings, fullscreen, overflow, editor actions, media transport, file types and the HUD.
- Redesigned Settings: iconed tabs, left-aligned titles and descriptions, clearer grouping, and a solid readable modal surface.
- File Info panel: corrected header (no stray glyph), consistent close icon, simpler “Copy details” action.
- Consistent sentence-case copy across tooltips, menus, toasts, empty state, About and unsupported-file screens; removed robotic marketing strings.

### Interaction & motion
- Controls now start visible on open, fade after 2.5 seconds idle, reappear on any mouse movement, and toggle on canvas click — identically for images, PDFs, SVG, Office, fonts, Markdown, notebooks, code, spreadsheets, audio and video.
- Media: the transport HUD gained a "More actions" button (file info, open with, show in folder, copy path, loop, fullscreen, settings) and the video surface now forwards right-clicks to the same menu.
- The HUD now takes its layout from its real window size in device pixels, and drops optional controls (log, loop, volume) in priority order instead of clipping them.
- A `More actions` menu opens on button press and is dismissed by the next click, Escape or WM_CANCELMODE like a normal popup menu.
- A click that follows a movement reveal keeps the controls instead of hiding them instantly.
- The auto-hide timer stays armed until the fade completes (previously it parked early, so toolbars sometimes never faded).
- Toolbar pills slide 6 px while fading; panels, overlays and HUD keep their existing short ease-out motion.
- Settings is now present on every full toolbar; responsive overflow appears only when an action cannot fit.
- Removed the accidental “fewer actions at large window sizes” behaviour: full toolbars always expose crop/search/rotate/info/open-with/settings/fullscreen as appropriate.

### Fixes
- Folders, archives, certificates and shortcuts now share the same File info / Open with / Settings / Fullscreen toolbar as every other format, with an overflow menu when the window is narrow.
- PDF, code, spreadsheet, SVG, font, Markdown and notebook toolbars no longer clip their rightmost action.
- Fixed the file-info title drawing two bytes past the string literal.
- Consistent close affordances (Settings, File Info, Search, Update popup) use the same X icon.
- Corrected document/editor tooltips and context-menu labels to match the new wording; the context menu gained Open with, Show in folder and Copy path on every format.
- Certificate and shortcut detail rows use sentence case labels ("Issued to", "Signature algorithm", "Working directory").

## [1.3.1] - 2026-11-11

### Stability & data safety
- Text and spreadsheet previews now auto-save on close, Escape, Alt+F4, file switching and focus loss instead of discarding edits. Saves are written to a sibling temp file and atomically replace the original.
- Files above the 16 MB editing cap open read-only with a visible notice; editing, undo/redo, cut and paste are disabled so a truncated buffer can never overwrite the untouched tail.
- UTF-16 BE files round-trip correctly; a UTF-8 file cut at the 16 MB boundary no longer falls back to ANSI decoding.
- Image saving is atomic, reports failures, and supports in-place `Ctrl+S` without truncating the source.
- Fixed duplicated `Ctrl+X` / `Ctrl+V` / `Ctrl+Z` execution (WM_KEYDOWN plus WM_CHAR), which could cut an extra line.
- Typing in the PDF search box no longer triggers global shortcuts (Space, S, F, I, R…).
- Typing in the code/table editor no longer triggers global single-key shortcuts (F used to toggle fullscreen, Space paged the document, digits zoomed); Left/Right now move the caret, with Alt+Left/Right navigating files.
- Keystrokes no longer leak into the document behind the Settings modal or hotkey recorder.
- Save dialogs no longer cause close-on-focus-loss to dismiss the preview.

### Rendering & correctness
- Fixed PDFium being called concurrently from the UI thread and the raster worker (text selection, search, outline).
- Removed the detached PDF raster thread's use-after-free window; renders serialize with teardown via a shared lifetime token, and queued results cannot land on a newly opened document.
- Fixed fit-to-window no longer refitting after a resize; rotation keeps fit mode.
- Toast and update-banner sizes no longer double-scale on HiDPI displays.
- Restored previews, thumbnails, album art and PDF pages after a GPU device reset/RDP reconnect; the HUD and update popup recreate their render targets.
- CSV parsing is bounded (columns, rows, cell length); a delimiter flood can no longer exhaust memory or hang the renderer.
- Markdown/notebook files are capped at 32 MB; notebook markdown cells preserve UTF-8 instead of mojibake.
- PNG/JPEG save no longer leaves a zero-byte file after an encoder failure.

### Security & platform
- Update manifests now carry `installer_sha256` and `installer_size`; the updater verifies both before offering and again before launching the installer, rejects non-HTTPS URLs and malformed hashes.
- Update engine rewritten as a serialized background job queue; the UI thread never joins a live network job, and startup uses one throttled check.
- Removed the raw-`main` manifest fallback that could advertise an unreleased version.
- LibHEIF is loaded only from the application directory with `LOAD_LIBRARY_SEARCH_*` flags.
- Hotkey configuration is read from a lock-free POD snapshot; the hook thread performs no registry I/O; no duplicate keyboard hooks after a failed shutdown.
- Space is no longer hijacked in shell dialogs, the taskbar/notification area, toolbar buttons or rename/search fields.
- Exif SubIFD and PDF bookmark recursion are depth/cycle limited; engine buffers are bounds-checked.
- Clipboard copy/paste handles allocation and unterminated clipboard data safely.

### Build & release engineering
- `publish.ps1` now stages every required runtime DLL (HEIC/HEIF stack), fails on missing dependencies, syncs installer `VersionInfoVersion`, builds the portable ZIP, generates a SHA-256 release manifest, and runs the verification suite.
- Deterministic rebuilds (`--no-insert-timestamp`), section GC and a smaller 1.49 MB executable.
- New `tests\production_check.ps1` renders 16 document types plus UI states and validates version/package/crash integrity; CI runs it on every push/PR and publishes only verified artifacts.

## [1.3.0] - 2026-10-29
- Content-derived dynamic toolbar sizing across all document types, eliminating icon clipping/overflow.
- State persistence wipe on dismiss, prevention of accidental canvas activations, bottom-centered notifications.

## [1.2.9] - 2026-10-27
- Clean media showcase and click-to-toggle GUI.

## [1.2.8] - 2026-10-26
- Crash prevention, COM/RPC caching and rapid-interaction hardening.

## [1.2.7] - 2026-10-24
- Background-first service architecture and close-to-tray lifecycle.

## [1.2.6] / [1.2.5]
- Seamless left/right arrow folder navigation across all document types.

## [1.2.4] - 2026-10-20
- Lucide-style vector icon system across the app.

## [1.2.3] - 2026-10-19
- Removed decorative corner crosshairs from cards and panels.

## [1.2.2] - 2026-10-18
- Restored smooth vinyl closing/rotation animation.

## [1.2.1] - 2026-10-18
- 60 FPS vinyl animation, global `Ctrl+,` settings, dock cleanup.

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
