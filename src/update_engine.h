#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <condition_variable>
#include <deque>
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
    // SHA-256 of the published installer (lowercase hex). Empty for legacy
    // manifests that predate integrity metadata; size is the fallback check.
    std::wstring installerSha256;
    long long installerSize = 0;
    bool isAvailable = false;
    std::wstring errorMessage;
};

class UpdateEngine {
public:
    UpdateEngine();
    ~UpdateEngine();

    UpdateEngine(const UpdateEngine&) = delete;
    UpdateEngine& operator=(const UpdateEngine&) = delete;

    // Queues an update check on the engine's background worker. Never blocks the
    // caller and never joins a running job. onComplete is invoked on the worker
    // thread (the UI marshals with PostMessage).
    void CheckForUpdatesAsync(std::function<void(const UpdateInfo&)> onComplete = nullptr);

    // Queues an installer download. Progress/completion callbacks run on the
    // worker thread. The downloaded file is SHA-256 verified against the signed
    // release manifest before it is ever reported as complete.
    void DownloadUpdateAsync(std::function<void(int percent)> onProgress = nullptr,
                             std::function<void(bool success, const std::wstring& filePath)> onComplete = nullptr);

    // Verifies and launches the downloaded installer, then exits the process.
    bool LaunchInstallerAndExit(HWND parentHwnd);

    // Deterministic UI test hook.
    void SimulateUpdateForTesting();
    bool IsSimulated() const { return m_isSimulated.load(); }

    // State getters (thread-safe).
    UpdateState GetState() const { return m_state.load(); }
    int GetDownloadProgress() const { return m_downloadProgress.load(); }
    // Returns a copy so callers never race with worker-thread mutations.
    UpdateInfo GetUpdateInfo() const;
    std::wstring GetDownloadedInstallerPath() const;

    void Dismiss() { m_state.store(UpdateState::Idle); }

private:
    void EnqueueJob(std::function<void()> job);
    void WorkerLoop();
    void FetchUpdateInfoSync(UpdateInfo& result);

    // Atomically publishes a freshly computed result and its terminal state.
    void CommitInfo(UpdateState state, const UpdateInfo& info);
    void SetDownloadedInstallerPath(const std::wstring& path);

    std::atomic<UpdateState> m_state{ UpdateState::Idle };
    std::atomic<int> m_downloadProgress{ 0 };
    std::atomic<bool> m_isSimulated{ false };
    std::atomic<bool> m_cancelJob{ false };

    // Guards the mutable data touched by worker threads alongside the UI thread.
    mutable std::mutex m_infoMutex;
    UpdateInfo m_info;
    std::wstring m_downloadedInstallerPath;

    // Single serialized worker: a check can never race a download, and the UI
    // thread never waits on network I/O.
    std::mutex m_jobMutex;
    std::condition_variable m_jobCv;
    std::deque<std::function<void()>> m_jobs;
    std::thread m_workerThread;
    bool m_stopping = false;
};

} // namespace WinPreview
