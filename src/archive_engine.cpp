#include "archive_engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <memory>

namespace WinPreview {

namespace {

constexpr size_t kMaxListingBytes = 128ull * 1024 * 1024;
constexpr size_t kMaxEntries = 100000;
constexpr UINT64 kMaxExpandedBytes = 1024ull * 1024 * 1024;  // 1 GB tar expansion cap
constexpr DWORD kListTimeoutMs = 60000;
constexpr DWORD kExpandTimeoutMs = 600000;

std::wstring ExeDirectory() {
    WCHAR path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0) return L"";
    std::wstring full(path, len);
    size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : full.substr(0, slash);
}

std::wstring TempRoot() {
    WCHAR temp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, temp)) return L"";
    return std::wstring(temp) + L"WinPreview-archive";
}

void AppendLog(const std::wstring& text) {
    WCHAR localAppData[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) return;
    std::wstring dir = std::wstring(localAppData) + L"\\WinPreview\\logs";
    CreateDirectoryW((std::wstring(localAppData) + L"\\WinPreview").c_str(), NULL);
    CreateDirectoryW(dir.c_str(), NULL);
    std::wstring file = dir + L"\\archive.log";

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER size;
        size.LowPart = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        if (size.QuadPart > 128 * 1024) DeleteFileW(file.c_str());
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    WCHAR stamp[64];
    swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d:%02d ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    std::wstring line = std::wstring(stamp) + text + L"\r\n";
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), NULL, 0, NULL, NULL);
    if (bytes > 0) {
        std::string utf8((size_t)bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), &utf8[0], bytes, NULL, NULL);
        DWORD written = 0;
        WriteFile(hFile, utf8.data(), (DWORD)utf8.size(), &written, NULL);
    }
    CloseHandle(hFile);
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0);
    if (len <= 0) return L"";
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], len);
    return out;
}

std::wstring LowerExtension(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext;
}

// ---------------------------------------------------------------------------
// Child process helpers
// ---------------------------------------------------------------------------

struct PipeReader {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::string data;
};

void DrainPipe(PipeReader& reader, size_t cap) {
    if (reader.handle == INVALID_HANDLE_VALUE) return;
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(reader.handle, NULL, 0, NULL, &available, NULL)) return;
        if (available == 0) return;
        if (reader.data.size() >= cap) return;
        char buffer[8192];
        DWORD want = (DWORD)(std::min)((size_t)available, sizeof(buffer));
        DWORD read = 0;
        if (!ReadFile(reader.handle, buffer, want, &read, NULL) || read == 0) return;
        reader.data.append(buffer, read);
    }
}

struct ProcessOutput {
    std::string out;
    std::string err;
    DWORD exitCode = (DWORD)-1;
    bool timedOut = false;
    bool tooLarge = false;
};

