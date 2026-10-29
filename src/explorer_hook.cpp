#include "explorer_hook.h"
#include <shellapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlwapi.h>
#include <memory>
#include <atomic>

namespace WinPreview {

static HHOOK g_hKeyboardHook = NULL;
static HWND g_hTargetHwnd = NULL;
static DWORD g_lastSpaceTime = 0;
static HotkeyConfig g_hotkeyConfig;
static bool g_hotkeyConfigLoaded = false;
static std::atomic<bool> g_suppressSpaceClose{false};
static std::atomic<bool> g_synthesizingSpace{false};
static const wchar_t* REG_KEY_SETTINGS = L"Software\\WinPreview\\Settings";

void ExplorerHook::SetSuppressSpaceClose(bool suppress) {
    g_suppressSpaceClose.store(suppress, std::memory_order_relaxed);
}

void ExplorerHook::PassSynthesizedSpace() {
    g_synthesizingSpace.store(true, std::memory_order_release);
    keybd_event(VK_SPACE, 0, 0, 0);
    keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
}

static HANDLE g_hHookThread = NULL;
static DWORD g_hookThreadId = 0;
static HANDLE g_hHookReadyEvent = NULL;
static DWORD s_cachedShellPid = 0;


HotkeyConfig ExplorerHook::GetHotkeyConfig() {
    if (!g_hotkeyConfigLoaded) LoadHotkeyConfig();
    return g_hotkeyConfig;
}

void ExplorerHook::SetHotkeyConfig(const HotkeyConfig& config) {
    g_hotkeyConfig = config;
    g_hotkeyConfigLoaded = true;
    SaveHotkeyConfig();
    if (g_hTargetHwnd) {
        ReapplyRegisteredHotkey(g_hTargetHwnd);
    }
}

void ExplorerHook::LoadHotkeyConfig() {
    g_hotkeyConfig.vkCode = VK_SPACE;
    g_hotkeyConfig.ctrl = false;
    g_hotkeyConfig.alt = false;
    g_hotkeyConfig.shift = false;
    g_hotkeyConfig.win = false;
    g_hotkeyConfig.name = L"Space";

    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_SETTINGS, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwVk = 0;
        DWORD dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"HotkeyVK", NULL, NULL, (LPBYTE)&dwVk, &dwSize) == ERROR_SUCCESS) {
            g_hotkeyConfig.vkCode = dwVk;
        }
        DWORD dwMods = 0;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"HotkeyModifiers", NULL, NULL, (LPBYTE)&dwMods, &dwSize) == ERROR_SUCCESS) {
            g_hotkeyConfig.ctrl = (dwMods & 1) != 0;
            g_hotkeyConfig.alt = (dwMods & 2) != 0;
            g_hotkeyConfig.shift = (dwMods & 4) != 0;
            g_hotkeyConfig.win = (dwMods & 8) != 0;
        }
        wchar_t szName[128] = {};
        dwSize = sizeof(szName);
        if (RegQueryValueExW(hKey, L"HotkeyName", NULL, NULL, (LPBYTE)szName, &dwSize) == ERROR_SUCCESS && szName[0] != L'\0') {
            g_hotkeyConfig.name = szName;
        } else {
            g_hotkeyConfig.name = FormatHotkeyName(g_hotkeyConfig.vkCode, g_hotkeyConfig.ctrl, g_hotkeyConfig.alt, g_hotkeyConfig.shift, g_hotkeyConfig.win);
        }
        RegCloseKey(hKey);
    } else {
        SaveHotkeyConfig();
    }
    g_hotkeyConfigLoaded = true;
}

