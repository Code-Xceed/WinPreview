#include "update_engine.h"
#include <wininet.h>
#include <urlmon.h>
#include <shellapi.h>
#include <shlobj.h>
#include <softpub.h>
#include <wincrypt.h>
#include <vector>
#include <sstream>
#include <mutex>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "urlmon.lib")

namespace WinPreview {

static const wchar_t* UPDATE_META_URL = L"https://raw.githubusercontent.com/Code-Xceed/WinPreview/main/version.json";
static const wchar_t* USER_AGENT = L"WinPreview-AutoUpdater/1.0 (Windows NT; x64)";

// Bounded network waits so a worker joins quickly when the app shuts down.
static const DWORD NET_CONNECT_TIMEOUT_MS = 15000;
static const DWORD NET_RECV_TIMEOUT_MS = 20000;

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
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
    std::string value = json.substr(pos + 1, endQuote - (pos + 1));
    return Utf8ToWide(value);
}

static bool IsVersionGreater(const std::wstring& remote, const std::wstring& current) {
    int rMaj = 0, rMin = 0, rPatch = 0, rBuild = 0;
    int cMaj = 0, cMin = 0, cPatch = 0, cBuild = 0;

    // Parse remote version, handle optional 'v' prefix
    const wchar_t* rPtr = remote.c_str();
    while (*rPtr == L'v' || *rPtr == L'V' || *rPtr == L' ') rPtr++;
    swscanf_s(rPtr, L"%d.%d.%d.%d", &rMaj, &rMin, &rPatch, &rBuild);

    // Parse current version
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
    // Politely ask any running job to stop, then wait for it. Downloads abort at the
    // next network chunk (bounded by NET_RECV_TIMEOUT_MS), so shutdown stays responsive.
    m_cancelJob.store(true);
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
// Async jobs — a single worker thread, serialized through m_workerThread.
// Callbacks receive snapshot copies and are invoked on the worker thread;
// UI side must marshal (AppWindow does via PostMessage).
// ---------------------------------------------------------------------------

void UpdateEngine::CheckForUpdatesAsync(std::function<void(const UpdateInfo&)> onComplete) {
    if (m_state.load() == UpdateState::Checking || m_state.load() == UpdateState::Downloading) {
        return;
    }

    m_state.store(UpdateState::Checking);
    m_cancelJob.store(false);

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    m_workerThread = std::thread([this, onComplete]() {
        m_isSimulated.store(false);
        UpdateInfo result;
        FetchUpdateInfoSync(result);
        if (onComplete) {
            // Snapshot copy: the callback can never observe m_info mid-commit,
            // and calling GetUpdateInfo() back on the worker is also safe.
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

    HINTERNET hUrl = InternetOpenUrlW(hInternet, UPDATE_META_URL, NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE |
        INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        result.errorMessage = L"Unable to contact update server.";
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    // Check HTTP status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    DWORD index = 0;
    if (HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, &index)) {
        if (statusCode < 200 || statusCode >= 300) {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            result.errorMessage = L"Update server returned status " + std::to_wstring(statusCode);
            CommitInfo(UpdateState::Failed, result);
            return;
        }
    }

    std::string responseBody;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        responseBody.append(buffer, bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    if (responseBody.empty()) {
        result.errorMessage = L"Empty update manifest.";
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    result.remoteVersion = ExtractJsonString(responseBody, "version");
    result.downloadUrl = ExtractJsonString(responseBody, "download_url");
    result.changelog = ExtractJsonString(responseBody, "changelog");
    result.releaseDate = ExtractJsonString(responseBody, "release_date");

    if (result.remoteVersion.empty()) {
        result.errorMessage = L"Invalid update metadata format.";
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    if (result.downloadUrl.empty()) {
        result.downloadUrl = L"https://github.com/Code-Xceed/WinPreview/releases/latest/download/WinPreview-Setup-x64.exe";
    }

    if (IsVersionGreater(result.remoteVersion, result.currentVersion)) {
        result.isAvailable = true;
        CommitInfo(UpdateState::UpdateAvailable, result);
    } else {
        result.isAvailable = false;
        CommitInfo(UpdateState::UpToDate, result);
    }
}

void UpdateEngine::DownloadUpdateAsync(std::function<void(int percent)> onProgress,
                                      std::function<void(bool success, const std::wstring& filePath)> onComplete) {
    if (m_state.load() == UpdateState::Downloading) {
        return;
    }

    m_state.store(UpdateState::Downloading);
    m_downloadProgress.store(0);
    m_cancelJob.store(false);

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    if (m_isSimulated.load()) {
        m_workerThread = std::thread([this, onProgress, onComplete]() {
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

    m_workerThread = std::thread([this, onProgress, onComplete]() {
        // Work against a snapshot: other threads may mutate m_info while we run.
        UpdateInfo info = GetUpdateInfo();

        WCHAR tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);

        // Try deleting any previous installer to clean up
        std::wstring defaultPath = std::wstring(tempPath) + L"WinPreview-Setup-x64.exe";
        DeleteFileW(defaultPath.c_str());

        // Always target a safe unique path to prevent sharing violations or AV locks
        std::wstring targetPath = std::wstring(tempPath) + L"WinPreview-Setup-v" +
                                  (info.remoteVersion.empty() ? L"latest" : info.remoteVersion) +
                                  L"-" + std::to_wstring(GetTickCount64()) + L".exe";
        SetDownloadedInstallerPath(targetPath);

        bool downloadSuccess = false;

        // =====================================================================
        // TIER 1: WinINet Stream Download (with live % progress callbacks)
        // =====================================================================
        HINTERNET hInternet = InternetOpenW(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (hInternet) {
            ApplyWinInetTimeouts(hInternet);
            HINTERNET hUrl = InternetOpenUrlW(hInternet, info.downloadUrl.c_str(), NULL, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE |
                INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS, 0);
            if (hUrl) {
                DWORD statusCode = 0;
                DWORD statusSize = sizeof(statusCode);
                DWORD statusIdx = 0;
                bool statusOk = true;
                if (HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, &statusIdx)) {
                    if (statusCode < 200 || statusCode >= 300) {
                        statusOk = false;
                    }
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
                        char buffer[65536];
                        DWORD bytesRead = 0;
                        DWORD totalBytesRead = 0;
                        bool writeFailed = false;

                        while (!m_cancelJob.load() &&
                               InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                            DWORD bytesWritten = 0;
                            if (!WriteFile(hFile, buffer, bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead) {
                                writeFailed = true;
                                break;
                            }
                            totalBytesRead += bytesRead;
                            if (contentLength > 0) {
                                int percent = (int)(((double)totalBytesRead / contentLength) * 100.0);
                                if (percent > 99) percent = 99;
                                m_downloadProgress.store(percent);
                                if (onProgress) onProgress(percent);
                            }
                        }
                        CloseHandle(hFile);

                        if (!writeFailed && totalBytesRead > 500000) {
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
        // TIER 2: URLDownloadToFileW Fallback (if WinINet failed for any reason)
        // =====================================================================
        if (!downloadSuccess && !m_cancelJob.load()) {
            DeleteFileW(targetPath.c_str());
            if (onProgress) onProgress(35);

            HRESULT hr = URLDownloadToFileW(NULL, info.downloadUrl.c_str(), targetPath.c_str(), 0, NULL);
            if (SUCCEEDED(hr)) {
                HANDLE hCheck = CreateFileW(targetPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                if (hCheck != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER sz;
                    if (GetFileSizeEx(hCheck, &sz) && sz.QuadPart > 500000) {
                        downloadSuccess = true;
                    }
                    CloseHandle(hCheck);
                }
                if (!downloadSuccess) {
                    DeleteFileW(targetPath.c_str());
                }
            }
        }

        // =====================================================================
        // Final Completion
        // =====================================================================
        if (m_cancelJob.load()) {
            DeleteFileW(targetPath.c_str());
            m_state.store(UpdateState::Idle);
            if (onComplete) onComplete(false, L"");
            return;
        }
        if (downloadSuccess) {
            m_downloadProgress.store(100);
            m_state.store(UpdateState::Downloaded);
            if (onProgress) onProgress(100);
            if (onComplete) onComplete(true, targetPath);
        } else {
            DeleteFileW(targetPath.c_str());
            m_state.store(UpdateState::Failed);
            if (onComplete) onComplete(false, L"");
        }
    });
}

// ---------------------------------------------------------------------------
// Installer integrity & launch
// ---------------------------------------------------------------------------

// Verifies the Authenticode signature chain of the downloaded installer against
// trusted root CAs. Never launches an unverified, unsigned or size-only-validated binary.
static bool VerifyInstallerSignature(const std::wstring& filePath) {
    WINTRUST_FILE_INFO fileInfo = {};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = filePath.c_str();

    GUID policyGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA trustData = {};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.pFile = &fileInfo;

    LONG status = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policyGuid, &trustData);

    // Release the verification state added by WTD_STATEACTION_VERIFY.
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policyGuid, &trustData);

    return status == ERROR_SUCCESS;
}

bool UpdateEngine::LaunchInstallerAndExit(HWND parentHwnd) {
    if (m_isSimulated.load()) {
        m_state.store(UpdateState::Idle);
        m_isSimulated.store(false);
        return true;
    }

    std::wstring installerPath = GetDownloadedInstallerPath();
    if (installerPath.empty()) return false;

    if (!VerifyInstallerSignature(installerPath)) {
        DeleteFileW(installerPath.c_str());
        SetDownloadedInstallerPath(L"");
        m_state.store(UpdateState::Failed);
        return false;
    }

    // Launch the installer independently without parent window dependency
    HINSTANCE hInst = ShellExecuteW(NULL, L"open", installerPath.c_str(),
                                    L"", NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hInst > 32) {
        // Terminate current process immediately to release all file locks on WinPreview.exe and pdfium.dll
        ExitProcess(0);
        return true;
    }
    return false;
}

} // namespace WinPreview