// Runs the backend with no window, stdin on NUL and both output pipes drained.
bool RunCapture(const std::wstring& command, const std::wstring& workDir,
                DWORD timeoutMs, ProcessOutput& result) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outRead = NULL, outWrite = NULL, errRead = NULL, errWrite = NULL;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) return false;
    if (!CreatePipe(&errRead, &errWrite, &sa, 0)) {
        CloseHandle(outRead); CloseHandle(outWrite);
        return false;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, NULL);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = (nul != INVALID_HANDLE_VALUE) ? nul : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(0);

    BOOL started = CreateProcessW(NULL, cmd.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW,
                                  NULL, workDir.empty() ? NULL : workDir.c_str(), &si, &pi);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (!started) {
        CloseHandle(outRead);
        CloseHandle(errRead);
        return false;
    }

    PipeReader stdoutReader{outRead, {}};
    PipeReader stderrReader{errRead, {}};

    const ULONGLONG start = GetTickCount64();
    for (;;) {
        DrainPipe(stdoutReader, kMaxListingBytes);
        DrainPipe(stderrReader, kMaxListingBytes);
        if (stdoutReader.data.size() >= kMaxListingBytes || stderrReader.data.size() >= kMaxListingBytes) {
            result.tooLarge = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
        if (WaitForSingleObject(pi.hProcess, 15) == WAIT_OBJECT_0) break;
        if (GetTickCount64() - start > timeoutMs) {
            result.timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }
    DrainPipe(stdoutReader, kMaxListingBytes);
    DrainPipe(stderrReader, kMaxListingBytes);
    WaitForSingleObject(pi.hProcess, 2000);

    DWORD exitCode = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = exitCode;
    result.out = std::move(stdoutReader.data);
    result.err = std::move(stderrReader.data);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(outRead);
    CloseHandle(errRead);
    return true;
}

// Runs the backend and writes stdout straight into a file (used to expand a tar
// stream). Aborts when the payload exceeds maxBytes.
bool RunToFile(const std::wstring& command, const std::wstring& workDir,
               const std::wstring& targetFile, UINT64 maxBytes, DWORD timeoutMs,
               DWORD& exitCode) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outRead = NULL, outWrite = NULL;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) return false;
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE errRead = NULL, errWrite = NULL;
    if (!CreatePipe(&errRead, &errWrite, &sa, 0)) {
        CloseHandle(outRead); CloseHandle(outWrite);
        return false;
    }
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE target = CreateFileW(targetFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (target == INVALID_HANDLE_VALUE) {
        CloseHandle(outRead); CloseHandle(outWrite);
        CloseHandle(errRead); CloseHandle(errWrite);
        return false;
    }

    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, NULL);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = (nul != INVALID_HANDLE_VALUE) ? nul : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmd(command.begin(), command.end());
    cmd.push_back(0);
    BOOL started = CreateProcessW(NULL, cmd.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW,
                                  NULL, workDir.empty() ? NULL : workDir.c_str(), &si, &pi);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (!started) {
        CloseHandle(outRead); CloseHandle(errRead); CloseHandle(target);
        return false;
    }

    PipeReader stderrReader{errRead, {}};
    UINT64 written = 0;
    bool overflow = false;
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(outRead, NULL, 0, NULL, &available, NULL) && available > 0) {
            char buffer[65536];
            DWORD want = (DWORD)(std::min)((size_t)available, sizeof(buffer));
            DWORD read = 0;
            if (ReadFile(outRead, buffer, want, &read, NULL) && read > 0) {
                DWORD wrote = 0;
                WriteFile(target, buffer, read, &wrote, NULL);
                written += read;
                if (written > maxBytes) {
                    overflow = true;
                    TerminateProcess(pi.hProcess, 1);
                    break;
                }
            }
        }
        DrainPipe(stderrReader, 64 * 1024);
        if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) break;
        if (GetTickCount64() - start > timeoutMs) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(outRead, NULL, 0, NULL, &available, NULL) || available == 0) break;
        char buffer[65536];
        DWORD want = (DWORD)(std::min)((size_t)available, sizeof(buffer));
        DWORD read = 0;
        if (!ReadFile(outRead, buffer, want, &read, NULL) || read == 0) break;
        DWORD wrote = 0;
        WriteFile(target, buffer, read, &wrote, NULL);
        written += read;
    }
    WaitForSingleObject(pi.hProcess, 2000);

    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    exitCode = code;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(outRead);
    CloseHandle(errRead);
    CloseHandle(target);
    return !overflow && code == 0;
}

// ---------------------------------------------------------------------------
// Listing parser
// ---------------------------------------------------------------------------

