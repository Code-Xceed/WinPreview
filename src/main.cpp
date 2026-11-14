#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <fstream>
#include <chrono>
#include "app_window.h"
#include "explorer_hook.h"

// Force integrated GPU to eliminate NVIDIA PCIe bus spin-up latency (~800ms)
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000000;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;
}

static auto g_startTime = std::chrono::high_resolution_clock::now();
namespace WinPreview {
void LogTime(const char* label) {
    auto now = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration_cast<std::chrono::microseconds>(now - g_startTime).count() / 1000.0;
    char buf[256];
    snprintf(buf, sizeof(buf), "[WinPreview][%0.2f ms] %s\n", ms, label);
    OutputDebugStringA(buf);

    static int s_enableFileLog = -1;
    if (s_enableFileLog == -1) {
        WCHAR envBuf[16] = {};
        s_enableFileLog = (GetEnvironmentVariableW(L"WINPREVIEW_LOG", envBuf, 16) > 0) ? 1 : 0;
    }
    if (s_enableFileLog == 1) {
        WCHAR tempPath[MAX_PATH];
        if (GetTempPathW(MAX_PATH, tempPath)) {
            std::wstring logFile = std::wstring(tempPath) + L"winpreview.log";
            FILE* f = _wfopen(logFile.c_str(), L"a");
            if (f) {
                fputs(buf, f);
                fclose(f);
            }
        }
    }
}
}
using WinPreview::LogTime;

typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

