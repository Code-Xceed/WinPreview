#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <functional>
#include <thread>

namespace WinPreview {

enum class UpdateState {
    Idle,
    Checking,
    UpdateAvailable,
    UpToDate,
    Downloading,
    Downloaded,
    Failed
};

struct UpdateInfo {
    std::wstring currentVersion = L"1.0.0";
    std::wstring remoteVersion;
    std::wstring downloadUrl;
    std::wstring changelog;
    std::wstring releaseDate;
    bool isAvailable = false;
    std::wstring errorMessage;
};

class UpdateEngine {
public:
    UpdateEngine();
    ~UpdateEngine();

    // Check for updates asynchronously without blocking the UI thread
    void CheckForUpdatesAsync(std::function<void(const UpdateInfo&)> onComplete = nullptr);

    // Download the installer executable asynchronously
    void DownloadUpdateAsync(std::function<void(int percent)> onProgress = nullptr,
                            std::function<void(bool success, const std::wstring& filePath)> onComplete = nullptr);

    // Launch the downloaded installer and exit
    bool LaunchInstallerAndExit(HWND parentHwnd);

    // Test simulation for update banner & download UX
    void SimulateUpdateForTesting();
    bool IsSimulated() const { return m_isSimulated.load(); }

    // State getters
    UpdateState GetState() const { return m_state.load(); }
    int GetDownloadProgress() const { return m_downloadProgress.load(); }
    const UpdateInfo& GetUpdateInfo() const { return m_info; }
    const std::wstring& GetDownloadedInstallerPath() const { return m_downloadedInstallerPath; }

    void Dismiss() { m_state.store(UpdateState::Idle); }
    void ResetToAvailable() {
        if (m_info.isAvailable && m_state.load() == UpdateState::Idle) {
            m_state.store(UpdateState::UpdateAvailable);
        }
    }

private:
    std::atomic<UpdateState> m_state{ UpdateState::Idle };
    std::atomic<int> m_downloadProgress{ 0 };
    std::atomic<bool> m_isSimulated{ false };
    UpdateInfo m_info;
    std::wstring m_downloadedInstallerPath;

    std::thread m_workerThread;
    void FetchUpdateInfoSync();
    void DownloadInstallerSync();
};

} // namespace WinPreview