FILETIME ParseTimestamp(const std::wstring& value) {
    SYSTEMTIME st = {};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (swscanf_s(value.c_str(), L"%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return FILETIME{};
    }
    if (year < 1601 || month < 1 || month > 12 || day < 1 || day > 31) return FILETIME{};
    st.wYear = (WORD)year;
    st.wMonth = (WORD)month;
    st.wDay = (WORD)day;
    st.wHour = (WORD)hour;
    st.wMinute = (WORD)minute;
    st.wSecond = (WORD)second;
    FILETIME local = {};
    if (!SystemTimeToFileTime(&st, &local)) return FILETIME{};
    FILETIME utc = {};
    LocalFileTimeToFileTime(&local, &utc);
    return utc;
}

std::wstring NormalizeEntryPath(const std::wstring& raw, bool& isDirectory) {
    std::wstring path = raw;
    std::replace(path.begin(), path.end(), L'/', L'\\');
    while (!path.empty() && path.front() == L'\\') path.erase(path.begin());
    if (!path.empty() && path.back() == L'\\') {
        isDirectory = true;
        path.pop_back();
    }
    return path;
}

size_t ParseListing(const std::string& utf8, std::vector<ArchiveEntryInfo>& entries, bool& anyEncrypted) {
    std::wstring text = Utf8ToWide(utf8);
    ArchiveEntryInfo current;
    bool havePath = false;
    bool folderFlag = false;
    bool dirByAttr = false;
    bool encrypted = false;

    auto flush = [&]() {
        if (havePath && !current.path.empty()) {
            current.isDirectory = current.isDirectory || folderFlag || dirByAttr;
            current.encrypted = encrypted;
            if (current.isDirectory) current.sizeBytes = 0;
            entries.push_back(current);
            if (encrypted) anyEncrypted = true;
        }
        current = ArchiveEntryInfo{};
        havePath = false;
        folderFlag = false;
        dirByAttr = false;
        encrypted = false;
    };

    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        start = end + 1;

        if (line.empty()) {
            flush();
            if (entries.size() >= kMaxEntries) break;
            continue;
        }
        size_t sep = line.find(L" = ");
        if (sep == std::wstring::npos) continue;
        std::wstring key = line.substr(0, sep);
        std::wstring value = line.substr(sep + 3);

        if (key == L"Path") {
            if (havePath) flush();
            current.path = NormalizeEntryPath(value, current.isDirectory);
            havePath = !current.path.empty();
        } else if (key == L"Folder") {
            folderFlag = (value.find(L'+') != std::wstring::npos);
        } else if (key == L"Attributes") {
            if (value.find(L'D') != std::wstring::npos) dirByAttr = true;
        } else if (key == L"Size") {
            current.sizeBytes = _wcstoui64(value.c_str(), NULL, 10);
        } else if (key == L"Packed Size") {
            current.packedBytes = _wcstoui64(value.c_str(), NULL, 10);
        } else if (key == L"Modified") {
            current.modified = ParseTimestamp(value);
        } else if (key == L"Encrypted") {
            encrypted = (value.find(L'+') != std::wstring::npos);
        }
    }
    flush();

    // A listing that only repeats the archive itself is not useful.
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [](const ArchiveEntryInfo& e) { return e.path.empty(); }),
                  entries.end());
    return entries.size();
}

bool LooksLikeTarName(const std::wstring& name) {
    std::wstring lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    return lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".tar") == 0;
}

} // namespace

bool IsSingleStreamArchive(const std::wstring& extension) {
    static const wchar_t* kStreams[] = {
        L".gz", L".gzip", L".bz2", L".bzip2", L".xz", L".z", L".lzma", L".zst", L".tgz", L".tbz2", L".txz"
    };
    for (const wchar_t* ext : kStreams) {
        if (_wcsicmp(extension.c_str(), ext) == 0) return true;
    }
    return false;
}

ArchiveEngine::ArchiveEngine() {}

ArchiveEngine::~ArchiveEngine() {
    Cleanup();
}

void ArchiveEngine::Cleanup() {
    for (const auto& file : m_tempFiles) {
        DeleteFileW(file.c_str());
    }
    m_tempFiles.clear();
    if (!m_workDir.empty()) {
        RemoveDirectoryW(m_workDir.c_str());
        m_workDir.clear();
    }
    m_encrypted = false;
}

bool ArchiveEngine::IsAvailable() {
    if (!m_backendPath.empty()) return true;
    const std::wstring dir = ExeDirectory();
    if (dir.empty()) return false;
    std::wstring candidate = dir + L"\\7z.exe";
    if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
        candidate = dir + L"\\7za.exe";
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
            AppendLog(L"backend missing next to " + dir);
            return false;
        }
    }
    m_backendPath = candidate;
    return true;
}

