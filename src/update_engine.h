#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include "version.h"

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
    std::wstring currentVersion = WINPREVIEW_VERSION_WSTR;
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

    // State getters (thread-safe)
    UpdateState GetState() const { return m_state.load(); }
    int GetDownloadProgress() const { return m_downloadProgress.load(); }
    // Returns a copy so callers never race with worker-thread mutations.
    UpdateInfo GetUpdateInfo() const;
    std::wstring GetDownloadedInstallerPath() const;

    void Dismiss() { m_state.store(UpdateState::Idle); }
    void ResetToAvailable() {
        if (GetUpdateInfo().isAvailable && m_state.load() == UpdateState::Idle) {
            m_state.store(UpdateState::UpdateAvailable);
        }
    }

private:
    std::atomic<UpdateState> m_state{ UpdateState::Idle };
    std::atomic<int> m_downloadProgress{ 0 };
    std::atomic<bool> m_isSimulated{ false };

    // Guards the mutable data touched by worker threads alongside the UI thread.
    mutable std::mutex m_infoMutex;
    UpdateInfo m_info;
    std::wstring m_downloadedInstallerPath;

    std::thread m_workerThread;
    std::atomic<bool> m_cancelJob{ false };
    void FetchUpdateInfoSync(UpdateInfo& result);

    // Atomically publishes a freshly computed result and its terminal state.
    void CommitInfo(UpdateState state, const UpdateInfo& info);
    void SetDownloadedInstallerPath(const std::wstring& path);
};

} // namespace WinPreview
