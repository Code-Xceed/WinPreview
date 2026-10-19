# WinPreview — Mini VS Code & Quick Look for Windows

WinPreview is a high-performance native Windows preview utility with an integrated **Mini VS Code text and source editor**.

## 🚀 Key Features

* **Instant Native Launch (<10ms)**: Written in modern C++20 with Direct2D and DirectWrite. Under 1MB binary footprint.
* **Mini VS Code Editor**:
  * Line numbering gutter with active line illumination.
  * DirectWrite monospace text rendering (`Cascadia Code` / `Consolas`).
  * 500ms blinking caret with fluid selection and word double-click.
  * Smart auto-indentation and Tab / Shift+Tab block indentation.
  * 100-level Undo/Redo stack (`Ctrl+Z`, `Ctrl+Y`).
  * Save in place on `Ctrl+S` with dirty tracking (`●`).
  * Full clipboard integration (`Ctrl+X`, `Ctrl+C`, `Ctrl+V`).
* **Mini Status Bar**:
  * Live `Ln X, Col Y` cursor position & selection count.
  * Clickable `Spaces: 4` / `Spaces: 2` toggle.
  * Clickable `Wrap: ON` / `Wrap: OFF`.
  * Encoding & line ending readouts (`UTF-8`, `CRLF`).
  * Clickable `● Modified (Ctrl+S)` save indicator.
* **Zero Disk Clutter**: Never generates unwanted log files or garbage files.

## ⌨️ Essential Shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Ctrl + S` | Save file directly to disk |
| `Ctrl + Z` | Undo last edit |
| `Ctrl + Y` / `Ctrl + Shift + Z` | Redo edit |
| `Ctrl + A` | Select all text |
| `Ctrl + C` | Copy selection or current line |
| `Ctrl + X` | Cut selection |
| `Ctrl + V` | Paste from clipboard |
| `Alt + Z` | Toggle word wrap |
| `Tab` / `Shift + Tab` | Indent / unindent line or block |
| `F11` | Fullscreen preview |