void ExplorerHook::SaveHotkeyConfig() {
    HKEY hKey = NULL;
    DWORD dwDisp = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_SETTINGS, 0, NULL, 0, KEY_WRITE, NULL, &hKey, &dwDisp) == ERROR_SUCCESS) {
        DWORD dwVk = g_hotkeyConfig.vkCode;
        RegSetValueExW(hKey, L"HotkeyVK", 0, REG_DWORD, (const BYTE*)&dwVk, sizeof(DWORD));
        DWORD dwMods = (g_hotkeyConfig.ctrl ? 1 : 0) |
                       (g_hotkeyConfig.alt ? 2 : 0) |
                       (g_hotkeyConfig.shift ? 4 : 0) |
                       (g_hotkeyConfig.win ? 8 : 0);
        RegSetValueExW(hKey, L"HotkeyModifiers", 0, REG_DWORD, (const BYTE*)&dwMods, sizeof(DWORD));
        RegSetValueExW(hKey, L"HotkeyName", 0, REG_SZ, (const BYTE*)g_hotkeyConfig.name.c_str(), (DWORD)((g_hotkeyConfig.name.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

std::wstring ExplorerHook::FormatHotkeyName(UINT vkCode, bool ctrl, bool alt, bool shift, bool win) {
    std::wstring str;
    if (win) str += L"Win + ";
    if (ctrl) str += L"Ctrl + ";
    if (alt) str += L"Alt + ";
    if (shift) str += L"Shift + ";

    if (vkCode == VK_SPACE) str += L"Space";
    else if (vkCode >= VK_F1 && vkCode <= VK_F24) {
        str += L"F" + std::to_wstring(vkCode - VK_F1 + 1);
    }
    else if (vkCode == VK_RETURN) str += L"Enter";
    else if (vkCode == VK_TAB) str += L"Tab";
    else if (vkCode == VK_ESCAPE) str += L"Esc";
    else if (vkCode == VK_BACK) str += L"Backspace";
    else if (vkCode == VK_DELETE) str += L"Delete";
    else if (vkCode == VK_INSERT) str += L"Insert";
    else if (vkCode == VK_HOME) str += L"Home";
    else if (vkCode == VK_END) str += L"End";
    else if (vkCode == VK_PRIOR) str += L"Page Up";
    else if (vkCode == VK_NEXT) str += L"Page Down";
    else if (vkCode >= 'A' && vkCode <= 'Z') {
        str += (wchar_t)vkCode;
    }
    else if (vkCode >= '0' && vkCode <= '9') {
        str += (wchar_t)vkCode;
    }
    else if (vkCode == VK_OEM_3) str += L"`";
    else {
        UINT sc = MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC);
        wchar_t keyName[64] = {};
        if (sc && GetKeyNameTextW((sc << 16), keyName, 64) > 0) {
            str += keyName;
        } else {
            str += L"Key " + std::to_wstring(vkCode);
        }
    }
    return str;
}

void ExplorerHook::ReapplyRegisteredHotkey(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    UnregisterHotKey(hwnd, HOTKEY_ID);

    if (!g_hotkeyConfigLoaded) LoadHotkeyConfig();

    // If hotkey has ANY modifier (Ctrl, Alt, Shift, Win), register natively with Windows Kernel!
    // Kernel hotkeys never time out, never drop, and survive lock/sleep natively.
    if (g_hotkeyConfig.ctrl || g_hotkeyConfig.alt || g_hotkeyConfig.shift || g_hotkeyConfig.win) {
        UINT mods = MOD_NOREPEAT;
        if (g_hotkeyConfig.ctrl) mods |= MOD_CONTROL;
        if (g_hotkeyConfig.alt) mods |= MOD_ALT;
        if (g_hotkeyConfig.shift) mods |= MOD_SHIFT;
        if (g_hotkeyConfig.win) mods |= MOD_WIN;
        RegisterHotKey(hwnd, HOTKEY_ID, mods, g_hotkeyConfig.vkCode);
    }
}

static bool ExtractSelectionFromShellView(IShellView* psv, std::wstring& outPath) {
    if (!psv) return false;

    // STRICT CHECK: Verify that items are actually selected.
    // In Windows Shell, an unselected folder view still reports a "focused item" (last clicked index).
    // If SVGIO_SELECTION count is 0, nothing is selected in this view!
    IFolderView* pfv = nullptr;
    bool hasPfv = SUCCEEDED(psv->QueryInterface(IID_IFolderView, (void**)&pfv)) && pfv != nullptr;
    if (hasPfv) {
        int selCount = 0;
        HRESULT hr = pfv->ItemCount(SVGIO_SELECTION, &selCount);
        if (SUCCEEDED(hr) && selCount <= 0) {
            pfv->Release();
            return false; // Zero items selected: immediately return false!
        }
    }

    // Tier 1: Modern IFolderView2::GetSelection (Direct IShellItemArray with SIGDN_FILESYSPATH)
    IFolderView2* pfv2 = nullptr;
    if (SUCCEEDED(psv->QueryInterface(IID_IFolderView2, (void**)&pfv2)) && pfv2) {
        IShellItemArray* psia = nullptr;
        if (SUCCEEDED(pfv2->GetSelection(FALSE, &psia)) && psia) {
            DWORD count = 0;
            if (SUCCEEDED(psia->GetCount(&count)) && count > 0) {
                IShellItem* psi = nullptr;
                if (SUCCEEDED(psia->GetItemAt(0, &psi)) && psi) {
                    PWSTR psz = nullptr;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                        outPath = psz;
                        CoTaskMemFree(psz);
                    } else if (SUCCEEDED(psi->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &psz)) && psz) {
                        if (GetFileAttributesW(psz) != INVALID_FILE_ATTRIBUTES) {
                            outPath = psz;
                        }
                        CoTaskMemFree(psz);
                    }
                    psi->Release();
                }
            }
            psia->Release();
        }
        pfv2->Release();
        if (!outPath.empty()) {
            if (hasPfv) pfv->Release();
            return true;
        }
    }

    // Tier 2: Universal CF_HDROP clipboard / drop data object from SVGIO_SELECTION
    IDataObject* pdo = nullptr;
    if (SUCCEEDED(psv->GetItemObject(SVGIO_SELECTION, IID_IDataObject, (void**)&pdo)) && pdo) {
        FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM med = {};
        if (SUCCEEDED(pdo->GetData(&fmt, &med))) {
            HDROP hDrop = (HDROP)GlobalLock(med.hGlobal);
            if (hDrop) {
                WCHAR szPath[32768] = {};
                if (DragQueryFileW(hDrop, 0, szPath, 32768)) {
                    outPath = szPath;
                }
                GlobalUnlock(med.hGlobal);
            }
            ReleaseStgMedium(&med);
        }
        pdo->Release();
        if (!outPath.empty()) {
            if (hasPfv) pfv->Release();
            return true;
        }
    }

    // Tier 3: IFolderView::Items(SVGIO_SELECTION, ...)
    // Note: STRICTLY avoid GetFocusedItem here because focused item remains set even when unselected!
    if (hasPfv) {
        IEnumIDList* pEnum = nullptr;
        if (SUCCEEDED(pfv->Items(SVGIO_SELECTION, IID_IEnumIDList, (void**)&pEnum)) && pEnum) {
            PITEMID_CHILD pidl = nullptr;
            ULONG fetched = 0;
            if (pEnum->Next(1, &pidl, &fetched) == S_OK && pidl) {
                IPersistFolder2* ppf2 = nullptr;
                if (SUCCEEDED(pfv->GetFolder(IID_IPersistFolder2, (void**)&ppf2)) && ppf2) {
                    PIDLIST_ABSOLUTE folderPidl = nullptr;
                    if (SUCCEEDED(ppf2->GetCurFolder(&folderPidl)) && folderPidl) {
                        PIDLIST_ABSOLUTE fullPidl = ILCombine(folderPidl, pidl);
                        if (fullPidl) {
                            WCHAR szPath[32768] = {};
                            if (SHGetPathFromIDListW(fullPidl, szPath)) {
                                outPath = szPath;
                            }
                            ILFree(fullPidl);
                        }
                        ILFree(folderPidl);
                    }
                    ppf2->Release();
                }
                ILFree(pidl);
            }
            pEnum->Release();
        }
        pfv->Release();
        if (!outPath.empty()) return true;
    }

    return false;
}

static bool ExtractFromWebBrowserApp(IWebBrowserApp* pwba, std::wstring& outPath) {
    if (!pwba) return false;

    // 1. Primary: Direct C++ IShellBrowser -> QueryActiveShellView (always accesses the active tab)
    IServiceProvider* psp = nullptr;
    if (SUCCEEDED(pwba->QueryInterface(IID_IServiceProvider, (void**)&psp)) && psp) {
        IShellBrowser* psb = nullptr;
        if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb)) && psb) {
            IShellView* psv = nullptr;
            if (SUCCEEDED(psb->QueryActiveShellView(&psv)) && psv) {
                bool ok = ExtractSelectionFromShellView(psv, outPath);
                psv->Release();
                psb->Release();
                psp->Release();
                // Authoritative for active tab: whether selection was found or empty
                return ok && !outPath.empty();
            }
            psb->Release();
        }
        psp->Release();
    }

    // 2. Secondary: IShellFolderViewDual (SelectedItems ONLY - NEVER fall back to FocusedItem!)
    IDispatch* pDoc = nullptr;
    if (SUCCEEDED(pwba->get_Document(&pDoc)) && pDoc) {
        IShellFolderViewDual* psfvd = nullptr;
        if (SUCCEEDED(pDoc->QueryInterface(IID_IShellFolderViewDual, (void**)&psfvd)) && psfvd) {
            FolderItems* pItems = nullptr;
            if (SUCCEEDED(psfvd->SelectedItems(&pItems)) && pItems) {
                long selCount = 0;
                pItems->get_Count(&selCount);
                if (selCount > 0) {
                    VARIANT v0; v0.vt = VT_I4; v0.lVal = 0;
                    FolderItem* pFi = nullptr;
                    if (SUCCEEDED(pItems->Item(v0, &pFi)) && pFi) {
                        BSTR bstr = nullptr;
                        pFi->get_Path(&bstr);
                        if (bstr) {
                            outPath = bstr;
                            SysFreeString(bstr);
                        }
                        pFi->Release();
                    }
                }
                pItems->Release();
            }
            psfvd->Release();
        }
        pDoc->Release();
    }
    return !outPath.empty();
}

