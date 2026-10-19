# WinPreview Test Suite & Quality Verification

This directory contains automated regression tests, interactive editor tests, and diagnostic scripts for validating WinPreview's native performance, memory safety, and rendering pipelines.

## 📁 Directory Structure

| Script / Folder | Purpose |
| :--- | :--- |
| `run_tests.ps1` | **Automated Smoke Test Suite**: Verifies binary architecture (x64 PE header), dependencies (`pdfium.dll`), sample assets, cold start launch, and strict memory budget (<50MB RAM). |
| `test_editor.ps1` | **Interactive Text Editor Verification**: Tests DirectWrite text rendering, real-time code typing, line counting, keyboard shortcuts (`Ctrl+S`, `Ctrl+Z`), and file saves. |
| `test_caret_and_context_menu.ps1` | **Editor Interaction & Context Menu**: Validates caret positioning, mouse clicking, text selection highlight, and native dark-themed context menu (`Copy`, `Cut`, `Paste`, `Select All`). |
| `test_runner.ps1` | **Visual Screenshot Capture Harness**: Automates headless/visual rendering captures across different file types (Code, Spreadsheets, Media). |
| `diagnostics/` | Internal diagnostic scripts used to inspect Windows Explorer UIAutomation handles, window class trees, and foreground thread state. |
| `sales_report.csv` | Sample tabular test fixture. |
| `sample.py` | Sample Python syntax-highlighted code test fixture. |

## 🚀 Running the Tests

To run the automated smoke test suite:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1
```

All tests execute in clean sub-processes, respect process sandboxing, and verify that zero leaked handles or unexpected log files remain on disk.
