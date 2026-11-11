# Contributing to WinPreview

First off, thank you for considering contributing to **WinPreview**! Projects like this thrive because of community input, bug reports, and code contributions.

## 🛠️ Development Environment

### Prerequisites
1. **Windows 10 / 11 (64-bit)**
2. **C++20 Compiler**:
   - [LLVM-MinGW / Clang](https://github.com/mstorsjo/llvm-mingw) (recommended) or MSVC (Visual Studio 2022 v17.0+ with Windows SDK 10.0.19041+)
3. **Optional Packaging Tools**:
   - [Inno Setup 6.2+](https://jrsoftware.org/isinfo.php) to compile installer packages (`WinPreview-Setup-x64.exe`).

### Building from Source

To compile the native release binary:

```cmd
# One-click build from root
build.bat
```

Or from PowerShell:
```powershell
.\publish.ps1
```

The compiled binary will be placed at `dist\WinPreview\WinPreview.exe`.

### Running Tests

Run the full production verification suite before submitting pull requests:

```powershell
powershell -ExecutionPolicy Bypass -File tests\production_check.ps1 -PackageDir dist\WinPreview
```

This covers version consistency, package integrity and a real render smoke test for every document engine. `tests\run_tests.ps1` remains available for a fast binary/memory sanity check.

---

## 📐 Coding Standards & Guidelines

- **Language**: Modern C++20 (`/std:c++20` or `-std=c++20`).
- **Zero Overhead Rule**: WinPreview targets a ~40 ms background-service launch, a ~150 ms cold path to first preview, and a ~25–40 MB private working set. Avoid introducing heavy external frameworks or runtime garbage collectors.
- **Resource Management**: Use RAII (`Microsoft::WRL::ComPtr` or standard smart pointers) for all COM interfaces (Direct2D, DirectWrite, WIC, MediaEngine). Never leave manual raw pointers or leaked handles.
- **DPI Awareness**: Direct2D render targets are configured with the monitor DPI, so drawing coordinates are already DIPs. Do **not** multiply layout constants by `dpiScale` a second time; reserve scaling for raw Win32 pixel sizes (`SetWindowPos`, window rectangles) and `PxToDip`/`DipToPx` conversions.
- **Formatting**: Adhere to the provided `.clang-format` and `.editorconfig`.

---

## 🔄 Pull Request Process

1. **Fork the repo** and create your branch from `main` (`feature/amazing-feature` or `fix/issue-description`).
2. If you added code, make sure to add or update relevant tests in `tests/`.
3. Verify `build.bat` compiles cleanly (no new compiler errors or warnings in files you touched).
4. Verify `tests\run_tests.ps1` passes all checks.
5. Commit using descriptive [Conventional Commits](https://www.conventionalcommits.org/) (`feat: ...`, `fix: ...`, `perf: ...`, `docs: ...`).
6. Push to your fork and submit a Pull Request to `Code-Xceed/WinPreview`.

Thank you for helping make WinPreview the fastest preview experience on Windows!