bool ExplorerHook::GetSelectedExplorerPath(std::wstring& outPath) {
    outPath.clear();
    IShellWindows* psw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL, IID_IShellWindows, (void**)&psw)) || !psw) {
        return false;
    }

    HWND fgHwnd = GetForegroundWindow();
    HWND topHwnd = fgHwnd ? GetAncestor(fgHwnd, GA_ROOT) : NULL;
    if (!topHwnd) topHwnd = fgHwnd;

    WCHAR clsTop[64] = {};
    if (topHwnd) GetClassNameW(topHwnd, clsTop, 64);
    WCHAR clsFg[64] = {};
    if (fgHwnd) GetClassNameW(fgHwnd, clsFg, 64);

    bool isDesktop = (wcscmp(clsTop, L"Progman") == 0 || wcscmp(clsTop, L"WorkerW") == 0 ||
                      wcscmp(clsFg, L"Progman") == 0 || wcscmp(clsFg, L"WorkerW") == 0);

    bool isExplorer = (wcscmp(clsTop, L"CabinetWClass") == 0 || wcscmp(clsTop, L"ExploreWClass") == 0 ||
                       wcscmp(clsTop, L"ShellTabWindowClass") == 0 || wcscmp(clsFg, L"ShellTabWindowClass") == 0 ||
                       wcscmp(clsTop, L"XamlExplorerHostIslandWindow") == 0 || wcscmp(clsFg, L"XamlExplorerHostIslandWindow") == 0);

    if (!isDesktop && !isExplorer && topHwnd) {
        DWORD topPid = 0;
        GetWindowThreadProcessId(topHwnd, &topPid);
        if (s_cachedShellPid == 0) {
            HWND hShell = GetShellWindow();
            if (hShell) GetWindowThreadProcessId(hShell, &s_cachedShellPid);
        }
        if (s_cachedShellPid != 0 && topPid == s_cachedShellPid) {
            isExplorer = true;
        }
    }

    bool found = false;

    // Helper lambda to query Desktop ShellView
    auto queryDesktop = [&]() -> bool {
        VARIANT vEmpty = {};
        VariantInit(&vEmpty);
        long lHwnd = 0;
        IDispatch* pDisp = nullptr;
        if (SUCCEEDED(psw->FindWindowSW(&vEmpty, &vEmpty, 8 /* SWC_DESKTOP */, &lHwnd, 1, &pDisp)) && pDisp) {
            IServiceProvider* psp = nullptr;
            if (SUCCEEDED(pDisp->QueryInterface(IID_IServiceProvider, (void**)&psp)) && psp) {
                IShellBrowser* psb = nullptr;
                if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb)) && psb) {
                    IShellView* psv = nullptr;
                    if (SUCCEEDED(psb->QueryActiveShellView(&psv)) && psv) {
                        found = ExtractSelectionFromShellView(psv, outPath);
                        psv->Release();
                    }
                    psb->Release();
                }
                psp->Release();
            }
            pDisp->Release();
        }
        return found && !outPath.empty();
    };

    // 1. If Desktop is the active foreground window: query Desktop ONLY.
    // If nothing is selected on Desktop, return false immediately (never fallback to background Explorer windows).
    if (isDesktop) {
        queryDesktop();
        psw->Release();
        if (found && !outPath.empty()) {
            DWORD attrs = GetFileAttributesW(outPath.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) {
                outPath.clear();
                return false;
            }
            return true;
        }
        return false;
    }

    // 2. If File Explorer is the active foreground window: query the active Explorer window/tab ONLY.
    if (isExplorer) {
        long count = 0;
        psw->get_Count(&count);

        for (long i = 0; i < count && !found; ++i) {
            VARIANT v; v.vt = VT_I4; v.lVal = i;
            IDispatch* pdisp = nullptr;
            if (SUCCEEDED(psw->Item(v, &pdisp)) && pdisp) {
                IWebBrowserApp* pwba = nullptr;
                if (SUCCEEDED(pdisp->QueryInterface(IID_IWebBrowserApp, (void**)&pwba)) && pwba) {
                    SHANDLE_PTR lHwnd = 0;
                    pwba->get_HWND(&lHwnd);
                    HWND hWba = (HWND)lHwnd;
                    bool isFgMatch = (hWba == topHwnd || hWba == fgHwnd ||
                                      GetAncestor(hWba, GA_ROOT) == topHwnd ||
                                      (topHwnd && IsChild(topHwnd, hWba)));
                    if (isFgMatch) {
                        found = ExtractFromWebBrowserApp(pwba, outPath);
                    }
                    pwba->Release();
                }
                pdisp->Release();
            }
        }

        psw->Release();
        if (found && !outPath.empty()) {
            DWORD attrs = GetFileAttributesW(outPath.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) {
                outPath.clear();
                return false;
            }
            return true;
        }
        // Active Explorer window had NO selection: STRICT return false!
        // Never fall back to background Explorer windows or Desktop!
        return false;
    }

    // 3. Fallback for non-Explorer / non-Desktop foreground (e.g. invoked via custom global hotkey from other apps):
    // Check visible Explorer windows
    long count = 0;
    psw->get_Count(&count);
    for (long i = 0; i < count && !found; ++i) {
        VARIANT v; v.vt = VT_I4; v.lVal = i;
        IDispatch* pdisp = nullptr;
        if (SUCCEEDED(psw->Item(v, &pdisp)) && pdisp) {
            IWebBrowserApp* pwba = nullptr;
            if (SUCCEEDED(pdisp->QueryInterface(IID_IWebBrowserApp, (void**)&pwba)) && pwba) {
                SHANDLE_PTR lHwnd = 0;
                pwba->get_HWND(&lHwnd);
                if (lHwnd && IsWindow((HWND)lHwnd) && IsWindowVisible((HWND)lHwnd)) {
                    found = ExtractFromWebBrowserApp(pwba, outPath);
                }
                pwba->Release();
            }
            pdisp->Release();
        }
    }

    if (!found) {
        queryDesktop();
    }

    psw->Release();

    if (found && !outPath.empty()) {
        DWORD attrs = GetFileAttributesW(outPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            outPath.clear();
            found = false;
        }
    }

    return found && !outPath.empty();
}

