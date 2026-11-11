#include "update_engine.h"
#include <wincrypt.h>
#include <wininet.h>
#include <urlmon.h>
#include <shellapi.h>
#include <shlobj.h>
#include <softpub.h>
#include <algorithm>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace WinPreview {

static const wchar_t* UPDATE_META_URL = L"https://github.com/Code-Xceed/WinPreview/releases/latest/download/version.json";
static const wchar_t* DEFAULT_INSTALLER_URL = L"https://github.com/Code-Xceed/WinPreview/releases/latest/download/WinPreview-Setup-x64.exe";
static const wchar_t* USER_AGENT = L"WinPreview-AutoUpdater/1.1 (Windows NT; x64)";

// Bounded network waits so shutdown never hangs indefinitely behind a dead socket.
static const DWORD NET_CONNECT_TIMEOUT_MS = 8000;
static const DWORD NET_RECV_TIMEOUT_MS = 10000;

static const long long MIN_INSTALLER_BYTES = 500000;

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    if (count <= 0) return L"";
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], count);
    return wstr;
}

static std::wstring ExtractJsonString(const std::string& json, const std::string& field) {
    std::string needle = "\"" + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return L"";
    pos = json.find(':', pos + needle.length());
    if (pos == std::string::npos) return L"";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return L"";
    size_t endQuote = json.find('"', pos + 1);
    if (endQuote == std::string::npos) return L"";
    return Utf8ToWide(json.substr(pos + 1, endQuote - (pos + 1)));
}

static long long ExtractJsonInteger(const std::string& json, const std::string& field, long long fallback = 0) {
    std::string needle = "\"" + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + needle.length());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) ++pos;
    if (pos >= json.size() || json[pos] < '0' || json[pos] > '9') return fallback;
    long long value = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        value = value * 10 + (json[pos] - '0');
        ++pos;
    }
    return value;
}

static std::wstring NormalizeSha256(const std::wstring& value) {
    if (value.size() != 64) return L"";
    std::wstring out;
    out.reserve(64);
    for (wchar_t c : value) {
        if (!iswxdigit(c)) return L"";
        out.push_back((wchar_t)towlower(c));
    }
    return out;
}

static bool IsHttpsUrl(const std::wstring& url) {
    return url.size() > 8 && _wcsnicmp(url.c_str(), L"https://", 8) == 0;
}

static bool IsVersionGreater(const std::wstring& remote, const std::wstring& current) {
    int rMaj = 0, rMin = 0, rPatch = 0, rBuild = 0;
    int cMaj = 0, cMin = 0, cPatch = 0, cBuild = 0;

    const wchar_t* rPtr = remote.c_str();
    while (*rPtr == L'v' || *rPtr == L'V' || *rPtr == L' ') rPtr++;
    swscanf_s(rPtr, L"%d.%d.%d.%d", &rMaj, &rMin, &rPatch, &rBuild);

    const wchar_t* cPtr = current.c_str();
    while (*cPtr == L'v' || *cPtr == L'V' || *cPtr == L' ') cPtr++;
    swscanf_s(cPtr, L"%d.%d.%d.%d", &cMaj, &cMin, &cPatch, &cBuild);

    if (rMaj != cMaj) return rMaj > cMaj;
    if (rMin != cMin) return rMin > cMin;
    if (rPatch != cPatch) return rPatch > cPatch;
    return rBuild > cBuild;
}

static std::wstring QueryModuleFileVersion() {
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) {
        return WINPREVIEW_VERSION_WSTR;
    }

    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (size == 0) {
        return WINPREVIEW_VERSION_WSTR;
    }

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path, handle, size, data.data())) {
        return WINPREVIEW_VERSION_WSTR;
    }

    VS_FIXEDFILEINFO* pFileInfo = nullptr;
    UINT len = 0;
    if (VerQueryValueW(data.data(), L"\\", (LPVOID*)&pFileInfo, &len) && len >= sizeof(VS_FIXEDFILEINFO) && pFileInfo) {
        int maj = HIWORD(pFileInfo->dwProductVersionMS);
        int min = LOWORD(pFileInfo->dwProductVersionMS);
        int patch = HIWORD(pFileInfo->dwProductVersionLS);
        int build = LOWORD(pFileInfo->dwProductVersionLS);
        WCHAR verStr[64];
        if (build > 0) {
            swprintf_s(verStr, L"%d.%d.%d.%d", maj, min, patch, build);
        } else {
            swprintf_s(verStr, L"%d.%d.%d", maj, min, patch);
        }
        return verStr;
    }
    return WINPREVIEW_VERSION_WSTR;
}

