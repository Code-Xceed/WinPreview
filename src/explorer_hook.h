#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace WinPreview {

struct HotkeyConfig {
    UINT vkCode = VK_SPACE;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool win = false;
    std::wstring name = L"Space";
};

class ExplorerHook {
public:
    static bool GetSelectedExplorerPath(std::wstring& outPath);
    static bool GetExplorerViewItems(const std::wstring& targetPath, std::vector<std::wstring>& outItems, int& outCurrentIndex);
    static bool SelectExplorerItem(const std::wstring& itemPath);
    static bool InstallHook(HWND targetHwnd);
    static void UninstallHook();
    static bool IsHookInstalled();
    static void EnsureHookActive(HWND targetHwnd);
    static void SetSuppressSpaceClose(bool suppress);

    // Hotkey configuration
    static HotkeyConfig GetHotkeyConfig();
    static void SetHotkeyConfig(const HotkeyConfig& config);
    static void LoadHotkeyConfig();
    static void SaveHotkeyConfig();
    static std::wstring FormatHotkeyName(UINT vkCode, bool ctrl, bool alt, bool shift, bool win);
    static void ReapplyRegisteredHotkey(HWND hwnd);

    // Single-instance and IPC message helpers
    static bool ForwardFileToExistingInstance(const std::wstring& filePath);
    static void PassSynthesizedSpace();
    static const UINT WM_WINPREVIEW_OPEN_FILE = WM_USER + 101;
    static const UINT WM_WINPREVIEW_EXPLORER_NAV = WM_USER + 102;
    static const UINT WM_WINPREVIEW_QUERY_PATH = WM_USER + 103;
    static const UINT WM_WINPREVIEW_EXPLORER_SPACE = WM_USER + 104;
    static const UINT WM_WINPREVIEW_CLOSE = WM_USER + 105;
    static const UINT WM_WINPREVIEW_NOTIFY_RUNNING = WM_USER + 106;
    static const UINT HOTKEY_ID = 9001;
};

} // namespace WinPreview