bool ExplorerHook::GetExplorerViewItems(
    const std::wstring& targetPath,
    std::vector<std::wstring>& outItems,
    int& outCurrentIndex)
{
    outItems.clear();
    outCurrentIndex = -1;
    if (targetPath.empty()) return false;

    std::wstring current = targetPath;
    while (!current.empty() && (current.back() == L'\\' || current.back() == L'/')) {
        current.pop_back();
    }
    size_t lastSlash = current.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) return false;

    std::wstring folder = current.substr(0, lastSlash);

    IShellWindows* psw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL, IID_IShellWindows, (void**)&psw)) || !psw) {
        return false;
    }

    auto extractItemsFromView = [&](IShellView* psv) -> bool {
        if (!psv) return false;
        IFolderView2* pfv2 = nullptr;
        if (FAILED(psv->QueryInterface(IID_IFolderView2, (void**)&pfv2)) || !pfv2) {
            return false;
        }

        int count = 0;
        HRESULT hr = pfv2->ItemCount(SVGIO_ALLVIEW, &count);
        if (FAILED(hr) || count <= 0) {
            pfv2->Release();
            return false;
        }

        outItems.reserve(count);
        for (int i = 0; i < count; ++i) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(pfv2->GetItem(i, IID_IShellItem, (void**)&psi)) && psi) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    std::wstring itemPath = psz;
                    CoTaskMemFree(psz);
                    if (!itemPath.empty()) {
                        if (_wcsicmp(itemPath.c_str(), targetPath.c_str()) == 0 ||
                            _wcsicmp(itemPath.c_str(), current.c_str()) == 0) {
                            outCurrentIndex = (int)outItems.size();
                        }
                        outItems.push_back(std::move(itemPath));
                    }
                }
                psi->Release();
            }
        }
        pfv2->Release();
        return !outItems.empty();
    };

    bool found = false;

    // Check Desktop if the target path is in Desktop directory
    WCHAR desktopPath[MAX_PATH] = {};
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath);
    WCHAR commonDesktopPath[MAX_PATH] = {};
    SHGetFolderPathW(NULL, CSIDL_COMMON_DESKTOPDIRECTORY, NULL, 0, commonDesktopPath);

    bool isTargetOnDesktop = (_wcsicmp(folder.c_str(), desktopPath) == 0 ||
                              _wcsicmp(folder.c_str(), commonDesktopPath) == 0);

    if (isTargetOnDesktop) {
        VARIANT vEmpty = {};
        VariantInit(&vEmpty);
        long lHwnd = 0;
        IDispatch* pDisp = nullptr;
        if (SUCCEEDED(psw->FindWindowSW(&vEmpty, &vEmpty, 8 /* SWC_DESKTOP */, &lHwnd, 1, &pDisp)) && pDisp) {
            IServiceProvider* psp = nullptr;
            if (SUCCEEDED(pDisp->QueryInterface(IID_IServiceProvider, (void**)&psp)) && psp) {
                IShellBrowser* psb = nullptr;
                if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb)) && psb) {
                    IShellView* psv = nullptr;
                    if (SUCCEEDED(psb->QueryActiveShellView(&psv)) && psv) {
                        found = extractItemsFromView(psv);
                        psv->Release();
                    }
                    psb->Release();
                }
                psp->Release();
            }
            pDisp->Release();
        }
    }

    if (!found) {
        long count = 0;
        psw->get_Count(&count);

        for (long i = 0; i < count && !found; ++i) {
            VARIANT v; v.vt = VT_I4; v.lVal = i;
            IDispatch* pdisp = nullptr;
            if (SUCCEEDED(psw->Item(v, &pdisp)) && pdisp) {
                IWebBrowserApp* pwba = nullptr;
                if (SUCCEEDED(pdisp->QueryInterface(IID_IWebBrowserApp, (void**)&pwba)) && pwba) {
                    IServiceProvider* psp = nullptr;
                    if (SUCCEEDED(pwba->QueryInterface(IID_IServiceProvider, (void**)&psp)) && psp) {
                        IShellBrowser* psb = nullptr;
                        if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb)) && psb) {
                            IShellView* psv = nullptr;
                            if (SUCCEEDED(psb->QueryActiveShellView(&psv)) && psv) {
                                IFolderView* pfv = nullptr;
                                if (SUCCEEDED(psv->QueryInterface(IID_IFolderView, (void**)&pfv)) && pfv) {
                                    IPersistFolder2* ppf2 = nullptr;
                                    if (SUCCEEDED(pfv->GetFolder(IID_IPersistFolder2, (void**)&ppf2)) && ppf2) {
                                        PIDLIST_ABSOLUTE folderPidl = nullptr;
                                        if (SUCCEEDED(ppf2->GetCurFolder(&folderPidl)) && folderPidl) {
                                            WCHAR szWindowFolder[MAX_PATH] = {};
                                            if (SHGetPathFromIDListW(folderPidl, szWindowFolder)) {
                                                if (_wcsicmp(szWindowFolder, folder.c_str()) == 0) {
                                                    found = extractItemsFromView(psv);
                                                }
                                            }
                                            ILFree(folderPidl);
                                        }
                                        ppf2->Release();
                                    }
                                    pfv->Release();
                                }
                                psv->Release();
                            }
                            psb->Release();
                        }
                        psp->Release();
                    }
                    pwba->Release();
                }
                pdisp->Release();
            }
        }
    }

    psw->Release();
    return found && !outItems.empty();
}