static bool HashFileSha256(const std::wstring& path, std::wstring& outHex) {
    outHex.clear();

    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        return false;
    }

    HCRYPTHASH hHash = 0;
    bool ok = false;
    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            ok = true;
            std::vector<BYTE> buffer(128 * 1024);
            for (;;) {
                DWORD bytesRead = 0;
                if (!ReadFile(hFile, buffer.data(), (DWORD)buffer.size(), &bytesRead, NULL)) {
                    ok = false;
                    break;
                }
                if (bytesRead == 0) break;
                if (!CryptHashData(hHash, buffer.data(), bytesRead, 0)) {
                    ok = false;
                    break;
                }
            }
            CloseHandle(hFile);
        }

        if (ok) {
            BYTE digest[32] = {};
            DWORD digestLen = sizeof(digest);
            if (CryptGetHashParam(hHash, HP_HASHVAL, digest, &digestLen, 0) && digestLen == sizeof(digest)) {
                static const wchar_t* kHex = L"0123456789abcdef";
                outHex.reserve(64);
                for (BYTE b : digest) {
                    outHex.push_back(kHex[b >> 4]);
                    outHex.push_back(kHex[b & 0x0F]);
                }
            } else {
                ok = false;
            }
        }
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
    return ok;
}

static void ApplyWinInetTimeouts(HINTERNET hInternet) {
    if (!hInternet) return;
    DWORD connectTimeout = NET_CONNECT_TIMEOUT_MS;
    DWORD recvTimeout = NET_RECV_TIMEOUT_MS;
    InternetSetOptionW(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
    InternetSetOptionW(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &recvTimeout, sizeof(recvTimeout));
}

UpdateEngine::UpdateEngine() {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    m_info.currentVersion = QueryModuleFileVersion();
}

UpdateEngine::~UpdateEngine() {
    // Ask the worker to stop, wake it, and wait for any in-flight network call
    // (bounded by the WinINet timeouts above). All queued jobs are dropped.
    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_stopping = true;
        m_cancelJob.store(true);
        m_jobs.clear();
    }
    m_jobCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

// ---------------------------------------------------------------------------
// Thread-safe accessors
// ---------------------------------------------------------------------------

UpdateInfo UpdateEngine::GetUpdateInfo() const {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    return m_info;
}

std::wstring UpdateEngine::GetDownloadedInstallerPath() const {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    return m_downloadedInstallerPath;
}

void UpdateEngine::CommitInfo(UpdateState state, const UpdateInfo& info) {
    {
        std::lock_guard<std::mutex> lock(m_infoMutex);
        m_info = info;
    }
    m_state.store(state);
}

void UpdateEngine::SetDownloadedInstallerPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    m_downloadedInstallerPath = path;
}

// ---------------------------------------------------------------------------
// Serialized background worker. Jobs run one at a time, in order; the UI thread
// never joins a live job (the old design blocked for up to 20 s on a slow check).
// ---------------------------------------------------------------------------

void UpdateEngine::EnqueueJob(std::function<void()> job) {
    if (!job) return;
    std::lock_guard<std::mutex> lock(m_jobMutex);
    if (m_stopping) return;
    m_jobs.push_back(std::move(job));
    if (!m_workerThread.joinable()) {
        m_workerThread = std::thread([this]() { WorkerLoop(); });
    }
    m_jobCv.notify_one();
}

void UpdateEngine::WorkerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_jobMutex);
            m_jobCv.wait(lock, [this]() { return m_stopping || !m_jobs.empty(); });
            if (m_stopping) return;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        job();
    }
}

// ---------------------------------------------------------------------------
// Update check
// ---------------------------------------------------------------------------

void UpdateEngine::CheckForUpdatesAsync(std::function<void(const UpdateInfo&)> onComplete) {
    const UpdateState state = m_state.load();
    if (state == UpdateState::Checking || state == UpdateState::Downloading) {
        return;
    }

    m_state.store(UpdateState::Checking);
    EnqueueJob([this, onComplete]() {
        UpdateInfo result;
        FetchUpdateInfoSync(result);
        if (onComplete) {
            // Snapshot copy: the callback can never observe m_info mid-commit.
            onComplete(GetUpdateInfo());
        }
    });
}

