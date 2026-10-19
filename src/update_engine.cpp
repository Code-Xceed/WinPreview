#include "update_engine.h"
#include <wininet.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>
#include <sstream>

#pragma comment(lib, "wininet.lib")

namespace WinPreview {

static const wchar_t* UPDATE_META_URL = L"https://raw.githubusercontent.com/Code-Xceed/WinPreview/main/version.json";
static const wchar_t* USER_AGENT = L"WinPreview-AutoUpdater/1.0 (Windows NT; x64)";

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
    pos = json.find('\"', pos + 1);
    if (pos == std::string::npos) return L"";
    size_t endQuote = json.find('\"', pos + 1);
    if (endQuote == std::string::npos) return L"";
    std::string value = json.substr(pos + 1, endQuote - (pos + 1));
    return Utf8ToWide(value);
}

static bool IsVersionGreater(const std::wstring& remote, const std::wstring& current) {
    int rMaj = 0, rMin = 0, rPatch = 0;
    int cMaj = 0, cMin = 0, cPatch = 0;
    
    // Parse remote version, handle optional 'v' prefix
    const wchar_t* rPtr = remote.c_str();
    if (rPtr[0] == L'v' || rPtr[0] == L'V') rPtr++;
    swscanf_s(rPtr, L"%d.%d.%d", &rMaj, &rMin, &rPatch);

    // Parse current version
    const wchar_t* cPtr = current.c_str();
    if (cPtr[0] == L'v' || cPtr[0] == L'V') cPtr++;
    swscanf_s(cPtr, L"%d.%d.%d", &cMaj, &cMin, &cPatch);

    if (rMaj > cMaj) return true;
    if (rMaj < cMaj) return false;
    if (rMin > cMin) return true;
    if (rMin < cMin) return false;
    return rPatch > cPatch;
}

UpdateEngine::UpdateEngine() = default;

UpdateEngine::~UpdateEngine() {
    if (m_workerThread.joinable()) {
        m_workerThread.detach();
    }
}

void UpdateEngine::CheckForUpdatesAsync(std::function<void(const UpdateInfo&)> onComplete) {
    if (m_state.load() == UpdateState::Checking || m_state.load() == UpdateState::Downloading) {
        return;
    }

    m_state.store(UpdateState::Checking);

    if (m_workerThread.joinable()) {
        m_workerThread.detach();
    }

    m_workerThread = std::thread([this, onComplete]() {
        FetchUpdateInfoSync();
        if (onComplete) {
            onComplete(m_info);
        }
    });
}

void UpdateEngine::FetchUpdateInfoSync() {
    m_info.isAvailable = false;
    m_info.errorMessage.clear();

    HINTERNET hInternet = InternetOpenW(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        m_info.errorMessage = L"Failed to initialize Internet connection.";
        m_state.store(UpdateState::Failed);
        return;
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
    HINTERNET hUrl = InternetOpenUrlW(hInternet, UPDATE_META_URL, NULL, 0, flags, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        m_info.errorMessage = L"Unable to contact update server.";
        m_state.store(UpdateState::Failed);
        return;
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
        m_info.errorMessage = L"Empty update manifest.";
        m_state.store(UpdateState::Failed);
        return;
    }

    m_info.remoteVersion = ExtractJsonString(responseBody, "version");
    m_info.downloadUrl = ExtractJsonString(responseBody, "download_url");
    m_info.changelog = ExtractJsonString(responseBody, "changelog");
    m_info.releaseDate = ExtractJsonString(responseBody, "release_date");

    if (m_info.remoteVersion.empty()) {
        m_info.errorMessage = L"Invalid update metadata format.";
        m_state.store(UpdateState::Failed);
        return;
    }

    if (m_info.downloadUrl.empty()) {
        m_info.downloadUrl = L"https://github.com/Code-Xceed/WinPreview/releases/latest/download/WinPreview-Setup-x64.exe";
    }

    if (IsVersionGreater(m_info.remoteVersion, m_info.currentVersion)) {
        m_info.isAvailable = true;
        m_state.store(UpdateState::UpdateAvailable);
    } else {
        m_info.isAvailable = false;
        m_state.store(UpdateState::UpToDate);
    }
}

void UpdateEngine::DownloadUpdateAsync(std::function<void(int percent)> onProgress,
                                      std::function<void(bool success, const std::wstring& filePath)> onComplete) {
    if (m_state.load() == UpdateState::Downloading) {
        return;
    }

    m_state.store(UpdateState::Downloading);
    m_downloadProgress.store(0);

    if (m_workerThread.joinable()) {
        m_workerThread.detach();
    }

    m_workerThread = std::thread([this, onProgress, onComplete]() {
        // Prepare target temporary path
        WCHAR tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        m_downloadedInstallerPath = std::wstring(tempPath) + L"WinPreview-Setup-x64.exe";

        HINTERNET hInternet = InternetOpenW(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) {
            m_state.store(UpdateState::Failed);
            if (onComplete) onComplete(false, L"");
            return;
        }

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
        HINTERNET hUrl = InternetOpenUrlW(hInternet, m_info.downloadUrl.c_str(), NULL, 0, flags, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            m_state.store(UpdateState::Failed);
            if (onComplete) onComplete(false, L"");
            return;
        }

        // Query Content-Length for progress computation
        DWORD contentLength = 0;
        DWORD headerLength = sizeof(contentLength);
        DWORD index = 0;
        HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &headerLength, &index);

        HANDLE hFile = CreateFileW(m_downloadedInstallerPath.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            m_state.store(UpdateState::Failed);
            if (onComplete) onComplete(false, L"");
            return;
        }

        char buffer[8192];
        DWORD bytesRead = 0;
        DWORD totalBytesRead = 0;
        bool writeFailed = false;

        while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            DWORD bytesWritten = 0;
            if (!WriteFile(hFile, buffer, bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead) {
                writeFailed = true;
                break;
            }
            totalBytesRead += bytesRead;
            if (contentLength > 0) {
                int percent = (int)(((double)totalBytesRead / contentLength) * 100.0);
                if (percent > 100) percent = 100;
                m_downloadProgress.store(percent);
                if (onProgress) onProgress(percent);
            }
        }

        CloseHandle(hFile);
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        if (writeFailed || totalBytesRead < 1000) {
            DeleteFileW(m_downloadedInstallerPath.c_str());
            m_state.store(UpdateState::Failed);
            if (onComplete) onComplete(false, L"");
        } else {
            m_downloadProgress.store(100);
            m_state.store(UpdateState::Downloaded);
            if (onProgress) onProgress(100);
            if (onComplete) onComplete(true, m_downloadedInstallerPath);
        }
    });
}

bool UpdateEngine::LaunchInstallerAndExit(HWND parentHwnd) {
    if (m_downloadedInstallerPath.empty()) return false;

    // Launch the installer
    HINSTANCE hInst = ShellExecuteW(parentHwnd, L"open", m_downloadedInstallerPath.c_str(),
                                    L"", NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hInst > 32) {
        // Signal caller to terminate current process cleanly
        if (parentHwnd && IsWindow(parentHwnd)) {
            PostMessageW(parentHwnd, WM_CLOSE, 0, 0);
        } else {
            ExitProcess(0);
        }
        return true;
    }
    return false;
}

} // namespace WinPreview