bool ExplorerHook::SelectExplorerItem(const std::wstring& itemPath) {
    if (itemPath.empty()) return false;

    std::wstring current = itemPath;
    while (!current.empty() && (current.back() == L'\\' || current.back() == L'/')) {
        current.pop_back();
    }
    size_t lastSlash = current.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) return false;

    std::wstring folder = current.substr(0, lastSlash);

    IShellWindows* psw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL, IID_IShellWindows, (void**)&psw)) || !psw) {
        return false;
    }

    auto selectInView = [&](IShellView* psv) -> bool {
        if (!psv) return false;
        IFolderView2* pfv2 = nullptr;
        if (FAILED(psv->QueryInterface(IID_IFolderView2, (void**)&pfv2)) || !pfv2) {
            return false;
        }

        int count = 0;
        pfv2->ItemCount(SVGIO_ALLVIEW, &count);
        bool selected = false;

        for (int i = 0; i < count; ++i) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(pfv2->GetItem(i, IID_IShellItem, (void**)&psi)) && psi) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    if (_wcsicmp(psz, itemPath.c_str()) == 0 || _wcsicmp(psz, current.c_str()) == 0) {
                        pfv2->SelectItem(i, SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_ENSUREVISIBLE | SVSI_FOCUSED);
                        selected = true;
                    }
                    CoTaskMemFree(psz);
                }
                psi->Release();
                if (selected) break;
            }
        }

        pfv2->Release();
        return selected;
    };

    bool done = false;

    WCHAR desktopPath[MAX_PATH] = {};
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath);
    WCHAR commonDesktopPath[MAX_PATH] = {};
    SHGetFolderPathW(NULL, CSIDL_COMMON_DESKTOPDIRECTORY, NULL, 0, commonDesktopPath);

    bool isTargetOnDesktop = (_wcsicmp(folder.c_str(), desktopPath) == 0 ||
                              _wcsicmp(folder.c_str(), commonDesktopPath) == 0);

    if (isTargetOnDesktop) {
        VARIANT vEmpty = {};
        VariantInit(&vEmpty);
        long lHwnd = 0;
        IDispatch* pDisp = nullptr;
        if (SUCCEEDED(psw->FindWindowSW(&vEmpty, &vEmpty, 8 /* SWC_DESKTOP */, &lHwnd, 1, &pDisp)) && pDisp) {
            IServiceProvider* psp = nullptr;
            if (SUCCEEDED(pDisp->QueryInterface(IID_IServiceProvider, (void**)&psp)) && psp) {
                IShellBrowser* psb = nullptr;
                if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb)) && psb) {
                    IShellView* psv = nullptr;
                    if (SUCCEEDED(psb->QueryActiveShellView(&psv)) && psv) {
                        done = selectInView(psv);
                        psv->Release();
                    }
                    psb->Release();
                }
                psp->Release();
            }
            pDisp->Release();
        }
    }

    if (!done) {
        long count = 0;
        psw->get_Count(&count);

        for (long i = 0; i < count && !done; ++i) {
            VARIANT v; v.vt = VT_I4; v.lVal = i;
            IDispatch* pdisp = nullptr;
            if (SUCCEEDED(psw->Item(v, &pdisp)) && pdisp) {
                IWebBrowserApp* pwba = nullptr;
                if (SUCCEEDED(pdisp->QueryInterface(IID_IWebBrowserApp, (void**)&pwba)) && pwba) {
                    IServiceProvider* psp = nullptr;
                    if (SUCCEEDED(pwba->QueryInterface(IID_IServiceProvider, (void**)&psp)) && psp) {
                        IShellBrowser* psb = nullptr;
                        if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb)) && psb) {
                            IShellView* psv = nullptr;
                            if (SUCCEEDED(psb->QueryActiveShellView(&psv)) && psv) {
                                IFolderView* pfv = nullptr;
                                if (SUCCEEDED(psv->QueryInterface(IID_IFolderView, (void**)&pfv)) && pfv) {
                                    IPersistFolder2* ppf2 = nullptr;
                                    if (SUCCEEDED(pfv->GetFolder(IID_IPersistFolder2, (void**)&ppf2)) && ppf2) {
                                        PIDLIST_ABSOLUTE folderPidl = nullptr;
                                        if (SUCCEEDED(ppf2->GetCurFolder(&folderPidl)) && folderPidl) {
                                            WCHAR szWindowFolder[MAX_PATH] = {};
                                            if (SHGetPathFromIDListW(folderPidl, szWindowFolder)) {
                                                if (_wcsicmp(szWindowFolder, folder.c_str()) == 0) {
                                                    done = selectInView(psv);
                                                }
                                            }
                                            ILFree(folderPidl);
                                        }
                                        ppf2->Release();
                                    }
                                    pfv->Release();
                                }
                                psv->Release();
                            }
                            psb->Release();
                        }
                        psp->Release();
                    }
                    pwba->Release();
                }
                pdisp->Release();
            }
        }
    }

    psw->Release();
    return done;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        if (g_synthesizingSpace.exchange(false, std::memory_order_acq_rel)) {
            return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
        }

        KBDLLHOOKSTRUCT* pkb = (KBDLLHOOKSTRUCT*)lParam;
        if (!pkb) return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);

        if (!g_hotkeyConfigLoaded) ExplorerHook::LoadHotkeyConfig();
        const HotkeyConfig curCfg = g_hotkeyConfig;

        // ULTRA-FAST SHORT CIRCUIT: If key does not match target hotkey,
        // check only escape/arrows if WinPreview is visible, otherwise return IMMEDIATELY.
        // Takes < 0.001 ms, zero GetAsyncKeyState calls, eliminates typing lag in all apps!
        if (pkb->vkCode != curCfg.vkCode) {
            if (g_hTargetHwnd && IsWindow(g_hTargetHwnd) && IsWindowVisible(g_hTargetHwnd)) {
                if (pkb->vkCode == VK_ESCAPE) {
                    // Close the preview only when the user is interacting with it
                    // (or with the Explorer session it overlays). If focus has
                    // moved to another app, let Escape pass through untouched so
                    // that app's dialogs, menus and in-place edits keep working.
                    HWND fgHwnd = GetForegroundWindow();
                    HWND topHwnd = fgHwnd ? GetAncestor(fgHwnd, GA_ROOT) : NULL;
                    if (!fgHwnd) topHwnd = NULL;

                    bool previewFocused = (fgHwnd == g_hTargetHwnd) || (topHwnd == g_hTargetHwnd);
                    bool explorerFocused = false;
                    if (!previewFocused && fgHwnd) {
                        WCHAR clsTop[64] = {}, clsFg[64] = {};
                        if (topHwnd) GetClassNameW(topHwnd, clsTop, 64);
                        GetClassNameW(fgHwnd, clsFg, 64);
                        explorerFocused =
                            wcscmp(clsTop, L"CabinetWClass") == 0 ||
                            wcscmp(clsTop, L"ExploreWClass") == 0 ||
                            wcscmp(clsTop, L"ShellTabWindowClass") == 0 ||
                            wcscmp(clsFg, L"ShellTabWindowClass") == 0 ||
                            wcscmp(clsTop, L"Progman") == 0 ||
                            wcscmp(clsTop, L"WorkerW") == 0 ||
                            wcscmp(clsFg, L"WorkerW") == 0;
                    }

                    if (previewFocused || explorerFocused) {
                        PostMessageW(g_hTargetHwnd, ExplorerHook::WM_WINPREVIEW_CLOSE, 0, 0);
                        return 1;
                    }
                    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
                }
                else if (pkb->vkCode == VK_UP || pkb->vkCode == VK_DOWN || pkb->vkCode == VK_LEFT || pkb->vkCode == VK_RIGHT) {
                    HWND fgHwnd = GetForegroundWindow();
                    if (fgHwnd != g_hTargetHwnd) {
                        PostMessageW(g_hTargetHwnd, ExplorerHook::WM_WINPREVIEW_EXPLORER_NAV, 1, 0);
                    }
                }
            }
            return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
        }

        // Key matches target hotkey: now check modifiers
        bool isCtrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool isAltDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool isShiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool isWinDown = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);

        if (isCtrlDown != curCfg.ctrl || isAltDown != curCfg.alt ||
            isShiftDown != curCfg.shift || isWinDown != curCfg.win) {
            return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
        }

        if (!g_hTargetHwnd || !IsWindow(g_hTargetHwnd)) {
            return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
        }

        DWORD now = GetTickCount();
        if (now - g_lastSpaceTime < 150) {
            return 1; // Debounce rapid key repeat
        }

        HWND fgHwnd = GetForegroundWindow();
        if (!fgHwnd) return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);

        HWND topHwnd = GetAncestor(fgHwnd, GA_ROOT);
        if (!topHwnd) topHwnd = fgHwnd;

        // 1. If WinPreview itself is the active foreground window
        if (fgHwnd == g_hTargetHwnd || topHwnd == g_hTargetHwnd) {
            if (g_suppressSpaceClose.load(std::memory_order_relaxed)) {
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }
            g_lastSpaceTime = now;
            PostMessageW(g_hTargetHwnd, ExplorerHook::WM_WINPREVIEW_CLOSE, 0, 0);
            return 1; // Handled & swallowed
        }

        // 2. WinPreview is hidden. Check if foreground is File Explorer or Desktop:
        bool isBareSpace = (curCfg.vkCode == VK_SPACE && !curCfg.ctrl && !curCfg.alt && !curCfg.shift && !curCfg.win);

        if (isBareSpace) {
            WCHAR clsTop[64] = {};
            GetClassNameW(topHwnd, clsTop, 64);
            WCHAR clsFg[64] = {};
            GetClassNameW(fgHwnd, clsFg, 64);

            bool isExplorerOrDesktop = (wcscmp(clsTop, L"CabinetWClass") == 0 || wcscmp(clsTop, L"ExploreWClass") == 0 ||
                                        wcscmp(clsTop, L"ShellTabWindowClass") == 0 || wcscmp(clsFg, L"ShellTabWindowClass") == 0 ||
                                        wcscmp(clsTop, L"XamlExplorerHostIslandWindow") == 0 || wcscmp(clsFg, L"XamlExplorerHostIslandWindow") == 0 ||
                                        wcscmp(clsTop, L"Progman") == 0 || wcscmp(clsTop, L"WorkerW") == 0 ||
                                        wcscmp(clsFg, L"Progman") == 0 || wcscmp(clsFg, L"WorkerW") == 0 ||
                                        wcscmp(clsTop, L"Shell_TrayWnd") == 0 || wcscmp(clsFg, L"Shell_TrayWnd") == 0 ||
                                        wcscmp(clsTop, L"#32770") == 0 || wcscmp(clsFg, L"#32770") == 0);

            if (!isExplorerOrDesktop) {
                if (s_cachedShellPid == 0) {
                    HWND hShell = GetShellWindow();
                    if (hShell) GetWindowThreadProcessId(hShell, &s_cachedShellPid);
                }
                DWORD fgPid = 0;
                GetWindowThreadProcessId(topHwnd, &fgPid);
                if (s_cachedShellPid != 0 && fgPid == s_cachedShellPid) {
                    isExplorerOrDesktop = true;
                }
            }

            if (!isExplorerOrDesktop) {
                return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
            }

            // In Explorer/Desktop: if user is typing in an Edit control (renaming file, search box)
            GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
            DWORD fgThread = GetWindowThreadProcessId(fgHwnd, NULL);
            if (GetGUIThreadInfo(fgThread, &gti) && gti.hwndFocus) {
                WCHAR focusCls[64] = {};
                GetClassNameW(gti.hwndFocus, focusCls, 64);
                if (_wcsicmp(focusCls, L"Edit") == 0 || _wcsicmp(focusCls, L"RichEdit") == 0 ||
                    _wcsicmp(focusCls, L"RICHEDIT50W") == 0 || _wcsicmp(focusCls, L"SearchBox") == 0) {
                    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
                }
            }
        }

        g_lastSpaceTime = now;
        PostMessageW(g_hTargetHwnd, ExplorerHook::WM_WINPREVIEW_EXPLORER_SPACE, 0, 0);
        return 1; // Eat the keystroke
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

