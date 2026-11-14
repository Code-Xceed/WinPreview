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

    // Manifest v2 metadata.
    std::wstring channel = L"stable";
    int rolloutPercent = 100;
    bool paused = false;
    bool mandatory = false;
    std::wstring minSupported;
    std::wstring notes;
    bool signaturePresent = false;
    bool signatureValid = false;
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

    // Update channel and (test-only) manifest URL override.
    void SetChannel(const std::wstring& channel);
    void SetManifestUrlOverride(const std::wstring& url) { m_manifestUrlOverride = url; }
    std::wstring GetManifestUrl() const;

    // Relaunch target for the update helper ("visible" or "background").
    void SetRelaunchBackground(bool background) { m_relaunchBackground = background; }


    // Aborts an in-flight download and forgets the partial file.
    void CancelDownload() { m_cancelJob.store(true); }

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

    std::wstring m_channel = L"stable";
    std::wstring m_manifestUrlOverride;
    bool m_relaunchBackground = false;
};

// ---------------------------------------------------------------------------
// Update helper / self-test entry points (implemented in update_engine.cpp)
// ---------------------------------------------------------------------------

// Applies a downloaded installer after the running app has exited:
// waits for waitPid, runs the installer silently, records the result and
// relaunches the app (in `relaunchMode`: "background" or "visible").
// Returns 0 when the update was applied, non-zero otherwise.
int RunUpdateHelper(const std::wstring& installerPath, unsigned long waitPid,
                    const std::wstring& relaunchMode, const std::wstring& logPath,
                    const std::wstring& resultPath, const std::wstring& restartExe);

// Offline verification of the signing key and manifest parser. Used by CI.
// Returns 0 when every self-test passes.
int RunUpdateSelfTest();

// Removes stale helper copies and downloaded installers from %TEMP% (older than
// six hours) so updates do not leave tens of megabytes behind.
void CleanupUpdateTempFiles();

// True when this copy has no uninstall entry, i.e. it was unpacked rather than
// installed. Portable copies get the portable download instead of an installer.
bool IsPortableInstall();

// Appends a line to update.log (recovery/boot diagnostics).
void LogUpdateDiagnostic(const std::wstring& message);

// Copies the current installation aside before an update is applied, recording
// the version in the registry. Returns false (and changes nothing) on failure.
bool CreateUpdateBackup(const std::wstring& exePath);

// Reads the backup recorded by CreateUpdateBackup. Returns false when there is
// no usable backup.
bool GetUpdateBackup(std::wstring& backupDir, std::wstring& version);

// Graceful recovery: restores the recorded backup over the install directory
// after the app exits, then relaunches. Runs from a temporary copy of the exe.
int RunRestoreHelper(const std::wstring& backupDir, const std::wstring& restartExe,
                     unsigned long waitPid, const std::wstring& relaunchMode,
                     const std::wstring& logPath);

// Spawns the restore helper from %TEMP% and returns immediately. The caller is
// expected to exit so the files can be replaced.
bool SpawnRestoreHelper(const std::wstring& exePath, const std::wstring& backupDir,
                        unsigned long waitPid, const std::wstring& relaunchMode);

} // namespace WinPreview