void UpdateEngine::SimulateUpdateForTesting() {
    UpdateInfo result;
    result.currentVersion = QueryModuleFileVersion();
    result.remoteVersion = L"1.1.0";
    result.changelog = L"Simulation test release with performance optimizations and new features.";
    result.isAvailable = true;
    CommitInfo(UpdateState::UpdateAvailable, result);
    m_isSimulated.store(true);
}

void UpdateEngine::FetchUpdateInfoSync(UpdateInfo& result) {
    result.currentVersion = QueryModuleFileVersion();
    result.isAvailable = false;
    result.errorMessage.clear();

    HINTERNET hInternet = InternetOpenW(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        result.errorMessage = L"Failed to initialize Internet connection.";
        CommitInfo(UpdateState::Failed, result);
        return;
    }
    ApplyWinInetTimeouts(hInternet);

    auto fetchFromUrl = [&](const wchar_t* url) -> std::string {
        HINTERNET hUrl = InternetOpenUrlW(hInternet, url, NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
        if (!hUrl) return "";
        DWORD statusCode = 0, statusSize = sizeof(statusCode), index = 0;
        if (HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, &index)) {
            if (statusCode < 200 || statusCode >= 300) {
                InternetCloseHandle(hUrl);
                return "";
            }
        }
        std::string body;
        char buf[8192];
        DWORD read = 0;
        while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0) {
            body.append(buf, read);
            if (body.size() > 256 * 1024) break; // manifest is tiny; refuse runaway bodies
        }
        InternetCloseHandle(hUrl);
        return body;
    };

    // releases/latest is the only trust anchor: the manifest is published as a
    // release asset so it can never advertise a version whose installer does not
    // exist yet (the old raw-main fallback caused exactly that).
    std::string responseBody = fetchFromUrl(UPDATE_META_URL);
    InternetCloseHandle(hInternet);

    if (responseBody.empty()) {
        result.errorMessage = L"Unable to contact update server.";
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    result.remoteVersion = ExtractJsonString(responseBody, "version");
    result.downloadUrl = ExtractJsonString(responseBody, "download_url");
    result.changelog = ExtractJsonString(responseBody, "changelog");
    result.releaseDate = ExtractJsonString(responseBody, "release_date");
    result.installerSha256 = NormalizeSha256(ExtractJsonString(responseBody, "installer_sha256"));
    result.installerSize = ExtractJsonInteger(responseBody, "installer_size");

    if (result.remoteVersion.empty()) {
        result.errorMessage = L"Invalid update metadata format.";
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    if (result.downloadUrl.empty()) {
        result.downloadUrl = DEFAULT_INSTALLER_URL;
    }
    if (!IsHttpsUrl(result.downloadUrl)) {
        result.errorMessage = L"Update manifest supplied a non-HTTPS download URL.";
        result.isAvailable = false;
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    // A present-but-malformed hash means the manifest is corrupt: fail closed.
    {
        std::wstring rawHash = ExtractJsonString(responseBody, "installer_sha256");
        if (!rawHash.empty() && result.installerSha256.empty()) {
            result.errorMessage = L"Update manifest contains an invalid integrity hash.";
            CommitInfo(UpdateState::Failed, result);
            return;
        }
    }

    if (IsVersionGreater(result.remoteVersion, result.currentVersion)) {
        result.isAvailable = true;
        CommitInfo(UpdateState::UpdateAvailable, result);
    } else {
        result.isAvailable = false;
        CommitInfo(UpdateState::UpToDate, result);
    }
}

// ---------------------------------------------------------------------------
// Download
// ---------------------------------------------------------------------------

void UpdateEngine::DownloadUpdateAsync(std::function<void(int percent)> onProgress,
                                      std::function<void(bool success, const std::wstring& filePath)> onComplete) {
    if (m_state.load() == UpdateState::Downloading) {
        return;
    }

    UpdateInfo info = GetUpdateInfo();
    if (!info.isAvailable && !m_isSimulated.load()) {
        m_state.store(UpdateState::Failed);
        if (onComplete) onComplete(false, L"");
        return;
    }
    if (!m_isSimulated.load() && !IsHttpsUrl(info.downloadUrl)) {
        m_state.store(UpdateState::Failed);
        if (onComplete) onComplete(false, L"");
        return;
    }

    m_state.store(UpdateState::Downloading);
    m_downloadProgress.store(0);
    m_cancelJob.store(false);

    if (m_isSimulated.load()) {
        EnqueueJob([this, onProgress, onComplete]() {
            for (int p = 5; p <= 100; p += 10) {
                if (m_cancelJob.load()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(180));
                m_downloadProgress.store(p);
                if (onProgress) onProgress(p);
            }
            m_state.store(UpdateState::Downloaded);
            if (onComplete) onComplete(true, L"SIMULATED_TEST");
        });
        return;
    }

    EnqueueJob([this, info, onProgress, onComplete]() {
        WCHAR tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);

        // Target a unique path so AV scanners or prior installs cannot hold a lock.
        std::wstring targetPath = std::wstring(tempPath) + L"WinPreview-Setup-v" +
                                  (info.remoteVersion.empty() ? L"latest" : info.remoteVersion) +
                                  L"-" + std::to_wstring(GetTickCount64()) + L".exe";
        SetDownloadedInstallerPath(targetPath);

        bool downloadSuccess = false;

        // =====================================================================
        // TIER 1: WinINet streamed download with live progress.
        // =====================================================================
        HINTERNET hInternet = InternetOpenW(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (hInternet) {
            ApplyWinInetTimeouts(hInternet);
            HINTERNET hUrl = InternetOpenUrlW(hInternet, info.downloadUrl.c_str(), NULL, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
            if (hUrl) {
                DWORD statusCode = 0;
                DWORD statusSize = sizeof(statusCode);
                DWORD statusIdx = 0;
                bool statusOk = true;
                if (HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, &statusIdx)) {
                    if (statusCode < 200 || statusCode >= 300) statusOk = false;
                }

                if (statusOk) {
                    DWORD contentLength = 0;
                    DWORD headerLength = sizeof(contentLength);
                    DWORD index = 0;
                    HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &headerLength, &index);

                    HANDLE hFile = CreateFileW(targetPath.c_str(), GENERIC_WRITE,
                                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        std::vector<char> buffer(128 * 1024);
                        DWORD bytesRead = 0;
                        unsigned long long totalBytesRead = 0;
                        bool writeFailed = false;

                        for (;;) {
                            if (m_cancelJob.load()) break;
                            if (!InternetReadFile(hUrl, buffer.data(), (DWORD)buffer.size(), &bytesRead)) {
                                writeFailed = true; // never treat a truncated stream as a complete installer
                                break;
                            }
                            if (bytesRead == 0) break;
                            DWORD bytesWritten = 0;
                            if (!WriteFile(hFile, buffer.data(), bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead) {
                                writeFailed = true;
                                break;
                            }
                            totalBytesRead += bytesRead;
                            if (contentLength > 0) {
                                int percent = (int)((totalBytesRead * 100ULL) / contentLength);
                                if (percent > 99) percent = 99;
                                m_downloadProgress.store(percent);
                                if (onProgress) onProgress(percent);
                            }
                        }
                        FlushFileBuffers(hFile);
                        CloseHandle(hFile);

                        if (!writeFailed && totalBytesRead >= (unsigned long long)MIN_INSTALLER_BYTES) {
                            downloadSuccess = true;
                        } else {
                            DeleteFileW(targetPath.c_str());
                        }
                    }
                }
                InternetCloseHandle(hUrl);
            }
            InternetCloseHandle(hInternet);
        }

        // =====================================================================
        // TIER 2: URLDownloadToFileW fallback.
        // =====================================================================
        if (!downloadSuccess && !m_cancelJob.load()) {
            DeleteFileW(targetPath.c_str());
            if (onProgress) onProgress(35);

            HRESULT hr = URLDownloadToFileW(NULL, info.downloadUrl.c_str(), targetPath.c_str(), 0, NULL);
            if (SUCCEEDED(hr)) {
                HANDLE hCheck = CreateFileW(targetPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                if (hCheck != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER sz{};
                    if (GetFileSizeEx(hCheck, &sz) && sz.QuadPart >= MIN_INSTALLER_BYTES) {
                        downloadSuccess = true;
                    }
                    CloseHandle(hCheck);
                }
                if (!downloadSuccess) DeleteFileW(targetPath.c_str());
            }
        }

        if (m_cancelJob.load()) {
            DeleteFileW(targetPath.c_str());
            SetDownloadedInstallerPath(L"");
            m_state.store(UpdateState::Idle);
            if (onComplete) onComplete(false, L"");
            return;
        }

        // =====================================================================
        // Integrity gate: size + SHA-256 from the release manifest. A file that
        // fails this is deleted and never offered to the launch path.
        // =====================================================================
        if (downloadSuccess) {
            WIN32_FILE_ATTRIBUTE_DATA attrs{};
            unsigned long long actualSize = 0;
            if (GetFileAttributesExW(targetPath.c_str(), GetFileExInfoStandard, &attrs)) {
                actualSize = ((unsigned long long)attrs.nFileSizeHigh << 32) | attrs.nFileSizeLow;
            }

            if (info.installerSize > 0 && actualSize != (unsigned long long)info.installerSize) {
                downloadSuccess = false;
            }

            if (downloadSuccess && !info.installerSha256.empty()) {
                std::wstring actualHash;
                if (!HashFileSha256(targetPath, actualHash) || actualHash != info.installerSha256) {
                    downloadSuccess = false;
                }
            }
        }

        if (downloadSuccess) {
            m_downloadProgress.store(100);
            m_state.store(UpdateState::Downloaded);
            if (onProgress) onProgress(100);
            if (onComplete) onComplete(true, targetPath);
        } else {
            DeleteFileW(targetPath.c_str());
            SetDownloadedInstallerPath(L"");
            m_state.store(UpdateState::Failed);
            if (onComplete) onComplete(false, L"");
        }
    });
}

// ---------------------------------------------------------------------------
// Installer integrity & launch
// ---------------------------------------------------------------------------

// Verifies PE structure, architecture, size, and (when the manifest provides
// one) the SHA-256 of the exact bytes we are about to execute.
static bool VerifyInstallerIntegrity(const std::wstring& filePath, const UpdateInfo& expected) {
    if (filePath.empty()) return false;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < MIN_INSTALLER_BYTES) {
        CloseHandle(hFile);
        return false;
    }
    if (expected.installerSize > 0 && fileSize.QuadPart != expected.installerSize) {
        CloseHandle(hFile);
        return false;
    }

    IMAGE_DOS_HEADER dosHeader{};
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL) || bytesRead != sizeof(dosHeader)) {
        CloseHandle(hFile);
        return false;
    }

    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE || dosHeader.e_lfanew <= 0) {
        CloseHandle(hFile);
        return false;
    }

    LARGE_INTEGER ntOffset{};
    ntOffset.QuadPart = dosHeader.e_lfanew;
    if (!SetFilePointerEx(hFile, ntOffset, NULL, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    DWORD ntSignature = 0;
    if (!ReadFile(hFile, &ntSignature, sizeof(ntSignature), &bytesRead, NULL) || bytesRead != sizeof(ntSignature)) {
        CloseHandle(hFile);
        return false;
    }

    if (ntSignature != IMAGE_NT_SIGNATURE) {
        CloseHandle(hFile);
        return false;
    }

    IMAGE_FILE_HEADER fileHeader{};
    if (!ReadFile(hFile, &fileHeader, sizeof(fileHeader), &bytesRead, NULL) || bytesRead != sizeof(fileHeader)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    if (fileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 && fileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        return false;
    }

    // Manifest hash is the primary trust anchor (works for unsigned open-source
    // builds); Authenticode is still validated when a signature is present.
    if (!expected.installerSha256.empty()) {
        std::wstring actualHash;
        if (!HashFileSha256(filePath, actualHash) || actualHash != expected.installerSha256) {
            return false;
        }
    }

    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = filePath.c_str();

    GUID policyGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.pFile = &fileInfo;

    LONG status = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policyGuid, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policyGuid, &trustData);

    if (status == ERROR_SUCCESS || status == TRUST_E_NOSIGNATURE || status == CERT_E_UNTRUSTEDROOT) {
        return true;
    }

    return false;
}

bool UpdateEngine::LaunchInstallerAndExit(HWND parentHwnd) {
    (void)parentHwnd;
    if (m_isSimulated.load()) {
        m_state.store(UpdateState::Idle);
        m_isSimulated.store(false);
        return true;
    }

    std::wstring installerPath = GetDownloadedInstallerPath();
    if (installerPath.empty()) return false;

    UpdateInfo expected = GetUpdateInfo();
    if (!VerifyInstallerIntegrity(installerPath, expected)) {
        DeleteFileW(installerPath.c_str());
        SetDownloadedInstallerPath(L"");
        m_state.store(UpdateState::Failed);
        return false;
    }

    HINSTANCE hInst = ShellExecuteW(NULL, L"open", installerPath.c_str(),
                                    L"", NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hInst > 32) {
        // Release file locks on WinPreview.exe / pdfium.dll before the installer runs.
        ExitProcess(0);
    }
    return false;
}

} // namespace WinPreview