static LONG WINAPI WinPreviewCrashFilter(EXCEPTION_POINTERS* ep) {
    WCHAR tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath)) {
        std::wstring crashLog = std::wstring(tempPath) + L"winpreview_crash.log";
        FILE* f = _wfopen(crashLog.c_str(), L"a");
        if (f) {
            DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
            void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
            fprintf(f, "[WinPreview Crash] ExceptionCode: 0x%08X at Address: %p\n", (unsigned int)code, addr);
            fclose(f);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    SetUnhandledExceptionFilter(WinPreviewCrashFilter);
    LogTime("wWinMain entered");

    // Set explicit Process Application User Model ID for Windows 10/11 taskbar grouping and branding
    SetCurrentProcessExplicitAppUserModelID(L"WinPreview.App");

    // Parse command-line arguments early
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring fileToOpen;
    bool isBackground = false;

    std::wstring screenshotPath;
    int screenshotWidth = 0;
    int screenshotHeight = 0;
    bool screenshotOpenMenu = false;
    bool screenshotOpenInspector = false;
    bool screenshotOpenSettings = false;
    int screenshotSettingsTab = 0;
    bool screenshotSimulateUpdate = false;

    std::wstring applyUpdateInstaller;
    std::wstring applyUpdateRelaunch = L"visible";
    std::wstring applyUpdateLog;
    std::wstring applyUpdateResult;
    std::wstring applyUpdateRestartExe;
    unsigned long applyUpdateWaitPid = 0;
    bool runUpdateSelfTest = false;
    bool forceUpdateCheck = false;
    bool downloadUpdateTest = false;
    bool openUpdateSheet = false;
    bool forceInstalled = false;
    std::wstring updateManifestOverride;
    std::wstring updateChannel;
    std::wstring failedUpdateCode;

    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--background") == 0 || _wcsicmp(argv[i], L"/background") == 0) {
                isBackground = true;
            } else if ((_wcsicmp(argv[i], L"--apply-update") == 0) && i + 1 < argc) {
                applyUpdateInstaller = argv[++i];
            } else if ((_wcsicmp(argv[i], L"--wait-pid") == 0) && i + 1 < argc) {
                applyUpdateWaitPid = (unsigned long)_wtoi(argv[++i]);
            } else if ((_wcsicmp(argv[i], L"--relaunch") == 0) && i + 1 < argc) {
                applyUpdateRelaunch = argv[++i];
            } else if ((_wcsicmp(argv[i], L"--log") == 0) && i + 1 < argc) {
                applyUpdateLog = argv[++i];
            } else if ((_wcsicmp(argv[i], L"--result") == 0) && i + 1 < argc) {
                applyUpdateResult = argv[++i];
            } else if ((_wcsicmp(argv[i], L"--restart-exe") == 0) && i + 1 < argc) {
                applyUpdateRestartExe = argv[++i];
            } else if (_wcsicmp(argv[i], L"--selftest-update") == 0) {
                runUpdateSelfTest = true;
            } else if (_wcsicmp(argv[i], L"--force-update-check") == 0) {
                forceUpdateCheck = true;
            } else if (_wcsicmp(argv[i], L"--download-update-test") == 0) {
                downloadUpdateTest = true;
            } else if (_wcsicmp(argv[i], L"--open-update-sheet") == 0) {
                openUpdateSheet = true;
            } else if (_wcsicmp(argv[i], L"--force-installed") == 0) {
                forceInstalled = true;
            } else if ((_wcsicmp(argv[i], L"--update-url") == 0) && i + 1 < argc) {
                updateManifestOverride = argv[++i];
            } else if ((_wcsicmp(argv[i], L"--update-channel") == 0) && i + 1 < argc) {
                updateChannel = argv[++i];
            } else if (_wcsicmp(argv[i], L"--restored") == 0) {
                // First launch after a successful update.
            } else if ((_wcsicmp(argv[i], L"--update-failed") == 0) && i + 1 < argc) {
                failedUpdateCode = argv[++i];
            } else if ((_wcsicmp(argv[i], L"--screenshot") == 0 || _wcsicmp(argv[i], L"/screenshot") == 0) && i + 1 < argc) {
                screenshotPath = argv[++i];
            } else if ((_wcsicmp(argv[i], L"--width") == 0 || _wcsicmp(argv[i], L"-w") == 0) && i + 1 < argc) {
                screenshotWidth = _wtoi(argv[++i]);
            } else if ((_wcsicmp(argv[i], L"--height") == 0 || _wcsicmp(argv[i], L"-h") == 0) && i + 1 < argc) {
                screenshotHeight = _wtoi(argv[++i]);
            } else if (_wcsicmp(argv[i], L"--open-menu") == 0) {
                screenshotOpenMenu = true;
            } else if (_wcsicmp(argv[i], L"--open-inspector") == 0) {
                screenshotOpenInspector = true;
            } else if (_wcsicmp(argv[i], L"--open-settings") == 0) {
                screenshotOpenSettings = true;
            } else if (_wcsicmp(argv[i], L"--settings-tab") == 0 && i + 1 < argc) {
                screenshotSettingsTab = _wtoi(argv[++i]);
            } else if (_wcsicmp(argv[i], L"--simulate-update") == 0) {
                screenshotSimulateUpdate = true;
            } else if (fileToOpen.empty() && argv[i][0] != L'-' && argv[i][0] != L'/') {
                fileToOpen = argv[i];
            }
        }
    }

    if (!fileToOpen.empty()) {
        WCHAR fullPath[MAX_PATH];
        if (GetFullPathNameW(fileToOpen.c_str(), MAX_PATH, fullPath, NULL)) {
            fileToOpen = fullPath;
        }
    }

    // -----------------------------------------------------------------------
    // Update helper / self-test run before any window, mutex or COM setup.
    // -----------------------------------------------------------------------
    if (!applyUpdateInstaller.empty()) {
        return WinPreview::RunUpdateHelper(applyUpdateInstaller, applyUpdateWaitPid,
                                           applyUpdateRelaunch, applyUpdateLog, applyUpdateResult,
                                           applyUpdateRestartExe);
    }
    if (runUpdateSelfTest) {
        return WinPreview::RunUpdateSelfTest();
    }

    // Single-instance enforcement: Forward file to running instance if already active
    HANDLE hMutex = NULL;
    if (screenshotPath.empty()) {
        SetLastError(0);
        hMutex = CreateMutexW(NULL, TRUE, L"Local\\WinPreview_SingleInstance_Mutex");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            LogTime("SingleInstance mutex already exists");
            HWND hExisting = FindWindowW(L"WinPreviewMainWindow", NULL);
            for (int retries = 0; !hExisting && retries < 25; ++retries) {
                Sleep(15);
                hExisting = FindWindowW(L"WinPreviewMainWindow", NULL);
            }
            if (hExisting) {
                LogTime("Found existing WinPreviewMainWindow");
                if (!fileToOpen.empty()) {
                    LogTime("Calling ForwardFileToExistingInstance");
                    WinPreview::ExplorerHook::ForwardFileToExistingInstance(fileToOpen);
                    LogTime("ForwardFileToExistingInstance done");
                } else if (!isBackground) {
                    PostMessageW(hExisting, WinPreview::ExplorerHook::WM_WINPREVIEW_NOTIFY_RUNNING, 0, 0);
                }
                if (argv) LocalFree(argv);
                if (hMutex) CloseHandle(hMutex);
                return 0;
            }
            // If no existing window exists, continue and become the active instance
        }
    }
    if (argv) LocalFree(argv);

    // High-DPI Awareness V2
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        PFN_SetProcessDpiAwarenessContext pfnSetDpi = 
            (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pfnSetDpi) {
            pfnSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
    LogTime("DPI awareness configured");

    // Initialize COM
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        return 1;
    }
    LogTime("COM initialized");

    // Background-first service architecture (QuickLook / Peek model):
    // When launched without a file argument, WinPreview always starts silently hidden in the system tray.
    // The default empty screen is never shown. Launching manually displays a friendly tray balloon.
    int initialShow = (!fileToOpen.empty() && screenshotPath.empty()) ? (isBackground ? SW_HIDE : nCmdShow) : SW_HIDE;
    bool showTrayBalloon = (!isBackground && fileToOpen.empty() && screenshotPath.empty());

    int exitCode = 0;
    {
        WinPreview::AppWindow app;
        if (!updateManifestOverride.empty()) app.GetUpdater().SetManifestUrlOverride(updateManifestOverride);
        if (!updateChannel.empty()) app.GetUpdater().SetChannel(updateChannel);
        app.GetUpdater().SetRelaunchBackground(isBackground);
        app.SetForceInstalled(forceInstalled);
        if (!app.Create(hInstance, initialShow, showTrayBalloon)) {
            LogTime("app.Create FAILED!");
            exitCode = 1;
        } else {
            LogTime("Window created");
            if (!failedUpdateCode.empty()) app.NotifyUpdateFailed(failedUpdateCode);
            if (forceUpdateCheck) app.StartUpdateCheckForTesting();
            if (downloadUpdateTest) app.DownloadUpdateForTesting();
            if (openUpdateSheet) app.SetShowUpdateSheet(true);
            if (std::wstring(pCmdLine).find(L"--restored") != std::wstring::npos) app.NotifyUpdateApplied();

            // Open file argument if provided
            if (!fileToOpen.empty()) {
                LogTime("Opening file from args");
                app.OpenFile(fileToOpen);
                if (screenshotPath.empty()) {
                    ShowWindow(app.GetHwnd(), SW_SHOW);
                    SetForegroundWindow(app.GetHwnd());
                }
                LogTime("File opened");
            }

            // Direct deterministic screenshot mode: captures Direct2D frame and exits immediately
            if (!screenshotPath.empty()) {
                if (screenshotWidth > 0 && screenshotHeight > 0) {
                    SetWindowPos(app.GetHwnd(), NULL, 0, 0, screenshotWidth, screenshotHeight, SWP_NOMOVE | SWP_NOZORDER);
                }
                if (screenshotOpenMenu) {
                    app.SetShowOverflowMenu(true);
                }
                if (screenshotOpenInspector) {
                    app.SetShowInspector(true);
                }
                if (screenshotOpenSettings) {
                    app.SetShowSettings(true);
                    app.SetSettingsActiveTab(screenshotSettingsTab);
                }
                if (screenshotSimulateUpdate) {
                    app.GetUpdater().SimulateUpdateForTesting();
                }
                // Let async document work (PDF raster on its worker thread, WIC decodes,
                // thumbnail generation) land before the deterministic capture — pump the
                // message loop until the frame settles or a short deadline elapses.
                LogTime("Settling async render for screenshot");
                DWORD settleDeadline = GetTickCount() + 500;
                if (openUpdateSheet) {
                    // Wait for the manifest fetch that fills the sheet.
                    DWORD sheetDeadline = GetTickCount() + 5000;
                    while (GetTickCount() < sheetDeadline &&
                           app.GetUpdater().GetState() != WinPreview::UpdateState::UpdateAvailable) {
                        MSG waitMsg;
                        while (PeekMessageW(&waitMsg, NULL, 0, 0, PM_REMOVE)) {
                            TranslateMessage(&waitMsg);
                            DispatchMessageW(&waitMsg);
                        }
                        Sleep(20);
                    }
                }
                while (GetTickCount() < settleDeadline) {
                    MSG settleMsg;
                    while (PeekMessageW(&settleMsg, NULL, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&settleMsg);
                        DispatchMessageW(&settleMsg);
                    }
                    Sleep(10);
                }
                LogTime("Calling SaveScreenshot");
                app.SaveScreenshot(screenshotPath);
                LogTime("SaveScreenshot completed");
            } else {
                LogTime("Entering message loop");
                app.RunMessageLoop();
                LogTime("Message loop exited");
            }
        }
    }

    CoUninitialize();
    if (hMutex) CloseHandle(hMutex);
    return exitCode;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nShowCmd);
}
