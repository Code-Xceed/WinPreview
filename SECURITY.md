# Security Policy

## Supported Versions

Only the latest release of WinPreview receives security updates and vulnerability patches.

| Version | Supported          |
| :------ | :----------------- |
| 1.3.x   | :white_check_mark: |
| < 1.3   | :x:                |

## Security Model

- Previews are rendered in-process from untrusted files. PDFium, LibHEIF/WIC decoders and PDF text extraction are treated as attack surface: page/file sizes are capped, recursive metadata (Exif SubIFDs, PDF bookmark trees) is depth-limited, archive/CSV structures are bounded, and text previews above 16 MB open read-only.
- The updater downloads only over HTTPS, requires the release manifest's SHA-256 and byte size to match the downloaded installer, rejects malformed manifests and non-HTTPS URLs, and re-verifies the hash immediately before execution.
- Update manifests are signed with ECDSA P-256 (SHA-256). The public key is pinned in the binary; the private key lives only in the CI secret store and is loaded from `%LOCALAPPDATA%\\WinPreview\\keys` for local builds. A manifest whose signature does not verify is rejected, and downloads are restricted to GitHub release hosts.
- The silent update handoff runs the installer through a separate helper invocation (`--apply-update`), which waits for the app to exit, records the installer exit code and relaunches the app; no update payload is ever executed from a user-writable search path.
- LibHEIF and PDFium are loaded only from the application directory (never from `%APPDATA%`, the current directory, or arbitrary user-writable DLL search paths).
- The Explorer keyboard hook never performs registry or file I/O inside the hook callback and reads a lock-free POD snapshot of the hotkey configuration.

## Reporting a Vulnerability

The WinPreview team takes the security and privacy of users seriously.

If you believe you have discovered a security vulnerability in WinPreview, please **do not report it on the public GitHub issue tracker**.

Instead, please send an advisory report to the maintainers or report a private vulnerability via [GitHub Security Advisories](https://github.com/Code-Xceed/WinPreview/security/advisories).

### What to Include in Your Report:
- A clear description of the vulnerability and its potential impact.
- Step-by-step reproduction instructions or a minimal proof of concept (PoC).
- The version of WinPreview and Windows build/architecture on which the issue was discovered.
- Any suggested mitigations or patches if available.

### Response Timeline:
- **Acknowledgement**: Within 48 hours.
- **Triage & Assessment**: Within 5 business days.
- **Fix & Public Advisory**: Coordinated with the reporter after testing and verification.