static DWORD WINAPI HookThreadProc(LPVOID lpParam) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    MSG msg;
    PeekMessageW(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(NULL), 0);

    if (g_hHookReadyEvent) {
        SetEvent(g_hHookReadyEvent);
    }

    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }
    return 0;
}

bool ExplorerHook::InstallHook(HWND targetHwnd) {
    g_hTargetHwnd = targetHwnd;
    LoadHotkeyConfig();
    ReapplyRegisteredHotkey(targetHwnd);

    // If hook thread already running and valid, keep it
    if (g_hHookThread != NULL && WaitForSingleObject(g_hHookThread, 0) == WAIT_TIMEOUT && g_hKeyboardHook != NULL) {
        return true;
    }

    // Clean up any stale thread
    UninstallHook();
    g_hTargetHwnd = targetHwnd;

    g_hHookReadyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hHookThread = CreateThread(NULL, 0, HookThreadProc, NULL, 0, &g_hookThreadId);
    if (!g_hHookThread) {
        if (g_hHookReadyEvent) { CloseHandle(g_hHookReadyEvent); g_hHookReadyEvent = NULL; }
        return false;
    }

    WaitForSingleObject(g_hHookReadyEvent, 50); // Fast 50ms wait instead of 500ms
    CloseHandle(g_hHookReadyEvent);
    g_hHookReadyEvent = NULL;

    return (g_hKeyboardHook != NULL);
}

