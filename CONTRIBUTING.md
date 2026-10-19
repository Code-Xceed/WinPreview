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

Run the test suite before submitting pull requests:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1
```

---

## 📐 Coding Standards & Guidelines

- **Language**: Modern C++20 (`/std:c++20` or `-std=c++20`).
- **Zero Overhead Rule**: WinPreview is designed for instant cold launch (<35ms) and low memory (<20MB RAM). Avoid introducing heavy external frameworks or runtime garbage collectors.
- **Resource Management**: Use RAII (`Microsoft::WRL::ComPtr` or standard smart pointers) for all COM interfaces (Direct2D, DirectWrite, WIC, MediaEngine). Never leave manual raw pointers or leaked handles.
- **DPI Awareness**: All coordinate calculations must account for the display DPI scale (`dpi / 96.0f`).
- **Formatting**: Adhere to the provided `.clang-format` and `.editorconfig`.

---

## 🔄 Pull Request Process

1. **Fork the repo** and create your branch from `main` (`feature/amazing-feature` or `fix/issue-description`).
2. If you added code, make sure to add or update relevant tests in `tests/`.
3. Verify `build.bat` compiles cleanly with zero warnings.
4. Verify `tests\run_tests.ps1` passes all checks.
5. Commit using descriptive [Conventional Commits](https://www.conventionalcommits.org/) (`feat: ...`, `fix: ...`, `perf: ...`, `docs: ...`).
6. Push to your fork and submit a Pull Request to `Code-Xceed/WinPreview`.

Thank you for helping make WinPreview the fastest preview experience on Windows!