ArchiveResult ArchiveEngine::Load(const std::wstring& archivePath,
                                  std::vector<ArchiveEntryInfo>& entries,
                                  std::wstring& displayName,
                                  std::wstring& errorText) {
    entries.clear();
    errorText.clear();
    m_encrypted = false;

    size_t slash = archivePath.find_last_of(L"\\/");
    displayName = (slash == std::wstring::npos) ? archivePath : archivePath.substr(slash + 1);

    if (!IsAvailable()) {
        errorText = L"The archive reader is missing from this installation.";
        return ArchiveResult::Unsupported;
    }
    if (GetFileAttributesW(archivePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        errorText = L"The file could not be opened.";
        return ArchiveResult::Failed;
    }

    const std::wstring extension = LowerExtension(archivePath);
    std::wstring command = L"\"" + m_backendPath + L"\" l -slt -ba -sccUTF-8 -bd \"" + archivePath + L"\"";

    ProcessOutput output;
    if (!RunCapture(command, L"", kListTimeoutMs, output)) {
        errorText = L"The archive reader could not be started.";
        return ArchiveResult::Failed;
    }
    if (output.timedOut) {
        errorText = L"Reading this archive took too long.";
        return ArchiveResult::Failed;
    }
    if (output.tooLarge) {
        errorText = L"This archive has too many entries to show.";
        return ArchiveResult::Unsupported;
    }

    bool anyEncrypted = false;
    size_t count = ParseListing(output.out, entries, anyEncrypted);

    if (output.exitCode != 0) {
        std::wstring err = Utf8ToWide(output.err + "\n" + output.out);
        std::wstring lower = err;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        const bool password = lower.find(L"password") != std::wstring::npos;
        entries.clear();
        if (password) {
            m_encrypted = true;
            errorText = L"This archive is password protected.";
            AppendLog(L"encrypted header: " + archivePath);
            return ArchiveResult::Encrypted;
        }
        if (err.find(L"Can not open the file as archive") != std::wstring::npos ||
            err.find(L"Cannot open the file as") != std::wstring::npos ||
            err.find(L"Is not archive") != std::wstring::npos) {
            errorText = L"This archive format or compression method is not supported.";
            AppendLog(L"unsupported: " + archivePath + L" / " + err);
            return ArchiveResult::Unsupported;
        }
        errorText = L"This archive could not be read.";
        AppendLog(L"failed: " + archivePath + L" / " + err);
        return ArchiveResult::Failed;
    }

    // .bz2/.xz listings carry no name at all; derive the inner file name from
    // the container so the row (and the tar expansion) still works.
    if (count == 0 && IsSingleStreamArchive(extension)) {
        const std::wstring file = displayName;
        std::wstring base = file;
        size_t dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos) base = base.substr(0, dot);
        std::wstring inner;
        if (_wcsicmp(extension.c_str(), L".tgz") == 0 ||
            _wcsicmp(extension.c_str(), L".tbz2") == 0 ||
            _wcsicmp(extension.c_str(), L".txz") == 0) {
            inner = base + L".tar";
        } else {
            inner = base;
        }
        ArchiveEntryInfo synthesized;
        synthesized.path = inner;
        entries.push_back(synthesized);
        count = 1;
    }

    if (count == 0) {
        errorText = L"This archive is empty.";
        return ArchiveResult::Ok;
    }

    m_encrypted = anyEncrypted;

    // Single compressed stream: expand the payload once so its contents can be
    // browsed (a .tar.gz shows the files inside the tar, like 7-Zip does).
    if (IsSingleStreamArchive(extension) && count == 1 && !entries[0].isDirectory) {
        const std::wstring inner = entries[0].path;
        if (LooksLikeTarName(inner) || extension == L".tgz" || extension == L".tbz2" || extension == L".txz") {
            if (m_workDir.empty()) {
                ULONGLONG stamp = GetTickCount64();
                m_workDir = TempRoot() + L"\\" + std::to_wstring(stamp);
                CreateDirectoryW(TempRoot().c_str(), NULL);
                CreateDirectoryW(m_workDir.c_str(), NULL);
            }
            std::wstring expanded = m_workDir + L"\\stream.tar";
            std::wstring extract = L"\"" + m_backendPath + L"\" e -so -bd -y -sccUTF-8 \"" + archivePath + L"\"";

            DWORD code = (DWORD)-1;
            if (RunToFile(extract, L"", expanded, kMaxExpandedBytes, kExpandTimeoutMs, code)) {
                std::vector<ArchiveEntryInfo> innerEntries;
                std::wstring innerDisplay;
                std::wstring innerError;
                ArchiveResult innerResult = Load(expanded, innerEntries, innerDisplay, innerError);
                m_encrypted = anyEncrypted;
                if (innerResult == ArchiveResult::Ok && !innerEntries.empty()) {
                    m_tempFiles.push_back(expanded);
                    entries = std::move(innerEntries);
                    displayName += L" \u203a " + inner;
                    AppendLog(L"expanded stream: " + archivePath + L" -> " + inner + L" (" +
                              std::to_wstring(entries.size()) + L" entries)");
                } else {
                    DeleteFileW(expanded.c_str());
                }
            } else {
                DeleteFileW(expanded.c_str());
            }
        }
    }

    if (entries.size() >= kMaxEntries) {
        errorText = L"Only the first " + std::to_wstring(kMaxEntries) + L" entries are shown.";
    }
    return ArchiveResult::Ok;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

namespace {

struct SelfTest {
    int passed = 0;
    int failed = 0;
    void Check(bool condition, const std::wstring& label, const std::wstring& detail = L"") {
        if (condition) {
            ++passed;
            wprintf(L" [PASS] %ls\n", label.c_str());
        } else {
            ++failed;
            wprintf(L" [FAIL] %ls", label.c_str());
            if (!detail.empty()) wprintf(L"\n        %ls", detail.c_str());
            wprintf(L"\n");
        }
    }
};

bool HasEntry(const std::vector<ArchiveEntryInfo>& entries, const wchar_t* path) {
    for (const auto& entry : entries) {
        if (_wcsicmp(entry.path.c_str(), path) == 0) return true;
    }
    return false;
}

void WriteTextFile(const std::wstring& path, const char* text) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(hFile, text, (DWORD)strlen(text), &written, NULL);
    CloseHandle(hFile);
}

} // namespace

