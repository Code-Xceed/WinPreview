#pragma once
#ifndef WINPREVIEW_ARCHIVE_ENGINE_H
#define WINPREVIEW_ARCHIVE_ENGINE_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

namespace WinPreview {

// One row of an archive listing, path relative to the archive root.
struct ArchiveEntryInfo {
    std::wstring path;              // backslash separated, no leading separator
    bool isDirectory = false;
    bool encrypted = false;
    UINT64 sizeBytes = 0;
    UINT64 packedBytes = 0;
    FILETIME modified = {};
};

enum class ArchiveResult {
    Ok,
    Encrypted,      // header encrypted, or the listing needs a password
    Unsupported,    // the backend cannot read this container
    Failed
};

// Archive listing and single-stream expansion, backed by the bundled 7-Zip
// console (src\7z.exe + 7z.dll). The engine never extracts entries other than
// the single stream inside .gz/.bz2/.xz/.z/.lzma/.zst, which is expanded to a
// temporary .tar so the contents can be browsed.
class ArchiveEngine {
public:
    ArchiveEngine();
    ~ArchiveEngine();

    // True when 7z.exe can be found next to the running executable.
    bool IsAvailable();

    // Lists the entries of an archive. For a single compressed stream whose
    // payload is a tar, the returned entries belong to that inner tar and
    // displayName says so ("file.tar.gz › file.tar").
    ArchiveResult Load(const std::wstring& archivePath,
                       std::vector<ArchiveEntryInfo>& entries,
                       std::wstring& displayName,
                       std::wstring& errorText);

    // Deletes expanded temporaries this session created.
    void Cleanup();

    // Extra text for the badge, e.g. "PASSWORD PROTECTED".
    bool WasEncrypted() const { return m_encrypted; }

private:
    std::wstring m_backendPath;
    std::wstring m_workDir;
    std::vector<std::wstring> m_tempFiles;
    bool m_encrypted = false;
};

// True for containers that are a single compressed stream (optionally a tar).
bool IsSingleStreamArchive(const std::wstring& extension);

// Offline check for the engine: builds fixtures in %TEMP% with the bundled
// backend and asserts the listings. Returns a process exit code.
int RunArchiveSelfTest();

} // namespace WinPreview

#endif // WINPREVIEW_ARCHIVE_ENGINE_H