void ExplorerHook::UninstallHook() {
    if (g_hTargetHwnd && IsWindow(g_hTargetHwnd)) {
        UnregisterHotKey(g_hTargetHwnd, HOTKEY_ID);
    }
    if (g_hookThreadId != 0) {
        PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
        g_hookThreadId = 0;
    }
    if (g_hHookThread != NULL) {
        WaitForSingleObject(g_hHookThread, 1000);
        CloseHandle(g_hHookThread);
        g_hHookThread = NULL;
    }
    g_hKeyboardHook = NULL;
}

void ExplorerHook::EnsureHookActive(HWND targetHwnd) {
    if (!targetHwnd || !IsWindow(targetHwnd)) return;
    s_cachedShellPid = 0; // Invalidate cached shell PID so Explorer restarts/session changes are handled immediately
    g_hTargetHwnd = targetHwnd;

    bool threadAlive = (g_hHookThread != NULL && WaitForSingleObject(g_hHookThread, 0) == WAIT_TIMEOUT);
    if (!threadAlive || g_hKeyboardHook == NULL) {
        InstallHook(targetHwnd);
    } else {
        ReapplyRegisteredHotkey(targetHwnd);
    }
}

bool ExplorerHook::IsHookInstalled() {
    return (g_hKeyboardHook != NULL && g_hHookThread != NULL && WaitForSingleObject(g_hHookThread, 0) == WAIT_TIMEOUT);
}

bool ExplorerHook::ForwardFileToExistingInstance(const std::wstring& filePath) {
    HWND hExisting = FindWindowW(L"WinPreviewMainWindow", NULL);
    if (!hExisting) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hExisting, &pid);
    if (pid) AllowSetForegroundWindow(pid);

    COPYDATASTRUCT cds = {};
    cds.dwData = 1; // Open file command
    cds.cbData = (DWORD)((filePath.length() + 1) * sizeof(wchar_t));
    cds.lpData = (void*)filePath.c_str();

    DWORD_PTR dwResult = 0;
    // Fast 300ms timeout with SMTO_NORMAL so the forwarding call never hangs
    LRESULT lr = SendMessageTimeoutW(hExisting, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG | SMTO_NORMAL, 300, &dwResult);
    return (lr != 0);
}

} // namespace WinPreview