int RunArchiveSelfTest() {
    ArchiveEngine engine;
    if (!engine.IsAvailable()) {
        wprintf(L" [FAIL] Archive backend not found next to the executable\n");
        return 1;
    }

    WCHAR temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    std::wstring root = std::wstring(temp) + L"WinPreview-archive-selftest";
    std::wstring tree = root + L"\\src";
    std::wstring docs = tree + L"\\docs";
    std::wstring img = tree + L"\\img";

    // Rebuild the fixture tree every run so results are deterministic.
    std::wstring cleanup = L"cmd /c rmdir /s /q \"" + root + L"\" >nul 2>&1";
    ProcessOutput ignored;
    RunCapture(cleanup, L"", 20000, ignored);

    CreateDirectoryW(root.c_str(), NULL);
    CreateDirectoryW(tree.c_str(), NULL);
    CreateDirectoryW(docs.c_str(), NULL);
    CreateDirectoryW(img.c_str(), NULL);
    WriteTextFile(tree + L"\\readme.txt", "hello archive");
    WriteTextFile(docs + L"\\guide.md", "# guide\n");
    WriteTextFile(tree + L"\\\u00fcn\u00efcode.txt", "unicode name");
    {
        // A little binary payload so size/packed-size values are non-trivial.
        std::string binary;
        for (int i = 0; i < 4096; ++i) binary.push_back((char)(i * 31 + 7));
        HANDLE hFile = CreateFileW((img + L"\\photo.bin").c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hFile, binary.data(), (DWORD)binary.size(), &written, NULL);
            CloseHandle(hFile);
        }
    }

    // The backend path is private; rebuild it the same way IsAvailable does.
    std::wstring z = ExeDirectory() + L"\\7z.exe";
    if (GetFileAttributesW(z.c_str()) == INVALID_FILE_ATTRIBUTES) z = ExeDirectory() + L"\\7za.exe";

    struct Fixture {
        const wchar_t* file;
        const wchar_t* type;
        bool addSwitch;
    };
    Fixture fixtures[] = {
        { L"a.zip", L"-tzip", true },
        { L"a.7z", L"-t7z", true },
        { L"a.tar", L"-ttar", true },
    };
    for (const auto& fixture : fixtures) {
        std::wstring out = root + L"\\" + fixture.file;
        std::wstring cmd = L"\"" + z + L"\" a " + fixture.type + L" \"" + out + L"\" \"" + tree + L"\" -r -y";
        ProcessOutput add;
        RunCapture(cmd, L"", 120000, add);
    }
    {
        // tar family wrapped in one compression stream
        struct Wrap { const wchar_t* file; const wchar_t* type; };
        Wrap wraps[] = { { L"a.tar.gz", L"-tgzip" }, { L"a.tar.bz2", L"-tbzip2" }, { L"a.tar.xz", L"-txz" } };
        for (const auto& wrap : wraps) {
            std::wstring cmd = L"\"" + z + L"\" a " + wrap.type + L" \"" + root + L"\\" + wrap.file +
                               L"\" \"" + root + L"\\a.tar\" -y";
            ProcessOutput add;
            RunCapture(cmd, L"", 120000, add);
        }
    }
    {
        std::wstring enc = root + L"\\enc.zip";
        std::wstring cmd = L"\"" + z + L"\" a -tzip -psecret \"" + enc + L"\" \"" + tree + L"\" -r -y";
        ProcessOutput add;
        RunCapture(cmd, L"", 120000, add);
    }
    {
        std::wstring hdr = root + L"\\hdr.7z";
        std::wstring cmd = L"\"" + z + L"\" a -t7z -psecret -mhe=on \"" + hdr + L"\" \"" + tree + L"\" -r -y";
        ProcessOutput add;
        RunCapture(cmd, L"", 120000, add);
    }
    WriteTextFile(root + L"\\broken.zip", "this is not an archive at all");

    SelfTest test;
    struct Case { const wchar_t* file; const wchar_t* label; ArchiveResult expected; };
    Case cases[] = {
        { L"a.zip", L"ZIP lists nested files", ArchiveResult::Ok },
        { L"a.7z", L"7z lists nested files", ArchiveResult::Ok },
        { L"a.tar", L"TAR lists nested files", ArchiveResult::Ok },
        { L"a.tar.gz", L"tar.gz expands to the tar contents", ArchiveResult::Ok },
        { L"a.tar.bz2", L"tar.bz2 expands to the tar contents", ArchiveResult::Ok },
        { L"a.tar.xz", L"tar.xz expands to the tar contents", ArchiveResult::Ok },
        { L"enc.zip", L"Encrypted ZIP lists names and flags entries", ArchiveResult::Ok },
        { L"hdr.7z", L"Header-encrypted 7z reports password state", ArchiveResult::Encrypted },
        { L"broken.zip", L"Corrupt file reports an unsupported container", ArchiveResult::Unsupported },
    };

    for (const auto& item : cases) {
        std::vector<ArchiveEntryInfo> entries;
        std::wstring display, error;
        ArchiveResult result = engine.Load(root + L"\\" + item.file, entries, display, error);
        bool ok = (result == item.expected);
        if (ok && result == ArchiveResult::Ok && item.expected == ArchiveResult::Ok) {
            ok = HasEntry(entries, L"src\\docs\\guide.md") && HasEntry(entries, L"src\\readme.txt");
        }
        test.Check(ok, item.label,
                   L"result=" + std::to_wstring((int)result) + L" entries=" + std::to_wstring(entries.size()) +
                   L" error=" + error);
    }

    {
        std::vector<ArchiveEntryInfo> entries;
        std::wstring display, error;
        engine.Load(root + L"\\a.tar.gz", entries, display, error);
        test.Check(display.find(L"\u203a") != std::wstring::npos, L"tar.gz display name marks the inner tar", display);
    }
    {
        std::vector<ArchiveEntryInfo> entries;
        std::wstring display, error;
        engine.Load(root + L"\\a.zip", entries, display, error);
        bool unicode = false, hasDir = false, hasSize = false;
        for (const auto& entry : entries) {
            if (entry.path == L"src\\\u00fcn\u00efcode.txt") unicode = true;
            if (entry.path == L"src\\docs" && entry.isDirectory) hasDir = true;
            if (entry.path == L"src\\readme.txt" && entry.sizeBytes > 0 && entry.modified.dwLowDateTime != 0) hasSize = true;
        }
        test.Check(unicode, L"Unicode entry names survive the listing");
        test.Check(hasDir, L"Directory entries are flagged");
        test.Check(hasSize, L"Sizes and timestamps are parsed");
    }
    {
        std::vector<ArchiveEntryInfo> entries;
        std::wstring display, error;
        engine.Load(root + L"\\enc.zip", entries, display, error);
        bool flagged = false;
        for (const auto& entry : entries) {
            if (entry.encrypted) flagged = true;
        }
        test.Check(flagged && engine.WasEncrypted(), L"Encrypted entries are flagged for the badge");
    }

    engine.Cleanup();
    RunCapture(L"cmd /c rmdir /s /q \"" + root + L"\" >nul 2>&1", L"", 20000, ignored);

    wprintf(L"\nArchive engine: %d passed, %d failed\n", test.passed, test.failed);
    return test.failed == 0 ? 0 : 1;
}

} // namespace WinPreview
