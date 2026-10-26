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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
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

    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--background") == 0 || _wcsicmp(argv[i], L"/background") == 0) {
                isBackground = true;
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
                    DWORD existingPid = 0;
                    GetWindowThreadProcessId(hExisting, &existingPid);
                    if (existingPid) AllowSetForegroundWindow(existingPid);
                    PostMessageW(hExisting, WM_COMMAND, MAKEWPARAM(2001 /* IDM_TRAY_OPEN */, 0), 0);
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

    // Performance optimization: If a specific file is being opened on launch or capturing screenshot,
    // suppress initial empty state window rendering. Show window only after the target document is loaded.
    int initialShow = (isBackground || !fileToOpen.empty() || !screenshotPath.empty()) ? SW_HIDE : nCmdShow;
    bool showTrayBalloon = false; // Start background resident completely silently without notification lag or shell stalls

    int exitCode = 0;
    {
        WinPreview::AppWindow app;
        if (!app.Create(hInstance, initialShow, showTrayBalloon)) {
            LogTime("app.Create FAILED!");
            exitCode = 1;
        } else {
            LogTime("Window created");

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
