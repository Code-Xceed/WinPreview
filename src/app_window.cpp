#include "app_window.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <shlwapi.h>
#include <commdlg.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>

namespace WinPreview {

enum ButtonId {
    BTN_NONE = 0,
    BTN_SIDEBAR = 1,
    BTN_PREV_PAGE = 2,
    BTN_NEXT_PAGE = 3,
    BTN_ZOOM_OUT = 4,
    BTN_ZOOM_IN = 5,
    BTN_ZOOM_FIT = 6,
    BTN_ZOOM_RESET = 7,
    BTN_ROTATE_L = 8,
    BTN_ROTATE_R = 9,
    BTN_PLAY_PAUSE = 10,
    BTN_MUTE = 11,
    BTN_FULLSCREEN = 12,
    BTN_LOOP = 13,
    BTN_TOOLBAR_FS = 14,
    BTN_SKIP_BACK = 15,
    BTN_SKIP_FORWARD = 16,
    BTN_TOOLBAR_MUTE = 17,
    BTN_HUD_LOOP = 18,
    BTN_SEARCH = 19,
    BTN_CROP = 21,
    BTN_INFO = 22,
    BTN_SIDEBAR_TAB_THUMBS = 23,
    BTN_SIDEBAR_TAB_OUTLINE = 24,
    BTN_SEARCH_PREV = 25,
    BTN_SEARCH_NEXT = 26,
    BTN_SEARCH_CLOSE = 27,
    BTN_COPY_TEXT = 28,
    BTN_WRAP_TEXT = 29,
    BTN_TABLE_VIEW = 30,
    BTN_SAVE_FILE = 31,
    BTN_UNDO = 32,
    BTN_REDO = 33,
    BTN_CUT = 34,
    BTN_PASTE = 35,
    BTN_SETTINGS = 36,
    BTN_SETTINGS_CLOSE = 37,
    BTN_SETTINGS_RECORD = 38,
    BTN_SETTINGS_RESET = 39,
    BTN_SETTINGS_DONE = 40,
    BTN_SETTINGS_PRESET_1 = 41,
    BTN_SETTINGS_PRESET_2 = 42,
    BTN_SETTINGS_PRESET_3 = 43,
    BTN_SETTINGS_PRESET_4 = 44,
    BTN_SETTINGS_PRESET_5 = 45,
    BTN_SETTINGS_AUTOSTART = 46,
    BTN_UNSUPPORTED_OPEN = 47,
    BTN_UNSUPPORTED_REVEAL = 48,
    BTN_UNSUPPORTED_COPY = 51,
    BTN_SYSTEM_PRIMARY = 49,
    BTN_SYSTEM_SECONDARY = 50,
    BTN_OPEN_WITH = 52,
    BTN_OVERFLOW_MENU = 53,
    BTN_INSPECTOR_CLOSE = 54,
    BTN_INSPECTOR_COPY = 55
};

static void CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return;
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.length() + 1) * sizeof(WCHAR);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            memcpy(GlobalLock(hMem), text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* app = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        app = (AppWindow*)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)app);
        app->m_hwnd = hwnd;
    } else {
        app = (AppWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (app) {
        return app->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

AppWindow::AppWindow() {}

static UINT g_wmTaskbarCreated = 0;
static HMODULE g_hWtsApi32 = NULL;

AppWindow::~AppWindow() {
    if (g_hWtsApi32 && m_hwnd) {
        typedef BOOL (WINAPI *PFN_WTSUnRegisterSessionNotification)(HWND);
        auto fnWtsUnreg = (PFN_WTSUnRegisterSessionNotification)GetProcAddress(g_hWtsApi32, "WTSUnRegisterSessionNotification");
        if (fnWtsUnreg) fnWtsUnreg(m_hwnd);
    }
    RemoveTrayIcon();
    ExplorerHook::UninstallHook();
    SetCursorVisibility(true);
    DestroyHudWindow();
    SafeRelease(m_imageBitmap);
    SafeRelease(m_pdfPageBitmap);
    SafeRelease(m_audioAlbumArt);
    SafeRelease(m_officeBitmap);
    m_svg.Close();
    m_font.Close();
    m_folder.Close();
    m_md.Close();
    m_text.Close();
    m_system.Close();
    SafeRelease(m_unsupportedIconBitmap);
    SafeRelease(m_appLogoBitmap);
    SafeRelease(m_dockLayer);
    if (m_hIconBig) { DestroyIcon(m_hIconBig); m_hIconBig = NULL; }
    if (m_hIconSmall) { DestroyIcon(m_hIconSmall); m_hIconSmall = NULL; }
}

void LogTime(const char* label);

bool AppWindow::Create(HINSTANCE hInstance, int nCmdShow, bool showTrayNotification) {
    LogTime("AppWindow::Create entered");
    m_hInstance = hInstance;

    // Register Window Class with Official App Icon
    m_hIconBig = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    m_hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // D2D paints everything
    wc.lpszClassName = L"WinPreviewMainWindow";
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.hIcon = m_hIconBig;
    wc.hIconSm = m_hIconSmall;
    RegisterClassExW(&wc);
    LogTime("RegisterClassExW done");

    // Initial welcome window size (compact, minimal, centered)
    int width = 960;
    int height = 640;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"WinPreviewMainWindow",
        L"WinPreview",
        WS_OVERLAPPEDWINDOW,
        x, y, width, height,
        NULL, NULL, hInstance, this
    );

    if (!m_hwnd) {
        LogTime("CreateWindowExW FAILED");
        return false;
    }
    LogTime("CreateWindowExW done");

    // Explicitly set window icons (ensures taskbar, titlebar, Alt+Tab show high-res logo)
    if (m_hIconBig) SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)m_hIconBig);
    if (m_hIconSmall) SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)m_hIconSmall);

    // Set explicit AppUserModelID on the window so Windows 11 taskbar links and groups it properly
    HMODULE hShell32 = GetModuleHandleW(L"shell32.dll");
    if (hShell32) {
        typedef HRESULT (WINAPI *PFN_SHGetPropertyStoreForWindow)(HWND, REFIID, void**);
        auto pfn = (PFN_SHGetPropertyStoreForWindow)GetProcAddress(hShell32, "SHGetPropertyStoreForWindow");
        if (pfn) {
            IPropertyStore* pps = nullptr;
            if (SUCCEEDED(pfn(m_hwnd, IID_PPV_ARGS(&pps))) && pps) {
                static const PROPERTYKEY kPKEY_AppUserModel_ID = {
                    { 0x9f4c2855, 0x9f79, 0x4b39, { 0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1, 0xd5, 0xf3 } },
                    5
                };
                PROPVARIANT pv;
                PropVariantInit(&pv);
                pv.vt = VT_LPWSTR;
                pv.pwszVal = (PWSTR)L"WinPreview.App";
                pps->SetValue(kPKEY_AppUserModel_ID, pv);
                pps->Commit();
                pps->Release();
            }
        }
    }

    // Enable Windows 11 / 10 Dark Mode & Seamless Deep Zinc Caption
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

    // Enable Windows Dark Mode theme for popups and context menus
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme) {
        typedef enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max } PreferredAppMode;
        typedef PreferredAppMode (WINAPI *pfnSetPreferredAppMode)(PreferredAppMode);
        auto fnSetPreferredAppMode = (pfnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        if (fnSetPreferredAppMode) {
            fnSetPreferredAppMode(ForceDark);
        }
        FreeLibrary(hUxtheme);
    }

    COLORREF captionColor = RGB(16, 16, 18); // #101012 seamless deep zinc
    DwmSetWindowAttribute(m_hwnd, 35 /* DWMWA_CAPTION_COLOR */, &captionColor, sizeof(captionColor));

    COLORREF captionTextColor = RGB(246, 246, 248);
    DwmSetWindowAttribute(m_hwnd, 36 /* DWMWA_TEXT_COLOR */, &captionTextColor, sizeof(captionTextColor));

    DWORD cornerPref = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(m_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));

    DWORD backdropType = 2; /* DWMSBT_MAINWINDOW (Mica) */
    DwmSetWindowAttribute(m_hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdropType, sizeof(backdropType));

    // Detect system / monitor DPI scale
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        GetDpiForWindowFn fnGetDpi = (GetDpiForWindowFn)GetProcAddress(hUser32, "GetDpiForWindow");
        if (fnGetDpi) {
            UINT dpi = fnGetDpi(m_hwnd);
            if (dpi > 0) {
                m_dpiScale = (float)dpi / 96.0f;
                m_d2d.UpdateDpi(dpi);
            }
        }
    }

    // Initialize D2D Engine factory
    if (!m_d2d.Initialize()) {
        LogTime("m_d2d.Initialize FAILED");
        return false;
    }
    LogTime("m_d2d.Initialize done");
    // Defer hardware device resources & animation timer until the window is actually shown (zero GPU & 0% CPU in background)
    if (nCmdShow != SW_HIDE) {
        m_d2d.CreateDeviceResources(m_hwnd);
        SetTimer(m_hwnd, 1, 30, NULL);
    }

    // Show Window if requested (otherwise start resident in tray)
    ShowWindow(m_hwnd, nCmdShow);
    if (nCmdShow != SW_HIDE) {
        UpdateWindow(m_hwnd);
    }
    LogTime("ShowWindow done");

    // Register TaskbarCreated message to restore tray icon if Explorer restarts
    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // Register for Windows session change notifications (unlock, logon, wake)
    if (!g_hWtsApi32) g_hWtsApi32 = LoadLibraryW(L"wtsapi32.dll");
    if (g_hWtsApi32) {
        typedef BOOL (WINAPI *PFN_WTSRegisterSessionNotification)(HWND, DWORD);
        auto fnWts = (PFN_WTSRegisterSessionNotification)GetProcAddress(g_hWtsApi32, "WTSRegisterSessionNotification");
        if (fnWts) fnWts(m_hwnd, 0 /* NOTIFY_FOR_THIS_SESSION */);
    }

    // Install Global Spacebar Explorer QuickLook Hook on isolated dedicated thread
    ExplorerHook::InstallHook(m_hwnd);
    LogTime("ExplorerHook::InstallHook done");

    // Initialize System Tray Icon
    InitTrayIcon(showTrayNotification);
    LogTime("InitTrayIcon done");

    // Initialize Top-Right Minimal Update Popup Overlay (Guaranteed on top of all media)
    CreateUpdatePopupWindow();
    LogTime("CreateUpdatePopupWindow done");

    // Start periodic auto-update check timer (runs every 15 minutes, zero CPU overhead)
    SetTimer(m_hwnd, TIMER_PERIODIC_UPDATE, 15 * 60 * 1000, NULL);

    // Initial Auto-Check 1.5s after launch (Non-blocking background thread)
    std::thread([this]() {
        Sleep(1500); // 1.5s delay keeps cold start sub-35ms
        m_lastUpdateCheckTick = GetTickCount64();
        m_updater.CheckForUpdatesAsync([this](const UpdateInfo& info) {
            if (info.isAvailable && m_hwnd && IsWindow(m_hwnd)) {
                PostMessageW(m_hwnd, WM_USER + 555, 0, 0);
            }
        });
    }).detach();
    LogTime("AutoCheck thread detached");

    return true;
}

void AppWindow::RunMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool AppWindow::OpenFile(const std::wstring& filePath) {
    if (filePath.empty()) return false;
    if (!m_d2d.GetRenderTarget()) {
        m_d2d.CreateDeviceResources(m_hwnd);
    }

    // Close previous
    m_media.CloseMedia();
    SafeRelease(m_imageBitmap);
    SafeRelease(m_pdfPageBitmap);
    SafeRelease(m_audioAlbumArt);
    SafeRelease(m_officeBitmap);
    m_svg.Close();
    m_font.Close();
    m_folder.Close();
    m_md.Close();
    m_text.Close();
    m_system.Close();
    ClearThumbnails();
    SafeRelease(m_unsupportedIconBitmap);
    m_unsupportedTypeName.clear();
    m_unsupportedSizeStr.clear();
    m_unsupportedDateStr.clear();
    m_unsupportedOpenBtnRect = {};
    m_unsupportedRevealBtnRect = {};
    m_unsupportedCopyBtnRect = {};
    m_unsupportedHexDump.clear();
    m_docType = DocumentType::None;
    ExplorerHook::SetSuppressSpaceClose(false);
    m_isAudioOnly = false;
    m_hasAdaptedVideoWindow = false;
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_hudOpacity = 0.0f;
    m_hudTargetOpacity = 0.0f;
    if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
    m_audioArtist.clear();
    m_audioAlbum.clear();
    m_audioDiscAngle = 0.0f;
    m_audioVinylSlide = 0.0f;
    m_audioAnimTime = 0.0f;
    m_audioHoverFrac = -1.0f;
    for (int i = 0; i < 64; ++i) {
        m_audioVisualizerBars[i] = 0.0f;
        m_audioVisualizerBottom[i] = 0.0f;
    }
    m_audioWaveformRect = {};
    m_isWaveformSeeking = false;
    if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);

    // Reset feature states
    m_showSearch = false;
    m_searchAnim = 0.0f;
    m_showOverflowMenu = false;
    m_overflowMenuAnim = 0.0f;
    m_overflowScrollY = 0.0f;
    m_targetOverflowScrollY = 0.0f;
    m_searchQuery.clear();
    m_searchMatches.clear();
    m_currentMatchIndex = -1;
    m_isCropActive = false;
    m_cropDragHandle = 0;
    m_selStartIndex = -1;
    m_selEndIndex = -1;
    m_isSelectingText = false;
    m_showInspector = false;
    m_inspectorAnim = 0.0f;
    m_sidebarTab = 0;
    m_sidebarScrollY = 0.0f;
    m_targetSidebarScrollY = 0.0f;
    m_outlineScrollY = 0.0f;
    m_targetOutlineScrollY = 0.0f;
    m_dragVelX = 0.0f;
    m_dragVelY = 0.0f;
    m_isMomentumActive = false;
    m_imageMeta = {};

    m_currentFilePath = filePath;
    size_t lastSlash = filePath.find_last_of(L"\\/");
    m_fileName = (lastSlash != std::wstring::npos) ? filePath.substr(lastSlash + 1) : filePath;

    // Detect file type by extension
    std::wstring ext;
    size_t dot = filePath.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext = filePath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }

    // Query Default App friendly name for "Open with <App>" quick action
    m_openWithAppName.clear();
    if (!ext.empty()) {
        WCHAR szFriendlyName[256] = {0};
        DWORD cchName = 256;
        HRESULT hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_FRIENDLYAPPNAME, ext.c_str(), L"open", szFriendlyName, &cchName);
        if (SUCCEEDED(hr) && cchName > 0 && szFriendlyName[0] != L'\0') {
            std::wstring name = szFriendlyName;
            size_t exePos = name.rfind(L".exe");
            if (exePos != std::wstring::npos) {
                name = name.substr(0, exePos);
            }
            if (_wcsicmp(name.c_str(), L"WinPreview") != 0 && !name.empty()) {
                m_openWithAppName = name;
            }
        }
    }
    if (m_openWithAppName.empty()) {
        m_openWithAppName = L"Default App";
    }

    bool success = false;

    // Helper lambdas for extension matching
    auto isTextExt = [](const std::wstring& e) {
        return (e == L".txt" || e == L".log" || e == L".rtf" ||
                e == L".ini" || e == L".cfg" || e == L".conf" || e == L".config" || e == L".env" || e == L".toml" ||
                e == L".json" || e == L".jsonc" || e == L".json5" || e == L".jsonl" || e == L".ndjson" || e == L".xml" || e == L".xaml" ||
                e == L".html" || e == L".htm" || e == L".xhtml" || e == L".css" || e == L".scss" || e == L".sass" || e == L".less" ||
                e == L".yaml" || e == L".yml" || e == L".properties" || e == L".gradle" || e == L".cmake" ||
                e == L".reg" || e == L".inf" || e == L".vbs" || e == L".wsf" || e == L".vba" || e == L".bas" ||
                e == L".js" || e == L".mjs" || e == L".cjs" || e == L".jsx" || e == L".ts" || e == L".mts" || e == L".tsx" ||
                e == L".py" || e == L".pyw" || e == L".cpp" || e == L".cxx" || e == L".cc" || e == L".c" || e == L".h" || e == L".hpp" || e == L".hxx" ||
                e == L".cs" || e == L".rs" || e == L".go" || e == L".java" || e == L".kt" || e == L".kts" ||
                e == L".swift" || e == L".dart" || e == L".zig" || e == L".lua" || e == L".r" || e == L".proto" ||
                e == L".scala" || e == L".sc" || e == L".clj" || e == L".cljs" || e == L".edn" ||
                e == L".ex" || e == L".exs" || e == L".erl" || e == L".hrl" || e == L".fs" || e == L".fsi" || e == L".fsx" ||
                e == L".hs" || e == L".lhs" || e == L".jl" || e == L".nim" || e == L".v" || e == L".sol" ||
                e == L".graphql" || e == L".gql" || e == L".svelte" || e == L".vue" || e == L".astro" ||
                e == L".php" || e == L".rb" || e == L".sh" || e == L".bash" || e == L".zsh" || e == L".fish" || e == L".bat" || e == L".cmd" ||
                e == L".ps1" || e == L".psm1" || e == L".psd1" || e == L".ps1xml" || e == L".sql" || e == L".diff" || e == L".patch" ||
                e == L".editorconfig" || e == L".gitconfig" || e == L".gitignore" || e == L".gitattributes" || e == L".gitmodules" ||
                e == L".dockerfile" || e == L".lock" || e == L".sum" || e == L".nfo" || e == L".mb" ||
                e == L".tf" || e == L".tfvars" || e == L".prisma" || e == L".tex" || e == L".latex" || e == L".bib" ||
                e == L".rst" || e == L".adoc" || e == L".asciidoc" ||
                e == L".srt" || e == L".vtt" || e == L".sub" || e == L".ass" || e == L".ssa" ||
                e == L".cue" || e == L".m3u" || e == L".m3u8" || e == L".pls" ||
                e == L".webmanifest" || e == L".manifest" || e == L".plist" ||
                e == L".csproj" || e == L".vcxproj" || e == L".fsproj" || e == L".vbproj" || e == L".props" || e == L".targets" || e == L".nuspec");
    };

    auto isKnownTextFilename = [](const std::wstring& fn) {
        std::wstring lower = fn;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        return (lower == L"makefile" || lower == L"dockerfile" || lower == L"gemfile" || lower == L"rakefile" ||
                lower == L"procfile" || lower == L"vagrantfile" || lower == L"jenkinsfile" || lower == L"brewfile" ||
                lower == L"cmakelists.txt" || lower == L"license" || lower == L"licence" || lower == L"copying" ||
                lower == L"authors" || lower == L"changelog" || lower == L"changes" || lower == L"readme" ||
                lower == L"todo" || lower == L"version" || lower == L".gitignore" || lower == L".gitattributes" ||
                lower == L".gitmodules" || lower == L".gitconfig" || lower == L".editorconfig" || lower == L".npmrc" ||
                lower == L".yarnrc" || lower == L".bowerrc" || lower == L".babelrc" || lower == L".eslintrc" ||
                lower == L".prettierrc" || lower == L".env" || lower.rfind(L".env.", 0) == 0);
    };

    auto isSpreadsheetExt = [](const std::wstring& e) {
        return (e == L".csv" || e == L".tsv" || e == L".tab" || e == L".psv");
    };

    auto isArchiveExt = [](const std::wstring& e) {
        return (e == L".zip" || e == L".jar" || e == L".apk" || e == L".nupkg" ||
                e == L".war" || e == L".aar" || e == L".whl" || e == L".msix" || e == L".appx" ||
                e == L".vsix" || e == L".cbz" || e == L".epub" || e == L".xpi" || e == L".crx" || e == L".egg");
    };

    auto isBinaryExt = [](const std::wstring& e) {
        return (e == L".exe" || e == L".dll" || e == L".sys" || e == L".ocx" ||
                e == L".cpl" || e == L".scr" || e == L".msi");
    };

    auto isShortcutExt = [](const std::wstring& e) {
        return (e == L".lnk" || e == L".url");
    };

    auto isCertificateExt = [](const std::wstring& e) {
        return (e == L".cer" || e == L".crt" || e == L".der" || e == L".pem");
    };

    auto isOfficeExt = [](const std::wstring& e) {
        return (e == L".pptx" || e == L".ppt" || e == L".xlsx" || e == L".xls" ||
                e == L".docx" || e == L".doc" || e == L".key" || e == L".numbers" || e == L".pages" ||
                e == L".odp" || e == L".ods" || e == L".odt");
    };

    auto isImageExt = [](const std::wstring& e) {
        return (e == L".png" || e == L".jpg" || e == L".jpeg" || e == L".jpe" || e == L".jfif" ||
                e == L".bmp" || e == L".dib" || e == L".gif" || e == L".tiff" || e == L".tif" ||
                e == L".ico" || e == L".webp" || e == L".heic" || e == L".heif" || e == L".avif" ||
                e == L".raw" || e == L".cr2" || e == L".nef" || e == L".arw" || e == L".dng" ||
                e == L".psd" || e == L".cur" || e == L".dds" || e == L".tga" || e == L".icns");
    };

    auto isMediaExt = [](const std::wstring& e) {
        return (e == L".mp4" || e == L".mkv" || e == L".avi" || e == L".mov" ||
                e == L".webm" || e == L".flv" || e == L".wmv" || e == L".m4v" || e == L".ts" ||
                e == L".3gp" || e == L".ogv" || e == L".vob" || e == L".m2ts" || e == L".mts" || e == L".f4v" ||
                e == L".mp3" || e == L".wav" || e == L".flac" || e == L".aac" || e == L".m4a" ||
                e == L".ogg" || e == L".wma" || e == L".opus" || e == L".weba" || e == L".aiff" ||
                e == L".aif" || e == L".ac3" || e == L".mka" || e == L".ape" || e == L".mid" || e == L".midi");
    };

    auto isAudioExt = [](const std::wstring& e) {
        return (e == L".mp3" || e == L".wav" || e == L".flac" || e == L".aac" || e == L".m4a" ||
                e == L".ogg" || e == L".wma" || e == L".opus" || e == L".weba" || e == L".aiff" ||
                e == L".aif" || e == L".ac3" || e == L".mka" || e == L".ape" || e == L".mid" || e == L".midi");
    };

    auto isMarkdownExt = [](const std::wstring& e) {
        return (e == L".md" || e == L".markdown" || e == L".mb" || e == L".mdown" || e == L".mkd" || e == L".mdx");
    };

    DWORD fileAttrs = GetFileAttributesW(filePath.c_str());

    // 0. Folders & Directories
    if (fileAttrs != INVALID_FILE_ATTRIBUTES && (fileAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (m_folder.LoadFromDirectory(filePath)) {
            m_docType = DocumentType::Folder;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            m_formatBadgeText = m_folder.GetFormatBadge();
            AdaptWindowToMedia(920, 680);
            success = true;
        }
    }
    // 1. PDF
    else if (ext == L".pdf") {
        if (!m_pdf.IsAvailable()) {
            m_pdf.Initialize();
        }
        if (m_pdf.OpenDocument(filePath)) {
            m_docType = DocumentType::Pdf;
            m_totalPages = m_pdf.GetPageCount();
            m_currentPage = 0;
            SetPage(0);
            LoadThumbnails();
            LoadOutline();

            WCHAR badge[64];
            swprintf_s(badge, L"PDF • %d PAGES", m_totalPages);
            m_formatBadgeText = badge;
            m_showSidebar = (m_totalPages > 1);
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            success = true;
        }
    }
    // 2. Video / Audio
    else if (isMediaExt(ext)) {
        m_isAudioOnly = isAudioExt(ext);
        if (!m_hudHwnd) {
            CreateHudWindow();
        }
        if (!m_media.IsAvailable()) {
            m_media.Initialize(m_hwnd);
        }
        if (m_media.OpenMedia(filePath)) {
            m_docType = DocumentType::Media;
            m_showSidebar = false;
            m_wasEnded = false;

            std::wstring upperExt = ext.substr(1);
            std::transform(upperExt.begin(), upperExt.end(), upperExt.begin(), ::towupper);
            m_formatBadgeText = upperExt + (m_isAudioOnly ? L" • AUDIO" : L" • VIDEO");

            if (m_isAudioOnly) {
                m_media.ShowVideoWindow(false);
                // Extract Album Art Thumbnail
                WicDecoder::LoadShellThumbnail(m_d2d.GetWICFactory(), m_d2d.GetRenderTarget(), filePath, 512, &m_audioAlbumArt);
                if (!m_audioAlbumArt && lastSlash != std::wstring::npos) {
                    std::wstring dir = filePath.substr(0, lastSlash + 1);
                    const wchar_t* covers[] = { L"cover.jpg", L"folder.jpg", L"album.jpg", L"Cover.jpg", L"Folder.jpg", L"cover.png", L"folder.png" };
                    for (const wchar_t* cov : covers) {
                        std::wstring covPath = dir + cov;
                        if (GetFileAttributesW(covPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            WicDecoder::LoadImageFromFile(m_d2d.GetWICFactory(), m_d2d.GetRenderTarget(), covPath, &m_audioAlbumArt, nullptr, nullptr, nullptr);
                            if (m_audioAlbumArt) break;
                        }
                    }
                }

                // Extract Audio Metadata (Artist & Album)
                auto SafeGetPropertyStore = [](const std::wstring& path, IPropertyStore** ppStore) -> HRESULT {
                    if (!ppStore) return E_POINTER;
                    *ppStore = nullptr;
                    typedef HRESULT (WINAPI *PFN_SHGetPropertyStoreFromParsingName)(PCWSTR, IBindCtx*, DWORD, REFIID, void**);
                    static PFN_SHGetPropertyStoreFromParsingName s_fn = []() -> PFN_SHGetPropertyStoreFromParsingName {
                        HMODULE h = GetModuleHandleW(L"propsys.dll");
                        if (!h) h = LoadLibraryW(L"propsys.dll");
                        return h ? (PFN_SHGetPropertyStoreFromParsingName)GetProcAddress(h, "SHGetPropertyStoreFromParsingName") : nullptr;
                    }();
                    if (!s_fn) return E_FAIL;
                    return s_fn(path.c_str(), NULL, 0, IID_PPV_ARGS(ppStore));
                };

                IPropertyStore* pStore = nullptr;
                if (SUCCEEDED(SafeGetPropertyStore(filePath, &pStore)) && pStore) {
                    PROPVARIANT prop = {};
                    PropVariantInit(&prop);
                    static const PROPERTYKEY kPKey_Artist = { { 0x56a3372e, 0xce9c, 0x11d2, { 0x9f, 0x0e, 0x00, 0x60, 0x97, 0xc6, 0x86, 0xf6 } }, 2 };
                    static const PROPERTYKEY kPKey_Album = { { 0x56a3372e, 0xce9c, 0x11d2, { 0x9f, 0x0e, 0x00, 0x60, 0x97, 0xc6, 0x86, 0xf6 } }, 4 };
                    if (SUCCEEDED(pStore->GetValue(kPKey_Artist, &prop)) && prop.vt == VT_LPWSTR && prop.pwszVal) {
                        m_audioArtist = prop.pwszVal;
                    }
                    PropVariantClear(&prop);
                    if (SUCCEEDED(pStore->GetValue(kPKey_Album, &prop)) && prop.vt == VT_LPWSTR && prop.pwszVal) {
                        m_audioAlbum = prop.pwszVal;
                    }
                    PropVariantClear(&prop);
                    pStore->Release();
                }

                AdaptWindowToMedia(620, 560);
            } else {
                unsigned int vw = 0, vh = 0;
                if (m_media.GetVideoDimensions(&vw, &vh) && vw > 0 && vh > 0) {
                    m_videoWidth = (int)vw;
                    m_videoHeight = (int)vh;
                    m_hasAdaptedVideoWindow = true;
                    AdaptWindowToMedia(m_videoWidth, m_videoHeight);
                } else {
                    auto SafeGetPropertyStore = [](const std::wstring& path, IPropertyStore** ppStore) -> HRESULT {
                        if (!ppStore) return E_POINTER;
                        *ppStore = nullptr;
                        typedef HRESULT (WINAPI *PFN_SHGetPropertyStoreFromParsingName)(PCWSTR, IBindCtx*, DWORD, REFIID, void**);
                        static PFN_SHGetPropertyStoreFromParsingName s_fn = []() -> PFN_SHGetPropertyStoreFromParsingName {
                            HMODULE h = GetModuleHandleW(L"propsys.dll");
                            if (!h) h = LoadLibraryW(L"propsys.dll");
                            return h ? (PFN_SHGetPropertyStoreFromParsingName)GetProcAddress(h, "SHGetPropertyStoreFromParsingName") : nullptr;
                        }();
                        if (!s_fn) return E_FAIL;
                        return s_fn(path.c_str(), NULL, 0, IID_PPV_ARGS(ppStore));
                    };

                    IPropertyStore* pStore = nullptr;
                    if (SUCCEEDED(SafeGetPropertyStore(filePath, &pStore)) && pStore) {
                        PROPVARIANT propW = {}, propH = {};
                        PropVariantInit(&propW);
                        PropVariantInit(&propH);
                        static const PROPERTYKEY kPKey_FrameWidth = { { 0x64440490, 0x4C87, 0x11D1, { 0xA2, 0x6C, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A } }, 3 };
                        static const PROPERTYKEY kPKey_FrameHeight = { { 0x64440490, 0x4C87, 0x11D1, { 0xA2, 0x6C, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A } }, 4 };
                        pStore->GetValue(kPKey_FrameWidth, &propW);
                        pStore->GetValue(kPKey_FrameHeight, &propH);
                        if (propW.vt == VT_UI4 && propH.vt == VT_UI4 && propW.ulVal > 0 && propH.ulVal > 0) {
                            m_videoWidth = (int)propW.ulVal;
                            m_videoHeight = (int)propH.ulVal;
                            m_hasAdaptedVideoWindow = true;
                            AdaptWindowToMedia(m_videoWidth, m_videoHeight);
                        }
                        PropVariantClear(&propW);
                        PropVariantClear(&propH);
                        pStore->Release();
                    }
                }
            }

            m_lastTimerTick = GetTickCount64();
            GetCursorPos(&m_lastGlobalMousePos);
            WakeHudAndCursor();
            if (m_isAudioOnly) {
                m_hudOpacity = 255.0f;
                m_hudTargetOpacity = 255.0f;
                if (m_hudHwnd && IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) {
                    SetLayeredWindowAttributes(m_hudHwnd, 0, 255, LWA_ALPHA);
                    ShowWindow(m_hudHwnd, SW_SHOWNOACTIVATE);
                    InvalidateRect(m_hudHwnd, NULL, FALSE);
                }
            }
            UpdateHudPosition();
            success = true;
        }
    }
    // 3. Tabular Spreadsheets (CSV, TSV)
    else if (isSpreadsheetExt(ext)) {
        if (m_text.LoadFile(filePath, m_d2d.GetDWriteFactory())) {
            m_docType = DocumentType::Spreadsheet;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);

            WCHAR badge[128];
            swprintf_s(badge, L"%s • %d COLS • %d ROWS",
                m_text.GetLanguageName().c_str(), m_text.GetColumnCount(), m_text.GetRowCount());
            m_formatBadgeText = badge;

            AdaptWindowToMedia(1080, 720);
            success = true;
        }
    }
    // 4. Source Code & Text Documents
    else if (isTextExt(ext) || isKnownTextFilename(m_fileName)) {
        if (m_text.LoadFile(filePath, m_d2d.GetDWriteFactory())) {
            m_docType = DocumentType::Text;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);

            WCHAR badge[128];
            swprintf_s(badge, L"%s • %d LINES • %0.1f KB",
                m_text.GetLanguageName().c_str(), m_text.GetLineCount(), (float)m_text.GetFileSize() / 1024.0f);
            m_formatBadgeText = badge;

            AdaptWindowToMedia(960, 720);
            success = true;
        }
    }
    // 5. SVG Vector Graphics
    else if (ext == L".svg") {
        if (m_svg.LoadFromFile(m_d2d.GetRenderTarget(), filePath)) {
            m_docType = DocumentType::Svg;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            m_formatBadgeText = m_svg.GetMetadataString();
            D2D1_SIZE_F sz = m_svg.GetOriginalSize();
            int targetW = (int)sz.width;
            int targetH = (int)sz.height;
            if (targetW > 1200) { targetH = (int)((float)targetH * 1200.0f / (float)targetW); targetW = 1200; }
            if (targetW < 520) { targetH = (int)((float)targetH * 520.0f / (float)(std::max)(targetW, 1)); targetW = 520; }
            AdaptWindowToMedia(targetW, targetH);
            success = true;
        }
    }
    // 6. Typographic Fonts (.ttf, .otf, .woff, .woff2)
    else if (ext == L".ttf" || ext == L".otf" || ext == L".woff" || ext == L".woff2") {
        if (m_font.LoadFromFile(m_d2d.GetDWriteFactory(), filePath)) {
            m_docType = DocumentType::Font;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            m_formatBadgeText = m_font.GetFormatBadge();
            AdaptWindowToMedia(920, 720);
            success = true;
        }
    }
    // 7. Jupyter Notebooks (.ipynb)
    else if (ext == L".ipynb") {
        if (m_md.LoadNotebookFile(filePath)) {
            m_docType = DocumentType::Notebook;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            m_formatBadgeText = m_md.GetFormatBadge();
            AdaptWindowToMedia(940, 760);
            success = true;
        }
    }
    // 8. Markdown Documents (.md, .markdown, .mb, .mdown, .mkd, .mdx)
    else if (isMarkdownExt(ext)) {
        if (m_text.LoadFile(filePath, m_d2d.GetDWriteFactory())) {
            m_docType = DocumentType::Text;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            WCHAR badge[128];
            swprintf_s(badge, L"%s • %d LINES • %0.1f KB",
                m_text.GetLanguageName().c_str(), m_text.GetLineCount(), (float)m_text.GetFileSize() / 1024.0f);
            m_formatBadgeText = badge;
            AdaptWindowToMedia(960, 720);
            success = true;
        } else if (m_md.LoadMarkdownFile(filePath)) {
            m_docType = DocumentType::Markdown;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            m_formatBadgeText = m_md.GetFormatBadge();
            AdaptWindowToMedia(920, 720);
            success = true;
        }
    }
    // 9. Presentation Slides & Office Documents (PPTX, XLSX, DOCX, ODT, ODS, ODP, KEY, NUMBERS, PAGES)
    else if (isOfficeExt(ext)) {
        if (WicDecoder::LoadShellThumbnail(m_d2d.GetWICFactory(), m_d2d.GetRenderTarget(), filePath, 1280, &m_officeBitmap)) {
            m_docType = DocumentType::Office;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);

            D2D1_SIZE_F sz = m_officeBitmap->GetSize();
            m_officeWidth = (UINT)sz.width;
            m_officeHeight = (UINT)sz.height;

            if (ext == L".pptx" || ext == L".ppt" || ext == L".key" || ext == L".odp") {
                m_formatBadgeText = L"PRESENTATION SLIDES • 16:9";
            } else if (ext == L".xlsx" || ext == L".xls" || ext == L".numbers" || ext == L".ods") {
                m_formatBadgeText = L"SPREADSHEET WORKBOOK";
            } else if (ext == L".docx" || ext == L".doc" || ext == L".pages" || ext == L".odt") {
                m_formatBadgeText = L"WORD DOCUMENT";
            } else {
                m_formatBadgeText = L"OFFICE DOCUMENT";
            }

            int targetW = (int)m_officeWidth;
            int targetH = (int)m_officeHeight;
            if (targetW > 1200) { targetH = (int)((float)targetH * 1200.0f / targetW); targetW = 1200; }
            AdaptWindowToMedia(targetW, targetH);
            success = true;
        }
    }
    // 10. Compressed Archives & Packages (.zip, .jar, .apk, .nupkg, .war, .aar, .whl, .msix, .appx, .vsix, .cbz, .epub, .xpi, .crx, .egg)
    else if (isArchiveExt(ext)) {
        if (m_folder.LoadFromZipArchive(filePath)) {
            m_docType = DocumentType::Folder;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            m_formatBadgeText = m_folder.GetFormatBadge();
            AdaptWindowToMedia(960, 700);
            success = true;
        }
    }
    // 11. Windows System Binaries (.exe, .dll, .sys, .ocx, .cpl, .scr, .msi)
    else if (isBinaryExt(ext)) {
        if (m_system.LoadBinary(filePath, m_d2d.GetDWriteFactory(), m_d2d.GetRenderTarget(), m_d2d.GetWICFactory())) {
            m_docType = DocumentType::Binary;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            m_formatBadgeText = m_system.GetFormatBadge();
            AdaptWindowToMedia(940, 680);
            success = true;
        }
    }
    // 12. Windows Shell Shortcuts & Links (.lnk, .url)
    else if (isShortcutExt(ext)) {
        if (m_system.LoadShortcut(filePath, m_d2d.GetDWriteFactory(), m_d2d.GetRenderTarget(), m_d2d.GetWICFactory())) {
            m_docType = DocumentType::Shortcut;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            m_formatBadgeText = m_system.GetFormatBadge();
            AdaptWindowToMedia(860, 600);
            success = true;
        }
    }
    // 13. Security Certificates (.cer, .crt, .der, .pem)
    else if (isCertificateExt(ext)) {
        if (m_system.LoadCertificate(filePath, m_d2d.GetDWriteFactory(), m_d2d.GetRenderTarget(), m_d2d.GetWICFactory())) {
            m_docType = DocumentType::Certificate;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            m_formatBadgeText = m_system.GetFormatBadge();
            AdaptWindowToMedia(880, 640);
            success = true;
        }
    }
    // 14. Image (via WIC)
    else if (isImageExt(ext) || ext.empty()) {
        m_hasDetectedContentBounds = false;
        m_trimBlackBars = true;
        D2D1_RECT_F bounds = {};
        if (WicDecoder::LoadImageFromFile(m_d2d.GetWICFactory(), m_d2d.GetRenderTarget(),
                                          filePath, &m_imageBitmap, &m_imageWidth, &m_imageHeight, &bounds)) {
            m_docType = DocumentType::Image;
            m_contentBounds = bounds;
            float cw = bounds.right - bounds.left;
            float ch = bounds.bottom - bounds.top;
            if ((cw < (float)m_imageWidth - 14.0f) || (ch < (float)m_imageHeight - 14.0f)) {
                m_hasDetectedContentBounds = true;
            }
            WicDecoder::GetImageMetadata(m_d2d.GetWICFactory(), filePath, &m_imageMeta);
            WCHAR badge[64];
            std::wstring upperExt = ext.empty() ? L"IMAGE" : ext.substr(1);
            std::transform(upperExt.begin(), upperExt.end(), upperExt.begin(), ::towupper);
            swprintf_s(badge, L"%s • %u × %u", upperExt.c_str(), m_imageWidth, m_imageHeight);
            m_formatBadgeText = badge;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
            success = true;

            int targetW = (int)m_imageWidth;
            int targetH = (int)m_imageHeight;
            if (m_hasDetectedContentBounds && m_trimBlackBars) {
                targetW = (int)(cw + 0.5f);
                targetH = (int)(ch + 0.5f);
            }
            AdaptWindowToMedia(targetW, targetH);
        }
    }

    // 15. Auto-Detect Plain Text Content for extensionless files or unrecognized extensions
    if (!success && fileAttrs != INVALID_FILE_ATTRIBUTES && !(fileAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        HANDLE hProbe = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hProbe != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER liProbe;
            if (GetFileSizeEx(hProbe, &liProbe) && liProbe.QuadPart > 0 && liProbe.QuadPart <= 16 * 1024 * 1024) {
                BYTE probeBuf[1024] = {};
                DWORD bytesRead = 0;
                if (ReadFile(hProbe, probeBuf, 1024, &bytesRead, NULL) && bytesRead > 0) {
                    bool hasNull = false;
                    for (DWORD i = 0; i < bytesRead; ++i) {
                        if (probeBuf[i] == 0) { hasNull = true; break; }
                    }
                    if (!hasNull) {
                        CloseHandle(hProbe);
                        hProbe = INVALID_HANDLE_VALUE;
                        if (m_text.LoadFile(filePath, m_d2d.GetDWriteFactory())) {
                            m_docType = DocumentType::Text;
                            m_showSidebar = false;
                            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
                            WCHAR badge[128];
                            swprintf_s(badge, L"%s • %d LINES • %0.1f KB",
                                m_text.GetLanguageName().c_str(), m_text.GetLineCount(), (float)m_text.GetFileSize() / 1024.0f);
                            m_formatBadgeText = badge;
                            AdaptWindowToMedia(960, 720);
                            success = true;
                        }
                    }
                }
            }
            if (hProbe != INVALID_HANDLE_VALUE) CloseHandle(hProbe);
        }
    }

    // 16. Fallback for non-supported binary formats (.bin, .dat, .iso, etc.)
    if (!success && fileAttrs != INVALID_FILE_ATTRIBUTES && !(fileAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        m_docType = DocumentType::Unsupported;
        LoadUnsupportedFileInfo(filePath);
        m_showSidebar = false;
        if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
        m_formatBadgeText = m_unsupportedTypeName;
        int targetW = (int)(620.0f * m_dpiScale + 0.5f);
        int targetH = (int)(460.0f * m_dpiScale + 0.5f);
        AdaptWindowToMedia(targetW, targetH);
        UpdateLayout();
        success = true;
    }

    if (success) {
        UpdateWindowTitle();

        m_rotation = 0;
        FitToWindow();
        UpdateLayout();
        m_lastTimerTick = GetTickCount64();
        GetCursorPos(&m_lastGlobalMousePos);
        POINT clPt = m_lastGlobalMousePos;
        ScreenToClient(m_hwnd, &clPt);
        m_lastActiveMousePos = clPt;
        WakeAllUiAndCursor();
        ExplorerHook::SetSuppressSpaceClose(m_docType == DocumentType::Text);
        CheckUpdateOnPreviewOpen();
        InvalidateRect(m_hwnd, NULL, FALSE);
    }

    return success;
}

void AppWindow::SetPage(int page) {
    if (m_docType != DocumentType::Pdf || page < 0 || page >= m_totalPages) return;
    m_currentPage = page;

    SafeRelease(m_pdfPageBitmap);

    float pw = 0.0f, ph = 0.0f;
    if (m_pdf.GetPageSize(page, &pw, &ph)) {
        m_pdfPageWidth = pw;
        m_pdfPageHeight = ph;

        // Render at crisp 1.5x retina supersample
        float renderW = pw * 1.5f;
        float renderH = ph * 1.5f;
        m_pdf.RenderPageToBitmap(page, renderW, renderH, m_d2d.GetRenderTarget(), &m_pdfPageBitmap);
        m_pdf.LoadTextPage(page);
    }

    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::OpenCurrentFileInDefaultApp() {
    if (m_currentFilePath.empty()) return;
    ShellExecuteW(NULL, L"open", m_currentFilePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    HideMainWindow();
}

void AppWindow::NavigateFolder(int delta) {
    if (m_currentFilePath.empty() || m_docType == DocumentType::None) return;

    std::wstring current = m_currentFilePath;
    while (!current.empty() && (current.back() == L'\\' || current.back() == L'/')) {
        current.pop_back();
    }
    size_t lastSlash = current.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) return;

    std::wstring folder = current.substr(0, lastSlash);
    std::wstring searchPattern = folder + L"\\*.*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<std::wstring> peerFiles;

    do {
        // Skip hidden and system files
        if (ffd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;

        std::wstring filename = ffd.cFileName;
        if (filename == L"." || filename == L"..") continue;
        if (!filename.empty() && filename[0] == L'.') continue;

        // If currently previewing a directory, navigate between peer directories
        if (m_docType == DocumentType::Folder) {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                peerFiles.push_back(folder + L"\\" + filename);
            }
        } else {
            // For all files, navigate peer non-directory files
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                peerFiles.push_back(folder + L"\\" + filename);
            }
        }
    } while (FindNextFileW(hFind, &ffd) != 0);

    FindClose(hFind);

    if (peerFiles.size() <= 1) return;

    // Windows Explorer natural numeric/logical sort
    std::sort(peerFiles.begin(), peerFiles.end(), [](const std::wstring& a, const std::wstring& b) {
        return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
    });

    int curIdx = -1;
    for (size_t i = 0; i < peerFiles.size(); i++) {
        if (_wcsicmp(peerFiles[i].c_str(), m_currentFilePath.c_str()) == 0 ||
            _wcsicmp(peerFiles[i].c_str(), current.c_str()) == 0) {
            curIdx = (int)i;
            break;
        }
    }

    if (curIdx >= 0) {
        int nextIdx = (curIdx + delta + (int)peerFiles.size()) % (int)peerFiles.size();
        OpenFile(peerFiles[nextIdx]);
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

void AppWindow::NextPage() {
    if (m_docType == DocumentType::Pdf && m_currentPage + 1 < m_totalPages) {
        SetPage(m_currentPage + 1);
    } else {
        NavigateFolder(1);
    }
}

void AppWindow::PrevPage() {
    if (m_docType == DocumentType::Pdf && m_currentPage > 0) {
        SetPage(m_currentPage - 1);
    } else {
        NavigateFolder(-1);
    }
}

void AppWindow::Rotate(int delta) {
    m_rotation = (m_rotation + delta + 360) % 360;
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::FitToWindow() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float clientWDip = PxToDip(rc.right - rc.left);
    float clientHDip = PxToDip(rc.bottom - rc.top);
    float pad = (m_docType == DocumentType::Pdf) ? 48.0f : 0.0f;
    float availW = clientWDip - (m_showSidebar ? m_sidebarWidth : 0.0f) - pad;
    float availH = clientHDip - pad;

    if (availW <= 10.0f || availH <= 10.0f) return;

    float docW = 0.0f, docH = 0.0f;
    if (m_docType == DocumentType::Image) {
        if (m_hasDetectedContentBounds && m_trimBlackBars) {
            docW = m_contentBounds.right - m_contentBounds.left;
            docH = m_contentBounds.bottom - m_contentBounds.top;
        } else {
            docW = (float)m_imageWidth;
            docH = (float)m_imageHeight;
        }
    } else if (m_docType == DocumentType::Pdf) {
        docW = m_pdfPageWidth;
        docH = m_pdfPageHeight;
    } else if (m_docType == DocumentType::Office) {
        docW = (float)m_officeWidth;
        docH = (float)m_officeHeight;
    } else {
        return;
    }

    if (docW <= 0.0f || docH <= 0.0f) return;

    if (m_rotation == 90 || m_rotation == 270) {
        std::swap(docW, docH);
    }

    float scaleX = availW / docW;
    float scaleY = availH / docH;
    m_zoom = (std::min)(scaleX, scaleY);
    if (m_zoom > 2.0f && m_docType == DocumentType::Pdf) m_zoom = 2.0f;

    m_panX = 0.0f;
    m_panY = 0.0f;
    m_targetZoom = m_zoom;
    m_targetPanX = m_panX;
    m_targetPanY = m_panY;
    m_isMomentumActive = false;
    m_dragVelX = 0.0f;
    m_dragVelY = 0.0f;
}

void AppWindow::AdaptWindowToMedia(int mediaW, int mediaH) {
    if (mediaW <= 0 || mediaH <= 0 || m_isFullscreen) return;

    HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfo(hMon, &mi)) return;

    RECT work = mi.rcWork;
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;

    float maxAvailW = (float)workW * 0.88f;
    float maxAvailH = (float)workH * 0.88f;
    float maxCanvasH = maxAvailH;
    float maxCanvasW = maxAvailW;

    float minCanvasW = 340.0f * m_dpiScale;
    float minCanvasH = 220.0f * m_dpiScale;

    float scale = 1.0f;
    if ((float)mediaW > maxCanvasW || (float)mediaH > maxCanvasH) {
        scale = (std::min)(maxCanvasW / (float)mediaW, maxCanvasH / (float)mediaH);
    }

    float targetCanvasW = (float)mediaW * scale;
    float targetCanvasH = (float)mediaH * scale;

    if (targetCanvasW < minCanvasW) {
        float up = minCanvasW / targetCanvasW;
        targetCanvasW = minCanvasW;
        targetCanvasH = (std::min)(targetCanvasH * up, maxCanvasH);
    }
    if (targetCanvasH < minCanvasH) {
        float up = minCanvasH / targetCanvasH;
        targetCanvasH = minCanvasH;
        targetCanvasW = (std::min)(targetCanvasW * up, maxCanvasW);
    }

    int clientW = (int)(targetCanvasW + 0.5f);
    int clientH = (int)(targetCanvasH + 0.5f);

    DWORD style = (DWORD)GetWindowLongPtr(m_hwnd, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
    RECT winRc = { 0, 0, clientW, clientH };

    typedef BOOL (WINAPI *PFN_AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
    typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    PFN_AdjustWindowRectExForDpi pfnAdjust = hUser32 ? (PFN_AdjustWindowRectExForDpi)GetProcAddress(hUser32, "AdjustWindowRectExForDpi") : nullptr;
    PFN_GetDpiForWindow pfnGetDpi = hUser32 ? (PFN_GetDpiForWindow)GetProcAddress(hUser32, "GetDpiForWindow") : nullptr;

    if (pfnAdjust && pfnGetDpi) {
        UINT dpi = pfnGetDpi(m_hwnd);
        pfnAdjust(&winRc, style, FALSE, exStyle, dpi);
    } else {
        AdjustWindowRectEx(&winRc, style, FALSE, exStyle);
    }

    int totalWinW = winRc.right - winRc.left;
    int totalWinH = winRc.bottom - winRc.top;

    int posX = work.left + (workW - totalWinW) / 2;
    int posY = work.top + (workH - totalWinH) / 2;
    if (posY < work.top) posY = work.top;
    if (posX < work.left) posX = work.left;

    SetWindowPos(m_hwnd, NULL, posX, posY, totalWinW, totalWinH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // Exact single-pixel client area verification & fine calibration
    RECT actualClient;
    GetClientRect(m_hwnd, &actualClient);
    int actualW = actualClient.right - actualClient.left;
    int actualH = actualClient.bottom - actualClient.top;
    if (actualW != clientW || actualH != clientH) {
        totalWinW += (clientW - actualW);
        totalWinH += (clientH - actualH);
        posX = work.left + (workW - totalWinW) / 2;
        posY = work.top + (workH - totalWinH) / 2;
        if (posY < work.top) posY = work.top;
        if (posX < work.left) posX = work.left;
        SetWindowPos(m_hwnd, NULL, posX, posY, totalWinW, totalWinH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    FitToWindow();
    UpdateLayout();
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ZoomIn() {
    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
        m_text.ZoomIn();
    } else {
        float oldZoom = m_zoom;
        m_zoom = (std::clamp)(m_zoom * 1.25f, 0.05f, 25.0f);
        float factor = m_zoom / (oldZoom > 0.001f ? oldZoom : 1.0f);
        m_panX *= factor;
        m_panY *= factor;
        m_targetZoom = m_zoom;
        m_targetPanX = m_panX;
        m_targetPanY = m_panY;
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ZoomOut() {
    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
        m_text.ZoomOut();
    } else {
        float oldZoom = m_zoom;
        m_zoom = (std::clamp)(m_zoom * 0.8f, 0.05f, 25.0f);
        float factor = m_zoom / (oldZoom > 0.001f ? oldZoom : 1.0f);
        m_panX *= factor;
        m_panY *= factor;
        m_targetZoom = m_zoom;
        m_targetPanX = m_panX;
        m_targetPanY = m_panY;
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ResetZoom() {
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;
    m_targetZoom = 1.0f;
    m_targetPanX = 0.0f;
    m_targetPanY = 0.0f;
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ToggleFullscreen() {
    if (!m_isFullscreen) {
        m_preFullscreenStyle = GetWindowLong(m_hwnd, GWL_STYLE);
        GetWindowRect(m_hwnd, &m_preFullscreenRect);

        SetWindowLong(m_hwnd, GWL_STYLE, m_preFullscreenStyle & ~(WS_CAPTION | WS_THICKFRAME));

        HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMon, &mi);

        SetWindowPos(m_hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        m_isFullscreen = true;
        WakeAllUiAndCursor();
    } else {
        SetWindowLong(m_hwnd, GWL_STYLE, m_preFullscreenStyle);
        SetWindowPos(m_hwnd, NULL,
            m_preFullscreenRect.left, m_preFullscreenRect.top,
            m_preFullscreenRect.right - m_preFullscreenRect.left,
            m_preFullscreenRect.bottom - m_preFullscreenRect.top,
            SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        m_isFullscreen = false;
        WakeAllUiAndCursor();
    }
    UpdateLayout();
    UpdateHudPosition();
}

void AppWindow::LoadThumbnails() {
    ClearThumbnails();
    if (m_docType != DocumentType::Pdf || m_totalPages <= 0) return;

    m_thumbnails.reserve(m_totalPages);
    for (int i = 0; i < m_totalPages; i++) {
        ThumbnailItem item;
        item.pageIndex = i;
        item.label = std::to_wstring(i + 1);
        item.bitmap = nullptr; // Lazily rendered when entering viewport!
        m_thumbnails.push_back(item);
    }
}

void AppWindow::ClearThumbnails() {
    for (auto& item : m_thumbnails) {
        SafeRelease(item.bitmap);
    }
    m_thumbnails.clear();
}

void AppWindow::LoadOutline() {
    m_outline.clear();
    if (m_docType == DocumentType::Pdf) {
        m_outline = m_pdf.GetOutline();
    }
}

D2D1_SIZE_F AppWindow::GetClientSizeDip() const {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float scale = (m_dpiScale > 0.05f) ? m_dpiScale : 1.0f;
    return D2D1::SizeF((float)(rc.right - rc.left) / scale, (float)(rc.bottom - rc.top) / scale);
}

D2D1_RECT_F AppWindow::GetCanvasRectDip() const {
    D2D1_SIZE_F sz = GetClientSizeDip();
    return D2D1::RectF(
        (m_showSidebar && m_docType == DocumentType::Pdf) ? m_sidebarWidth : 0.0f,
        0.0f,
        sz.width,
        sz.height
    );
}

POINT AppWindow::WindowToCanvas(const POINT& pt, const D2D1_RECT_F& canvasRect) {
    POINT cPt;
    cPt.x = (LONG)((float)pt.x - canvasRect.left);
    cPt.y = (LONG)((float)pt.y - canvasRect.top);
    return cPt;
}

D2D1_POINT_2F AppWindow::CanvasToPageCoords(float mouseX, float mouseY, const D2D1_RECT_F& canvasRect) {
    if (m_docType != DocumentType::Pdf || m_pdfPageWidth <= 0.0f || m_pdfPageHeight <= 0.0f) {
        return D2D1::Point2F(0.0f, 0.0f);
    }
    float targetW = m_pdfPageWidth * m_zoom;
    float targetH = m_pdfPageHeight * m_zoom;
    float canvasCenterX = canvasRect.left + (canvasRect.right - canvasRect.left) / 2.0f;
    float canvasCenterY = canvasRect.top + (canvasRect.bottom - canvasRect.top) / 2.0f;
    float destLeft = canvasCenterX - (targetW / 2.0f) + m_panX;
    float destTop = canvasCenterY - (targetH / 2.0f) + m_panY;

    // Center of page for rotation
    D2D1_POINT_2F center = D2D1::Point2F(destLeft + targetW / 2.0f, destTop + targetH / 2.0f);
    float rad = -(float)m_rotation * 3.14159265358979323846f / 180.0f;
    float dx = mouseX - center.x;
    float dy = mouseY - center.y;
    float unrotX = center.x + dx * std::cos(rad) - dy * std::sin(rad);
    float unrotY = center.y + dx * std::sin(rad) + dy * std::cos(rad);

    float relX = unrotX - destLeft;
    float relY = unrotY - destTop;

    float pdfX = (relX / targetW) * m_pdfPageWidth;
    float pdfY = m_pdfPageHeight - (relY / targetH) * m_pdfPageHeight; // PDF Y is inverted (0 at bottom)
    return D2D1::Point2F(pdfX, pdfY);
}

void AppWindow::UpdateLayout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    m_d2d.Resize(width, height);

    if (m_docType == DocumentType::Media) {
        if (m_isAudioOnly) {
            m_media.ShowVideoWindow(false);
        } else {
            int canvasX = m_showSidebar ? (int)(m_sidebarWidth * m_dpiScale + 0.5f) : 0;
            int canvasY = 0;
            int canvasW = width - canvasX;
            int canvasH = height;
            m_media.Resize(canvasX, canvasY, canvasW, canvasH);
            m_media.ShowVideoWindow(true);
        }
        UpdateHudPosition();
    } else {
        m_media.ShowVideoWindow(false);
        if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
    }
}

void AppWindow::DrawIconButton(const D2D1_RECT_F& rect, IconType icon, int buttonId, bool enabled) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, 6.0f, 6.0f);

    float cx = rect.left + (rect.right - rect.left) / 2.0f;
    float cy = rect.top + (rect.bottom - rect.top) / 2.0f;
    float iconSize = 16.0f;

    bool isLoopActive = (buttonId == BTN_LOOP) && m_isLooping;
    bool isCropActive = (buttonId == BTN_CROP) && (m_isCropActive || (m_hasDetectedContentBounds && m_trimBlackBars));
    bool isSearchActive = (buttonId == BTN_SEARCH) && m_showSearch;
    bool isInfoActive = (buttonId == BTN_INFO) && m_showInspector;
    bool isSettingsActive = (buttonId == BTN_SETTINGS) && m_showSettings;
    bool isWrapActive = (buttonId == BTN_WRAP_TEXT) && m_text.IsWordWrap();
    bool isTableActive = (buttonId == BTN_TABLE_VIEW) && m_text.IsTableView();
    bool isSaveActive = (buttonId == BTN_SAVE_FILE) && m_text.IsModified();
    bool isOverflowActive = (buttonId == BTN_OVERFLOW_MENU) && m_showOverflowMenu;

    bool isStateActive = isLoopActive || isCropActive || isSearchActive || isInfoActive || isSettingsActive || isWrapActive || isTableActive || isSaveActive || isOverflowActive;

    // Active (press) state: tactile scale down (0.95) + active fill (Emil Kowalski / better-ui)
    if (m_pressedButtonId == buttonId && enabled) {
        D2D1_RECT_F insetRect = D2D1::RectF(rect.left + 0.6f, rect.top + 0.6f, rect.right - 0.6f, rect.bottom - 0.6f);
        D2D1_ROUNDED_RECT insetR = D2D1::RoundedRect(insetRect, 5.0f, 5.0f);
        rt->FillRoundedRectangle(insetR, m_d2d.BrushActive);
        iconSize *= 0.95f;
        cy += 0.5f; // optical press down
    } else if (isStateActive) {
        rt->FillRoundedRectangle(rrect, m_d2d.BrushCardSelected);
        rt->DrawRoundedRectangle(rrect, m_d2d.BrushAccent, 1.2f);
    } else if (m_hoveredButtonId == buttonId && enabled) {
        rt->FillRoundedRectangle(rrect, m_d2d.BrushHover);
    }

    ID2D1Brush* brush = isStateActive ? (ID2D1Brush*)m_d2d.BrushAccent :
                        (enabled ? (ID2D1Brush*)m_d2d.BrushText : (ID2D1Brush*)m_d2d.BrushTextMuted);

    switch (icon) {
        case ICON_SIDEBAR:        m_d2d.DrawIconSidebar(cx, cy, iconSize, m_showSidebar, brush); break;
        case ICON_CHEVRON_LEFT:   m_d2d.DrawIconChevronLeft(cx, cy, iconSize, brush); break;
        case ICON_CHEVRON_RIGHT:  m_d2d.DrawIconChevronRight(cx, cy, iconSize, brush); break;
        case ICON_ZOOM_OUT:       m_d2d.DrawIconZoomOut(cx, cy, iconSize, brush); break;
        case ICON_ZOOM_IN:        m_d2d.DrawIconZoomIn(cx, cy, iconSize, brush); break;
        case ICON_ZOOM_FIT:       m_d2d.DrawIconFit(cx, cy, iconSize, brush); break;
        case ICON_ROTATE_LEFT:    m_d2d.DrawIconRotateLeft(cx, cy, iconSize, brush); break;
        case ICON_ROTATE_RIGHT:   m_d2d.DrawIconRotateRight(cx, cy, iconSize, brush); break;
        case ICON_PLAY:           m_d2d.DrawIconPlay(cx, cy, iconSize, brush); break;
        case ICON_PAUSE:          m_d2d.DrawIconPause(cx, cy, iconSize, brush); break;
        case ICON_VOLUME:         m_d2d.DrawIconVolume(cx, cy, iconSize, m_media.IsMuted(), brush); break;
        case ICON_FULLSCREEN:     m_d2d.DrawIconFullscreen(cx, cy, iconSize, m_isFullscreen, brush); break;
        case ICON_SKIP_BACK:      m_d2d.DrawIconSkipBack(cx, cy, iconSize, brush); break;
        case ICON_SKIP_FORWARD:   m_d2d.DrawIconSkipForward(cx, cy, iconSize, brush); break;
        case ICON_LOOP:           m_d2d.DrawIconLoop(cx, cy, iconSize, m_isLooping, brush); break;
        case ICON_FILE_PDF:       m_d2d.DrawIconFilePdf(cx, cy, iconSize, brush); break;
        case ICON_FILE_IMAGE:     m_d2d.DrawIconFileImage(cx, cy, iconSize, brush); break;
        case ICON_FILE_VIDEO:     m_d2d.DrawIconFileVideo(cx, cy, iconSize, brush); break;
        case ICON_SEARCH:         m_d2d.DrawIconSearch(cx, cy, iconSize, brush); break;
        case ICON_CROP:           m_d2d.DrawIconCrop(cx, cy, iconSize, brush); break;
        case ICON_INFO:           m_d2d.DrawIconInfo(cx, cy, iconSize, brush); break;
        case ICON_SETTINGS:       m_d2d.DrawIconSettings(cx, cy, iconSize, brush); break;
        case ICON_OUTLINE:        m_d2d.DrawIconOutline(cx, cy, iconSize, brush); break;
        case ICON_COPY:           m_d2d.DrawIconCopy(cx, cy, iconSize, brush); break;
        case ICON_WRAP:           m_d2d.DrawIconWrap(cx, cy, iconSize, m_text.IsWordWrap(), brush); break;
        case ICON_TABLE:          m_d2d.DrawIconTable(cx, cy, iconSize, m_text.IsTableView(), brush); break;
        case ICON_SAVE:           m_d2d.DrawIconSave(cx, cy, iconSize, m_text.IsModified(), brush); break;
        case ICON_UNDO:           m_d2d.DrawIconUndo(cx, cy, iconSize, brush); break;
        case ICON_REDO:           m_d2d.DrawIconRedo(cx, cy, iconSize, brush); break;
        case ICON_CUT:            m_d2d.DrawIconCut(cx, cy, iconSize, brush); break;
        case ICON_PASTE:          m_d2d.DrawIconPaste(cx, cy, iconSize, brush); break;
        case ICON_OPEN_WITH:      m_d2d.DrawIconExternalLink(cx, cy, iconSize, brush); break;
        case ICON_MORE:           m_d2d.DrawIconHamburger(cx, cy, iconSize, brush); break;
    }
}

void AppWindow::DrawTextButton(const D2D1_RECT_F& rect, const WCHAR* label, int buttonId, bool enabled) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, 6.0f, 6.0f);
    D2D1_RECT_F drawRect = rect;

    if (m_pressedButtonId == buttonId && enabled) {
        D2D1_RECT_F insetRect = D2D1::RectF(rect.left + 0.6f, rect.top + 0.6f, rect.right - 0.6f, rect.bottom - 0.6f);
        D2D1_ROUNDED_RECT insetR = D2D1::RoundedRect(insetRect, 5.0f, 5.0f);
        rt->FillRoundedRectangle(insetR, m_d2d.BrushActive);
        drawRect.top += 0.5f;
        drawRect.bottom += 0.5f;
    } else if (m_hoveredButtonId == buttonId && enabled) {
        rt->FillRoundedRectangle(rrect, m_d2d.BrushHover);
        rt->DrawRoundedRectangle(rrect, m_d2d.BrushBorderSubtle, 1.0f);
    }

    ID2D1Brush* brush = enabled ? (ID2D1Brush*)m_d2d.BrushText : (ID2D1Brush*)m_d2d.BrushTextMuted;
    IDWriteTextFormat* font = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
    rt->DrawText(label, (UINT32)wcslen(label), font, drawRect, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

const WCHAR* AppWindow::GetButtonTooltip(int buttonId) const {
    switch (buttonId) {
        case BTN_SIDEBAR: return m_showSidebar ? L"Hide Sidebar (S)" : L"Show Sidebar (S)";
        case BTN_PREV_PAGE: return (m_docType == DocumentType::Pdf && m_totalPages > 1) ? L"Previous Page (←)" : L"Previous File (←)";
        case BTN_NEXT_PAGE: return (m_docType == DocumentType::Pdf && m_totalPages > 1) ? L"Next Page (→)" : L"Next File (→)";
        case BTN_ZOOM_OUT: return L"Zoom Out (-)";
        case BTN_ZOOM_IN: return L"Zoom In (+)";
        case BTN_ZOOM_FIT: return L"Fit to Window (0)";
        case BTN_ZOOM_RESET: return L"Actual Size 100% (1)";
        case BTN_ROTATE_L: return L"Rotate Left (L)";
        case BTN_ROTATE_R: return L"Rotate Right (R)";
        case BTN_PLAY_PAUSE: return m_media.IsPlaying() ? L"Pause (Space)" : L"Play (Space)";
        case BTN_MUTE:
        case BTN_TOOLBAR_MUTE: return m_media.IsMuted() ? L"Unmute (M)" : L"Mute (M)";
        case BTN_FULLSCREEN:
        case BTN_TOOLBAR_FS: return m_isFullscreen ? L"Exit Fullscreen (Esc)" : L"Fullscreen (F)";
        case BTN_LOOP:
        case BTN_HUD_LOOP: return m_isLooping ? L"Loop: On" : L"Loop: Off";
        case BTN_SKIP_BACK: return L"Rewind 5s (←)";
        case BTN_SKIP_FORWARD: return L"Forward 5s (→)";
        case BTN_SEARCH: return L"Search Document (Ctrl+F)";
        case BTN_CROP:
            if (m_hasDetectedContentBounds) {
                return m_trimBlackBars ? L"Show Full Canvas (Include Black Bars) (Ctrl+T)" : L"Trim Black Bars (Fit Content) (Ctrl+T)";
            }
            return m_isCropActive ? L"Commit Crop (Enter)" : L"Crop Image (C)";
        case BTN_INFO: return L"File Info (I)";
        case BTN_SIDEBAR_TAB_THUMBS: return L"Thumbnails View";
        case BTN_SIDEBAR_TAB_OUTLINE: return L"Table of Contents Outline";
        case BTN_SEARCH_PREV: return L"Previous Match (Shift+Enter)";
        case BTN_SEARCH_NEXT: return L"Next Match (Enter)";
        case BTN_SEARCH_CLOSE: return L"Close Search (Esc)";
        case BTN_INSPECTOR_CLOSE: return L"Close Inspector (Esc)";
        case BTN_INSPECTOR_COPY: return L"Copy Details to Clipboard";
        case BTN_UNSUPPORTED_OPEN: return L"Open File with Default Program (Enter)";
        case BTN_UNSUPPORTED_REVEAL: return L"Reveal in File Explorer (Ctrl+Shift+R)";
        case BTN_UNSUPPORTED_COPY: return L"Copy File Path to Clipboard (Ctrl+C)";
        case BTN_SAVE_FILE: return m_text.IsModified() ? L"Save Changes (Ctrl+S)" : L"File Saved (Ctrl+S)";
        case BTN_UNDO: return L"Undo (Ctrl+Z)";
        case BTN_REDO: return L"Redo (Ctrl+Y)";
        case BTN_CUT: return L"Cut Selection (Ctrl+X)";
        case BTN_COPY_TEXT: return (m_docType == DocumentType::Spreadsheet && m_text.IsTableView()) ? L"Copy Table (Ctrl+C)" : (m_text.HasSelection() ? L"Copy Selection (Ctrl+C)" : L"Copy Line (Ctrl+C)");
        case BTN_PASTE: return L"Paste (Ctrl+V)";
        case BTN_WRAP_TEXT: return m_text.IsWordWrap() ? L"Disable Word Wrap (W)" : L"Enable Word Wrap (W)";
        case BTN_TABLE_VIEW: return m_text.IsTableView() ? L"Switch to Raw Text Editor (T)" : L"Switch to Table Grid View (T)";
        case BTN_OPEN_WITH:
            m_openWithTooltip = L"Open with " + (m_openWithAppName.empty() ? L"Default App" : m_openWithAppName) + L" (Enter)";
            return m_openWithTooltip.c_str();
        case BTN_SETTINGS: return L"Keybind Settings (Ctrl+,)";
        case BTN_OVERFLOW_MENU: return L"More Actions (⋯)";
        default: return nullptr;
    }
}

D2D1_RECT_F AppWindow::GetButtonRect(int buttonId) const {
    switch (buttonId) {
        case BTN_SIDEBAR: return m_btnSidebarRect;
        case BTN_PREV_PAGE: return m_btnPrevPageRect;
        case BTN_NEXT_PAGE: return m_btnNextPageRect;
        case BTN_ZOOM_OUT: return m_btnZoomOutRect;
        case BTN_ZOOM_IN: return m_btnZoomInRect;
        case BTN_ZOOM_FIT: return m_btnZoomFitRect;
        case BTN_ZOOM_RESET: return m_btnZoomResetRect;
        case BTN_ROTATE_L: return m_btnRotateLeftRect;
        case BTN_ROTATE_R: return m_btnRotateRightRect;
        case BTN_PLAY_PAUSE: return m_hudBtnPlayPause;
        case BTN_LOOP: return m_hudBtnLoop;
        case BTN_TOOLBAR_MUTE: return m_hudBtnMute;
        case BTN_TOOLBAR_FS: return m_btnToolbarFsRect;
        case BTN_SEARCH: return m_btnSearchRect;
        case BTN_CROP: return m_btnCropRect;
        case BTN_INFO: return m_btnInfoRect;
        case BTN_OPEN_WITH: return m_btnOpenWithRect;
        case BTN_SETTINGS: return m_btnSettingsRect;
        case BTN_SIDEBAR_TAB_THUMBS: return m_tabThumbsRect;
        case BTN_SIDEBAR_TAB_OUTLINE: return m_tabOutlineRect;
        case BTN_SEARCH_PREV: return m_searchPrevBtn;
        case BTN_SEARCH_NEXT: return m_searchNextBtn;
        case BTN_SEARCH_CLOSE: return m_searchCloseBtn;
        case BTN_INSPECTOR_CLOSE: return m_inspectorCloseRect;
        case BTN_INSPECTOR_COPY: return m_inspectorCopyRect;
        case BTN_UNSUPPORTED_OPEN: return m_unsupportedOpenBtnRect;
        case BTN_UNSUPPORTED_REVEAL: return m_unsupportedRevealBtnRect;
        case BTN_UNSUPPORTED_COPY: return m_unsupportedCopyBtnRect;
        case BTN_SAVE_FILE: return m_btnSaveTextRect;
        case BTN_UNDO: return m_btnUndoRect;
        case BTN_REDO: return m_btnRedoRect;
        case BTN_CUT: return m_btnCutTextRect;
        case BTN_COPY_TEXT: return m_btnCopyTextRect;
        case BTN_PASTE: return m_btnPasteTextRect;
        case BTN_WRAP_TEXT: return m_btnWrapTextRect;
        case BTN_TABLE_VIEW: return m_btnTableViewRect;
        case BTN_OVERFLOW_MENU: return m_btnOverflowRect;
        default: return D2D1::RectF(0, 0, 0, 0);
    }
}

bool AppWindow::SaveScreenshot(const std::wstring& outPath) {
    LogTime("SaveScreenshot entered");
    if (!m_d2d.GetRenderTarget()) {
        m_d2d.CreateDeviceResources(m_hwnd);
    }
    UpdateLayout();
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return false;

    // Use exact pixel dimensions of the Direct2D render target
    D2D1_SIZE_U pixSize = rt->GetPixelSize();
    int width = (int)pixSize.width;
    int height = (int)pixSize.height;
    if (width <= 0) width = 1240;
    if (height <= 0) height = 820;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height; // Top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP hBitmap = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBitmap);

    Render(memDC);

    // Guarantee full opacity across all pixel rows
    if (pBits) {
        DWORD* pPixels = (DWORD*)pBits;
        int total = width * height;
        for (int i = 0; i < total; ++i) {
            pPixels[i] |= 0xFF000000;
        }
    }

    IWICImagingFactory* pWic = m_d2d.GetWICFactory();
    bool saved = false;
    if (pWic) {
        IWICBitmap* wicBmp = nullptr;
        HRESULT hr = pWic->CreateBitmapFromHBITMAP(hBitmap, NULL, WICBitmapUseAlpha, &wicBmp);
        if (FAILED(hr)) {
            char err[64];
            sprintf_s(err, "CreateBitmapFromHBITMAP failed: 0x%08X", (unsigned int)hr);
            LogTime(err);
        }
        if (SUCCEEDED(hr) && wicBmp) {
            IWICStream* stream = nullptr;
            pWic->CreateStream(&stream);
            if (stream) {
                DeleteFileW(outPath.c_str());
                hr = stream->InitializeFromFilename(outPath.c_str(), GENERIC_WRITE);
                if (FAILED(hr)) {
                    char err[64];
                    sprintf_s(err, "InitializeFromFilename failed: 0x%08X", (unsigned int)hr);
                    LogTime(err);
                }
                if (SUCCEEDED(hr)) {
                    IWICBitmapEncoder* encoder = nullptr;
                    pWic->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder);
                    if (encoder) {
                        encoder->Initialize(stream, WICBitmapEncoderNoCache);
                        IWICBitmapFrameEncode* frame = nullptr;
                        encoder->CreateNewFrame(&frame, NULL);
                        if (frame) {
                            frame->Initialize(NULL);
                            frame->WriteSource(wicBmp, NULL);
                            frame->Commit();
                            frame->Release();
                        }
                        encoder->Commit();
                        encoder->Release();
                        saved = true;
                    }
                }
                stream->Release();
            }
            wicBmp->Release();
        }
    }
    LogTime(saved ? "SaveScreenshot SUCCESS" : "SaveScreenshot FAILED");

    SelectObject(memDC, oldBmp);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
    return saved;
}

void AppWindow::Render(HDC outHdc) {
    if (!m_d2d.GetRenderTarget()) {
        m_d2d.CreateDeviceResources(m_hwnd);
    }
    m_d2d.BeginDraw();
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    D2D1_SIZE_F size = rt->GetSize();

    // 1. Clear background (#101012 Deep Zinc)
    rt->Clear(D2D1::ColorF(0.063f, 0.063f, 0.071f, 1.0f));

    // Calculate regions
    D2D1_RECT_F sidebarRect = D2D1::RectF(0.0f, 0.0f, m_sidebarWidth, size.height);
    D2D1_RECT_F canvasRect = D2D1::RectF(
        (m_showSidebar && m_docType == DocumentType::Pdf) ? m_sidebarWidth : 0.0f,
        0.0f,
        size.width,
        size.height
    );

    // 2. Render Canvas or Empty State or Unsupported State
    if (m_docType == DocumentType::None) {
        RenderEmptyState(canvasRect);
    } else if (m_docType == DocumentType::Unsupported) {
        RenderUnsupportedState(canvasRect);
    } else {
        RenderCanvas(canvasRect);
    }

    // 3. Render Sidebar (if visible)
    if (m_showSidebar && m_docType == DocumentType::Pdf) {
        RenderSidebar(sidebarRect);
    }

    // 4. Render Toolbar on top
    if ((m_docType == DocumentType::Image || m_docType == DocumentType::Pdf ||
         m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet ||
         m_docType == DocumentType::Office || m_docType == DocumentType::Markdown ||
         m_docType == DocumentType::Notebook || m_docType == DocumentType::Svg ||
         m_docType == DocumentType::Font) && m_dockOpacity > 0.005f) {
        if (!m_dockLayer) {
            rt->CreateLayer(NULL, &m_dockLayer);
        }
        if (m_dockLayer) {
            rt->PushLayer(
                D2D1::LayerParameters(
                    D2D1::InfiniteRect(),
                    NULL,
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                    D2D1::IdentityMatrix(),
                    m_dockOpacity
                ),
                m_dockLayer
            );
            RenderToolbar(D2D1::RectF(0.0f, 0.0f, size.width, size.height));
            rt->PopLayer();
        } else {
            RenderToolbar(D2D1::RectF(0.0f, 0.0f, size.width, size.height));
        }
    } else {
        m_topDockRect = {};
        m_btnSidebarRect = {};
        m_btnPrevPageRect = {};
        m_btnNextPageRect = {};
        m_btnZoomOutRect = {};
        m_btnZoomResetRect = {};
        m_btnZoomInRect = {};
        m_btnZoomFitRect = {};
        m_btnRotateLeftRect = {};
        m_btnRotateRightRect = {};
        m_btnToolbarPlayPauseRect = {};
        m_btnLoopRect = {};
        m_btnToolbarMuteRect = {};
        m_btnToolbarFsRect = {};
        m_btnSearchRect = {};
        m_btnCropRect = {};
        m_btnInfoRect = {};
        m_btnSaveTextRect = {};
        m_btnUndoRect = {};
        m_btnRedoRect = {};
        m_btnCutTextRect = {};
        m_btnCopyTextRect = {};
        m_btnPasteTextRect = {};
        m_btnWrapTextRect = {};
        m_btnTableViewRect = {};
        m_btnSettingsRect = {};
        m_btnOpenWithRect = {};
        m_btnOverflowRect = {};
        m_showOverflowMenu = false;
    }

    // 5. Render Floating Overlays (Search Bar, Inspector, Settings Modal, and Overflow Menu)
    if (m_showSearch && m_docType == DocumentType::Pdf) {
        RenderSearchBar(canvasRect);
    }
    if (m_showInspector) {
        RenderInspector(canvasRect);
    }
    if (m_showSettings) {
        RenderSettingsModal(canvasRect);
    }
    if (m_showOverflowMenu) {
        RenderOverflowMenu(canvasRect);
    }

    // 6. Render Micro-tooltip if active
    bool isDockBtn = (m_hoveredButtonId >= BTN_SIDEBAR && m_hoveredButtonId <= BTN_TOOLBAR_FS) ||
                     m_hoveredButtonId == BTN_SEARCH || m_hoveredButtonId == BTN_CROP ||
                     m_hoveredButtonId == BTN_INFO || m_hoveredButtonId == BTN_OPEN_WITH ||
                     m_hoveredButtonId == BTN_SETTINGS || m_hoveredButtonId == BTN_OVERFLOW_MENU ||
                     (m_hoveredButtonId >= BTN_SAVE_FILE && m_hoveredButtonId <= BTN_TABLE_VIEW);

    if (m_tooltipAlpha > 0.05f && m_hoveredButtonId != BTN_NONE && (!isDockBtn || m_dockOpacity > 0.5f)) {
        const WCHAR* tipText = GetButtonTooltip(m_hoveredButtonId);
        if (tipText) {
            D2D1_RECT_F btnRect = GetButtonRect(m_hoveredButtonId);
            float anchorX = btnRect.left + (btnRect.right - btnRect.left) / 2.0f;
            float anchorY = btnRect.bottom + 4.0f;
            m_d2d.DrawMicroTooltip(anchorX, anchorY, tipText, m_tooltipAlpha);
        }
    }

    // 7. Render Toast Notification (always on top of canvas and dock)
    if (m_toastOpacity > 0.01f) {
        RenderToast(canvasRect);
    }

    // Note: In-App Auto-Update Notification is now rendered via top-level floating overlay (m_updateHwnd)
    // so it floats strictly above LibVLC video playback, DirectX swapchains, and all media.

    if (outHdc) {
        ID2D1GdiInteropRenderTarget* gdiInterop = nullptr;
        if (SUCCEEDED(rt->QueryInterface(__uuidof(ID2D1GdiInteropRenderTarget), (void**)&gdiInterop)) && gdiInterop) {
            HDC hdcD2D = nullptr;
            if (SUCCEEDED(gdiInterop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &hdcD2D)) && hdcD2D) {
                // Direct2D GetDC sets a custom mapping mode matching DIPs.
                // Reset to 1:1 physical pixel mapping (MM_TEXT) with identity transform
                // so BitBlt transfers the entire physical buffer without clipping or scaling distortions!
                SetMapMode(hdcD2D, MM_TEXT);
                SetGraphicsMode(hdcD2D, GM_ADVANCED);
                XFORM identity = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
                SetWorldTransform(hdcD2D, &identity);

                D2D1_SIZE_U pixSize = rt->GetPixelSize();
                BitBlt(outHdc, 0, 0, (int)pixSize.width, (int)pixSize.height, hdcD2D, 0, 0, SRCCOPY);
                gdiInterop->ReleaseDC(nullptr);
            }
            gdiInterop->Release();
        }
    }

    HRESULT hr = m_d2d.EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        SafeRelease(m_dockLayer);
        SafeRelease(m_imageBitmap);
        SafeRelease(m_pdfPageBitmap);
    }
}

void AppWindow::RenderToolbar(const D2D1_RECT_F& rect) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    // Clear hitboxes
    m_topDockRect = {};
    m_btnOverflowRect = {};
    m_btnSidebarRect = {};
    m_btnPrevPageRect = {};
    m_btnNextPageRect = {};
    m_btnZoomOutRect = {};
    m_btnZoomResetRect = {};
    m_btnZoomInRect = {};
    m_btnZoomFitRect = {};
    m_btnRotateLeftRect = {};
    m_btnRotateRightRect = {};
    m_btnToolbarPlayPauseRect = {};
    m_btnLoopRect = {};
    m_btnToolbarMuteRect = {};
    m_btnToolbarFsRect = {};
    m_btnSearchRect = {};
    m_btnCropRect = {};
    m_btnInfoRect = {};
    m_btnSettingsRect = {};
    m_btnSaveTextRect = {};
    m_btnUndoRect = {};
    m_btnRedoRect = {};
    m_btnCutTextRect = {};
    m_btnCopyTextRect = {};
    m_btnPasteTextRect = {};
    m_btnWrapTextRect = {};
    m_btnTableViewRect = {};
    m_btnOpenWithRect = {};

    if (m_docType != DocumentType::Image && m_docType != DocumentType::Pdf &&
        m_docType != DocumentType::Text && m_docType != DocumentType::Spreadsheet &&
        m_docType != DocumentType::Office && m_docType != DocumentType::Markdown &&
        m_docType != DocumentType::Notebook && m_docType != DocumentType::Svg &&
        m_docType != DocumentType::Font) return;
    if (m_dockOpacity <= 0.005f) return;

    float totalW = rect.right - rect.left;
    float availLeft = (m_showSidebar && m_docType == DocumentType::Pdf) ? m_sidebarWidth : 0.0f;
    float availW = totalW - availLeft;
    float dockH = 38.0f;
    float dockRadius = dockH / 2.0f; // 19.0f smooth circular pill caps
    float dockY = 12.0f;
    float btnH = 30.0f;
    float btnY = dockY + (dockH - btnH) / 2.0f;

    // Dynamic width calculation based on active mode and responsive tier
    float dockW = 0.0f;
    bool showPdfNav = (m_docType == DocumentType::Pdf && m_totalPages > 1);

    if (m_docType == DocumentType::Image) {
        if (availW >= 460.0f) dockW = 427.0f;
        else if (availW >= 320.0f) dockW = 210.0f;
        else dockW = 136.0f;
    } else if (m_docType == DocumentType::Pdf) {
        if (showPdfNav) {
            if (availW >= 560.0f) dockW = 538.0f;
            else if (availW >= 380.0f) dockW = 322.0f;
            else dockW = 194.0f;
        } else {
            if (availW >= 460.0f) dockW = 427.0f;
            else if (availW >= 320.0f) dockW = 210.0f;
            else dockW = 136.0f;
        }
    } else if (m_docType == DocumentType::Text) {
        if (availW >= 560.0f) dockW = 514.0f;
        else if (availW >= 420.0f) dockW = 364.0f;
        else dockW = 232.0f;
    } else if (m_docType == DocumentType::Spreadsheet) {
        if (availW >= 560.0f) dockW = 514.0f;
        else if (availW >= 420.0f) dockW = 364.0f;
        else dockW = 232.0f;
    } else if (m_docType == DocumentType::Office || m_docType == DocumentType::Svg) {
        if (availW >= 440.0f) dockW = 393.0f;
        else if (availW >= 320.0f) dockW = 210.0f;
        else dockW = 136.0f;
    } else if (m_docType == DocumentType::Markdown || m_docType == DocumentType::Notebook) {
        if (availW >= 320.0f) dockW = 218.0f;
        else dockW = 88.0f;
    } else if (m_docType == DocumentType::Font) {
        if (availW >= 280.0f) dockW = 172.0f;
        else dockW = 74.0f;
    }

    if (dockW > availW - 16.0f) dockW = availW - 16.0f;
    float dockX = availLeft + (availW - dockW) / 2.0f;
    if (dockX < availLeft + 8.0f) dockX = availLeft + 8.0f;

    m_topDockRect = D2D1::RectF(dockX, dockY, dockX + dockW, dockY + dockH);

    // 1. Ambient Drop Shadow (Multi-stop smooth falloff)
    D2D1_ROUNDED_RECT rShadow = D2D1::RoundedRect(
        D2D1::RectF(dockX, dockY + 2.5f, dockX + dockW, dockY + dockH + 5.5f),
        dockRadius, dockRadius
    );
    m_d2d.BrushShadowDark->SetOpacity(0.35f);
    rt->FillRoundedRectangle(rShadow, m_d2d.BrushShadowDark);
    m_d2d.BrushShadowDark->SetOpacity(0.40f);

    // 2. Elevated Floating Pill Container (#18181B / 96% opacity dark glass)
    D2D1_ROUNDED_RECT rDock = D2D1::RoundedRect(m_topDockRect, dockRadius, dockRadius);
    m_d2d.BrushToolbar->SetOpacity(0.96f);
    rt->FillRoundedRectangle(rDock, m_d2d.BrushToolbar);
    m_d2d.BrushToolbar->SetOpacity(1.0f);

    // 3. Subtle outer 1px border
    rt->DrawRoundedRectangle(rDock, m_d2d.BrushBorderSubtle, 1.0f);

    // Helper for vertical separator line
    auto drawDivider = [&](float x) {
        rt->DrawLine(
            D2D1::Point2F(x, dockY + 9.0f),
            D2D1::Point2F(x, dockY + dockH - 9.0f),
            m_d2d.BrushBorder, 1.0f
        );
    };

    float curX = dockX + 5.0f;

    // =========================================================================
    // DOCK RENDERING: IMAGE MODE
    // =========================================================================
    if (m_docType == DocumentType::Image) {
        bool isFull = (availW >= 460.0f);
        bool isCompact = (availW < 320.0f);

        if (!isCompact) {
            // Segment 1: Inset Zoom Capsule [ - | 100% | + ]
            float capH = 28.0f;
            float capY = dockY + (dockH - capH) / 2.0f;
            float zoomBtnW = 27.0f;
            float zoomTextW = 50.0f;
            float totalCapW = zoomBtnW + 1.0f + zoomTextW + 1.0f + zoomBtnW; // 106px

            D2D1_RECT_F capRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
            D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(capRect, 6.0f, 6.0f);
            rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
            rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

            // Zoom Out [-]
            m_btnZoomOutRect = D2D1::RectF(curX, capY, curX + zoomBtnW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_OUT) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_OUT) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushHover);
            }
            float minusCx = m_btnZoomOutRect.left + zoomBtnW / 2.0f;
            float minusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_OUT ? 0.5f : 0.0f);
            m_d2d.DrawIconZoomOut(minusCx, minusCy, 13.0f, m_d2d.BrushText);

            float div1X = curX + zoomBtnW;
            rt->DrawLine(D2D1::Point2F(div1X, capY + 4.0f), D2D1::Point2F(div1X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

            // Zoom Percent Readout
            m_btnZoomResetRect = D2D1::RectF(div1X + 1.0f, capY, div1X + 1.0f + zoomTextW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_RESET) {
                rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushHover);
            }
            WCHAR zoomBuf[16];
            swprintf_s(zoomBuf, L"%d%%", (int)(m_zoom * 100.0f + 0.5f));
            IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
            rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

            float div2X = div1X + 1.0f + zoomTextW;
            rt->DrawLine(D2D1::Point2F(div2X, capY + 4.0f), D2D1::Point2F(div2X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

            // Zoom In [+]
            m_btnZoomInRect = D2D1::RectF(div2X + 1.0f, capY, curX + totalCapW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_IN) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_IN) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushHover);
            }
            float plusCx = m_btnZoomInRect.left + zoomBtnW / 2.0f;
            float plusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_IN ? 0.5f : 0.0f);
            m_d2d.DrawIconZoomIn(plusCx, plusCy, 13.0f, m_d2d.BrushText);

            curX += totalCapW;
            drawDivider(curX + 8.0f);
            curX += 17.0f;

            // Fit to Window [ <-> ]
            m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
            curX += 30.0f;

            if (isFull) {
                // Rotation [ ↺  ↻ ]
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnRotateLeftRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnRotateLeftRect, ICON_ROTATE_LEFT, BTN_ROTATE_L, true);
                curX += 32.0f;

                m_btnRotateRightRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnRotateRightRect, ICON_ROTATE_RIGHT, BTN_ROTATE_R, true);
                curX += 30.0f;

                drawDivider(curX + 8.0f);
                curX += 17.0f;

                // Actions [ ⯐ ] [ ⓘ ] [ ↗ ] [ ⚙ ] [ ⛶ ]
                m_btnCropRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnCropRect, ICON_CROP, BTN_CROP, true);
                curX += 34.0f;

                m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
                curX += 34.0f;

                m_btnOpenWithRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnOpenWithRect, ICON_OPEN_WITH, BTN_OPEN_WITH, true);
                curX += 34.0f;

                m_btnSettingsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnSettingsRect, ICON_SETTINGS, BTN_SETTINGS, true);
                curX += 34.0f;

                m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
            } else {
                // Responsive Overflow Button [ ☰ ]
                drawDivider(curX + 8.0f);
                curX += 17.0f;
                m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
            }
        } else {
            // Ultra-compact tier (< 320px down to 200px)
            // Inset Zoom Reset Pill [ 100% ]
            float capH = 26.0f;
            float capY = dockY + (dockH - capH) / 2.0f;
            float totalCapW = 46.0f;

            m_btnZoomResetRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
            D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(m_btnZoomResetRect, 6.0f, 6.0f);
            if (m_pressedButtonId == BTN_ZOOM_RESET) {
                rt->FillRoundedRectangle(capR, m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                rt->FillRoundedRectangle(capR, m_d2d.BrushHover);
            } else {
                rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
            }
            rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

            WCHAR zoomBuf[16];
            swprintf_s(zoomBuf, L"%d%%", (int)(m_zoom * 100.0f + 0.5f));
            IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
            rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

            curX += totalCapW;
            drawDivider(curX + 5.5f);
            curX += 12.0f;

            // Fit [ <-> ]
            m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
            curX += 28.0f;

            // Overflow [ ☰ ]
            drawDivider(curX + 5.5f);
            curX += 12.0f;
            m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
        }
    }
        // =========================================================================
    // DOCK RENDERING: PDF MODE
    // =========================================================================
    else if (m_docType == DocumentType::Pdf) {
        bool isFull = showPdfNav ? (availW >= 620.0f) : (availW >= 540.0f);
        bool isCompact = (availW < 420.0f);

        if (showPdfNav) {
            if (!isCompact) {
                // Sidebar Toggle
                m_btnSidebarRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnSidebarRect, ICON_SIDEBAR, BTN_SIDEBAR, true);
                curX += 30.0f;

                drawDivider(curX + 8.0f);
                curX += 17.0f;

                // Page Nav Capsule: [ ◀  1 / N  ▶ ] (108px)
                float navH = 28.0f;
                float navY = dockY + (dockH - navH) / 2.0f;
                float navBtnW = 26.0f;
                float navTextW = 54.0f;
                float totalNavW = navBtnW + 1.0f + navTextW + 1.0f + navBtnW; // 108px

                D2D1_RECT_F navRect = D2D1::RectF(curX, navY, curX + totalNavW, navY + navH);
                D2D1_ROUNDED_RECT navR = D2D1::RoundedRect(navRect, 6.0f, 6.0f);
                rt->FillRoundedRectangle(navR, m_d2d.BrushCard);
                rt->DrawRoundedRectangle(navR, m_d2d.BrushBorderSubtle, 1.0f);

                // Prev Page [◀]
                m_btnPrevPageRect = D2D1::RectF(curX, navY, curX + navBtnW, navY + navH);
                if (m_pressedButtonId == BTN_PREV_PAGE && m_currentPage > 0) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnPrevPageRect, 5.0f, 5.0f), m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_PREV_PAGE && m_currentPage > 0) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnPrevPageRect, 5.0f, 5.0f), m_d2d.BrushHover);
                }
                float prevCx = m_btnPrevPageRect.left + navBtnW / 2.0f;
                float prevCy = navY + navH / 2.0f + (m_pressedButtonId == BTN_PREV_PAGE ? 0.5f : 0.0f);
                m_d2d.DrawIconChevronLeft(prevCx, prevCy, 13.0f, (m_currentPage > 0) ? (ID2D1Brush*)m_d2d.BrushText : (ID2D1Brush*)m_d2d.BrushTextMuted);

                // Divider 1
                float div1X = curX + navBtnW;
                rt->DrawLine(D2D1::Point2F(div1X, navY + 4.0f), D2D1::Point2F(div1X, navY + navH - 4.0f), m_d2d.BrushBorder, 1.0f);

                // Page text readout
                D2D1_RECT_F pageTextRect = D2D1::RectF(div1X + 1.0f, navY, div1X + 1.0f + navTextW, navY + navH);
                WCHAR pageBuf[32];
                swprintf_s(pageBuf, L"%d / %d", m_currentPage + 1, m_totalPages);
                IDWriteTextFormat* fontNav = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
                rt->DrawText(pageBuf, (UINT32)wcslen(pageBuf), fontNav, pageTextRect, m_d2d.BrushText);

                // Divider 2
                float div2X = div1X + 1.0f + navTextW;
                rt->DrawLine(D2D1::Point2F(div2X, navY + 4.0f), D2D1::Point2F(div2X, navY + navH - 4.0f), m_d2d.BrushBorder, 1.0f);

                // Next Page [▶]
                m_btnNextPageRect = D2D1::RectF(div2X + 1.0f, navY, curX + totalNavW, navY + navH);
                if (m_pressedButtonId == BTN_NEXT_PAGE && m_currentPage + 1 < m_totalPages) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnNextPageRect, 5.0f, 5.0f), m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_NEXT_PAGE && m_currentPage + 1 < m_totalPages) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnNextPageRect, 5.0f, 5.0f), m_d2d.BrushHover);
                }
                float nextCx = m_btnNextPageRect.left + navBtnW / 2.0f;
                float nextCy = navY + navH / 2.0f + (m_pressedButtonId == BTN_NEXT_PAGE ? 0.5f : 0.0f);
                m_d2d.DrawIconChevronRight(nextCx, nextCy, 13.0f, (m_currentPage + 1 < m_totalPages) ? (ID2D1Brush*)m_d2d.BrushText : (ID2D1Brush*)m_d2d.BrushTextMuted);

                curX += totalNavW;
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                // Inset Zoom Capsule [ - | 100% | + ]
                float capH = 28.0f;
                float capY = dockY + (dockH - capH) / 2.0f;
                float zoomBtnW = 27.0f;
                float zoomTextW = 50.0f;
                float totalCapW = zoomBtnW + 1.0f + zoomTextW + 1.0f + zoomBtnW; // 106px

                D2D1_RECT_F capRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
                D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(capRect, 6.0f, 6.0f);
                rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
                rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

                // Zoom Out [-]
                m_btnZoomOutRect = D2D1::RectF(curX, capY, curX + zoomBtnW, capY + capH);
                if (m_pressedButtonId == BTN_ZOOM_OUT) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_ZOOM_OUT) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushHover);
                }
                float minusCx = m_btnZoomOutRect.left + zoomBtnW / 2.0f;
                float minusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_OUT ? 0.5f : 0.0f);
                m_d2d.DrawIconZoomOut(minusCx, minusCy, 13.0f, m_d2d.BrushText);

                float div1CapX = curX + zoomBtnW;
                rt->DrawLine(D2D1::Point2F(div1CapX, capY + 4.0f), D2D1::Point2F(div1CapX, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

                // Zoom Readout
                m_btnZoomResetRect = D2D1::RectF(div1CapX + 1.0f, capY, div1CapX + 1.0f + zoomTextW, capY + capH);
                if (m_pressedButtonId == BTN_ZOOM_RESET) {
                    rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                    rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushHover);
                }
                WCHAR zoomBuf[16];
                swprintf_s(zoomBuf, L"%d%%", (int)(m_zoom * 100.0f + 0.5f));
                IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
                rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

                float div2CapX = div1CapX + 1.0f + zoomTextW;
                rt->DrawLine(D2D1::Point2F(div2CapX, capY + 4.0f), D2D1::Point2F(div2CapX, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

                // Zoom In [+]
                m_btnZoomInRect = D2D1::RectF(div2CapX + 1.0f, capY, curX + totalCapW, capY + capH);
                if (m_pressedButtonId == BTN_ZOOM_IN) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_ZOOM_IN) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushHover);
                }
                float plusCx = m_btnZoomInRect.left + zoomBtnW / 2.0f;
                float plusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_IN ? 0.5f : 0.0f);
                m_d2d.DrawIconZoomIn(plusCx, plusCy, 13.0f, m_d2d.BrushText);

                curX += totalCapW;
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                // Fit to Window
                m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
                curX += 30.0f;

                if (isFull) {
                    // Rotate Left & Right
                    drawDivider(curX + 8.0f);
                    curX += 17.0f;

                    m_btnRotateLeftRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnRotateLeftRect, ICON_ROTATE_LEFT, BTN_ROTATE_L, true);
                    curX += 32.0f;

                    m_btnRotateRightRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnRotateRightRect, ICON_ROTATE_RIGHT, BTN_ROTATE_R, true);
                    curX += 30.0f;

                    drawDivider(curX + 8.0f);
                    curX += 17.0f;

                    // Search, Info, Open With, Settings, Fullscreen
                    m_btnSearchRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnSearchRect, ICON_SEARCH, BTN_SEARCH, true);
                    curX += 34.0f;

                    m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
                    curX += 34.0f;

                    m_btnOpenWithRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnOpenWithRect, ICON_OPEN_WITH, BTN_OPEN_WITH, true);
                    curX += 34.0f;

                    m_btnSettingsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnSettingsRect, ICON_SETTINGS, BTN_SETTINGS, true);
                    curX += 34.0f;

                    m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
                } else {
                    // Overflow Button [ ☰ ]
                    drawDivider(curX + 8.0f);
                    curX += 17.0f;
                    m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
                }
            } else {
                // Ultra-compact tier (< 420px): Sidebar moved to overflow menu!
                float navH = 26.0f;
                float navY = dockY + (dockH - navH) / 2.0f;
                float navBtnW = 23.0f;
                float navTextW = 48.0f;
                float totalNavW = 96.0f;

                D2D1_RECT_F navRect = D2D1::RectF(curX, navY, curX + totalNavW, navY + navH);
                D2D1_ROUNDED_RECT navR = D2D1::RoundedRect(navRect, 6.0f, 6.0f);
                rt->FillRoundedRectangle(navR, m_d2d.BrushCard);
                rt->DrawRoundedRectangle(navR, m_d2d.BrushBorderSubtle, 1.0f);

                // Prev Page [◀]
                m_btnPrevPageRect = D2D1::RectF(curX, navY, curX + navBtnW, navY + navH);
                if (m_pressedButtonId == BTN_PREV_PAGE && m_currentPage > 0) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnPrevPageRect, 5.0f, 5.0f), m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_PREV_PAGE && m_currentPage > 0) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnPrevPageRect, 5.0f, 5.0f), m_d2d.BrushHover);
                }
                float prevCx = m_btnPrevPageRect.left + navBtnW / 2.0f;
                float prevCy = navY + navH / 2.0f + (m_pressedButtonId == BTN_PREV_PAGE ? 0.5f : 0.0f);
                m_d2d.DrawIconChevronLeft(prevCx, prevCy, 12.0f, (m_currentPage > 0) ? (ID2D1Brush*)m_d2d.BrushText : (ID2D1Brush*)m_d2d.BrushTextMuted);

                float div1X = curX + navBtnW;
                rt->DrawLine(D2D1::Point2F(div1X, navY + 4.0f), D2D1::Point2F(div1X, navY + navH - 4.0f), m_d2d.BrushBorder, 1.0f);

                // Page text readout
                D2D1_RECT_F pageTextRect = D2D1::RectF(div1X + 1.0f, navY, div1X + 1.0f + navTextW, navY + navH);
                WCHAR pageBuf[32];
                swprintf_s(pageBuf, L"%d/%d", m_currentPage + 1, m_totalPages);
                IDWriteTextFormat* fontNav = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
                rt->DrawText(pageBuf, (UINT32)wcslen(pageBuf), fontNav, pageTextRect, m_d2d.BrushText);

                float div2X = div1X + 1.0f + navTextW;
                rt->DrawLine(D2D1::Point2F(div2X, navY + 4.0f), D2D1::Point2F(div2X, navY + navH - 4.0f), m_d2d.BrushBorder, 1.0f);

                // Next Page [▶]
                m_btnNextPageRect = D2D1::RectF(div2X + 1.0f, navY, curX + totalNavW, navY + navH);
                if (m_pressedButtonId == BTN_NEXT_PAGE && m_currentPage + 1 < m_totalPages) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnNextPageRect, 5.0f, 5.0f), m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_NEXT_PAGE && m_currentPage + 1 < m_totalPages) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnNextPageRect, 5.0f, 5.0f), m_d2d.BrushHover);
                }
                float nextCx = m_btnNextPageRect.left + navBtnW / 2.0f;
                float nextCy = navY + navH / 2.0f + (m_pressedButtonId == BTN_NEXT_PAGE ? 0.5f : 0.0f);
                m_d2d.DrawIconChevronRight(nextCx, nextCy, 12.0f, (m_currentPage + 1 < m_totalPages) ? (ID2D1Brush*)m_d2d.BrushText : (ID2D1Brush*)m_d2d.BrushTextMuted);

                curX += totalNavW;
                drawDivider(curX + 5.5f);
                curX += 12.0f;

                // Compact Zoom Pill [ 100% ]
                float capH = 26.0f;
                float capY = dockY + (dockH - capH) / 2.0f;
                float totalCapW = 46.0f;

                m_btnZoomResetRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
                D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(m_btnZoomResetRect, 6.0f, 6.0f);
                if (m_pressedButtonId == BTN_ZOOM_RESET) {
                    rt->FillRoundedRectangle(capR, m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                    rt->FillRoundedRectangle(capR, m_d2d.BrushHover);
                } else {
                    rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
                }
                rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

                WCHAR zoomBuf[16];
                swprintf_s(zoomBuf, L"%d%%", (int)(m_zoom * 100.0f + 0.5f));
                IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
                rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

                curX += totalCapW;
                drawDivider(curX + 5.5f);
                curX += 12.0f;

                // Fit [ <-> ]
                m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
                DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
                curX += 28.0f;

                // Overflow [ ☰ ]
                drawDivider(curX + 5.5f);
                curX += 12.0f;
                m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
                DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
            }
        } else {
            // Single-page PDF (No folder navigation chevrons)
            if (!isCompact) {
                float capH = 28.0f;
                float capY = dockY + (dockH - capH) / 2.0f;
                float zoomBtnW = 27.0f;
                float zoomTextW = 50.0f;
                float totalCapW = zoomBtnW + 1.0f + zoomTextW + 1.0f + zoomBtnW;

                D2D1_RECT_F capRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
                D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(capRect, 6.0f, 6.0f);
                rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
                rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

                m_btnZoomOutRect = D2D1::RectF(curX, capY, curX + zoomBtnW, capY + capH);
                if (m_pressedButtonId == BTN_ZOOM_OUT) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_ZOOM_OUT) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushHover);
                }
                float minusCx = m_btnZoomOutRect.left + zoomBtnW / 2.0f;
                float minusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_OUT ? 0.5f : 0.0f);
                m_d2d.DrawIconZoomOut(minusCx, minusCy, 13.0f, m_d2d.BrushText);

                float div1X = curX + zoomBtnW;
                rt->DrawLine(D2D1::Point2F(div1X, capY + 4.0f), D2D1::Point2F(div1X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

                m_btnZoomResetRect = D2D1::RectF(div1X + 1.0f, capY, div1X + 1.0f + zoomTextW, capY + capH);
                if (m_pressedButtonId == BTN_ZOOM_RESET) {
                    rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                    rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushHover);
                }
                WCHAR zoomBuf[16];
                swprintf_s(zoomBuf, L"%d%%", (int)(m_zoom * 100.0f + 0.5f));
                IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
                rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

                float div2X = div1X + 1.0f + zoomTextW;
                rt->DrawLine(D2D1::Point2F(div2X, capY + 4.0f), D2D1::Point2F(div2X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

                m_btnZoomInRect = D2D1::RectF(div2X + 1.0f, capY, curX + totalCapW, capY + capH);
                if (m_pressedButtonId == BTN_ZOOM_IN) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_ZOOM_IN) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushHover);
                }
                float plusCx = m_btnZoomInRect.left + zoomBtnW / 2.0f;
                float plusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_IN ? 0.5f : 0.0f);
                m_d2d.DrawIconZoomIn(plusCx, plusCy, 13.0f, m_d2d.BrushText);

                curX += totalCapW;
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
                curX += 30.0f;

                if (isFull) {
                    drawDivider(curX + 8.0f);
                    curX += 17.0f;

                    m_btnRotateLeftRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnRotateLeftRect, ICON_ROTATE_LEFT, BTN_ROTATE_L, true);
                    curX += 32.0f;

                    m_btnRotateRightRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnRotateRightRect, ICON_ROTATE_RIGHT, BTN_ROTATE_R, true);
                    curX += 30.0f;

                    drawDivider(curX + 8.0f);
                    curX += 17.0f;

                    m_btnSearchRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnSearchRect, ICON_SEARCH, BTN_SEARCH, true);
                    curX += 34.0f;

                    m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
                    curX += 34.0f;

                    m_btnOpenWithRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnOpenWithRect, ICON_OPEN_WITH, BTN_OPEN_WITH, true);
                    curX += 34.0f;

                    m_btnSettingsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnSettingsRect, ICON_SETTINGS, BTN_SETTINGS, true);
                    curX += 34.0f;

                    m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
                } else {
                    drawDivider(curX + 8.0f);
                    curX += 17.0f;
                    m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                    DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
                }
            } else {
                // Compact Single-page PDF
                float capH = 26.0f;
                float capY = dockY + (dockH - capH) / 2.0f;
                float totalCapW = 46.0f;

                m_btnZoomResetRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
                D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(m_btnZoomResetRect, 6.0f, 6.0f);
                if (m_pressedButtonId == BTN_ZOOM_RESET) {
                    rt->FillRoundedRectangle(capR, m_d2d.BrushActive);
                } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                    rt->FillRoundedRectangle(capR, m_d2d.BrushHover);
                } else {
                    rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
                }
                rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

                WCHAR zoomBuf[16];
                swprintf_s(zoomBuf, L"%d%%", (int)(m_zoom * 100.0f + 0.5f));
                IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
                rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

                curX += totalCapW;
                drawDivider(curX + 5.5f);
                curX += 12.0f;

                m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
                DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
                curX += 28.0f;

                drawDivider(curX + 5.5f);
                curX += 12.0f;
                m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
                DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
            }
        }
    }
        // =========================================================================
    // DOCK RENDERING: TEXT & CODE MODE
    // =========================================================================
    else if (m_docType == DocumentType::Text) {
        bool isFull = (availW >= 560.0f);
        bool isCompact = (availW < 420.0f);

        if (!isCompact) {
            // Save, Undo, Redo
            m_btnSaveTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnSaveTextRect, ICON_SAVE, BTN_SAVE_FILE, true);
            curX += 30.0f;

            m_btnUndoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnUndoRect, ICON_UNDO, BTN_UNDO, m_text.CanUndo());
            curX += 30.0f;

            m_btnRedoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnRedoRect, ICON_REDO, BTN_REDO, m_text.CanRedo());
            curX += 30.0f;

            drawDivider(curX + 8.0f);
            curX += 17.0f;

            if (isFull) {
                m_btnCutTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnCutTextRect, ICON_CUT, BTN_CUT, true);
                curX += 30.0f;

                m_btnCopyTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnCopyTextRect, ICON_COPY, BTN_COPY_TEXT, true);
                curX += 30.0f;

                m_btnPasteTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnPasteTextRect, ICON_PASTE, BTN_PASTE, true);
                curX += 30.0f;
            } else {
                m_btnCopyTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnCopyTextRect, ICON_COPY, BTN_COPY_TEXT, true);
                curX += 30.0f;
            }

            drawDivider(curX + 8.0f);
            curX += 17.0f;

            // Inset Font Zoom Capsule [ - | 100% | + ]
            float capH = 28.0f;
            float capY = dockY + (dockH - capH) / 2.0f;
            float zoomBtnW = 27.0f;
            float zoomTextW = 50.0f;
            float totalCapW = zoomBtnW + 1.0f + zoomTextW + 1.0f + zoomBtnW; // 106px

            D2D1_RECT_F capRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
            D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(capRect, 6.0f, 6.0f);
            rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
            rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

            m_btnZoomOutRect = D2D1::RectF(curX, capY, curX + zoomBtnW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_OUT) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_OUT) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushHover);
            }
            float minusCx = m_btnZoomOutRect.left + zoomBtnW / 2.0f;
            float minusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_OUT ? 0.5f : 0.0f);
            m_d2d.DrawIconZoomOut(minusCx, minusCy, 13.0f, m_d2d.BrushText);

            float div1X = curX + zoomBtnW;
            rt->DrawLine(D2D1::Point2F(div1X, capY + 4.0f), D2D1::Point2F(div1X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

            m_btnZoomResetRect = D2D1::RectF(div1X + 1.0f, capY, div1X + 1.0f + zoomTextW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_RESET) {
                rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushHover);
            }
            WCHAR zoomBuf[16];
            swprintf_s(zoomBuf, L"%d%%", (int)(m_text.GetFontSize() / 13.0f * 100.0f + 0.5f));
            IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
            rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

            float div2X = div1X + 1.0f + zoomTextW;
            rt->DrawLine(D2D1::Point2F(div2X, capY + 4.0f), D2D1::Point2F(div2X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

            m_btnZoomInRect = D2D1::RectF(div2X + 1.0f, capY, curX + totalCapW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_IN) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_IN) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushHover);
            }
            float plusCx = m_btnZoomInRect.left + zoomBtnW / 2.0f;
            float plusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_IN ? 0.5f : 0.0f);
            m_d2d.DrawIconZoomIn(plusCx, plusCy, 13.0f, m_d2d.BrushText);

            curX += totalCapW;

            if (isFull) {
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnWrapTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnWrapTextRect, ICON_WRAP, BTN_WRAP_TEXT, true);
                curX += 34.0f;

                m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
                curX += 34.0f;

                m_btnOpenWithRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnOpenWithRect, ICON_OPEN_WITH, BTN_OPEN_WITH, true);
                curX += 34.0f;

                m_btnSettingsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnSettingsRect, ICON_SETTINGS, BTN_SETTINGS, true);
                curX += 34.0f;

                m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
            } else {
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnWrapTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnWrapTextRect, ICON_WRAP, BTN_WRAP_TEXT, true);
                curX += 30.0f;

                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
            }
        } else {
            // Compact tier (< 420px)
            m_btnSaveTextRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnSaveTextRect, ICON_SAVE, BTN_SAVE_FILE, true);
            curX += 28.0f;

            m_btnUndoRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnUndoRect, ICON_UNDO, BTN_UNDO, m_text.CanUndo());
            curX += 28.0f;

            m_btnRedoRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnRedoRect, ICON_REDO, BTN_REDO, m_text.CanRedo());
            curX += 28.0f;

            drawDivider(curX + 5.5f);
            curX += 12.0f;

            m_btnCopyTextRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnCopyTextRect, ICON_COPY, BTN_COPY_TEXT, true);
            curX += 28.0f;

            drawDivider(curX + 5.5f);
            curX += 12.0f;

            // Compact Font Zoom Readout [ 100% ]
            float capH = 26.0f;
            float capY = dockY + (dockH - capH) / 2.0f;
            float totalCapW = 46.0f;

            m_btnZoomResetRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
            D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(m_btnZoomResetRect, 6.0f, 6.0f);
            if (m_pressedButtonId == BTN_ZOOM_RESET) {
                rt->FillRoundedRectangle(capR, m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                rt->FillRoundedRectangle(capR, m_d2d.BrushHover);
            } else {
                rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
            }
            rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

            WCHAR zoomBuf[16];
            swprintf_s(zoomBuf, L"%d%%", (int)(m_text.GetFontSize() / 13.0f * 100.0f + 0.5f));
            IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
            rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

            curX += totalCapW;
            drawDivider(curX + 5.5f);
            curX += 12.0f;

            m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
        }
    }
    // =========================================================================
    // DOCK RENDERING: SPREADSHEET MODE
    // =========================================================================
    else if (m_docType == DocumentType::Spreadsheet) {
        bool isFull = (availW >= 560.0f);
        bool isCompact = (availW < 420.0f);

        if (!isCompact) {
            // Save, Undo, Redo
            m_btnSaveTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnSaveTextRect, ICON_SAVE, BTN_SAVE_FILE, true);
            curX += 30.0f;

            m_btnUndoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnUndoRect, ICON_UNDO, BTN_UNDO, m_text.CanUndo());
            curX += 30.0f;

            m_btnRedoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnRedoRect, ICON_REDO, BTN_REDO, m_text.CanRedo());
            curX += 30.0f;

            drawDivider(curX + 8.0f);
            curX += 17.0f;

            m_btnCopyTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnCopyTextRect, ICON_COPY, BTN_COPY_TEXT, true);
            curX += 30.0f;

            if (isFull) {
                m_btnPasteTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnPasteTextRect, ICON_PASTE, BTN_PASTE, true);
                curX += 30.0f;
            }

            drawDivider(curX + 8.0f);
            curX += 17.0f;

            // Inset Font Zoom Capsule
            float capH = 28.0f;
            float capY = dockY + (dockH - capH) / 2.0f;
            float zoomBtnW = 27.0f;
            float zoomTextW = 50.0f;
            float totalCapW = zoomBtnW + 1.0f + zoomTextW + 1.0f + zoomBtnW;

            D2D1_RECT_F capRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
            D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(capRect, 6.0f, 6.0f);
            rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
            rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

            m_btnZoomOutRect = D2D1::RectF(curX, capY, curX + zoomBtnW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_OUT) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_OUT) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushHover);
            }
            float minusCx = m_btnZoomOutRect.left + zoomBtnW / 2.0f;
            float minusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_OUT ? 0.5f : 0.0f);
            m_d2d.DrawIconZoomOut(minusCx, minusCy, 13.0f, m_d2d.BrushText);

            float div1X = curX + zoomBtnW;
            rt->DrawLine(D2D1::Point2F(div1X, capY + 4.0f), D2D1::Point2F(div1X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

            m_btnZoomResetRect = D2D1::RectF(div1X + 1.0f, capY, div1X + 1.0f + zoomTextW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_RESET) {
                rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushHover);
            }
            WCHAR zoomBuf[16];
            swprintf_s(zoomBuf, L"%d%%", (int)(m_text.GetFontSize() / 13.0f * 100.0f + 0.5f));
            IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
            rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

            float div2X = div1X + 1.0f + zoomTextW;
            rt->DrawLine(D2D1::Point2F(div2X, capY + 4.0f), D2D1::Point2F(div2X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

            m_btnZoomInRect = D2D1::RectF(div2X + 1.0f, capY, curX + totalCapW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_IN) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_IN) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushHover);
            }
            float plusCx = m_btnZoomInRect.left + zoomBtnW / 2.0f;
            float plusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_IN ? 0.5f : 0.0f);
            m_d2d.DrawIconZoomIn(plusCx, plusCy, 13.0f, m_d2d.BrushText);

            curX += totalCapW;

            if (isFull) {
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnTableViewRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnTableViewRect, ICON_TABLE, BTN_TABLE_VIEW, true);
                curX += 34.0f;

                m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
                curX += 34.0f;

                m_btnOpenWithRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnOpenWithRect, ICON_OPEN_WITH, BTN_OPEN_WITH, true);
                curX += 34.0f;

                m_btnSettingsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnSettingsRect, ICON_SETTINGS, BTN_SETTINGS, true);
                curX += 34.0f;

                m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
            } else {
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnTableViewRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnTableViewRect, ICON_TABLE, BTN_TABLE_VIEW, true);
                curX += 30.0f;

                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
            }
        } else {
            // Compact tier
            m_btnSaveTextRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnSaveTextRect, ICON_SAVE, BTN_SAVE_FILE, true);
            curX += 28.0f;

            m_btnUndoRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnUndoRect, ICON_UNDO, BTN_UNDO, m_text.CanUndo());
            curX += 28.0f;

            m_btnRedoRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnRedoRect, ICON_REDO, BTN_REDO, m_text.CanRedo());
            curX += 28.0f;

            drawDivider(curX + 5.5f);
            curX += 12.0f;

            m_btnCopyTextRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnCopyTextRect, ICON_COPY, BTN_COPY_TEXT, true);
            curX += 28.0f;

            drawDivider(curX + 5.5f);
            curX += 12.0f;

            // Compact Font Zoom Readout
            float capH = 26.0f;
            float capY = dockY + (dockH - capH) / 2.0f;
            float totalCapW = 46.0f;

            m_btnZoomResetRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
            D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(m_btnZoomResetRect, 6.0f, 6.0f);
            if (m_pressedButtonId == BTN_ZOOM_RESET) {
                rt->FillRoundedRectangle(capR, m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                rt->FillRoundedRectangle(capR, m_d2d.BrushHover);
            } else {
                rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
            }
            rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

            WCHAR zoomBuf[16];
            swprintf_s(zoomBuf, L"%d%%", (int)(m_text.GetFontSize() / 13.0f * 100.0f + 0.5f));
            IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
            rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

            curX += totalCapW;
            drawDivider(curX + 5.5f);
            curX += 12.0f;

            m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
        }
    }
    // =========================================================================
    // DOCK RENDERING: OFFICE / PRESENTATION SLIDE MODE
    // =========================================================================
    else if (m_docType == DocumentType::Office || m_docType == DocumentType::Svg) {
        bool isFull = (availW >= 440.0f);
        bool isCompact = (availW < 320.0f);

        if (!isCompact) {
            float capH = 28.0f;
            float capY = dockY + (dockH - capH) / 2.0f;
            float zoomBtnW = 27.0f;
            float zoomTextW = 50.0f;
            float totalCapW = zoomBtnW + 1.0f + zoomTextW + 1.0f + zoomBtnW;

            D2D1_RECT_F capRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
            D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(capRect, 6.0f, 6.0f);
            rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
            rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

            m_btnZoomOutRect = D2D1::RectF(curX, capY, curX + zoomBtnW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_OUT) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_OUT) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomOutRect, 5.0f, 5.0f), m_d2d.BrushHover);
            }
            float minusCx = m_btnZoomOutRect.left + zoomBtnW / 2.0f;
            float minusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_OUT ? 0.5f : 0.0f);
            m_d2d.DrawIconZoomOut(minusCx, minusCy, 13.0f, m_d2d.BrushText);

            float div1X = curX + zoomBtnW;
            rt->DrawLine(D2D1::Point2F(div1X, capY + 4.0f), D2D1::Point2F(div1X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

            m_btnZoomResetRect = D2D1::RectF(div1X + 1.0f, capY, div1X + 1.0f + zoomTextW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_RESET) {
                rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                rt->FillRectangle(m_btnZoomResetRect, m_d2d.BrushHover);
            }
            WCHAR zoomBuf[16];
            swprintf_s(zoomBuf, L"%d%%", (int)(m_zoom * 100.0f + 0.5f));
            IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
            rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

            float div2X = div1X + 1.0f + zoomTextW;
            rt->DrawLine(D2D1::Point2F(div2X, capY + 4.0f), D2D1::Point2F(div2X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

            m_btnZoomInRect = D2D1::RectF(div2X + 1.0f, capY, curX + totalCapW, capY + capH);
            if (m_pressedButtonId == BTN_ZOOM_IN) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_IN) {
                rt->FillRoundedRectangle(D2D1::RoundedRect(m_btnZoomInRect, 5.0f, 5.0f), m_d2d.BrushHover);
            }
            float plusCx = m_btnZoomInRect.left + zoomBtnW / 2.0f;
            float plusCy = capY + capH / 2.0f + (m_pressedButtonId == BTN_ZOOM_IN ? 0.5f : 0.0f);
            m_d2d.DrawIconZoomIn(plusCx, plusCy, 13.0f, m_d2d.BrushText);

            curX += totalCapW;
            drawDivider(curX + 8.0f);
            curX += 17.0f;

            m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
            curX += 30.0f;

            if (isFull) {
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnRotateLeftRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnRotateLeftRect, ICON_ROTATE_LEFT, BTN_ROTATE_L, true);
                curX += 32.0f;

                m_btnRotateRightRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnRotateRightRect, ICON_ROTATE_RIGHT, BTN_ROTATE_R, true);
                curX += 30.0f;

                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
                curX += 34.0f;

                m_btnOpenWithRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnOpenWithRect, ICON_OPEN_WITH, BTN_OPEN_WITH, true);
                curX += 34.0f;

                m_btnSettingsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnSettingsRect, ICON_SETTINGS, BTN_SETTINGS, true);
                curX += 34.0f;

                m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
            } else {
                drawDivider(curX + 8.0f);
                curX += 17.0f;

                m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
                DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
            }
        } else {
            // Compact tier
            float capH = 26.0f;
            float capY = dockY + (dockH - capH) / 2.0f;
            float totalCapW = 46.0f;

            m_btnZoomResetRect = D2D1::RectF(curX, capY, curX + totalCapW, capY + capH);
            D2D1_ROUNDED_RECT capR = D2D1::RoundedRect(m_btnZoomResetRect, 6.0f, 6.0f);
            if (m_pressedButtonId == BTN_ZOOM_RESET) {
                rt->FillRoundedRectangle(capR, m_d2d.BrushActive);
            } else if (m_hoveredButtonId == BTN_ZOOM_RESET) {
                rt->FillRoundedRectangle(capR, m_d2d.BrushHover);
            } else {
                rt->FillRoundedRectangle(capR, m_d2d.BrushCard);
            }
            rt->DrawRoundedRectangle(capR, m_d2d.BrushBorderSubtle, 1.0f);

            WCHAR zoomBuf[16];
            swprintf_s(zoomBuf, L"%d%%", (int)(m_zoom * 100.0f + 0.5f));
            IDWriteTextFormat* fontZoom = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
            rt->DrawText(zoomBuf, (UINT32)wcslen(zoomBuf), fontZoom, m_btnZoomResetRect, m_d2d.BrushText);

            curX += totalCapW;
            drawDivider(curX + 5.5f);
            curX += 12.0f;

            m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
            curX += 28.0f;

            drawDivider(curX + 5.5f);
            curX += 12.0f;
            m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
        }
    }
        // =========================================================================
    // DOCK RENDERING: MARKDOWN & NOTEBOOK MODE
    // =========================================================================
    else if (m_docType == DocumentType::Markdown || m_docType == DocumentType::Notebook) {
        bool isFull = (availW >= 320.0f);

        m_btnCopyTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnCopyTextRect, ICON_COPY, BTN_COPY_TEXT, true);
        curX += 30.0f;

        if (isFull) {
            drawDivider(curX + 8.0f);
            curX += 17.0f;

            m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
            curX += 34.0f;

            m_btnOpenWithRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnOpenWithRect, ICON_OPEN_WITH, BTN_OPEN_WITH, true);
            curX += 34.0f;

            m_btnSettingsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnSettingsRect, ICON_SETTINGS, BTN_SETTINGS, true);
            curX += 34.0f;

            m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
        } else {
            drawDivider(curX + 8.0f);
            curX += 17.0f;

            m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
        }
    }
        // =========================================================================
    // DOCK RENDERING: FONT MODE
    // =========================================================================
    else if (m_docType == DocumentType::Font) {
        bool isFull = (availW >= 280.0f);

        if (isFull) {
            m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
            curX += 34.0f;

            m_btnOpenWithRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnOpenWithRect, ICON_OPEN_WITH, BTN_OPEN_WITH, true);
            curX += 34.0f;

            m_btnSettingsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnSettingsRect, ICON_SETTINGS, BTN_SETTINGS, true);
            curX += 34.0f;

            m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
        } else {
            m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
            curX += 30.0f;

            drawDivider(curX + 5.5f);
            curX += 12.0f;

            m_btnOverflowRect = D2D1::RectF(curX, btnY, curX + 28.0f, btnY + btnH);
            DrawIconButton(m_btnOverflowRect, ICON_MORE, BTN_OVERFLOW_MENU, true);
        }
    }
}

std::vector<AppWindow::OverflowMenuItem> AppWindow::GetCurrentOverflowItems() const {
    std::vector<OverflowMenuItem> items;
    std::wstring appName = m_openWithAppName.empty() ? L"Default App" : m_openWithAppName;
    if (appName.length() > 18) {
        appName = appName.substr(0, 16) + L"...";
    }
    std::wstring openWithLabel = L"Open with " + appName;

    if (m_docType == DocumentType::Image) {
        items.push_back({ BTN_ROTATE_L, ICON_ROTATE_LEFT, L"Rotate Left", L"L", false, false, true });
        items.push_back({ BTN_ROTATE_R, ICON_ROTATE_RIGHT, L"Rotate Right", L"R", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_CROP, ICON_CROP, L"Crop Image", L"C", false, false, true });
        items.push_back({ BTN_INFO, ICON_INFO, L"File Info", L"I", false, false, true });
        items.push_back({ BTN_OPEN_WITH, ICON_OPEN_WITH, openWithLabel, L"Enter", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_SETTINGS, ICON_SETTINGS, L"Keybind Settings", L"Ctrl+,", false, false, true });
        items.push_back({ BTN_TOOLBAR_FS, ICON_FULLSCREEN, m_isFullscreen ? L"Exit Fullscreen" : L"Fullscreen", L"F", false, false, true });
    } else if (m_docType == DocumentType::Pdf) {
        if (m_totalPages > 1) {
            items.push_back({ BTN_SIDEBAR, ICON_SIDEBAR, m_showSidebar ? L"Hide Sidebar" : L"Show Sidebar", L"S", false, false, true });
        }
        items.push_back({ BTN_SEARCH, ICON_SEARCH, L"Search Document", L"Ctrl+F", false, false, true });
        items.push_back({ BTN_ROTATE_L, ICON_ROTATE_LEFT, L"Rotate Left", L"L", false, false, true });
        items.push_back({ BTN_ROTATE_R, ICON_ROTATE_RIGHT, L"Rotate Right", L"R", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_INFO, ICON_INFO, L"File Info", L"I", false, false, true });
        items.push_back({ BTN_OPEN_WITH, ICON_OPEN_WITH, openWithLabel, L"Enter", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_SETTINGS, ICON_SETTINGS, L"Keybind Settings", L"Ctrl+,", false, false, true });
        items.push_back({ BTN_TOOLBAR_FS, ICON_FULLSCREEN, m_isFullscreen ? L"Exit Fullscreen" : L"Fullscreen", L"F", false, false, true });
    } else if (m_docType == DocumentType::Text) {
        items.push_back({ BTN_CUT, ICON_CUT, L"Cut Selection", L"Ctrl+X", false, false, m_text.HasSelection() });
        items.push_back({ BTN_COPY_TEXT, ICON_COPY, L"Copy", L"Ctrl+C", false, false, true });
        items.push_back({ BTN_PASTE, ICON_PASTE, L"Paste", L"Ctrl+V", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_WRAP_TEXT, ICON_WRAP, m_text.IsWordWrap() ? L"Disable Word Wrap" : L"Enable Word Wrap", L"W", false, m_text.IsWordWrap(), true });
        items.push_back({ BTN_INFO, ICON_INFO, L"File Info", L"I", false, false, true });
        items.push_back({ BTN_OPEN_WITH, ICON_OPEN_WITH, openWithLabel, L"Enter", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_SETTINGS, ICON_SETTINGS, L"Keybind Settings", L"Ctrl+,", false, false, true });
        items.push_back({ BTN_TOOLBAR_FS, ICON_FULLSCREEN, m_isFullscreen ? L"Exit Fullscreen" : L"Fullscreen", L"F", false, false, true });
    } else if (m_docType == DocumentType::Spreadsheet) {
        items.push_back({ BTN_TABLE_VIEW, ICON_TABLE, m_text.IsTableView() ? L"Raw Text Editor" : L"Table Grid View", L"T", false, m_text.IsTableView(), true });
        items.push_back({ BTN_COPY_TEXT, ICON_COPY, L"Copy Table / Text", L"Ctrl+C", false, false, true });
        items.push_back({ BTN_PASTE, ICON_PASTE, L"Paste", L"Ctrl+V", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_INFO, ICON_INFO, L"File Info", L"I", false, false, true });
        items.push_back({ BTN_OPEN_WITH, ICON_OPEN_WITH, openWithLabel, L"Enter", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_SETTINGS, ICON_SETTINGS, L"Keybind Settings", L"Ctrl+,", false, false, true });
        items.push_back({ BTN_TOOLBAR_FS, ICON_FULLSCREEN, m_isFullscreen ? L"Exit Fullscreen" : L"Fullscreen", L"F", false, false, true });
    } else if (m_docType == DocumentType::Office || m_docType == DocumentType::Svg) {
        items.push_back({ BTN_ROTATE_L, ICON_ROTATE_LEFT, L"Rotate Left", L"L", false, false, true });
        items.push_back({ BTN_ROTATE_R, ICON_ROTATE_RIGHT, L"Rotate Right", L"R", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_INFO, ICON_INFO, L"File Info", L"I", false, false, true });
        items.push_back({ BTN_OPEN_WITH, ICON_OPEN_WITH, openWithLabel, L"Enter", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_SETTINGS, ICON_SETTINGS, L"Keybind Settings", L"Ctrl+,", false, false, true });
        items.push_back({ BTN_TOOLBAR_FS, ICON_FULLSCREEN, m_isFullscreen ? L"Exit Fullscreen" : L"Fullscreen", L"F", false, false, true });
    } else if (m_docType == DocumentType::Markdown || m_docType == DocumentType::Notebook) {
        items.push_back({ BTN_COPY_TEXT, ICON_COPY, L"Copy All Text", L"Ctrl+C", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_INFO, ICON_INFO, L"File Info", L"I", false, false, true });
        items.push_back({ BTN_OPEN_WITH, ICON_OPEN_WITH, openWithLabel, L"Enter", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_SETTINGS, ICON_SETTINGS, L"Keybind Settings", L"Ctrl+,", false, false, true });
        items.push_back({ BTN_TOOLBAR_FS, ICON_FULLSCREEN, m_isFullscreen ? L"Exit Fullscreen" : L"Fullscreen", L"F", false, false, true });
    } else if (m_docType == DocumentType::Font) {
        items.push_back({ BTN_INFO, ICON_INFO, L"Font Info", L"I", false, false, true });
        items.push_back({ BTN_OPEN_WITH, ICON_OPEN_WITH, openWithLabel, L"Enter", false, false, true });
        items.push_back({ 0, ICON_MORE, L"", L"", true, false, false }); // divider
        items.push_back({ BTN_SETTINGS, ICON_SETTINGS, L"Keybind Settings", L"Ctrl+,", false, false, true });
        items.push_back({ BTN_TOOLBAR_FS, ICON_FULLSCREEN, m_isFullscreen ? L"Exit Fullscreen" : L"Fullscreen", L"F", false, false, true });
    }

    return items;
}

void AppWindow::RenderOverflowMenu(const D2D1_RECT_F& canvasRect) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt || !m_showOverflowMenu) {
        m_overflowMenuRect = {};
        return;
    }

    std::vector<OverflowMenuItem> items = GetCurrentOverflowItems();
    if (items.empty()) return;

    float itemH = 28.0f;
    float dividerH = 5.0f;
    float padY = 5.0f;
    float menuW = 230.0f;

    float totalMenuH = padY * 2.0f;
    for (const auto& it : items) {
        if (it.isDivider) totalMenuH += dividerH;
        else totalMenuH += itemH;
    }
    m_overflowTotalH = totalMenuH;

    // Anchor: right-aligned with m_btnOverflowRect, STRICTLY downwards!
    float menuX = m_btnOverflowRect.right - menuW;
    float menuY = m_btnOverflowRect.bottom + 6.0f;

    // Horizontal bounds clamping
    if (menuX + menuW > canvasRect.right - 8.0f) menuX = canvasRect.right - 8.0f - menuW;
    if (menuX < 8.0f) menuX = 8.0f;

    // Constrain height strictly to canvas bounds downwards
    float availH = canvasRect.bottom - menuY - 8.0f;
    if (availH < 80.0f) availH = 80.0f;

    float renderH = (totalMenuH < availH) ? totalMenuH : availH;
    bool needsScroll = (totalMenuH > renderH);

    if (!needsScroll) {
        m_overflowScrollY = 0.0f;
        m_targetOverflowScrollY = 0.0f;
    } else {
        float maxScroll = totalMenuH - renderH;
        if (m_overflowScrollY < 0.0f) m_overflowScrollY = 0.0f;
        if (m_overflowScrollY > maxScroll) m_overflowScrollY = maxScroll;
    }

    m_overflowMenuRect = D2D1::RectF(menuX, menuY, menuX + menuW, menuY + renderH);

    // 1. Ambient drop shadow (multi-pass falloff)
    D2D1_ROUNDED_RECT rShadow = D2D1::RoundedRect(
        D2D1::RectF(menuX, menuY + 2.5f, menuX + menuW, menuY + renderH + 6.0f),
        8.0f, 8.0f
    );
    m_d2d.BrushShadowDark->SetOpacity(0.40f);
    rt->FillRoundedRectangle(rShadow, m_d2d.BrushShadowDark);

    // 2. Acrylic dark glass background (#18181BF8)
    D2D1_ROUNDED_RECT rMenu = D2D1::RoundedRect(m_overflowMenuRect, 8.0f, 8.0f);
    m_d2d.BrushToolbar->SetOpacity(0.97f);
    rt->FillRoundedRectangle(rMenu, m_d2d.BrushToolbar);
    m_d2d.BrushToolbar->SetOpacity(1.0f);

    // 3. Subtle 1px outer border
    rt->DrawRoundedRectangle(rMenu, m_d2d.BrushBorderSubtle, 1.0f);

    // 4. Render Items with Axis Aligned Clip for smooth scrolling
    rt->PushAxisAlignedClip(m_overflowMenuRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float curY = menuY + padY - m_overflowScrollY;

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (it.isDivider) {
            float lineY = curY + dividerH / 2.0f;
            if (lineY >= menuY && lineY <= menuY + renderH) {
                rt->DrawLine(
                    D2D1::Point2F(menuX + 10.0f, lineY),
                    D2D1::Point2F(menuX + menuW - 10.0f, lineY),
                    m_d2d.BrushBorder, 1.0f
                );
            }
            curY += dividerH;
        } else {
            D2D1_RECT_F itemRect = D2D1::RectF(menuX + 4.0f, curY, menuX + menuW - 4.0f, curY + itemH);

            if (itemRect.bottom >= menuY && itemRect.top <= menuY + renderH) {
                bool isHovered = (m_overflowHoveredIndex == (int)i);
                if (isHovered && it.isEnabled) {
                    D2D1_ROUNDED_RECT rHover = D2D1::RoundedRect(itemRect, 4.0f, 4.0f);
                    rt->FillRoundedRectangle(rHover, m_d2d.BrushHover);
                }

                ID2D1Brush* textBrush = it.isEnabled ? (ID2D1Brush*)m_d2d.BrushText : (ID2D1Brush*)m_d2d.BrushTextMuted;

                // Draw Icon at left (center = itemRect.left + 14.0f, curY + itemH / 2.0f)
                float iconCx = itemRect.left + 14.0f;
                float iconCy = curY + itemH / 2.0f;
                DrawMenuIcon(iconCx, iconCy, 14.0f, it.icon, textBrush);

                // Draw Label
                D2D1_RECT_F labelRect = D2D1::RectF(itemRect.left + 32.0f, curY, itemRect.right - 54.0f, curY + itemH);
                rt->DrawText(it.label.c_str(), (UINT32)it.label.length(), m_d2d.FontMenu ? m_d2d.FontMenu : m_d2d.FontCaption, labelRect, textBrush);

                // Draw Shortcut Hint at right
                if (!it.shortcut.empty()) {
                    D2D1_RECT_F scRect = D2D1::RectF(itemRect.right - 52.0f, curY, itemRect.right - 8.0f, curY + itemH);
                    IDWriteTextFormat* scFont = m_d2d.FontCaptionRight ? m_d2d.FontCaptionRight : m_d2d.FontCaption;
                    rt->DrawText(it.shortcut.c_str(), (UINT32)it.shortcut.length(), scFont, scRect, m_d2d.BrushTextMuted);
                }
            }

            curY += itemH;
        }
    }

    rt->PopAxisAlignedClip();

    // 5. Scroll indicator when scrollable
    if (needsScroll) {
        float trackH = renderH - 8.0f;
        float thumbH = (std::max)(18.0f, (renderH / totalMenuH) * trackH);
        float maxScroll = totalMenuH - renderH;
        float scrollPct = (maxScroll > 0.0f) ? (m_overflowScrollY / maxScroll) : 0.0f;
        float thumbY = menuY + 4.0f + scrollPct * (trackH - thumbH);
        D2D1_RECT_F thumbRect = D2D1::RectF(menuX + menuW - 4.5f, thumbY, menuX + menuW - 2.0f, thumbY + thumbH);
        D2D1_ROUNDED_RECT thumbR = D2D1::RoundedRect(thumbRect, 1.5f, 1.5f);
        m_d2d.BrushBorderSubtle->SetOpacity(0.85f);
        rt->FillRoundedRectangle(thumbR, m_d2d.BrushBorderSubtle);
        m_d2d.BrushBorderSubtle->SetOpacity(1.0f);
    }
}

void AppWindow::DrawMenuIcon(float cx, float cy, float size, IconType icon, ID2D1Brush* brush) {
    switch (icon) {
        case ICON_SIDEBAR:        m_d2d.DrawIconSidebar(cx, cy, size, m_showSidebar, brush); break;
        case ICON_CHEVRON_LEFT:   m_d2d.DrawIconChevronLeft(cx, cy, size, brush); break;
        case ICON_CHEVRON_RIGHT:  m_d2d.DrawIconChevronRight(cx, cy, size, brush); break;
        case ICON_ZOOM_OUT:       m_d2d.DrawIconZoomOut(cx, cy, size, brush); break;
        case ICON_ZOOM_IN:        m_d2d.DrawIconZoomIn(cx, cy, size, brush); break;
        case ICON_ZOOM_FIT:       m_d2d.DrawIconFit(cx, cy, size, brush); break;
        case ICON_ROTATE_LEFT:    m_d2d.DrawIconRotateLeft(cx, cy, size, brush); break;
        case ICON_ROTATE_RIGHT:   m_d2d.DrawIconRotateRight(cx, cy, size, brush); break;
        case ICON_FULLSCREEN:     m_d2d.DrawIconFullscreen(cx, cy, size, m_isFullscreen, brush); break;
        case ICON_SEARCH:         m_d2d.DrawIconSearch(cx, cy, size, brush); break;
        case ICON_CROP:           m_d2d.DrawIconCrop(cx, cy, size, brush); break;
        case ICON_INFO:           m_d2d.DrawIconInfo(cx, cy, size, brush); break;
        case ICON_SETTINGS:       m_d2d.DrawIconSettings(cx, cy, size, brush); break;
        case ICON_COPY:           m_d2d.DrawIconCopy(cx, cy, size, brush); break;
        case ICON_WRAP:           m_d2d.DrawIconWrap(cx, cy, size, m_text.IsWordWrap(), brush); break;
        case ICON_TABLE:          m_d2d.DrawIconTable(cx, cy, size, m_text.IsTableView(), brush); break;
        case ICON_SAVE:           m_d2d.DrawIconSave(cx, cy, size, m_text.IsModified(), brush); break;
        case ICON_UNDO:           m_d2d.DrawIconUndo(cx, cy, size, brush); break;
        case ICON_REDO:           m_d2d.DrawIconRedo(cx, cy, size, brush); break;
        case ICON_CUT:            m_d2d.DrawIconCut(cx, cy, size, brush); break;
        case ICON_PASTE:          m_d2d.DrawIconPaste(cx, cy, size, brush); break;
        case ICON_OPEN_WITH:      m_d2d.DrawIconExternalLink(cx, cy, size, brush); break;
        case ICON_MORE:           m_d2d.DrawIconHamburger(cx, cy, size, brush); break;
        default: break;
    }
}

int AppWindow::GetOverflowItemIndexAt(const D2D1_POINT_2F& pt) const {
    if (!m_showOverflowMenu) return -1;
    if (pt.x < m_overflowMenuRect.left || pt.x > m_overflowMenuRect.right ||
        pt.y < m_overflowMenuRect.top || pt.y > m_overflowMenuRect.bottom) {
        return -1;
    }

    std::vector<OverflowMenuItem> items = GetCurrentOverflowItems();
    float itemH = 28.0f;
    float dividerH = 5.0f;
    float padY = 5.0f;
    float curY = m_overflowMenuRect.top + padY - m_overflowScrollY;

    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].isDivider) {
            curY += dividerH;
        } else {
            if (pt.y >= curY && pt.y < curY + itemH) {
                return (int)i;
            }
            curY += itemH;
        }
    }
    return -1;
}

void AppWindow::HandleOverflowMenuClick(const D2D1_POINT_2F& pt) {
    int idx = GetOverflowItemIndexAt(pt);
    if (idx >= 0) {
        std::vector<OverflowMenuItem> items = GetCurrentOverflowItems();
        if (idx < (int)items.size() && items[idx].isEnabled && !items[idx].isDivider) {
            ExecuteOverflowCommand(items[idx].id);
        }
    }
}

void AppWindow::ExecuteOverflowCommand(int buttonId) {
    m_showOverflowMenu = false;
    InvalidateRect(m_hwnd, NULL, FALSE);

    switch (buttonId) {
        case BTN_SIDEBAR:
            m_showSidebar = !m_showSidebar;
            UpdateLayout();
            InvalidateRect(m_hwnd, NULL, FALSE);
            break;
        case BTN_ROTATE_L:
            Rotate(-90);
            break;
        case BTN_ROTATE_R:
            Rotate(90);
            break;
        case BTN_CROP:
            ToggleCrop();
            break;
        case BTN_SEARCH:
            ToggleSearch();
            break;
        case BTN_INFO:
            ToggleInspector();
            break;
        case BTN_OPEN_WITH:
            OpenCurrentFileInDefaultApp();
            break;
        case BTN_SETTINGS:
            ToggleSettings();
            break;
        case BTN_TOOLBAR_FS:
            ToggleFullscreen();
            break;
        case BTN_CUT:
            if (m_text.Cut(m_hwnd)) {
                UpdateWindowTitle();
                ShowToast(L"Cut to clipboard");
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
            break;
        case BTN_COPY_TEXT:
            if (m_docType == DocumentType::Spreadsheet && m_text.IsTableView()) {
                CopyTextToClipboard(m_hwnd, m_text.GetAllText());
                ShowToast(L"Table copied to clipboard!");
            } else {
                if (m_text.Copy(m_hwnd)) {
                    ShowToast(m_text.HasSelection() ? L"Copied selection" : L"Copied text");
                }
            }
            InvalidateRect(m_hwnd, NULL, FALSE);
            break;
        case BTN_PASTE:
            if (m_text.Paste(m_hwnd)) {
                UpdateWindowTitle();
                ShowToast(L"Pasted");
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
            break;
        case BTN_WRAP_TEXT:
            m_text.ToggleWordWrap();
            ShowToast(m_text.IsWordWrap() ? L"Word Wrap: ON" : L"Word Wrap: OFF");
            InvalidateRect(m_hwnd, NULL, FALSE);
            break;
        case BTN_TABLE_VIEW:
            m_text.ToggleTableView();
            ShowToast(m_text.IsTableView() ? L"Grid View" : L"Raw Text View");
            InvalidateRect(m_hwnd, NULL, FALSE);
            break;
        default:
            break;
    }
}


void AppWindow::RenderSidebar(const D2D1_RECT_F& rect) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();

    // Background & right divider
    rt->FillRectangle(rect, m_d2d.BrushSidebar);
    rt->DrawLine(
        D2D1::Point2F(rect.right, rect.top),
        D2D1::Point2F(rect.right, rect.bottom),
        m_d2d.BrushBorder, 1.0f
    );

    // Segmented tab capsule at top of sidebar
    float tabY = rect.top + 10.0f;
    float tabH = 28.0f;
    float halfW = (rect.right - rect.left - 24.0f) / 2.0f;

    m_tabThumbsRect = D2D1::RectF(rect.left + 12.0f, tabY, rect.left + 12.0f + halfW, tabY + tabH);
    m_tabOutlineRect = D2D1::RectF(rect.left + 12.0f + halfW, tabY, rect.right - 12.0f, tabY + tabH);

    D2D1_ROUNDED_RECT capOuter = D2D1::RoundedRect(D2D1::RectF(rect.left + 12.0f, tabY, rect.right - 12.0f, tabY + tabH), 6.0f, 6.0f);
    rt->FillRoundedRectangle(capOuter, m_d2d.BrushCard);
    rt->DrawRoundedRectangle(capOuter, m_d2d.BrushBorderSubtle, 1.0f);

    if (m_sidebarTab == 0) {
        D2D1_ROUNDED_RECT selTab = D2D1::RoundedRect(m_tabThumbsRect, 5.0f, 5.0f);
        rt->FillRoundedRectangle(selTab, m_d2d.BrushCardSelected);
        rt->DrawRoundedRectangle(selTab, m_d2d.BrushAccent, 1.0f);
    } else {
        D2D1_ROUNDED_RECT selTab = D2D1::RoundedRect(m_tabOutlineRect, 5.0f, 5.0f);
        rt->FillRoundedRectangle(selTab, m_d2d.BrushCardSelected);
        rt->DrawRoundedRectangle(selTab, m_d2d.BrushAccent, 1.0f);
    }

    D2D1_RECT_F t1Text = D2D1::RectF(m_tabThumbsRect.left, tabY + 5.0f, m_tabThumbsRect.right, tabY + tabH);
    rt->DrawText(L"Pages", 5, m_d2d.FontBadge, t1Text, m_sidebarTab == 0 ? (ID2D1Brush*)m_d2d.BrushAccent : (ID2D1Brush*)m_d2d.BrushTextMuted);

    D2D1_RECT_F t2Text = D2D1::RectF(m_tabOutlineRect.left, tabY + 5.0f, m_tabOutlineRect.right, tabY + tabH);
    rt->DrawText(L"Outline", 7, m_d2d.FontBadge, t2Text, m_sidebarTab == 1 ? (ID2D1Brush*)m_d2d.BrushAccent : (ID2D1Brush*)m_d2d.BrushTextMuted);

    // Separator line under tabs
    rt->DrawLine(D2D1::Point2F(rect.left, tabY + tabH + 8.0f), D2D1::Point2F(rect.right, tabY + tabH + 8.0f), m_d2d.BrushBorder, 1.0f);

    if (m_sidebarTab == 0) {
        // Thumbnails container
        float startY = rect.top + 54.0f + m_sidebarScrollY;
        float cardW = rect.right - rect.left - 24.0f;

        for (size_t i = 0; i < m_thumbnails.size(); i++) {
            auto& item = m_thumbnails[i];
            float cardH = 136.0f;
            D2D1_RECT_F cardRect = D2D1::RectF(rect.left + 12.0f, startY, rect.left + 12.0f + cardW, startY + cardH);
            item.rect = cardRect;

            // Clip/skip invisible cards
            if (cardRect.bottom >= rect.top && cardRect.top <= rect.bottom) {
                D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(cardRect, 8.0f, 8.0f);

                bool isSelected = (item.pageIndex == m_currentPage);
                bool isHovered = (m_hoveredThumbnailIndex == (int)i);

                if (isSelected) {
                    rt->FillRoundedRectangle(rrect, m_d2d.BrushCardSelected);
                    rt->DrawRoundedRectangle(rrect, m_d2d.BrushAccent, 1.8f);

                    // Left active indicator pill
                    D2D1_ROUNDED_RECT indPill = D2D1::RoundedRect(
                        D2D1::RectF(cardRect.left - 8.0f, cardRect.top + (cardH - 32.0f) / 2.0f,
                                    cardRect.left - 4.0f, cardRect.top + (cardH + 32.0f) / 2.0f),
                        2.0f, 2.0f
                    );
                    rt->FillRoundedRectangle(indPill, m_d2d.BrushAccent);
                } else if (isHovered) {
                    rt->FillRoundedRectangle(rrect, m_d2d.BrushHover);
                    rt->DrawRoundedRectangle(rrect, m_d2d.BrushBorder, 1.0f);
                } else {
                    rt->FillRoundedRectangle(rrect, m_d2d.BrushCard);
                    rt->DrawRoundedRectangle(rrect, m_d2d.BrushBorderSubtle, 1.0f);
                }

                // Lazy load thumbnail bitmap on demand if visible in viewport!
                if (!item.bitmap) {
                    m_pdf.RenderThumbnail(item.pageIndex, 160.0f, m_d2d.GetRenderTarget(), &item.bitmap);
                }

                // Draw thumbnail bitmap
                if (item.bitmap) {
                    D2D1_SIZE_F bmpSize = item.bitmap->GetSize();
                    float bmpAspect = bmpSize.width / bmpSize.height;
                    float thumbMaxW = cardW - 16.0f;
                    float thumbMaxH = cardH - 32.0f;

                    float drawW = thumbMaxW;
                    float drawH = thumbMaxW / bmpAspect;
                    if (drawH > thumbMaxH) {
                        drawH = thumbMaxH;
                        drawW = thumbMaxH * bmpAspect;
                    }

                    float thumbX = cardRect.left + (cardW - drawW) / 2.0f;
                    float thumbY = cardRect.top + (thumbMaxH - drawH) / 2.0f + 6.0f;
                    D2D1_RECT_F thumbRect = D2D1::RectF(thumbX, thumbY, thumbX + drawW, thumbY + drawH);

                    // Subtle shadow behind thumbnail
                    D2D1_RECT_F thumbShadow = D2D1::RectF(thumbRect.left - 1.0f, thumbRect.top, thumbRect.right + 1.0f, thumbRect.bottom + 2.0f);
                    rt->DrawRectangle(thumbShadow, m_d2d.BrushShadowSoft, 1.0f);

                    rt->DrawBitmap(item.bitmap, thumbRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                }

                // Page label pill badge
                float badgeW = 32.0f;
                float badgeH = 16.0f;
                float badgeX = cardRect.left + (cardW - badgeW) / 2.0f;
                float badgeY = cardRect.bottom - badgeH - 6.0f;
                D2D1_ROUNDED_RECT badgePill = D2D1::RoundedRect(D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH), 4.0f, 4.0f);

                if (isSelected) {
                    rt->FillRoundedRectangle(badgePill, m_d2d.BrushAccent);
                    D2D1_RECT_F textR = D2D1::RectF(badgeX, badgeY + 1.0f, badgeX + badgeW, badgeY + badgeH);
                    rt->DrawText(item.label.c_str(), (UINT32)item.label.length(), m_d2d.FontBadge, textR, m_d2d.BrushText);
                } else {
                    rt->FillRoundedRectangle(badgePill, m_d2d.BrushBorderSubtle);
                    D2D1_RECT_F textR = D2D1::RectF(badgeX, badgeY + 1.0f, badgeX + badgeW, badgeY + badgeH);
                    rt->DrawText(item.label.c_str(), (UINT32)item.label.length(), m_d2d.FontBadge, textR, m_d2d.BrushTextMuted);
                }
            } else {
                // Memory optimization: if far offscreen, free bitmap to keep RAM usage minimal
                if (item.bitmap && (cardRect.bottom < rect.top - 600.0f || cardRect.top > rect.bottom + 600.0f)) {
                    SafeRelease(item.bitmap);
                }
            }

            startY += cardH + 12.0f;
        }
    } else {
        // Outline / TOC list
        float startY = rect.top + 54.0f + m_outlineScrollY;
        if (m_outline.empty()) {
            D2D1_RECT_F emptyR = D2D1::RectF(rect.left + 16.0f, startY + 20.0f, rect.right - 16.0f, startY + 60.0f);
            const WCHAR* noOutline = L"No outline found in document";
            rt->DrawText(noOutline, (UINT32)wcslen(noOutline), m_d2d.FontCaption, emptyR, m_d2d.BrushTextMuted);
        } else {
            for (size_t i = 0; i < m_outline.size(); i++) {
                const auto& item = m_outline[i];
                float itemH = 28.0f;
                float indent = (std::min)((float)item.level * 12.0f, 48.0f);
                D2D1_RECT_F itemR = D2D1::RectF(rect.left + 10.0f + indent, startY, rect.right - 10.0f, startY + itemH);

                if (itemR.bottom >= rect.top && itemR.top <= rect.bottom) {
                    bool isCur = (item.pageIndex == m_currentPage);
                    D2D1_ROUNDED_RECT rItem = D2D1::RoundedRect(itemR, 4.0f, 4.0f);
                    if (isCur) {
                        rt->FillRoundedRectangle(rItem, m_d2d.BrushCardSelected);
                        rt->DrawRoundedRectangle(rItem, m_d2d.BrushAccent, 1.0f);
                    }

                    // Title
                    D2D1_RECT_F titleR = D2D1::RectF(itemR.left + 6.0f, itemR.top + 5.0f, itemR.right - 34.0f, itemR.bottom);
                    rt->DrawText(item.title.c_str(), (UINT32)item.title.length(), m_d2d.FontCaption, titleR,
                                 isCur ? (ID2D1Brush*)m_d2d.BrushAccent : (ID2D1Brush*)m_d2d.BrushText);

                    // Page number
                    WCHAR pBuf[16];
                    swprintf_s(pBuf, L"%d", item.pageIndex + 1);
                    D2D1_RECT_F pR = D2D1::RectF(itemR.right - 32.0f, itemR.top + 5.0f, itemR.right - 4.0f, itemR.bottom);
                    rt->DrawText(pBuf, (UINT32)wcslen(pBuf), m_d2d.FontBadge, pR, m_d2d.BrushTextMuted);
                }
                startY += itemH + 3.0f;
            }
        }
    }
}

void AppWindow::RenderEmptyState(const D2D1_RECT_F& rect) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;
    float cx = rect.left + width / 2.0f;

    // Total center stack height:
    // 68 (squircle) + 16 (gap) + 30 (title) + 8 (gap) + 22 (sub) + 24 (gap) + 36 (pill) = 204px
    float totalStackH = 204.0f;
    float startY = rect.top + (height - totalStackH) / 2.0f - 18.0f;
    if (startY < rect.top + 20.0f) startY = rect.top + 20.0f;

    // 1. Sleek Frosted QuickLook Icon Squircle (72x72, 20px radius)
    float iconTileSize = 72.0f;
    D2D1_RECT_F iconBox = D2D1::RectF(cx - iconTileSize / 2.0f, startY, cx + iconTileSize / 2.0f, startY + iconTileSize);
    D2D1_ROUNDED_RECT rIconBox = D2D1::RoundedRect(iconBox, 20.0f, 20.0f);
    rt->FillRoundedRectangle(rIconBox, m_d2d.BrushCard);
    rt->DrawRoundedRectangle(rIconBox, m_d2d.BrushBorder, 1.2f);

    // Load and render official WinPreview App Logo
    if (!m_appLogoBitmap) {
        // Priority 1: High-res 32bpp PNG embedded as RCDATA 102
        HRSRC hRes = FindResourceW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(102), RT_RCDATA);
        if (hRes) {
            HGLOBAL hGlobal = LoadResource(GetModuleHandleW(NULL), hRes);
            if (hGlobal) {
                void* pData = LockResource(hGlobal);
                DWORD resSize = SizeofResource(GetModuleHandleW(NULL), hRes);
                if (pData && resSize > 0) {
                    WicDecoder::LoadImageFromMemory(m_d2d.GetWICFactory(), rt, pData, resSize, &m_appLogoBitmap);
                }
            }
        }
        // Priority 2: Extract from Icon resource 101
        if (!m_appLogoBitmap) {
            HICON hLogo = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(101), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
            if (hLogo) {
                IWICImagingFactory* pWic = m_d2d.GetWICFactory();
                if (pWic) {
                    IWICBitmap* pWicBmp = nullptr;
                    if (SUCCEEDED(pWic->CreateBitmapFromHICON(hLogo, &pWicBmp)) && pWicBmp) {
                        rt->CreateBitmapFromWicBitmap(pWicBmp, &m_appLogoBitmap);
                        pWicBmp->Release();
                    }
                }
                DestroyIcon(hLogo);
            }
        }
    }

    if (m_appLogoBitmap) {
        float logoSize = 52.0f;
        D2D1_RECT_F logoRect = D2D1::RectF(cx - logoSize / 2.0f, startY + (iconTileSize - logoSize) / 2.0f,
                                          cx + logoSize / 2.0f, startY + (iconTileSize + logoSize) / 2.0f);
        rt->DrawBitmap(m_appLogoBitmap, logoRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        m_d2d.DrawIconEye(cx, startY + iconTileSize / 2.0f, 26.0f, m_d2d.BrushAccent);
    }

    // 2. Hero Title (Crisp Segoe UI Variable Display, perfectly centered)
    float titleY = startY + iconTileSize + 16.0f;
    const WCHAR* title = L"WinPreview";
    D2D1_RECT_F titleRect = D2D1::RectF(rect.left + 16.0f, titleY, rect.right - 16.0f, titleY + 30.0f);
    rt->DrawText(title, (UINT32)wcslen(title), m_d2d.FontHeroCenter ? m_d2d.FontHeroCenter : m_d2d.FontHero, titleRect, m_d2d.BrushText);

    // 3. Subtitle (Centered, minimal, clear)
    float subY = titleY + 30.0f + 8.0f;
    const WCHAR* subtitle = L"Select any file in File Explorer and press Space to preview";
    D2D1_RECT_F subRect = D2D1::RectF(rect.left + 16.0f, subY, rect.right - 16.0f, subY + 22.0f);
    rt->DrawText(subtitle, (UINT32)wcslen(subtitle), m_d2d.FontBodyCenter ? m_d2d.FontBodyCenter : m_d2d.FontBody, subRect, m_d2d.BrushTextMuted);

    // 4. Interactive Keybind Indicator Pill (Click to open Settings)
    float pillY = subY + 22.0f + 24.0f;
    HotkeyConfig curCfg = ExplorerHook::GetHotkeyConfig();
    std::wstring keybindLabel = (width < 390.0f) ?
        (L"[ " + curCfg.name + L" ]   Settings (Ctrl+,)") :
        (L"Preview Keybind: [ " + curCfg.name + L" ]   •   Settings (Ctrl+,)");
    float pillW = 380.0f;
    if (pillW > width - 24.0f) pillW = width - 24.0f;
    float pillH = 36.0f;
    m_emptyStateKeybindBtn = D2D1::RectF(cx - pillW / 2.0f, pillY, cx + pillW / 2.0f, pillY + pillH);
    D2D1_ROUNDED_RECT pillR = D2D1::RoundedRect(m_emptyStateKeybindBtn, 18.0f, 18.0f);
    if (m_hoveredButtonId == BTN_SETTINGS) {
        rt->FillRoundedRectangle(pillR, m_d2d.BrushHover);
        rt->DrawRoundedRectangle(pillR, m_d2d.BrushAccent, 1.2f);
    } else {
        rt->FillRoundedRectangle(pillR, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(pillR, m_d2d.BrushBorderSubtle, 1.0f);
    }
    rt->DrawText(keybindLabel.c_str(), (UINT32)keybindLabel.length(), m_d2d.FontButton, m_emptyStateKeybindBtn, m_d2d.BrushText);

    // 5. Clean Keyboard Legend at bottom (Always centered, never clips)
    if (height >= 260.0f) {
        float legendW = 440.0f;
        if (legendW > width - 24.0f) legendW = width - 24.0f;
        float legendH = 30.0f;
        float legendY = rect.bottom - 46.0f;
        D2D1_RECT_F legendRect = D2D1::RectF(cx - legendW / 2.0f, legendY, cx + legendW / 2.0f, legendY + legendH);
        D2D1_ROUNDED_RECT rLegend = D2D1::RoundedRect(legendRect, 15.0f, 15.0f);
        rt->FillRoundedRectangle(rLegend, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(rLegend, m_d2d.BrushBorderSubtle, 1.0f);

        const WCHAR* hints = (width < 450.0f) ?
            L"Space  Preview   •   ←/→  Next   •   Esc  Close" :
            L"Space  Preview / Dismiss   •   ←/→  Next File   •   Esc  Close   •   Ctrl+,  Settings";
        rt->DrawText(hints, (UINT32)wcslen(hints), m_d2d.FontCaptionCenter ? m_d2d.FontCaptionCenter : m_d2d.FontCaption, legendRect, m_d2d.BrushTextMuted);
    }
}

void AppWindow::LoadUnsupportedFileInfo(const std::wstring& filePath) {
    SafeRelease(m_unsupportedIconBitmap);
    m_unsupportedTypeName.clear();
    m_unsupportedSizeStr.clear();
    m_unsupportedDateStr.clear();
    m_unsupportedHexDump.clear();

    // 1. Single-pass Shell Query for File Type and Large Icon (cuts shell latency in half)
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(filePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_TYPENAME | SHGFI_ICON | SHGFI_LARGEICON)) {
        if (sfi.szTypeName[0] != L'\0') {
            m_unsupportedTypeName = sfi.szTypeName;
        }
        if (sfi.hIcon) {
            IWICImagingFactory* pWic = m_d2d.GetWICFactory();
            ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
            if (pWic && rt) {
                IWICBitmap* pWicBmp = nullptr;
                if (SUCCEEDED(pWic->CreateBitmapFromHICON(sfi.hIcon, &pWicBmp)) && pWicBmp) {
                    rt->CreateBitmapFromWicBitmap(pWicBmp, &m_unsupportedIconBitmap);
                    pWicBmp->Release();
                }
            }
            DestroyIcon(sfi.hIcon);
        }
    }

    if (m_unsupportedTypeName.empty()) {
        size_t dot = filePath.find_last_of(L'.');
        if (dot != std::wstring::npos) {
            std::wstring ext = filePath.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towupper);
            m_unsupportedTypeName = ext + L" File";
        } else {
            m_unsupportedTypeName = L"Binary File";
        }
    }

    // 2. File Size & Clean Date Modified
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER uli;
        uli.LowPart = fad.nFileSizeLow;
        uli.HighPart = fad.nFileSizeHigh;
        ULONGLONG bytes = uli.QuadPart;

        WCHAR szSize[64];
        if (bytes < 1024) {
            swprintf_s(szSize, L"%llu B", bytes);
        } else if (bytes < 1024 * 1024) {
            swprintf_s(szSize, L"%0.1f KB", (double)bytes / 1024.0);
        } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
            swprintf_s(szSize, L"%0.2f MB", (double)bytes / (1024.0 * 1024.0));
        } else {
            swprintf_s(szSize, L"%0.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
        }
        m_unsupportedSizeStr = szSize;

        FILETIME ftLocal;
        FileTimeToLocalFileTime(&fad.ftLastWriteTime, &ftLocal);
        SYSTEMTIME st;
        FileTimeToSystemTime(&ftLocal, &st);
        WCHAR szDate[64];
        GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, L"MMM d, yyyy", szDate, 64);
        WCHAR szTime[64];
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, szTime, 64);
        m_unsupportedDateStr = std::wstring(szDate) + L", " + szTime;
    }

    // 3. Magic Bytes / Hex Inspector (reads first 32 bytes to provide developer insight)
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        BYTE buffer[32] = {0};
        DWORD bytesRead = 0;
        if (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
            std::wstring hexLines;
            for (DWORD offset = 0; offset < bytesRead; offset += 16) {
                DWORD chunk = (bytesRead - offset > 16) ? 16 : (bytesRead - offset);
                WCHAR line[128];
                swprintf_s(line, L"%04X:  ", offset);
                hexLines += line;

                for (DWORD i = 0; i < 16; ++i) {
                    if (i < chunk) {
                        swprintf_s(line, L"%02X ", buffer[offset + i]);
                        hexLines += line;
                    } else {
                        hexLines += L"   ";
                    }
                    if (i == 7) hexLines += L" ";
                }

                hexLines += L" |";
                for (DWORD i = 0; i < chunk; ++i) {
                    BYTE b = buffer[offset + i];
                    hexLines += (b >= 32 && b <= 126) ? (wchar_t)b : L'.';
                }
                hexLines += L"|\n";
            }
            if (!hexLines.empty() && hexLines.back() == L'\n') hexLines.pop_back();
            m_unsupportedHexDump = hexLines;
        }
        CloseHandle(hFile);
    }
}

void AppWindow::RenderUnsupportedState(const D2D1_RECT_F& rect) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;
    float cx = rect.left + width / 2.0f;

    // Total vertical stack:
    // 64 (icon squircle) + 16 (gap) + 26 (name) + 6 (gap) + 18 (meta) + 14 (gap) + 46 (hex/badge) + 18 (gap) + 34 (buttons) = 236px
    float totalStackH = 236.0f;
    float startY = rect.top + (height - totalStackH) / 2.0f - 14.0f;
    if (startY < rect.top + 24.0f) startY = rect.top + 24.0f;

    // 1. Sleek Elevated File Icon Squircle (64x64, 16px radius)
    float iconTileSize = 64.0f;
    D2D1_RECT_F iconBox = D2D1::RectF(cx - iconTileSize / 2.0f, startY, cx + iconTileSize / 2.0f, startY + iconTileSize);
    D2D1_ROUNDED_RECT rIconBox = D2D1::RoundedRect(iconBox, 16.0f, 16.0f);
    rt->FillRoundedRectangle(rIconBox, m_d2d.BrushCard);
    rt->DrawRoundedRectangle(rIconBox, m_d2d.BrushBorder, 1.2f);

    if (m_unsupportedIconBitmap) {
        D2D1_SIZE_F bmpSz = m_unsupportedIconBitmap->GetSize();
        float drawW = (std::min)(40.0f, bmpSz.width);
        float drawH = (std::min)(40.0f, bmpSz.height);
        D2D1_RECT_F dstRect = D2D1::RectF(cx - drawW / 2.0f, startY + (iconTileSize - drawH) / 2.0f,
                                          cx + drawW / 2.0f, startY + (iconTileSize + drawH) / 2.0f);
        rt->DrawBitmap(m_unsupportedIconBitmap, dstRect);
    } else {
        m_d2d.DrawIconFileDocument(cx, startY + iconTileSize / 2.0f, 28.0f, m_d2d.BrushAccent);
    }

    // 2. File Name (Large, crisp, semi-bold, centered with ellipsis)
    float nameY = startY + iconTileSize + 16.0f;
    D2D1_RECT_F nameRect = D2D1::RectF(rect.left + 24.0f, nameY, rect.right - 24.0f, nameY + 26.0f);
    rt->DrawText(m_fileName.c_str(), (UINT32)m_fileName.length(), m_d2d.FontTitleCenter ? m_d2d.FontTitleCenter : m_d2d.FontHeroCenter, nameRect, m_d2d.BrushText);

    // 3. Metadata line: [ Type Name ] • [ Size ] • [ Modified Date ]
    std::wstring metaLine;
    if (!m_unsupportedTypeName.empty()) metaLine += m_unsupportedTypeName;
    if (!m_unsupportedSizeStr.empty()) {
        if (!metaLine.empty()) metaLine += L"   •   ";
        metaLine += m_unsupportedSizeStr;
    }
    if (!m_unsupportedDateStr.empty()) {
        if (!metaLine.empty()) metaLine += L"   •   ";
        metaLine += m_unsupportedDateStr;
    }
    float metaY = nameY + 26.0f + 6.0f;
    D2D1_RECT_F metaRect = D2D1::RectF(rect.left + 24.0f, metaY, rect.right - 24.0f, metaY + 18.0f);
    rt->DrawText(metaLine.c_str(), (UINT32)metaLine.length(), m_d2d.FontCaptionCenter ? m_d2d.FontCaptionCenter : m_d2d.FontBodyCenter, metaRect, m_d2d.BrushTextMuted);

    // 4. Developer Magic Bytes Hex Inspector or Format Notice Badge
    float middleY = metaY + 18.0f + 14.0f;
    if (!m_unsupportedHexDump.empty()) {
        float hexW = (std::min)(520.0f, width - 40.0f);
        float hexH = 46.0f;
        D2D1_RECT_F hexBox = D2D1::RectF(cx - hexW / 2.0f, middleY, cx + hexW / 2.0f, middleY + hexH);
        D2D1_ROUNDED_RECT rHex = D2D1::RoundedRect(hexBox, 8.0f, 8.0f);
        rt->FillRoundedRectangle(rHex, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(rHex, m_d2d.BrushBorderSubtle, 1.0f);

        D2D1_RECT_F textR = D2D1::RectF(hexBox.left + 14.0f, hexBox.top + 7.0f, hexBox.right - 14.0f, hexBox.bottom - 4.0f);
        rt->DrawText(m_unsupportedHexDump.c_str(), (UINT32)m_unsupportedHexDump.length(),
                     m_d2d.FontMono ? m_d2d.FontMono : m_d2d.FontCaption, textR, m_d2d.BrushTextMuted);
        middleY += hexH;
    } else {
        const WCHAR* noticeText = L"No preview available for this format";
        float noticeW = 240.0f;
        float noticeH = 24.0f;
        D2D1_RECT_F noticeRect = D2D1::RectF(cx - noticeW / 2.0f, middleY, cx + noticeW / 2.0f, middleY + noticeH);
        D2D1_ROUNDED_RECT rNotice = D2D1::RoundedRect(noticeRect, 12.0f, 12.0f);
        rt->FillRoundedRectangle(rNotice, m_d2d.BrushBorderSubtle);
        D2D1_RECT_F noticeTextR = D2D1::RectF(noticeRect.left, middleY + 3.0f, noticeRect.right, middleY + noticeH);
        rt->DrawText(noticeText, (UINT32)wcslen(noticeText), m_d2d.FontBadge ? m_d2d.FontBadge : m_d2d.FontCaptionCenter, noticeTextR, m_d2d.BrushTextMuted);
        middleY += noticeH;
    }

    // 5. Action Buttons: "Open File (Enter)", "Show in Folder", "Copy Path (Ctrl+C)"
    float btnW = 140.0f;
    float btnH = 34.0f;
    float btnGap = 10.0f;
    float totalBtnW = btnW * 3.0f + btnGap * 2.0f;
    float btnY = middleY + 18.0f;

    m_unsupportedOpenBtnRect = D2D1::RectF(cx - totalBtnW / 2.0f, btnY, cx - totalBtnW / 2.0f + btnW, btnY + btnH);
    m_unsupportedRevealBtnRect = D2D1::RectF(m_unsupportedOpenBtnRect.right + btnGap, btnY, m_unsupportedOpenBtnRect.right + btnGap + btnW, btnY + btnH);
    m_unsupportedCopyBtnRect = D2D1::RectF(m_unsupportedRevealBtnRect.right + btnGap, btnY, m_unsupportedRevealBtnRect.right + btnGap + btnW, btnY + btnH);

    // Primary: Open File
    D2D1_ROUNDED_RECT rOpenBtn = D2D1::RoundedRect(m_unsupportedOpenBtnRect, 17.0f, 17.0f);
    if (m_hoveredButtonId == BTN_UNSUPPORTED_OPEN) {
        rt->FillRoundedRectangle(rOpenBtn, m_d2d.BrushAccent);
        rt->DrawRoundedRectangle(rOpenBtn, m_d2d.BrushText, 1.2f);
    } else {
        rt->FillRoundedRectangle(rOpenBtn, m_d2d.BrushAccent);
    }
    const WCHAR* openText = L"Open File (Enter)";
    rt->DrawText(openText, (UINT32)wcslen(openText), m_d2d.FontButton, m_unsupportedOpenBtnRect, m_d2d.BrushText);

    // Secondary: Show in Folder
    D2D1_ROUNDED_RECT rRevealBtn = D2D1::RoundedRect(m_unsupportedRevealBtnRect, 17.0f, 17.0f);
    if (m_hoveredButtonId == BTN_UNSUPPORTED_REVEAL) {
        rt->FillRoundedRectangle(rRevealBtn, m_d2d.BrushHover);
        rt->DrawRoundedRectangle(rRevealBtn, m_d2d.BrushAccent, 1.2f);
    } else {
        rt->FillRoundedRectangle(rRevealBtn, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(rRevealBtn, m_d2d.BrushBorder, 1.0f);
    }
    const WCHAR* revealText = L"Show in Folder";
    rt->DrawText(revealText, (UINT32)wcslen(revealText), m_d2d.FontButton, m_unsupportedRevealBtnRect, m_d2d.BrushText);

    // Tertiary: Copy Path
    D2D1_ROUNDED_RECT rCopyBtn = D2D1::RoundedRect(m_unsupportedCopyBtnRect, 17.0f, 17.0f);
    if (m_hoveredButtonId == BTN_UNSUPPORTED_COPY) {
        rt->FillRoundedRectangle(rCopyBtn, m_d2d.BrushHover);
        rt->DrawRoundedRectangle(rCopyBtn, m_d2d.BrushAccent, 1.2f);
    } else {
        rt->FillRoundedRectangle(rCopyBtn, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(rCopyBtn, m_d2d.BrushBorder, 1.0f);
    }
    const WCHAR* copyText = L"Copy Path";
    rt->DrawText(copyText, (UINT32)wcslen(copyText), m_d2d.FontButton, m_unsupportedCopyBtnRect, m_d2d.BrushText);

    // 6. Clean bottom shortcuts legend pill
    float legendW = 340.0f;
    float legendH = 26.0f;
    float legendY = rect.bottom - 42.0f;
    D2D1_RECT_F legendRect = D2D1::RectF(cx - legendW / 2.0f, legendY, cx + legendW / 2.0f, legendY + legendH);
    D2D1_ROUNDED_RECT rLegend = D2D1::RoundedRect(legendRect, 13.0f, 13.0f);
    rt->FillRoundedRectangle(rLegend, m_d2d.BrushCard);
    rt->DrawRoundedRectangle(rLegend, m_d2d.BrushBorderSubtle, 1.0f);

    const WCHAR* dismissHint = L"Space  Dismiss   •   Enter  Open   •   Ctrl+C  Copy Path";
    rt->DrawText(dismissHint, (UINT32)wcslen(dismissHint), m_d2d.FontCaptionCenter, legendRect, m_d2d.BrushTextMuted);
}

void AppWindow::ShowToast(const std::wstring& msg) {
    m_toastMessage = msg;
    m_toastOpacity = 1.0f;
    m_toastStartTime = GetTickCount64();
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::RenderToast(const D2D1_RECT_F& rect) {
    if (m_toastOpacity <= 0.01f || m_toastMessage.empty()) return;
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    float dpiScale = (m_dpiScale > 0.05f) ? m_dpiScale : 1.0f;
    ULONGLONG elapsed = GetTickCount64() - m_toastStartTime;

    // Smooth spring ease-out entrance (0..220ms)
    float enterProgress = std::clamp((float)elapsed / 220.0f, 0.0f, 1.0f);
    float easeOut = 1.0f - std::pow(1.0f - enterProgress, 3.0f);
    float slideY = (1.0f - easeOut) * 12.0f * dpiScale;
    float currentAlpha = m_toastOpacity * std::min(1.0f, enterProgress * 1.5f);

    // Dynamic width calculation based on exact message measurement
    float textW = 0.0f;
    IDWriteFactory* dwrite = m_d2d.GetDWriteFactory();
    if (dwrite && m_d2d.FontButton) {
        IDWriteTextLayout* layout = nullptr;
        if (SUCCEEDED(dwrite->CreateTextLayout(m_toastMessage.c_str(), (UINT32)m_toastMessage.length(), m_d2d.FontButton, 2000.0f, 100.0f, &layout))) {
            DWRITE_TEXT_METRICS tm = {};
            layout->GetMetrics(&tm);
            textW = tm.widthIncludingTrailingWhitespace;
            layout->Release();
        }
    }
    if (textW <= 1.0f) {
        textW = (float)m_toastMessage.length() * 8.2f * dpiScale;
    }

    float toastW = std::max(160.0f * dpiScale, textW + 44.0f * dpiScale);
    float toastH = 36.0f * dpiScale;
    float cornerR = toastH / 2.0f;

    float cx = rect.left + (rect.right - rect.left) / 2.0f;
    float toastY = rect.bottom - 48.0f * dpiScale + slideY;
    D2D1_RECT_F toastRect = D2D1::RectF(cx - toastW / 2.0f, toastY - toastH / 2.0f, cx + toastW / 2.0f, toastY + toastH / 2.0f);
    D2D1_ROUNDED_RECT rToast = D2D1::RoundedRect(toastRect, cornerR, cornerR);

    // 1. Ambient soft shadow
    D2D1_ROUNDED_RECT rShadowAmbient = D2D1::RoundedRect(
        D2D1::RectF(toastRect.left - 2.0f * dpiScale, toastRect.top + 3.0f * dpiScale, toastRect.right + 2.0f * dpiScale, toastRect.bottom + 9.0f * dpiScale),
        cornerR, cornerR
    );
    m_d2d.BrushShadowDark->SetOpacity(currentAlpha * 0.38f);
    rt->FillRoundedRectangle(rShadowAmbient, m_d2d.BrushShadowDark);

    // 2. Direct edge shadow
    D2D1_ROUNDED_RECT rShadowEdge = D2D1::RoundedRect(
        D2D1::RectF(toastRect.left, toastRect.top + 1.0f * dpiScale, toastRect.right, toastRect.bottom + 3.0f * dpiScale),
        cornerR, cornerR
    );
    m_d2d.BrushShadowDark->SetOpacity(currentAlpha * 0.22f);
    rt->FillRoundedRectangle(rShadowEdge, m_d2d.BrushShadowDark);

    // 3. Frosted Dark Pill Surface (#18181BEB)
    m_d2d.BrushToolbar->SetOpacity(currentAlpha * 0.96f);
    rt->FillRoundedRectangle(rToast, m_d2d.BrushToolbar);

    // 4. Subtle Border
    m_d2d.BrushBorder->SetOpacity(currentAlpha * 0.85f);
    rt->DrawRoundedRectangle(rToast, m_d2d.BrushBorder, 1.0f);

    // 5. Crisp Text
    m_d2d.BrushText->SetOpacity(currentAlpha);
    rt->DrawText(m_toastMessage.c_str(), (UINT32)m_toastMessage.length(), m_d2d.FontButton, toastRect, m_d2d.BrushText);

    // Restore brush opacities to default
    m_d2d.BrushShadowDark->SetOpacity(0.40f);
    m_d2d.BrushToolbar->SetOpacity(1.0f);
    m_d2d.BrushBorder->SetOpacity(1.0f);
    m_d2d.BrushText->SetOpacity(1.0f);
}

void AppWindow::CheckUpdateOnPreviewOpen() {
    // 1. If an update was already detected, re-arm and display the top-right minimal popup on top of this preview/media!
    if (m_updater.GetUpdateInfo().isAvailable) {
        m_updater.ResetToAvailable();
        ShowUpdatePopup();
        return;
    }

    // 2. Periodic background check: query GitHub raw CDN if > 10 mins since last check
    ULONGLONG now = GetTickCount64();
    if (now - m_lastUpdateCheckTick > 10 * 60 * 1000) {
        m_lastUpdateCheckTick = now;
        m_updater.CheckForUpdatesAsync([this](const UpdateInfo& info) {
            if (info.isAvailable && m_hwnd && IsWindow(m_hwnd)) {
                PostMessageW(m_hwnd, WM_USER + 555, 0, 0);
            }
        });
    }
}

void AppWindow::CheckForUpdates(bool isUserInitiated) {
    if (isUserInitiated) {
        ShowToast(L"Checking for updates...");
    }
    m_lastUpdateCheckTick = GetTickCount64();
    m_updater.CheckForUpdatesAsync([this, isUserInitiated](const UpdateInfo& info) {
        if (m_hwnd && IsWindow(m_hwnd)) {
            if (info.isAvailable) {
                if (isUserInitiated) {
                    ShowToast(L"Update available: v" + info.remoteVersion + L"! Click Update in the top banner.");
                }
                PostMessageW(m_hwnd, WM_USER + 555, 0, 0);
            } else if (isUserInitiated) {
                if (info.errorMessage.empty()) {
                    ShowToast(L"WinPreview is up to date (v" + info.currentVersion + L")");
                } else {
                    ShowToast(L"Update check failed: " + info.errorMessage);
                }
            }
        }
    });
}

void AppWindow::RenderUpdateBanner(const D2D1_RECT_F& rect) {
    UpdateState state = m_updater.GetState();
    if (state != UpdateState::UpdateAvailable && state != UpdateState::Downloading && state != UpdateState::Downloaded) {
        return;
    }

    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    float dpiScale = (m_dpiScale > 0.05f) ? m_dpiScale : 1.0f;
    const UpdateInfo& info = m_updater.GetUpdateInfo();

    // Sleek minimal side pill dimensions (matches toast styling)
    float bannerW = 276.0f * dpiScale;
    float bannerH = 38.0f * dpiScale;
    float cornerR = bannerH / 2.0f; // Pill radius

    float rightMargin = 18.0f * dpiScale;
    float topMargin = 50.0f * dpiScale;
    m_updateBannerRect = D2D1::RectF(
        rect.right - rightMargin - bannerW,
        rect.top + topMargin,
        rect.right - rightMargin,
        rect.top + topMargin + bannerH
    );

    // 1. Layered Ambient Drop Shadow
    D2D1_ROUNDED_RECT rShadow = D2D1::RoundedRect(
        D2D1::RectF(m_updateBannerRect.left - 2.0f * dpiScale, m_updateBannerRect.top + 2.0f * dpiScale,
                    m_updateBannerRect.right + 2.0f * dpiScale, m_updateBannerRect.bottom + 8.0f * dpiScale),
        cornerR, cornerR
    );
    if (m_d2d.BrushShadowDark) {
        m_d2d.BrushShadowDark->SetOpacity(0.38f);
        rt->FillRoundedRectangle(rShadow, m_d2d.BrushShadowDark);
    }

    // 2. Frosted Dark Pill Surface Fill
    D2D1_ROUNDED_RECT rBanner = D2D1::RoundedRect(m_updateBannerRect, cornerR, cornerR);
    ID2D1SolidColorBrush* bgBrush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.09f, 0.12f, 0.96f), &bgBrush);
    if (bgBrush) {
        rt->FillRoundedRectangle(rBanner, bgBrush);
        bgBrush->Release();
    }

    // 3. Subtle Hairline Border
    ID2D1SolidColorBrush* borderBrush = nullptr;
    D2D1_COLOR_F borderColor = (state == UpdateState::Downloaded) 
        ? D2D1::ColorF(0.12f, 0.70f, 0.38f, 0.55f)
        : D2D1::ColorF(0.24f, 0.42f, 0.85f, 0.55f);
    rt->CreateSolidColorBrush(borderColor, &borderBrush);
    if (borderBrush) {
        rt->DrawRoundedRectangle(rBanner, borderBrush, 1.0f);
        borderBrush->Release();
    }

    // 4. Glowing Status Indicator Dot
    D2D1_POINT_2F dotCenter = D2D1::Point2F(m_updateBannerRect.left + 16.0f * dpiScale, m_updateBannerRect.top + bannerH / 2.0f);
    ID2D1SolidColorBrush* dotBrush = nullptr;
    D2D1_COLOR_F dotColor = (state == UpdateState::Downloaded)
        ? D2D1::ColorF(0.12f, 0.85f, 0.45f, 1.0f)
        : ((state == UpdateState::Downloading) ? D2D1::ColorF(0.95f, 0.65f, 0.15f, 1.0f) : D2D1::ColorF(0.25f, 0.65f, 1.0f, 1.0f));
    rt->CreateSolidColorBrush(dotColor, &dotBrush);
    if (dotBrush) {
        rt->FillEllipse(D2D1::Ellipse(dotCenter, 4.0f * dpiScale, 4.0f * dpiScale), dotBrush);
        dotBrush->Release();
    }

    // 5. Content Rendering
    if (state == UpdateState::UpdateAvailable) {
        std::wstring title = L"Update v" + info.remoteVersion;
        D2D1_RECT_F textRect = D2D1::RectF(
            m_updateBannerRect.left + 28.0f * dpiScale,
            m_updateBannerRect.top + 9.0f * dpiScale,
            m_updateBannerRect.right - 96.0f * dpiScale,
            m_updateBannerRect.bottom - 9.0f * dpiScale
        );
        if (m_d2d.BrushText && m_d2d.FontButton) {
            rt->DrawText(title.c_str(), (UINT32)title.length(), m_d2d.FontButton, textRect, m_d2d.BrushText);
        }

        // Action Pill Button: [ Update ]
        m_updateActionBtnRect = D2D1::RectF(
            m_updateBannerRect.right - 92.0f * dpiScale,
            m_updateBannerRect.top + 6.0f * dpiScale,
            m_updateBannerRect.right - 30.0f * dpiScale,
            m_updateBannerRect.bottom - 6.0f * dpiScale
        );
        D2D1_ROUNDED_RECT rBtn = D2D1::RoundedRect(m_updateActionBtnRect, 13.0f * dpiScale, 13.0f * dpiScale);
        ID2D1SolidColorBrush* btnBrush = nullptr;
        D2D1_COLOR_F btnColor = m_updateActionBtnHover ? D2D1::ColorF(0.24f, 0.52f, 0.98f, 1.0f) : D2D1::ColorF(0.14f, 0.40f, 0.90f, 1.0f);
        rt->CreateSolidColorBrush(btnColor, &btnBrush);
        if (btnBrush) {
            rt->FillRoundedRectangle(rBtn, btnBrush);
            btnBrush->Release();
        }

        std::wstring btnLabel = L"Update";
        if (m_d2d.BrushText && m_d2d.FontButton) {
            rt->DrawText(btnLabel.c_str(), (UINT32)btnLabel.length(), m_d2d.FontButton, m_updateActionBtnRect, m_d2d.BrushText);
        }

        // Dismiss Button: [ ✕ ]
        m_updateDismissBtnRect = D2D1::RectF(
            m_updateBannerRect.right - 25.0f * dpiScale,
            m_updateBannerRect.top + 7.0f * dpiScale,
            m_updateBannerRect.right - 8.0f * dpiScale,
            m_updateBannerRect.bottom - 7.0f * dpiScale
        );
        std::wstring closeLabel = L"✕";
        ID2D1SolidColorBrush* closeBrush = nullptr;
        rt->CreateSolidColorBrush(m_updateDismissBtnHover ? D2D1::ColorF(0.95f, 0.95f, 0.95f) : D2D1::ColorF(0.55f, 0.55f, 0.58f), &closeBrush);
        if (closeBrush && m_d2d.FontButton) {
            rt->DrawText(closeLabel.c_str(), (UINT32)closeLabel.length(), m_d2d.FontButton, m_updateDismissBtnRect, closeBrush);
            closeBrush->Release();
        }
    } else if (state == UpdateState::Downloading) {
        int pct = m_updater.GetDownloadProgress();
        std::wstring dlText = L"Downloading... " + std::to_wstring(pct) + L"%";
        D2D1_RECT_F textRect = D2D1::RectF(
            m_updateBannerRect.left + 28.0f * dpiScale,
            m_updateBannerRect.top + 6.0f * dpiScale,
            m_updateBannerRect.right - 18.0f * dpiScale,
            m_updateBannerRect.top + 22.0f * dpiScale
        );
        if (m_d2d.BrushText && m_d2d.FontButton) {
            rt->DrawText(dlText.c_str(), (UINT32)dlText.length(), m_d2d.FontButton, textRect, m_d2d.BrushText);
        }

        // Inset Progress Bar Track
        D2D1_RECT_F trackRect = D2D1::RectF(
            m_updateBannerRect.left + 18.0f * dpiScale,
            m_updateBannerRect.bottom - 9.0f * dpiScale,
            m_updateBannerRect.right - 18.0f * dpiScale,
            m_updateBannerRect.bottom - 6.0f * dpiScale
        );
        D2D1_ROUNDED_RECT rTrack = D2D1::RoundedRect(trackRect, 1.5f * dpiScale, 1.5f * dpiScale);
        ID2D1SolidColorBrush* trackBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.20f, 0.25f, 1.0f), &trackBrush);
        if (trackBrush) {
            rt->FillRoundedRectangle(rTrack, trackBrush);
            trackBrush->Release();
        }

        float fillW = (trackRect.right - trackRect.left) * (std::max(1, pct) / 100.0f);
        D2D1_RECT_F fillRect = D2D1::RectF(trackRect.left, trackRect.top, trackRect.left + fillW, trackRect.bottom);
        D2D1_ROUNDED_RECT rFill = D2D1::RoundedRect(fillRect, 1.5f * dpiScale, 1.5f * dpiScale);
        ID2D1SolidColorBrush* fillBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.55f, 0.98f, 1.0f), &fillBrush);
        if (fillBrush) {
            rt->FillRoundedRectangle(rFill, fillBrush);
            fillBrush->Release();
        }
    } else if (state == UpdateState::Downloaded) {
        std::wstring title = L"Update Ready";
        D2D1_RECT_F textRect = D2D1::RectF(
            m_updateBannerRect.left + 28.0f * dpiScale,
            m_updateBannerRect.top + 9.0f * dpiScale,
            m_updateBannerRect.left + 140.0f * dpiScale,
            m_updateBannerRect.bottom - 9.0f * dpiScale
        );
        if (m_d2d.BrushText && m_d2d.FontButton) {
            rt->DrawText(title.c_str(), (UINT32)title.length(), m_d2d.FontButton, textRect, m_d2d.BrushText);
        }

        // Action Pill Button: [ Restart ]
        m_updateActionBtnRect = D2D1::RectF(
            m_updateBannerRect.right - 100.0f * dpiScale,
            m_updateBannerRect.top + 6.0f * dpiScale,
            m_updateBannerRect.right - 14.0f * dpiScale,
            m_updateBannerRect.bottom - 6.0f * dpiScale
        );
        D2D1_ROUNDED_RECT rBtn = D2D1::RoundedRect(m_updateActionBtnRect, 13.0f * dpiScale, 13.0f * dpiScale);
        ID2D1SolidColorBrush* btnBrush = nullptr;
        D2D1_COLOR_F btnColor = m_updateActionBtnHover ? D2D1::ColorF(0.12f, 0.75f, 0.42f, 1.0f) : D2D1::ColorF(0.08f, 0.62f, 0.35f, 1.0f);
        rt->CreateSolidColorBrush(btnColor, &btnBrush);
        if (btnBrush) {
            rt->FillRoundedRectangle(rBtn, btnBrush);
            btnBrush->Release();
        }

        std::wstring btnLabel = L"Restart";
        if (m_d2d.BrushText && m_d2d.FontButton) {
            rt->DrawText(btnLabel.c_str(), (UINT32)btnLabel.length(), m_d2d.FontButton, m_updateActionBtnRect, m_d2d.BrushText);
        }
    }
}

void AppWindow::UpdateWindowTitle() {
    if (!m_hwnd) return;
    std::wstring title;
    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
        if (m_text.IsModified()) {
            title = L"● " + m_fileName + L" - WinPreview";
        } else {
            title = m_fileName + L" - WinPreview";
        }
    } else if (!m_fileName.empty()) {
        title = m_fileName + L" - WinPreview";
    } else {
        title = L"WinPreview";
    }
    SetWindowTextW(m_hwnd, title.c_str());
}

void AppWindow::SaveCurrentTextFile() {
    if (m_docType != DocumentType::Text && m_docType != DocumentType::Spreadsheet) return;
    if (m_text.SaveFile()) {
        ShowToast(L"Saved successfully (Ctrl+S)");
        UpdateWindowTitle();
        InvalidateRect(m_hwnd, NULL, FALSE);
    } else {
        ShowToast(L"Error saving file!");
    }
}

void AppWindow::ShowContextMenu(int screenX, int screenY) {
    if (m_docType == DocumentType::None) return;

    POINT ptScreen = { screenX, screenY };
    POINT ptClient = ptScreen;
    ScreenToClient(m_hwnd, &ptClient);
    D2D1_POINT_2F pt = ClientToDip(ptClient);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
        D2D1_RECT_F canvasRect = GetCanvasRectDip();

        // If not in table grid view, right-click repositions cursor if clicked outside selection
        if (m_docType == DocumentType::Text || !m_text.IsTableView()) {
            TextPos clickPos = m_text.PointToPosition(pt.x, pt.y, canvasRect, m_dpiScale);
            bool clickInSel = false;
            if (m_text.HasSelection()) {
                TextPos selS = m_text.GetSelectionStart();
                TextPos selE = m_text.GetSelectionEnd();
                if (clickPos >= selS && clickPos <= selE) {
                    clickInSel = true;
                }
            }
            if (!clickInSel && pt.y >= m_text.GetHeaderTopPadding(m_dpiScale)) {
                m_text.HandleClick(pt.x, pt.y, canvasRect, false, m_dpiScale);
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
        }

        AppendMenuW(hMenu, m_text.CanUndo() ? MF_STRING : (MF_STRING | MF_GRAYED), IDM_CTX_UNDO, L"Undo\tCtrl+Z");
        AppendMenuW(hMenu, m_text.CanRedo() ? MF_STRING : (MF_STRING | MF_GRAYED), IDM_CTX_REDO, L"Redo\tCtrl+Y");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, m_text.HasSelection() ? MF_STRING : (MF_STRING | MF_GRAYED), IDM_CTX_CUT, L"Cut\tCtrl+X");
        AppendMenuW(hMenu, (m_text.HasSelection() || !m_text.GetAllText().empty()) ? MF_STRING : (MF_STRING | MF_GRAYED), IDM_CTX_COPY, L"Copy\tCtrl+C");
        AppendMenuW(hMenu, IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_STRING : (MF_STRING | MF_GRAYED), IDM_CTX_PASTE, L"Paste\tCtrl+V");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_CTX_SELECT_ALL, L"Select All\tCtrl+A");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_CTX_SAVE, m_text.IsModified() ? L"Save File*\tCtrl+S" : L"Save File\tCtrl+S");
        AppendMenuW(hMenu, MF_STRING | (m_text.IsWordWrap() ? MF_CHECKED : MF_UNCHECKED), IDM_CTX_WORD_WRAP, L"Word Wrap\tAlt+Z");
        if (m_docType == DocumentType::Spreadsheet) {
            AppendMenuW(hMenu, MF_STRING | (m_text.IsTableView() ? MF_CHECKED : MF_UNCHECKED), IDM_CTX_TABLE_VIEW, L"Grid Table View\tT");
        }
    } else if (m_docType == DocumentType::Image || m_docType == DocumentType::Pdf) {
        AppendMenuW(hMenu, MF_STRING, IDM_CTX_ZOOM_IN, L"Zoom In\t+");
        AppendMenuW(hMenu, MF_STRING, IDM_CTX_ZOOM_OUT, L"Zoom Out\t-");
        AppendMenuW(hMenu, MF_STRING, IDM_CTX_ZOOM_RESET, L"Actual Size (100%)\t0");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_CTX_ROTATE_LEFT, L"Rotate Left\tCtrl+L");
        AppendMenuW(hMenu, MF_STRING, IDM_CTX_ROTATE_RIGHT, L"Rotate Right\tCtrl+R");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING | (m_isFullscreen ? MF_CHECKED : MF_UNCHECKED), IDM_CTX_FULLSCREEN, L"Fullscreen\tF11");
        AppendMenuW(hMenu, MF_STRING, IDM_CTX_INFO, L"File Inspector\tI");
    } else {
        AppendMenuW(hMenu, MF_STRING | (m_isFullscreen ? MF_CHECKED : MF_UNCHECKED), IDM_CTX_FULLSCREEN, L"Fullscreen\tF11");
        AppendMenuW(hMenu, MF_STRING, IDM_CTX_INFO, L"File Inspector\tI");
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_CTX_SETTINGS, L"Keybind Settings...\tCtrl+,");
    AppendMenuW(hMenu, MF_STRING, IDM_CTX_CHECK_UPDATES, L"Check for Updates...\tCtrl+U");
    AppendMenuW(hMenu, MF_STRING, IDM_CTX_SIMULATE_UPDATE, L"Simulate Update Prompt (Test)\tCtrl+Shift+U");

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_RETURNCMD, screenX, screenY, 0, m_hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == 0) return;

    switch (cmd) {
        case IDM_CTX_UNDO:
            if (m_text.Undo()) { UpdateWindowTitle(); InvalidateRect(m_hwnd, NULL, FALSE); }
            break;
        case IDM_CTX_REDO:
            if (m_text.Redo()) { UpdateWindowTitle(); InvalidateRect(m_hwnd, NULL, FALSE); }
            break;
        case IDM_CTX_CUT:
            if (m_text.Cut(m_hwnd)) { UpdateWindowTitle(); ShowToast(L"Cut to clipboard"); InvalidateRect(m_hwnd, NULL, FALSE); }
            break;
        case IDM_CTX_COPY:
            if (m_docType == DocumentType::Spreadsheet && m_text.IsTableView()) {
                CopyTextToClipboard(m_hwnd, m_text.GetAllText());
                ShowToast(L"Copied table");
            } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                if (m_text.Copy(m_hwnd)) {
                    ShowToast(m_text.HasSelection() ? L"Copied selection" : L"Copied line");
                }
            }
            break;
        case IDM_CTX_PASTE:
            if (m_text.Paste(m_hwnd)) { UpdateWindowTitle(); ShowToast(L"Pasted"); InvalidateRect(m_hwnd, NULL, FALSE); }
            break;
        case IDM_CTX_SELECT_ALL:
            m_text.SelectAll();
            InvalidateRect(m_hwnd, NULL, FALSE);
            break;
        case IDM_CTX_SAVE:
            SaveCurrentTextFile();
            break;
        case IDM_CTX_WORD_WRAP:
            m_text.ToggleWordWrap();
            ShowToast(m_text.IsWordWrap() ? L"Word Wrap: ON" : L"Word Wrap: OFF");
            InvalidateRect(m_hwnd, NULL, FALSE);
            break;
        case IDM_CTX_TABLE_VIEW:
            m_text.ToggleTableView();
            ShowToast(m_text.IsTableView() ? L"Grid View" : L"Raw Text View");
            InvalidateRect(m_hwnd, NULL, FALSE);
            break;
        case IDM_CTX_ZOOM_IN:
            ZoomIn();
            break;
        case IDM_CTX_ZOOM_OUT:
            ZoomOut();
            break;
        case IDM_CTX_ZOOM_RESET:
            ResetZoom();
            break;
        case IDM_CTX_ROTATE_LEFT:
            Rotate(-90);
            break;
        case IDM_CTX_ROTATE_RIGHT:
            Rotate(90);
            break;
        case IDM_CTX_FULLSCREEN:
            ToggleFullscreen();
            break;
        case IDM_CTX_INFO:
            ToggleInspector();
            break;
        case IDM_CTX_SETTINGS:
            ToggleSettings();
            break;
        case IDM_CTX_CHECK_UPDATES:
            CheckForUpdates(true);
            break;
        case IDM_CTX_SIMULATE_UPDATE:
            m_updater.SimulateUpdateForTesting();
            ShowToast(L"Simulated Update: v1.1.0 Ready to Test");
            InvalidateRect(m_hwnd, NULL, FALSE);
            break;
    }
}

void AppWindow::RenderCanvas(const D2D1_RECT_F& rect) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();

    // 1. Text & Code Documents
    if (m_docType == DocumentType::Text) {
        m_text.RenderText(rt, m_d2d, rect, m_dpiScale);
        return;
    }

    // 2. Tabular Spreadsheets (CSV / TSV)
    if (m_docType == DocumentType::Spreadsheet) {
        if (m_text.IsTableView()) {
            m_text.RenderSpreadsheet(rt, m_d2d, rect, m_dpiScale);
        } else {
            m_text.RenderText(rt, m_d2d, rect, m_dpiScale);
        }
        return;
    }

    // 3. Audio & Video
    if (m_docType == DocumentType::Media) {
        if (!m_isAudioOnly) {
            return; // Child HWND handles video surface
        }

        // --- High-Fidelity Single-Dock Audio Player Showcase ---
        float clientW = rect.right - rect.left;
        float clientH = rect.bottom - rect.top;
        float cx = rect.left + clientW / 2.0f;

        // Reserve space for the floating bottom player dock (88px)
        float dockClearance = 88.0f;
        float stageH = (std::max)(300.0f, clientH - dockClearance);
        float cy = rect.top + stageH / 2.0f;

        bool isPlaying = m_media.IsPlaying();

        // Smooth physical slide transition for vinyl disc
        float targetSlide = isPlaying ? 58.0f : 20.0f;
        m_audioVinylSlide += (targetSlide - m_audioVinylSlide) * 0.16f;

        // 1. Responsive Visual Sizing (Minimal, zero hard background circles)
        float artSize = std::clamp(stageH * 0.36f, 140.0f, 200.0f);
        float mediaY = cy - 80.0f;

        if (m_audioAlbumArt) {
            // Dual Composition: Album jacket on left, vinyl disc sliding out on right
            float jacketX = cx - (m_audioVinylSlide * 0.5f);
            float vinylX = jacketX + m_audioVinylSlide;
            float vinylR = artSize * 0.48f;

            // --- A. Sliding Vinyl LP Disc (behind jacket) ---
            {
                D2D1::Matrix3x2F oldXform;
                rt->GetTransform(&oldXform);
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(m_audioDiscAngle, D2D1::Point2F(vinylX, mediaY)) * oldXform);

                // Vinyl outer body (#0C0C0F deep vinyl)
                D2D1_ELLIPSE discOuter = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), vinylR, vinylR);
                rt->FillEllipse(discOuter, m_d2d.BrushBg);
                rt->DrawEllipse(discOuter, m_d2d.BrushBorder, 1.2f);

                // Concentric micro-grooves
                for (float r = vinylR * 0.40f; r < vinylR - 5.0f; r += (vinylR * 0.11f)) {
                    D2D1_ELLIPSE discGroove = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), r, r);
                    rt->DrawEllipse(discGroove, m_d2d.BrushBorderSubtle, 0.6f);
                }

                // Dynamic light reflections (specular sheens at 45 deg and 225 deg)
                m_d2d.BrushActive->SetOpacity(0.06f);
                rt->DrawLine(
                    D2D1::Point2F(vinylX - vinylR * 0.85f, mediaY - vinylR * 0.85f),
                    D2D1::Point2F(vinylX + vinylR * 0.85f, mediaY + vinylR * 0.85f),
                    m_d2d.BrushActive,
                    vinylR * 0.28f
                );
                m_d2d.BrushActive->SetOpacity(0.14f);

                // Center vinyl label
                float labelR = vinylR * 0.32f;
                D2D1_ELLIPSE discLabel = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), labelR, labelR);
                rt->FillEllipse(discLabel, m_d2d.BrushCard);
                rt->DrawEllipse(discLabel, m_d2d.BrushAccent, 1.0f);

                // Brass spindle center hole
                D2D1_ELLIPSE spindle = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), 3.0f, 3.0f);
                rt->FillEllipse(spindle, m_d2d.BrushBorder);

                rt->SetTransform(oldXform);
            }

            // --- B. Front Album Jacket / Sleeve ---
            D2D1_RECT_F artRect = D2D1::RectF(
                jacketX - artSize / 2.0f,
                mediaY - artSize / 2.0f,
                jacketX + artSize / 2.0f,
                mediaY + artSize / 2.0f
            );
            D2D1_ROUNDED_RECT rArt = D2D1::RoundedRect(artRect, 14.0f, 14.0f);

            // Ambient soft diffusion shadow
            D2D1_ROUNDED_RECT artShadow = D2D1::RoundedRect(
                D2D1::RectF(artRect.left - 4.0f, artRect.top + 4.0f, artRect.right + 4.0f, artRect.bottom + 16.0f),
                16.0f, 16.0f
            );
            rt->FillRoundedRectangle(artShadow, m_d2d.BrushShadowDark);

            // Draw Album Art with rounded clip
            ID2D1Layer* artLayer = nullptr;
            rt->CreateLayer(NULL, &artLayer);
            if (artLayer) {
                ID2D1RoundedRectangleGeometry* geom = nullptr;
                m_d2d.GetD2DFactory()->CreateRoundedRectangleGeometry(rArt, &geom);
                if (geom) {
                    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geom), artLayer);
                    rt->DrawBitmap(m_audioAlbumArt, artRect);
                    rt->PopLayer();
                    geom->Release();
                }
                artLayer->Release();
            }
            rt->DrawRoundedRectangle(rArt, m_d2d.BrushBorderSubtle, 1.2f);

            // Album jacket thumb notch on right edge
            D2D1_ELLIPSE thumbNotch = D2D1::Ellipse(D2D1::Point2F(artRect.right, mediaY), 7.0f, 16.0f);
            rt->DrawEllipse(thumbNotch, m_d2d.BrushBorderSubtle, 0.8f);

            m_audioArtworkRect = D2D1::RectF(artRect.left, artRect.top, vinylX + vinylR, artRect.bottom);
        } else {
            // High-Craft Minimalist 180g Vinyl Turntable
            float discR = (std::clamp)(stageH * 0.23f, 75.0f, 105.0f);
            float vinylX = cx;

            // Soft drop shadow under the vinyl disc
            D2D1_ELLIPSE discShadow = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY + 5.0f), discR + 3.0f, discR + 3.0f);
            rt->FillEllipse(discShadow, m_d2d.BrushShadowDark);

            D2D1::Matrix3x2F oldXform;
            rt->GetTransform(&oldXform);
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(m_audioDiscAngle, D2D1::Point2F(vinylX, mediaY)) * oldXform);

            // Vinyl outer body (Deep Obsidian #0F0F14)
            D2D1_ELLIPSE discOuter = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), discR, discR);
            rt->FillEllipse(discOuter, m_d2d.BrushBg);
            rt->DrawEllipse(discOuter, m_d2d.BrushBorder, 1.2f);

            // Concentric precision micro-grooves
            for (float r = discR * 0.40f; r < discR - 6.0f; r += (discR * 0.09f)) {
                D2D1_ELLIPSE discGroove = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), r, r);
                rt->DrawEllipse(discGroove, m_d2d.BrushBorderSubtle, 0.6f);
            }

            // Dual specular sheen light sweeps (45 deg and 225 deg)
            m_d2d.BrushActive->SetOpacity(0.06f);
            rt->DrawLine(
                D2D1::Point2F(vinylX - discR * 0.85f, mediaY - discR * 0.85f),
                D2D1::Point2F(vinylX + discR * 0.85f, mediaY + discR * 0.85f),
                m_d2d.BrushActive,
                discR * 0.26f
            );
            rt->DrawLine(
                D2D1::Point2F(vinylX - discR * 0.70f, mediaY + discR * 0.70f),
                D2D1::Point2F(vinylX + discR * 0.70f, mediaY - discR * 0.70f),
                m_d2d.BrushActive,
                discR * 0.14f
            );
            m_d2d.BrushActive->SetOpacity(0.14f);

            // Center record label core
            float coreR = discR * 0.36f;
            D2D1_ELLIPSE discCenter = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), coreR, coreR);
            rt->FillEllipse(discCenter, m_d2d.BrushCard);
            rt->DrawEllipse(discCenter, m_d2d.BrushAccent, 1.2f);

            // Subtle inner concentric groove in label
            D2D1_ELLIPSE discInnerRing = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), coreR * 0.85f, coreR * 0.85f);
            rt->DrawEllipse(discInnerRing, m_d2d.BrushBorderSubtle, 0.8f);

            // Center vector music emblem (crisp Direct2D vector drawing)
            m_d2d.DrawIconMusic(vinylX, mediaY, coreR * 0.92f, m_d2d.BrushText);

            // Spindle chrome micro-ring
            D2D1_ELLIPSE spindle = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), 2.2f, 2.2f);
            rt->DrawEllipse(spindle, m_d2d.BrushBorder, 0.8f);

            rt->SetTransform(oldXform);

            m_audioArtworkRect = D2D1::RectF(vinylX - discR, mediaY - discR, vinylX + discR, mediaY + discR);
        }

        // 2. Track Title & Artist Subtitle (Refined Typographic Hierarchy & Spacing)
        float discVisualR = m_audioAlbumArt ? (artSize / 2.0f) : ((std::clamp)(stageH * 0.23f, 75.0f, 105.0f));
        float titleY = mediaY + discVisualR + 24.0f; // Generous 24px clearance below disc
        float titleH = 34.0f;
        float textW = (std::min)(clientW - 64.0f, 540.0f);
        float textX = cx - textW / 2.0f;

        std::wstring displayTitle = m_fileName;
        size_t dotPos = displayTitle.rfind(L'.');
        if (dotPos != std::wstring::npos && dotPos > 0) {
            displayTitle = displayTitle.substr(0, dotPos);
        }
        // Strip leading track numbering and delimiters ("01_", "01 - ", "01 ", "01. ", etc.)
        size_t numIdx = 0;
        while (numIdx < displayTitle.length() && iswdigit(displayTitle[numIdx])) numIdx++;
        if (numIdx > 0 && numIdx < displayTitle.length()) {
            while (numIdx < displayTitle.length() && (displayTitle[numIdx] == L' ' || displayTitle[numIdx] == L'_' || displayTitle[numIdx] == L'-' || displayTitle[numIdx] == L'.')) {
                numIdx++;
            }
            if (numIdx < displayTitle.length()) {
                displayTitle = displayTitle.substr(numIdx);
            }
        }
        // Convert remaining underscores to spaces
        for (auto& ch : displayTitle) {
            if (ch == L'_') ch = L' ';
        }

        IDWriteTextLayout* titleLayout = nullptr;
        m_d2d.GetDWriteFactory()->CreateTextLayout(displayTitle.c_str(), (UINT32)displayTitle.length(),
            m_d2d.FontHero ? m_d2d.FontHero : m_d2d.FontTitle, textW, titleH, &titleLayout);
        if (titleLayout) {
            titleLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            titleLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(textX, titleY), titleLayout, m_d2d.BrushText);
            titleLayout->Release();
        }

        std::wstring artistLine = m_audioArtist;
        if (!m_audioAlbum.empty()) {
            if (!artistLine.empty()) artistLine += L" • ";
            artistLine += m_audioAlbum;
        }

        float nextY = titleY + titleH;

        // Only draw subtitle if actual artist / album metadata exists (no dummy text)
        if (!artistLine.empty()) {
            float subY = nextY + 8.0f;
            float subH = 20.0f;
            IDWriteTextLayout* artistLayout = nullptr;
            m_d2d.GetDWriteFactory()->CreateTextLayout(artistLine.c_str(), (UINT32)artistLine.length(),
                m_d2d.FontBody ? m_d2d.FontBody : m_d2d.FontCaption, textW, subH, &artistLayout);
            if (artistLayout) {
                artistLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                artistLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(textX, subY), artistLayout, 
                    m_d2d.BrushAudioSubtitle ? m_d2d.BrushAudioSubtitle : m_d2d.BrushTextMuted);
                artistLayout->Release();
            }
            nextY = subY + subH;
        }

        // 3. Precision Quality Pill Badge (Clean 16px gap below subtitle/title)
        float badgeY = nextY + 16.0f;
        std::wstring badgeText = m_formatBadgeText.empty() ? L"MP3 • AUDIO" : m_formatBadgeText;
        float badgeW = (std::clamp)(34.0f + (float)badgeText.length() * 6.8f, 108.0f, 160.0f);
        float badgeH = 22.0f;
        D2D1_RECT_F badgeRect = D2D1::RectF(cx - badgeW / 2.0f, badgeY, cx + badgeW / 2.0f, badgeY + badgeH);
        D2D1_ROUNDED_RECT rBadge = D2D1::RoundedRect(badgeRect, 11.0f, 11.0f);
        rt->FillRoundedRectangle(rBadge, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(rBadge, m_d2d.BrushBorder, 1.0f);

        // Status glowing dot inside badge
        float dotCenterX = badgeRect.left + 12.0f;
        float dotCenterY = badgeY + badgeH / 2.0f;
        if (isPlaying) {
            D2D1_ELLIPSE halo = D2D1::Ellipse(D2D1::Point2F(dotCenterX, dotCenterY), 4.5f, 4.5f);
            rt->FillEllipse(halo, m_d2d.BrushAccentGlow);
            D2D1_ELLIPSE core = D2D1::Ellipse(D2D1::Point2F(dotCenterX, dotCenterY), 2.5f, 2.5f);
            rt->FillEllipse(core, m_d2d.BrushAccent);
        } else {
            D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(dotCenterX, dotCenterY), 2.2f, 2.2f);
            rt->FillEllipse(dot, m_d2d.BrushBorder);
        }

        // Badge text
        D2D1_RECT_F badgeTextR = D2D1::RectF(badgeRect.left + 20.0f, badgeY, badgeRect.right - 8.0f, badgeY + badgeH);
        rt->DrawText(badgeText.c_str(), (UINT32)badgeText.length(), m_d2d.FontBadge, badgeTextR, 
            m_d2d.BrushAudioSubtitle ? m_d2d.BrushAudioSubtitle : m_d2d.BrushTextMuted);

        // 4. Studio Dynamic Spectrum Waveform Engine (64-Bar Multi-Band Acoustic Simulation)
        int barCount = 64;
        float barW = 2.4f;
        float barGap = 2.2f;
        float totalEqW = barCount * barW + (barCount - 1) * barGap;
        float startX = cx - totalEqW / 2.0f;
        // Guaranteed 34px clearance below badge (at least 10px-20px above tallest peak!)
        float eqBaseY = badgeY + badgeH + 34.0f;

        m_audioWaveformRect = D2D1::RectF(startX - 10.0f, eqBaseY - 26.0f, startX + totalEqW + 10.0f, eqBaseY + 22.0f);

        float pos = m_media.GetPosition();
        float curPlayedFrac = (std::clamp)(pos, 0.0f, 1.0f);
        float t = m_audioAnimTime;

        for (int i = 0; i < barCount; ++i) {
            float phase = (float)i / (float)(barCount - 1);

            // A. Deterministic Mastered Track Terrain (Peaks & Valleys)
            float terrain = 0.36f 
                + 0.24f * sinf(phase * 6.28318f * 1.5f + 0.85f) 
                + 0.16f * cosf(phase * 6.28318f * 3.4f - 1.15f)
                + 0.12f * sinf(phase * 6.28318f * 6.2f + 2.10f);
            float barNoise = (float)(((i * 37 + 17) % 23) - 11) / 46.0f;
            float baseProfile = (std::clamp)(terrain + barNoise, 0.22f, 0.95f);

            // B. Multi-Band Energetic Audio Simulation
            // Bass Kick / Sub (bars 0..14)
            float kick = powf((std::max)(0.0f, sinf(t * 4.4f)), 3.0f);
            float subBass = sinf(t * 2.2f + phase * 5.0f) * 0.35f;
            float bassEnergy = (kick * 0.75f + subBass + 0.65f);

            // Mids / Melodic Chords (bars 15..45)
            float v1 = sinf(t * 5.8f - phase * 14.0f) * 0.36f;
            float v2 = cosf(t * 3.6f + phase * 9.5f) * 0.28f;
            float v3 = sinf(t * 8.6f - phase * 20.0f) * 0.18f;
            float midEnergy = (v1 + v2 + v3 + 0.70f);

            // Treble / Transients / Hi-Hats (bars 46..63)
            float hi1 = sinf(t * 14.2f + phase * 26.0f) * 0.38f;
            float hi2 = cosf(t * 8.2f - phase * 16.0f) * 0.26f;
            float hiEnergy = (hi1 + hi2 + 0.65f);

            // Seamless crossfade across the spectrum
            float dynamicMod = 0.0f;
            if (phase < 0.22f) {
                float w = phase / 0.22f;
                dynamicMod = bassEnergy * (1.0f - w) + midEnergy * w;
            } else if (phase < 0.70f) {
                float w = (phase - 0.22f) / 0.48f;
                dynamicMod = midEnergy * (1.0f - w) + hiEnergy * w;
            } else {
                float w = (phase - 0.70f) / 0.30f;
                dynamicMod = hiEnergy * (1.0f - w) + (hiEnergy * 0.8f + subBass * 0.2f) * w;
            }
            dynamicMod = (std::clamp)(dynamicMod, 0.15f, 1.45f);

            // C. Studio Positive Crest & Softer Negative Reflection
            float activeTop = 4.0f + baseProfile * dynamicMod * 20.0f;
            float targetTop = isPlaying ? activeTop : (2.5f + baseProfile * 8.5f);
            float targetBottom = isPlaying ? (activeTop * 0.62f) : (targetTop * 0.55f);

            // D. Ballistic Spring Physics (Punchy attack, smooth analog decay)
            float attackDecayTop = (targetTop > m_audioVisualizerBars[i]) ? 0.44f : 0.18f;
            m_audioVisualizerBars[i] += (targetTop - m_audioVisualizerBars[i]) * attackDecayTop;

            float attackDecayBottom = (targetBottom > m_audioVisualizerBottom[i]) ? 0.42f : 0.16f;
            m_audioVisualizerBottom[i] += (targetBottom - m_audioVisualizerBottom[i]) * attackDecayBottom;

            float bx = startX + (float)i * (barW + barGap);
            float topH = m_audioVisualizerBars[i];
            float botH = m_audioVisualizerBottom[i];

            D2D1_ROUNDED_RECT barR = D2D1::RoundedRect(
                D2D1::RectF(bx, eqBaseY - topH, bx + barW, eqBaseY + botH),
                1.2f, 1.2f
            );

            float barFrac = (float)i / (float)(barCount - 1);
            bool isPlayed = (barFrac <= curPlayedFrac);
            bool isPlayhead = (std::abs(barFrac - curPlayedFrac) <= (1.2f / (float)barCount));
            bool isHovered = (m_audioHoverFrac >= 0.0f && barFrac <= m_audioHoverFrac && !isPlayed);

            ID2D1Brush* barBrush = nullptr;
            if (isPlayhead) {
                barBrush = m_d2d.BrushAudioWavePlayhead ? (ID2D1Brush*)m_d2d.BrushAudioWavePlayhead : (ID2D1Brush*)m_d2d.BrushAccent;
            } else if (isPlayed) {
                barBrush = m_d2d.BrushAudioWavePlayed ? (ID2D1Brush*)m_d2d.BrushAudioWavePlayed : (ID2D1Brush*)m_d2d.BrushAccent;
            } else if (isHovered) {
                barBrush = m_d2d.BrushAccentGlow ? (ID2D1Brush*)m_d2d.BrushAccentGlow : (ID2D1Brush*)m_d2d.BrushActive;
            } else {
                barBrush = m_d2d.BrushAudioWaveUnplayed ? (ID2D1Brush*)m_d2d.BrushAudioWaveUnplayed : (ID2D1Brush*)m_d2d.BrushBorderSubtle;
            }

            rt->FillRoundedRectangle(barR, barBrush);
        }

        return;
    }

    // 4. Vector Graphics (.svg)
    if (m_docType == DocumentType::Svg) {
        m_svg.Render(rt, rect, m_zoom, m_panX, m_panY);
        return;
    }

    // 5. Typographic Fonts (.ttf, .otf, .woff, .woff2)
    if (m_docType == DocumentType::Font) {
        m_font.Render(rt, m_d2d, rect, m_dpiScale);
        return;
    }

    // 6. Folders / Directories
    if (m_docType == DocumentType::Folder) {
        m_folder.Render(rt, m_d2d, rect, m_dpiScale);
        return;
    }

    // 7. Markdown Documents & Jupyter Notebooks (.md, .ipynb)
    if (m_docType == DocumentType::Markdown || m_docType == DocumentType::Notebook) {
        m_md.Render(rt, m_d2d, rect, m_dpiScale);
        return;
    }

    // 8. System Binaries, Shortcuts & Certificates
    if (m_docType == DocumentType::Binary || m_docType == DocumentType::Shortcut || m_docType == DocumentType::Certificate) {
        int hoveredId = 0;
        if (m_hoveredButtonId == BTN_SYSTEM_PRIMARY) hoveredId = 1001;
        else if (m_hoveredButtonId == BTN_SYSTEM_SECONDARY) hoveredId = 1002;
        m_system.Render(rt, m_d2d, rect, m_dpiScale, hoveredId);
        return;
    }

    ID2D1Bitmap* bitmap = (m_docType == DocumentType::Image) ? m_imageBitmap : ((m_docType == DocumentType::Pdf) ? m_pdfPageBitmap : m_officeBitmap);
    if (!bitmap) return;

    bool useContentTrim = (m_docType == DocumentType::Image && m_hasDetectedContentBounds && m_trimBlackBars);
    float docW = 0.0f, docH = 0.0f;
    if (useContentTrim) {
        docW = m_contentBounds.right - m_contentBounds.left;
        docH = m_contentBounds.bottom - m_contentBounds.top;
    } else if (m_docType == DocumentType::Image) {
        docW = (float)m_imageWidth;
        docH = (float)m_imageHeight;
    } else if (m_docType == DocumentType::Office) {
        docW = (float)m_officeWidth;
        docH = (float)m_officeHeight;
    } else {
        docW = m_pdfPageWidth;
        docH = m_pdfPageHeight;
    }

    float targetW = docW * m_zoom;
    float targetH = docH * m_zoom;

    float canvasCenterX = rect.left + (rect.right - rect.left) / 2.0f;
    float canvasCenterY = rect.top + (rect.bottom - rect.top) / 2.0f;

    float destLeft = canvasCenterX - (targetW / 2.0f) + m_panX;
    float destTop = canvasCenterY - (targetH / 2.0f) + m_panY;
    D2D1_RECT_F destRect = D2D1::RectF(destLeft, destTop, destLeft + targetW, destTop + targetH);

    // Apply rotation around center of document
    D2D1_POINT_2F rotCenter = D2D1::Point2F(destLeft + targetW / 2.0f, destTop + targetH / 2.0f);
    D2D1::Matrix3x2F rotMatrix = D2D1::Matrix3x2F::Rotation((float)m_rotation, rotCenter);
    rt->SetTransform(rotMatrix);

    // Drop shadow: paper shadow for PDF, subtle shadow for floating/panned images
    if (m_docType == DocumentType::Pdf) {
        D2D1_ROUNDED_RECT ambShadow = D2D1::RoundedRect(
            D2D1::RectF(destRect.left - 6.0f, destRect.top + 2.0f, destRect.right + 6.0f, destRect.bottom + 14.0f), 6.0f, 6.0f);
        rt->FillRoundedRectangle(ambShadow, m_d2d.BrushShadowSoft);

        D2D1_ROUNDED_RECT conShadow = D2D1::RoundedRect(
            D2D1::RectF(destRect.left - 2.0f, destRect.top, destRect.right + 2.0f, destRect.bottom + 5.0f), 4.0f, 4.0f);
        rt->FillRoundedRectangle(conShadow, m_d2d.BrushShadowDark);
    } else if (destRect.left > rect.left + 4.0f || destRect.top > rect.top + 4.0f) {
        D2D1_ROUNDED_RECT ambShadow = D2D1::RoundedRect(
            D2D1::RectF(destRect.left - 4.0f, destRect.top + 2.0f, destRect.right + 4.0f, destRect.bottom + 8.0f), 4.0f, 4.0f);
        rt->FillRoundedRectangle(ambShadow, m_d2d.BrushShadowSoft);
    }

    // Draw the image / page
    if (useContentTrim) {
        rt->DrawBitmap(bitmap, destRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &m_contentBounds);
    } else {
        rt->DrawBitmap(bitmap, destRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    // 1px crisp outline border only for PDF or floating image
    if (m_docType == DocumentType::Pdf || destRect.left > rect.left + 4.0f || destRect.top > rect.top + 4.0f) {
        rt->DrawRectangle(destRect, m_d2d.BrushBorderSubtle, 1.0f);
    }

    // PDF Text Selection
    if (m_docType == DocumentType::Pdf && m_selStartIndex >= 0 && m_selEndIndex >= 0 && m_selStartIndex != m_selEndIndex) {
        std::vector<D2D1_RECT_F> selRects = m_pdf.GetSelectionRects(m_selStartIndex, m_selEndIndex);
        for (const auto& r : selRects) {
            float sLeft = destLeft + (r.left / m_pdfPageWidth) * targetW;
            float sRight = destLeft + (r.right / m_pdfPageWidth) * targetW;
            float sTop = destTop + (1.0f - r.top / m_pdfPageHeight) * targetH;
            float sBottom = destTop + (1.0f - r.bottom / m_pdfPageHeight) * targetH;
            rt->FillRectangle(D2D1::RectF(sLeft, sTop, sRight, sBottom), m_d2d.BrushSelection);
        }
    }

    // In-Document Search Highlights
    if (m_docType == DocumentType::Pdf && !m_searchMatches.empty()) {
        for (size_t mi = 0; mi < m_searchMatches.size(); mi++) {
            const auto& sm = m_searchMatches[mi];
            if (sm.pageIndex == m_currentPage) {
                bool isActive = ((int)mi == m_currentMatchIndex);
                for (const auto& r : sm.rects) {
                    float sLeft = destLeft + (r.left / m_pdfPageWidth) * targetW;
                    float sRight = destLeft + (r.right / m_pdfPageWidth) * targetW;
                    float sTop = destTop + (1.0f - r.top / m_pdfPageHeight) * targetH;
                    float sBottom = destTop + (1.0f - r.bottom / m_pdfPageHeight) * targetH;
                    D2D1_RECT_F mBox = D2D1::RectF(sLeft, sTop, sRight, sBottom);
                    if (isActive) {
                        rt->FillRectangle(mBox, m_d2d.BrushActiveMatch);
                        rt->DrawRectangle(mBox, m_d2d.BrushAccent, 1.2f);
                    } else {
                        rt->FillRectangle(mBox, m_d2d.BrushHighlight);
                    }
                }
            }
        }
    }

    // Reset transform
    rt->SetTransform(D2D1::Matrix3x2F::Identity());

    // Screen-space HUD overlays
    if (m_isCropActive) {
        RenderCropOverlay(rect);
    }
}

void AppWindow::RenderSearchBar(const D2D1_RECT_F& canvasRect) {
    if (!m_showSearch || m_docType != DocumentType::Pdf) {
        m_searchBarRect = {};
        m_searchPrevBtn = {};
        m_searchNextBtn = {};
        m_searchCloseBtn = {};
        return;
    }
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    float availW = canvasRect.right - canvasRect.left;
    float barW = (std::min)(340.0f, availW - 24.0f);
    float barH = 38.0f;
    float barX = canvasRect.right - barW - 14.0f;
    if (barX < canvasRect.left + 12.0f) barX = canvasRect.left + 12.0f;

    // Anchor cleanly below the top dock pill
    float dockBottom = (m_topDockRect.bottom > 0.0f) ? m_topDockRect.bottom : 0.0f;
    float barY = (std::max)(dockBottom + 10.0f, canvasRect.top + 14.0f);

    m_searchBarRect = D2D1::RectF(barX, barY, barX + barW, barY + barH);
    D2D1_ROUNDED_RECT barR = D2D1::RoundedRect(m_searchBarRect, 8.0f, 8.0f);

    // Drop shadow
    D2D1_ROUNDED_RECT shadowR = D2D1::RoundedRect(
        D2D1::RectF(barX - 4.0f, barY + 2.0f, barX + barW + 4.0f, barY + barH + 8.0f), 10.0f, 10.0f);
    rt->FillRoundedRectangle(shadowR, m_d2d.BrushShadowSoft);

    // Surface & Border
    rt->FillRoundedRectangle(barR, m_d2d.BrushCard);
    rt->DrawRoundedRectangle(barR, m_d2d.BrushAccentGlow, 1.2f);

    // Search Icon
    float iconCx = barX + 20.0f;
    float iconCy = barY + barH / 2.0f;
    m_d2d.DrawIconSearch(iconCx, iconCy, 14.0f, m_d2d.BrushAccent);

    // Query text / placeholder
    float textX = barX + 36.0f;
    float textW = (std::max)(60.0f, barW - 190.0f);
    D2D1_RECT_F textR = D2D1::RectF(textX, barY + 9.0f, textX + textW, barY + barH);

    if (m_searchQuery.empty()) {
        const WCHAR* placeholder = L"Find in document...";
        rt->DrawText(placeholder, (UINT32)wcslen(placeholder), m_d2d.FontCaption, textR, m_d2d.BrushTextMuted);
    } else {
        rt->DrawText(m_searchQuery.c_str(), (UINT32)m_searchQuery.length(), m_d2d.FontBody, textR, m_d2d.BrushText);
    }

    // Match count pill: e.g. "2 of 9" or "0 found"
    float pillX = textX + textW + 4.0f;
    float pillW = 68.0f;
    float pillH = 22.0f;
    float pillY = barY + (barH - pillH) / 2.0f;

    if (!m_searchQuery.empty()) {
        D2D1_ROUNDED_RECT pillR = D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH), 4.0f, 4.0f);
        WCHAR countBuf[32];
        if (m_searchMatches.empty()) {
            wcscpy_s(countBuf, L"0 found");
            rt->FillRoundedRectangle(pillR, m_d2d.BrushBorderSubtle);
            D2D1_RECT_F pTextR = D2D1::RectF(pillX, pillY + 3.0f, pillX + pillW, pillY + pillH);
            rt->DrawText(countBuf, (UINT32)wcslen(countBuf), m_d2d.FontBadge, pTextR, m_d2d.BrushTextMuted);
        } else {
            swprintf_s(countBuf, L"%d of %d", m_currentMatchIndex + 1, (int)m_searchMatches.size());
            rt->FillRoundedRectangle(pillR, m_d2d.BrushCardSelected);
            rt->DrawRoundedRectangle(pillR, m_d2d.BrushAccent, 1.0f);
            D2D1_RECT_F pTextR = D2D1::RectF(pillX, pillY + 3.0f, pillX + pillW, pillY + pillH);
            rt->DrawText(countBuf, (UINT32)wcslen(countBuf), m_d2d.FontBadge, pTextR, m_d2d.BrushAccent);
        }
    }

    // Prev / Next Chevron buttons and Close button
    float btnSize = 24.0f;
    float btnY = barY + (barH - btnSize) / 2.0f;

    float prevX = barX + barW - 74.0f;
    m_searchPrevBtn = D2D1::RectF(prevX, btnY, prevX + 22.0f, btnY + btnSize);
    DrawIconButton(m_searchPrevBtn, ICON_CHEVRON_LEFT, BTN_SEARCH_PREV, !m_searchMatches.empty());

    float nextX = prevX + 24.0f;
    m_searchNextBtn = D2D1::RectF(nextX, btnY, nextX + 22.0f, btnY + btnSize);
    DrawIconButton(m_searchNextBtn, ICON_CHEVRON_RIGHT, BTN_SEARCH_NEXT, !m_searchMatches.empty());

    float closeX = nextX + 24.0f;
    m_searchCloseBtn = D2D1::RectF(closeX, btnY, closeX + 22.0f, btnY + btnSize);
    if (m_hoveredButtonId == BTN_SEARCH_CLOSE) {
        D2D1_ROUNDED_RECT clR = D2D1::RoundedRect(m_searchCloseBtn, 4.0f, 4.0f);
        rt->FillRoundedRectangle(clR, m_d2d.BrushHover);
    }
    float cx = m_searchCloseBtn.left + 11.0f;
    float cy = m_searchCloseBtn.top + 12.0f;
    float cr = 4.0f;
    rt->DrawLine(D2D1::Point2F(cx - cr, cy - cr), D2D1::Point2F(cx + cr, cy + cr), m_d2d.BrushTextMuted, 1.5f);
    rt->DrawLine(D2D1::Point2F(cx + cr, cy - cr), D2D1::Point2F(cx - cr, cy + cr), m_d2d.BrushTextMuted, 1.5f);

    if (!m_showSearch) {
        m_searchBarRect = {};
        m_searchPrevBtn = {};
        m_searchNextBtn = {};
        m_searchCloseBtn = {};
    }
}

void AppWindow::RenderCropOverlay(const D2D1_RECT_F& canvasRect) {
    if (!m_isCropActive || m_docType != DocumentType::Image || m_imageWidth == 0 || m_imageHeight == 0) return;
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    float targetW = (float)m_imageWidth * m_zoom;
    float targetH = (float)m_imageHeight * m_zoom;
    float canvasCenterX = canvasRect.left + (canvasRect.right - canvasRect.left) / 2.0f;
    float canvasCenterY = canvasRect.top + (canvasRect.bottom - canvasRect.top) / 2.0f;
    float destLeft = canvasCenterX - (targetW / 2.0f) + m_panX;
    float destTop = canvasCenterY - (targetH / 2.0f) + m_panY;
    float destRight = destLeft + targetW;
    float destBottom = destTop + targetH;

    // Map m_cropRect (image space) to screen coordinates
    float cLeft = destLeft + (m_cropRect.left / (float)m_imageWidth) * targetW;
    float cTop = destTop + (m_cropRect.top / (float)m_imageHeight) * targetH;
    float cRight = destLeft + (m_cropRect.right / (float)m_imageWidth) * targetW;
    float cBottom = destTop + (m_cropRect.bottom / (float)m_imageHeight) * targetH;

    // Clamp
    cLeft = (std::max)(cLeft, destLeft);
    cTop = (std::max)(cTop, destTop);
    cRight = (std::min)(cRight, destRight);
    cBottom = (std::min)(cBottom, destBottom);

    // 1. Dim 4 outer regions
    rt->FillRectangle(D2D1::RectF(destLeft, destTop, destRight, cTop), m_d2d.BrushCropOverlay);
    rt->FillRectangle(D2D1::RectF(destLeft, cBottom, destRight, destBottom), m_d2d.BrushCropOverlay);
    rt->FillRectangle(D2D1::RectF(destLeft, cTop, cLeft, cBottom), m_d2d.BrushCropOverlay);
    rt->FillRectangle(D2D1::RectF(cRight, cTop, destRight, cBottom), m_d2d.BrushCropOverlay);

    // 2. Crop border
    D2D1_RECT_F cropBox = D2D1::RectF(cLeft, cTop, cRight, cBottom);
    rt->DrawRectangle(cropBox, m_d2d.BrushText, 1.5f);

    // 3. Rule of Thirds grid lines
    float w = cRight - cLeft;
    float h = cBottom - cTop;
    if (w > 30.0f && h > 30.0f) {
        float x1 = cLeft + w / 3.0f;
        float x2 = cLeft + 2.0f * w / 3.0f;
        float y1 = cTop + h / 3.0f;
        float y2 = cTop + 2.0f * h / 3.0f;

        rt->DrawLine(D2D1::Point2F(x1, cTop), D2D1::Point2F(x1, cBottom), m_d2d.BrushBorderSubtle, 1.0f);
        rt->DrawLine(D2D1::Point2F(x2, cTop), D2D1::Point2F(x2, cBottom), m_d2d.BrushBorderSubtle, 1.0f);
        rt->DrawLine(D2D1::Point2F(cLeft, y1), D2D1::Point2F(cRight, y1), m_d2d.BrushBorderSubtle, 1.0f);
        rt->DrawLine(D2D1::Point2F(cLeft, y2), D2D1::Point2F(cRight, y2), m_d2d.BrushBorderSubtle, 1.0f);
    }

    // 4. Corner Handles (10x10 white square pills)
    float hs = 10.0f;
    auto drawHandle = [&](float x, float y) {
        D2D1_RECT_F hr = D2D1::RectF(x - hs/2, y - hs/2, x + hs/2, y + hs/2);
        rt->FillRectangle(hr, m_d2d.BrushText);
        rt->DrawRectangle(hr, m_d2d.BrushBg, 1.0f);
    };
    drawHandle(cLeft, cTop);         // 1: LT
    drawHandle(cRight, cTop);        // 2: RT
    drawHandle(cRight, cBottom);     // 3: RB
    drawHandle(cLeft, cBottom);      // 4: LB

    // 5. Dimension & Action pill floating below crop box
    WCHAR dimBuf[64];
    int curW = (int)std::abs(m_cropRect.right - m_cropRect.left);
    int curH = (int)std::abs(m_cropRect.bottom - m_cropRect.top);
    swprintf_s(dimBuf, L"%d × %d px   •   Enter to Save   •   Esc to Cancel", curW, curH);

    float pillW = (std::min)(340.0f, canvasRect.right - canvasRect.left - 20.0f);
    float pillH = 30.0f;
    float pillX = cLeft + (w - pillW) / 2.0f;
    if (pillX < canvasRect.left + 10.0f) pillX = canvasRect.left + 10.0f;
    if (pillX + pillW > canvasRect.right - 10.0f) pillX = canvasRect.right - pillW - 10.0f;
    float pillY = cBottom + 12.0f;
    if (pillY + pillH > canvasRect.bottom - 10.0f) {
        pillY = cTop - pillH - 12.0f;
    }

    D2D1_ROUNDED_RECT pillR = D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH), 15.0f, 15.0f);
    rt->FillRoundedRectangle(pillR, m_d2d.BrushCard);
    rt->DrawRoundedRectangle(pillR, m_d2d.BrushBorder, 1.0f);
    D2D1_RECT_F textR = D2D1::RectF(pillX, pillY + 6.0f, pillX + pillW, pillY + pillH);
    rt->DrawText(dimBuf, (UINT32)wcslen(dimBuf), m_d2d.FontCaption, textR, m_d2d.BrushText);
}

void AppWindow::RenderInspector(const D2D1_RECT_F& canvasRect) {
    if (!m_showInspector) {
        m_inspectorRect = {};
        m_inspectorCloseRect = {};
        m_inspectorCopyRect = {};
        return;
    }
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    float availW = canvasRect.right - canvasRect.left;
    float cardW = (std::min)(310.0f, availW - 24.0f);
    float cardX = canvasRect.right - cardW - 14.0f;
    if (cardX < canvasRect.left + 10.0f) cardX = canvasRect.left + 10.0f;

    // Anchor below search bar if active, otherwise below top dock pill
    float dockBottom = (m_topDockRect.bottom > 0.0f) ? m_topDockRect.bottom : 0.0f;
    float cardY = (m_showSearch && m_searchBarRect.bottom > 0.0f)
        ? (m_searchBarRect.bottom + 10.0f)
        : ((dockBottom > 0.0f) ? (dockBottom + 10.0f) : (canvasRect.top + 14.0f));

    float maxH = canvasRect.bottom - cardY - 14.0f;
    float cardH = (std::min)(420.0f, (std::max)(180.0f, maxH));

    m_inspectorRect = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);

    D2D1_ROUNDED_RECT cardR = D2D1::RoundedRect(m_inspectorRect, 12.0f, 12.0f);

    // Drop shadow
    D2D1_ROUNDED_RECT shadowR = D2D1::RoundedRect(
        D2D1::RectF(cardX - 6.0f, cardY + 4.0f, cardX + cardW + 6.0f, cardY + cardH + 16.0f), 16.0f, 16.0f);
    rt->FillRoundedRectangle(shadowR, m_d2d.BrushShadowSoft);

    // Body & border
    rt->FillRoundedRectangle(cardR, m_d2d.BrushCard);
    rt->DrawRoundedRectangle(cardR, m_d2d.BrushBorder, 1.2f);

    // Header bar
    D2D1_RECT_F headerR = D2D1::RectF(cardX + 16.0f, cardY + 12.0f, cardX + cardW - 44.0f, cardY + 36.0f);
    rt->DrawText(L"Inspector", 9, m_d2d.FontTitle ? m_d2d.FontTitle : m_d2d.FontBody, headerR, m_d2d.BrushText);

    // Close button (24x24 hit area, optical center)
    float closeX = cardX + cardW - 24.0f;
    float closeY = cardY + 24.0f;
    m_inspectorCloseRect = D2D1::RectF(closeX - 12.0f, closeY - 12.0f, closeX + 12.0f, closeY + 12.0f);
    D2D1_ROUNDED_RECT closeR = D2D1::RoundedRect(m_inspectorCloseRect, 6.0f, 6.0f);

    if (m_pressedButtonId == BTN_INSPECTOR_CLOSE) {
        rt->FillRoundedRectangle(closeR, m_d2d.BrushActive);
        closeY += 0.5f;
    } else if (m_hoveredButtonId == BTN_INSPECTOR_CLOSE) {
        rt->FillRoundedRectangle(closeR, m_d2d.BrushHover);
    }
    float cr = 4.5f;
    ID2D1Brush* closeBrush = (m_hoveredButtonId == BTN_INSPECTOR_CLOSE || m_pressedButtonId == BTN_INSPECTOR_CLOSE) ? (ID2D1Brush*)m_d2d.BrushText : (ID2D1Brush*)m_d2d.BrushTextMuted;
    rt->DrawLine(D2D1::Point2F(closeX - cr, closeY - cr), D2D1::Point2F(closeX + cr, closeY + cr), closeBrush, 1.5f);
    rt->DrawLine(D2D1::Point2F(closeX + cr, closeY - cr), D2D1::Point2F(closeX - cr, closeY + cr), closeBrush, 1.5f);

    // Divider line
    rt->DrawLine(D2D1::Point2F(cardX + 16.0f, cardY + 44.0f), D2D1::Point2F(cardX + cardW - 16.0f, cardY + 44.0f), m_d2d.BrushBorderSubtle, 1.0f);

    // Action button at bottom: Copy Metadata
    float copyBtnH = 26.0f;
    float copyBtnY = cardY + cardH - copyBtnH - 12.0f;
    m_inspectorCopyRect = D2D1::RectF(cardX + 16.0f, copyBtnY, cardX + cardW - 16.0f, copyBtnY + copyBtnH);
    D2D1_ROUNDED_RECT copyR = D2D1::RoundedRect(m_inspectorCopyRect, 6.0f, 6.0f);

    if (m_pressedButtonId == BTN_INSPECTOR_COPY) {
        rt->FillRoundedRectangle(copyR, m_d2d.BrushActive);
    } else if (m_hoveredButtonId == BTN_INSPECTOR_COPY) {
        rt->FillRoundedRectangle(copyR, m_d2d.BrushHover);
        rt->DrawRoundedRectangle(copyR, m_d2d.BrushAccent, 1.0f);
    } else {
        rt->FillRoundedRectangle(copyR, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(copyR, m_d2d.BrushBorderSubtle, 1.0f);
    }
    const WCHAR* copyLabel = L"Copy Metadata";
    rt->DrawText(copyLabel, (UINT32)wcslen(copyLabel), m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption, m_inspectorCopyRect, m_d2d.BrushText);

    // Rows rendering
    float rowY = cardY + 52.0f;
    float rowH = 24.0f;
    float labelW = 95.0f;
    float valX = cardX + 16.0f + labelW;
    float valW = cardW - 32.0f - labelW;

    auto drawRow = [&](const WCHAR* label, const std::wstring& val) {
        if (val.empty() || rowY + rowH > copyBtnY - 4.0f) return;
        D2D1_RECT_F lblR = D2D1::RectF(cardX + 16.0f, rowY, cardX + 16.0f + labelW, rowY + rowH);
        D2D1_RECT_F vR = D2D1::RectF(valX, rowY, valX + valW, rowY + rowH);
        rt->DrawText(label, (UINT32)wcslen(label), m_d2d.FontCaption, lblR, m_d2d.BrushTextMuted);
        rt->DrawText(val.c_str(), (UINT32)val.length(), m_d2d.FontCaption, vR, m_d2d.BrushText);
        rowY += rowH;
    };

    drawRow(L"File Name", m_fileName);

    // Location directory
    if (!m_currentFilePath.empty()) {
        size_t slash = m_currentFilePath.find_last_of(L"\\/");
        std::wstring dirPath = (slash != std::wstring::npos) ? m_currentFilePath.substr(0, slash) : L"";
        if (!dirPath.empty()) {
            drawRow(L"Location", dirPath);
        }
    }

    // File Size for all document types
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(m_currentFilePath.c_str(), GetFileExInfoStandard, &fad)) {
        ULONGLONG sizeBytes = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        WCHAR szBuf[64];
        if (sizeBytes < 1024) {
            swprintf_s(szBuf, L"%lld bytes", (long long)sizeBytes);
        } else if (sizeBytes < 1024 * 1024) {
            swprintf_s(szBuf, L"%.1f KB (%lld bytes)", (float)sizeBytes / 1024.0f, (long long)sizeBytes);
        } else if (sizeBytes < 1024 * 1024 * 1024) {
            swprintf_s(szBuf, L"%.2f MB (%lld bytes)", (float)sizeBytes / (1024.0f * 1024.0f), (long long)sizeBytes);
        } else {
            swprintf_s(szBuf, L"%.2f GB", (float)sizeBytes / (1024.0f * 1024.0f * 1024.0f));
        }
        drawRow(L"File Size", szBuf);
    }

    if (m_docType == DocumentType::Image) {
        WCHAR dimBuf[64];
        swprintf_s(dimBuf, L"%u × %u px", m_imageWidth, m_imageHeight);
        drawRow(L"Dimensions", dimBuf);

        if (m_imageMeta.dpiX > 0.0f) {
            WCHAR dpiBuf[32];
            swprintf_s(dpiBuf, L"%.0f × %.0f DPI", m_imageMeta.dpiX, m_imageMeta.dpiY);
            drawRow(L"Resolution", dpiBuf);
        }
        if (!m_imageMeta.cameraMake.empty() || !m_imageMeta.cameraModel.empty()) {
            std::wstring cam = m_imageMeta.cameraMake + L" " + m_imageMeta.cameraModel;
            drawRow(L"Device", cam);
        }
        if (!m_imageMeta.exposureTime.empty()) {
            drawRow(L"Exposure", m_imageMeta.exposureTime);
        }
        if (!m_imageMeta.fNumber.empty()) {
            drawRow(L"Aperture", m_imageMeta.fNumber);
        }
        if (!m_imageMeta.isoSpeed.empty()) {
            drawRow(L"ISO", m_imageMeta.isoSpeed);
        }
        if (!m_imageMeta.focalLength.empty()) {
            drawRow(L"Focal Length", m_imageMeta.focalLength);
        }
        if (!m_imageMeta.dateTaken.empty()) {
            drawRow(L"Date Taken", m_imageMeta.dateTaken);
        }
    } else if (m_docType == DocumentType::Pdf) {
        WCHAR pageBuf[32];
        swprintf_s(pageBuf, L"%d pages", m_totalPages);
        drawRow(L"Total Pages", pageBuf);

        WCHAR curPageBuf[32];
        swprintf_s(curPageBuf, L"Page %d", m_currentPage + 1);
        drawRow(L"Current Page", curPageBuf);

        WCHAR sizeBuf[64];
        swprintf_s(sizeBuf, L"%.0f × %.0f pt", m_pdfPageWidth, m_pdfPageHeight);
        drawRow(L"Page Size", sizeBuf);
    } else if (m_docType == DocumentType::Media) {
        drawRow(L"Media Type", m_isAudioOnly ? L"Audio Stream" : L"Video Stream");

        if (!m_isAudioOnly && m_videoWidth > 0 && m_videoHeight > 0) {
            WCHAR resBuf[64];
            swprintf_s(resBuf, L"%d × %d px", m_videoWidth, m_videoHeight);
            drawRow(L"Resolution", resBuf);

            int gcdW = m_videoWidth, gcdH = m_videoHeight;
            while (gcdH != 0) { int t = gcdH; gcdH = gcdW % gcdH; gcdW = t; }
            if (gcdW > 0 && m_videoWidth / gcdW <= 21 && m_videoHeight / gcdW <= 9) {
                WCHAR arBuf[32];
                swprintf_s(arBuf, L"%d:%d", m_videoWidth / gcdW, m_videoHeight / gcdW);
                drawRow(L"Aspect Ratio", arBuf);
            }
        }

        libvlc_time_t totalLen = m_media.GetLength();
        if (totalLen > 0) {
            std::wstring dur = m_media.FormatTime(totalLen);
            drawRow(L"Duration", dur);
        }

        WCHAR volBuf[32];
        swprintf_s(volBuf, L"%d%% %s", m_media.GetVolume(), m_media.IsMuted() ? L"(Muted)" : L"");
        drawRow(L"Volume", volBuf);

        drawRow(L"Looping", m_isLooping ? L"Enabled" : L"Disabled");
    } else if (m_docType == DocumentType::Text) {
        drawRow(L"File Format", m_text.GetLanguageName());
        drawRow(L"Total Lines", std::to_wstring(m_text.GetLineCount()));
        drawRow(L"Encoding", m_text.GetEncodingName());
        drawRow(L"Word Wrap", m_text.IsWordWrap() ? L"Enabled" : L"Disabled");
    } else if (m_docType == DocumentType::Spreadsheet) {
        drawRow(L"Format", L"Delimited Spreadsheet");
        drawRow(L"Columns", std::to_wstring(m_text.GetColumnCount()));
        drawRow(L"Rows", std::to_wstring(m_text.GetRowCount()));
        drawRow(L"View Mode", m_text.IsTableView() ? L"Tabular Grid" : L"Raw Delimited");
    } else if (m_docType == DocumentType::Office) {
        drawRow(L"Format", m_formatBadgeText);
        WCHAR dimBuf[64];
        swprintf_s(dimBuf, L"%u × %u px", m_officeWidth, m_officeHeight);
        drawRow(L"Slide Preview", dimBuf);
    }

    drawRow(L"Folder Nav", L"Arrow keys (← / →)");
}

void AppWindow::CopyInspectorMetadata() {
    std::wstring text;
    text += L"File Name: " + m_fileName + L"\r\n";
    if (!m_currentFilePath.empty()) {
        text += L"Path: " + m_currentFilePath + L"\r\n";
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(m_currentFilePath.c_str(), GetFileExInfoStandard, &fad)) {
        ULONGLONG sizeBytes = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        WCHAR szBuf[64];
        if (sizeBytes < 1024) swprintf_s(szBuf, L"%lld bytes", (long long)sizeBytes);
        else if (sizeBytes < 1024 * 1024) swprintf_s(szBuf, L"%.1f KB (%lld bytes)", (float)sizeBytes / 1024.0f, (long long)sizeBytes);
        else swprintf_s(szBuf, L"%.2f MB (%lld bytes)", (float)sizeBytes / (1024.0f * 1024.0f), (long long)sizeBytes);
        text += L"File Size: ";
        text += szBuf;
        text += L"\r\n";
    }

    if (m_docType == DocumentType::Image) {
        WCHAR dimBuf[64];
        swprintf_s(dimBuf, L"%u x %u px", m_imageWidth, m_imageHeight);
        text += L"Dimensions: "; text += dimBuf; text += L"\r\n";
        if (m_imageMeta.dpiX > 0.0f) {
            WCHAR dpiBuf[32];
            swprintf_s(dpiBuf, L"%.0f x %.0f DPI", m_imageMeta.dpiX, m_imageMeta.dpiY);
            text += L"Resolution: "; text += dpiBuf; text += L"\r\n";
        }
        if (!m_imageMeta.cameraMake.empty() || !m_imageMeta.cameraModel.empty()) {
            text += L"Device: " + m_imageMeta.cameraMake + L" " + m_imageMeta.cameraModel + L"\r\n";
        }
        if (!m_imageMeta.dateTaken.empty()) {
            text += L"Date Taken: " + m_imageMeta.dateTaken + L"\r\n";
        }
    } else if (m_docType == DocumentType::Pdf) {
        text += L"Total Pages: " + std::to_wstring(m_totalPages) + L"\r\n";
        text += L"Current Page: " + std::to_wstring(m_currentPage + 1) + L"\r\n";
        WCHAR sizeBuf[64];
        swprintf_s(sizeBuf, L"%.0f x %.0f pt", m_pdfPageWidth, m_pdfPageHeight);
        text += L"Page Size: "; text += sizeBuf; text += L"\r\n";
    } else if (m_docType == DocumentType::Media) {
        text += L"Media Type: "; text += m_isAudioOnly ? L"Audio" : L"Video"; text += L"\r\n";
        if (!m_isAudioOnly && m_videoWidth > 0 && m_videoHeight > 0) {
            WCHAR resBuf[64];
            swprintf_s(resBuf, L"%d x %d px", m_videoWidth, m_videoHeight);
            text += L"Resolution: "; text += resBuf; text += L"\r\n";
        }
        libvlc_time_t totalLen = m_media.GetLength();
        if (totalLen > 0) {
            text += L"Duration: " + m_media.FormatTime(totalLen) + L"\r\n";
        }
    } else if (m_docType == DocumentType::Text) {
        text += L"Format: " + m_text.GetLanguageName() + L"\r\n";
        text += L"Lines: " + std::to_wstring(m_text.GetLineCount()) + L"\r\n";
        text += L"Encoding: " + m_text.GetEncodingName() + L"\r\n";
    } else if (m_docType == DocumentType::Spreadsheet) {
        text += L"Columns: " + std::to_wstring(m_text.GetColumnCount()) + L"\r\n";
        text += L"Rows: " + std::to_wstring(m_text.GetRowCount()) + L"\r\n";
    }

    CopyTextToClipboard(m_hwnd, text);
    ShowToast(L"Metadata copied to clipboard");
}

void AppWindow::CopySelectedText() {
    if (m_docType != DocumentType::Pdf || m_selStartIndex < 0 || m_selEndIndex < 0 || m_selStartIndex == m_selEndIndex) return;

    std::wstring text = m_pdf.GetSelectedText(m_selStartIndex, m_selEndIndex);
    if (text.empty()) return;

    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.length() + 1) * sizeof(WCHAR);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            memcpy(GlobalLock(hMem), text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

void AppWindow::ToggleSearch() {
    if (m_docType != DocumentType::Pdf) return;
    m_showSearch = !m_showSearch;
    if (m_showSearch) {
        m_searchMatches.clear();
        m_currentMatchIndex = -1;
    } else {
        m_searchBarRect = {};
        m_searchPrevBtn = {};
        m_searchNextBtn = {};
        m_searchCloseBtn = {};
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ExecuteSearch() {
    if (m_docType != DocumentType::Pdf) return;
    if (m_searchQuery.empty()) {
        m_searchMatches.clear();
        m_currentMatchIndex = -1;
        InvalidateRect(m_hwnd, NULL, FALSE);
        return;
    }

    m_searchMatches = m_pdf.Search(m_searchQuery, false, false);
    if (!m_searchMatches.empty()) {
        m_currentMatchIndex = 0;
        for (size_t i = 0; i < m_searchMatches.size(); i++) {
            if (m_searchMatches[i].pageIndex >= m_currentPage) {
                m_currentMatchIndex = (int)i;
                break;
            }
        }
        if (m_searchMatches[m_currentMatchIndex].pageIndex != m_currentPage) {
            SetPage(m_searchMatches[m_currentMatchIndex].pageIndex);
        }
    } else {
        m_currentMatchIndex = -1;
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::NextSearchMatch() {
    if (m_searchMatches.empty()) return;
    m_currentMatchIndex = (m_currentMatchIndex + 1) % (int)m_searchMatches.size();
    if (m_searchMatches[m_currentMatchIndex].pageIndex != m_currentPage) {
        SetPage(m_searchMatches[m_currentMatchIndex].pageIndex);
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::PrevSearchMatch() {
    if (m_searchMatches.empty()) return;
    m_currentMatchIndex = (m_currentMatchIndex - 1 + (int)m_searchMatches.size()) % (int)m_searchMatches.size();
    if (m_searchMatches[m_currentMatchIndex].pageIndex != m_currentPage) {
        SetPage(m_searchMatches[m_currentMatchIndex].pageIndex);
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ToggleCrop() {
    if (m_docType != DocumentType::Image || m_imageWidth == 0 || m_imageHeight == 0) return;
    m_isCropActive = !m_isCropActive;
    if (m_isCropActive) {
        float padX = (float)m_imageWidth * 0.1f;
        float padY = (float)m_imageHeight * 0.1f;
        m_cropRect = D2D1::RectF(padX, padY, (float)m_imageWidth - padX, (float)m_imageHeight - padY);
    }
    m_cropDragHandle = 0;
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::CancelCrop() {
    m_isCropActive = false;
    m_cropDragHandle = 0;
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ApplyCrop() {
    if (!m_isCropActive || m_docType != DocumentType::Image) return;
    SaveImageFile(true);
}

void AppWindow::SaveImageFile(bool promptSaveAs) {
    if (m_docType != DocumentType::Image || m_currentFilePath.empty()) return;

    WCHAR szFile[MAX_PATH] = {};
    if (promptSaveAs) {
        std::wstring defaultName = m_fileName;
        size_t dot = defaultName.find_last_of(L'.');
        if (m_isCropActive) {
            if (dot != std::wstring::npos) {
                defaultName = defaultName.substr(0, dot) + L"-cropped" + defaultName.substr(dot);
            } else {
                defaultName += L"-cropped.png";
            }
        }
        wcsncpy_s(szFile, defaultName.c_str(), MAX_PATH - 1);

        OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0JPEG Image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0Bitmap (*.bmp)\0*.bmp\0TIFF Image (*.tif;*.tiff)\0*.tif;*.tiff\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = L"png";

        if (!GetSaveFileNameW(&ofn)) return;
    } else {
        wcsncpy_s(szFile, m_currentFilePath.c_str(), MAX_PATH - 1);
    }

    std::wstring destPath = szFile;
    GUID container = GUID_ContainerFormatPng;
    std::wstring destExt;
    size_t dDot = destPath.find_last_of(L'.');
    if (dDot != std::wstring::npos) {
        destExt = destPath.substr(dDot);
        std::transform(destExt.begin(), destExt.end(), destExt.begin(), ::towlower);
    }
    if (destExt == L".jpg" || destExt == L".jpeg") container = GUID_ContainerFormatJpeg;
    else if (destExt == L".bmp") container = GUID_ContainerFormatBmp;
    else if (destExt == L".tif" || destExt == L".tiff") container = GUID_ContainerFormatTiff;

    bool saved = WicDecoder::SaveImage(
        m_d2d.GetWICFactory(),
        m_currentFilePath,
        destPath,
        m_isCropActive ? &m_cropRect : nullptr,
        m_rotation,
        m_flipH,
        m_flipV,
        0.92f
    );
    if (saved) {
        m_isCropActive = false;
        m_cropDragHandle = 0;
        OpenFile(destPath);
    }
}

void AppWindow::ToggleInspector() {
    m_showInspector = !m_showInspector;
    if (!m_showInspector) {
        m_inspectorRect = {};
        m_inspectorCloseRect = {};
        m_inspectorCopyRect = {};
    } else {
        WakeAllUiAndCursor("ToggleInspector");
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ToggleSettings() {
    m_showSettings = !m_showSettings;
    m_isRecordingKeybind = false;
    ExplorerHook::SetSuppressSpaceClose(m_showSettings);
    if (m_showSettings) {
        RECT rc = {};
        GetWindowRect(m_hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int minW = (int)(640.0f * m_dpiScale + 0.5f);
        int minH = (int)(540.0f * m_dpiScale + 0.5f);
        if (w < minW || h < minH) {
            int newW = (std::max)(w, minW);
            int newH = (std::max)(h, minH);
            int newX = rc.left - (newW - w) / 2;
            int newY = rc.top - (newH - h) / 2;
            if (newX < 10) newX = 10;
            if (newY < 10) newY = 10;
            SetWindowPos(m_hwnd, NULL, newX, newY, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    } else {
        m_settingsModalRect = {};
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::TogglePreviewFromHook() {
    PostMessageW(m_hwnd, ExplorerHook::WM_WINPREVIEW_EXPLORER_SPACE, 0, 0);
}

void AppWindow::RenderSettingsModal(const D2D1_RECT_F& canvasRect) {
    if (!m_showSettings) {
        m_settingsModalRect = {};
        m_settingsCloseBtn = {};
        m_settingsRecordBtn = {};
        m_settingsResetBtn = {};
        m_settingsDoneBtn = {};
        m_settingsAutostartBtn = {};
        for (int i = 0; i < 5; ++i) m_settingsPresetRects[i] = {};
        return;
    }

    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    // 1. Full-screen backdrop tint (#000000 at 55% opacity)
    m_d2d.BrushShadowDark->SetOpacity(0.55f);
    rt->FillRectangle(canvasRect, m_d2d.BrushShadowDark);
    m_d2d.BrushShadowDark->SetOpacity(0.40f);

    float availW = canvasRect.right - canvasRect.left;
    float availH = canvasRect.bottom - canvasRect.top;
    float modalW = (std::min)(520.0f, availW - 32.0f);
    float modalH = (std::min)(440.0f, availH - 32.0f);
    float cx = canvasRect.left + availW / 2.0f;
    float cy = canvasRect.top + availH / 2.0f;

    m_settingsModalRect = D2D1::RectF(cx - modalW / 2.0f, cy - modalH / 2.0f, cx + modalW / 2.0f, cy + modalH / 2.0f);

    // 2. Card Drop Shadow
    D2D1_ROUNDED_RECT shadowR = D2D1::RoundedRect(
        D2D1::RectF(m_settingsModalRect.left - 6.0f, m_settingsModalRect.top + 4.0f, m_settingsModalRect.right + 6.0f, m_settingsModalRect.bottom + 14.0f),
        16.0f, 16.0f
    );
    rt->FillRoundedRectangle(shadowR, m_d2d.BrushShadowSoft);

    // 3. Card Surface & Border
    D2D1_ROUNDED_RECT cardR = D2D1::RoundedRect(m_settingsModalRect, 14.0f, 14.0f);
    m_d2d.BrushCard->SetOpacity(0.97f);
    rt->FillRoundedRectangle(cardR, m_d2d.BrushCard);
    m_d2d.BrushCard->SetOpacity(1.0f);
    rt->DrawRoundedRectangle(cardR, m_d2d.BrushBorder, 1.2f);

    // 4. Header Bar
    float curY = m_settingsModalRect.top + 20.0f;
    float padX = 24.0f;
    float iconCx = m_settingsModalRect.left + padX + 14.0f;
    float iconCy = curY + 14.0f;
    m_d2d.DrawIconSettings(iconCx, iconCy, 18.0f, m_d2d.BrushAccent);

    // Title & Subtitle
    D2D1_RECT_F titleR = D2D1::RectF(iconCx + 20.0f, curY, m_settingsModalRect.right - 60.0f, curY + 22.0f);
    const WCHAR* titleText = L"Preferences & Keybinds";
    rt->DrawText(titleText, (UINT32)wcslen(titleText), m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption, titleR, m_d2d.BrushText);

    D2D1_RECT_F subR = D2D1::RectF(iconCx + 20.0f, curY + 22.0f, m_settingsModalRect.right - 60.0f, curY + 38.0f);
    const WCHAR* subText = L"Customize the global hotkey to preview selected files in File Explorer";
    rt->DrawText(subText, (UINT32)wcslen(subText), m_d2d.FontBadge ? m_d2d.FontBadge : m_d2d.FontCaption, subR, m_d2d.BrushTextMuted);

    // Close button (X)
    m_settingsCloseBtn = D2D1::RectF(m_settingsModalRect.right - 44.0f, curY - 2.0f, m_settingsModalRect.right - 18.0f, curY + 24.0f);
    float closeCx = m_settingsCloseBtn.left + 13.0f;
    float closeCy = m_settingsCloseBtn.top + 13.0f;
    if (m_hoveredButtonId == BTN_SETTINGS_CLOSE) {
        rt->FillRoundedRectangle(D2D1::RoundedRect(m_settingsCloseBtn, 6.0f, 6.0f), m_d2d.BrushHover);
    }
    rt->DrawLine(D2D1::Point2F(closeCx - 5.0f, closeCy - 5.0f), D2D1::Point2F(closeCx + 5.0f, closeCy + 5.0f), m_d2d.BrushTextMuted, 1.4f);
    rt->DrawLine(D2D1::Point2F(closeCx + 5.0f, closeCy - 5.0f), D2D1::Point2F(closeCx - 5.0f, closeCy + 5.0f), m_d2d.BrushTextMuted, 1.4f);

    curY += 50.0f;
    // Divider
    rt->DrawLine(D2D1::Point2F(m_settingsModalRect.left + padX, curY), D2D1::Point2F(m_settingsModalRect.right - padX, curY), m_d2d.BrushBorderSubtle, 1.0f);
    curY += 16.0f;

    // 5. Active Keybind Display & Recorder Section
    D2D1_RECT_F secLabelR = D2D1::RectF(m_settingsModalRect.left + padX, curY, m_settingsModalRect.right - padX, curY + 16.0f);
    const WCHAR* secLabel = L"CURRENT KEYBIND";
    rt->DrawText(secLabel, (UINT32)wcslen(secLabel), m_d2d.FontBadge ? m_d2d.FontBadge : m_d2d.FontCaption, secLabelR, m_d2d.BrushTextMuted);
    curY += 20.0f;

    // Inset Card for Keybind Display (Height 72px)
    float keyCardW = modalW - padX * 2.0f;
    float keyCardH = 72.0f;
    D2D1_RECT_F keyCardRect = D2D1::RectF(m_settingsModalRect.left + padX, curY, m_settingsModalRect.left + padX + keyCardW, curY + keyCardH);
    D2D1_ROUNDED_RECT keyCardR = D2D1::RoundedRect(keyCardRect, 8.0f, 8.0f);
    rt->FillRoundedRectangle(keyCardR, m_d2d.BrushSidebar);
    if (m_isRecordingKeybind) {
        rt->DrawRoundedRectangle(keyCardR, m_d2d.BrushAccent, 1.6f);
    } else {
        rt->DrawRoundedRectangle(keyCardR, m_d2d.BrushBorderSubtle, 1.0f);
    }

    HotkeyConfig curCfg = ExplorerHook::GetHotkeyConfig();

    // Keybind Badge pill
    float badgeH = 38.0f;
    float badgeY = curY + (keyCardH - badgeH) / 2.0f;
    float badgeX = m_settingsModalRect.left + padX + 16.0f;
    float badgeW = 210.0f;
    D2D1_RECT_F badgeRect = D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
    D2D1_ROUNDED_RECT badgeR = D2D1::RoundedRect(badgeRect, 6.0f, 6.0f);

    if (m_isRecordingKeybind) {
        m_d2d.BrushActiveMatch->SetOpacity(0.30f);
        rt->FillRoundedRectangle(badgeR, m_d2d.BrushActiveMatch);
        m_d2d.BrushActiveMatch->SetOpacity(1.0f);
        rt->DrawRoundedRectangle(badgeR, m_d2d.BrushAccent, 1.5f);

        const WCHAR* recText = L"Press any key...";
        D2D1_RECT_F recTextR = D2D1::RectF(badgeX, badgeY + 10.0f, badgeX + badgeW, badgeY + badgeH);
        rt->DrawText(recText, (UINT32)wcslen(recText), m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption, recTextR, m_d2d.BrushAccent);
    } else {
        rt->FillRoundedRectangle(badgeR, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(badgeR, m_d2d.BrushBorder, 1.0f);

        std::wstring keyStr = L"[  " + curCfg.name + L"  ]";
        D2D1_RECT_F keyTextR = D2D1::RectF(badgeX, badgeY + 8.0f, badgeX + badgeW, badgeY + badgeH);
        rt->DrawText(keyStr.c_str(), (UINT32)keyStr.length(), m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption, keyTextR, m_d2d.BrushText);
    }

    // "Record Shortcut" Button on right of inset card
    float recBtnW = 150.0f;
    float recBtnH = 34.0f;
    float recBtnX = keyCardRect.right - recBtnW - 16.0f;
    float recBtnY = curY + (keyCardH - recBtnH) / 2.0f;
    m_settingsRecordBtn = D2D1::RectF(recBtnX, recBtnY, recBtnX + recBtnW, recBtnY + recBtnH);
    D2D1_ROUNDED_RECT recBtnR = D2D1::RoundedRect(m_settingsRecordBtn, 6.0f, 6.0f);

    if (m_isRecordingKeybind) {
        rt->FillRoundedRectangle(recBtnR, m_d2d.BrushActive);
        rt->DrawRoundedRectangle(recBtnR, m_d2d.BrushBorder, 1.0f);
        const WCHAR* stopRecText = L"Cancel";
        D2D1_RECT_F tR = D2D1::RectF(recBtnX, recBtnY + 8.0f, recBtnX + recBtnW, recBtnY + recBtnH);
        rt->DrawText(stopRecText, (UINT32)wcslen(stopRecText), m_d2d.FontCaption, tR, m_d2d.BrushText);
    } else {
        if (m_hoveredButtonId == BTN_SETTINGS_RECORD) {
            rt->FillRoundedRectangle(recBtnR, m_d2d.BrushHover);
        } else {
            rt->FillRoundedRectangle(recBtnR, m_d2d.BrushCard);
        }
        rt->DrawRoundedRectangle(recBtnR, m_d2d.BrushBorder, 1.0f);
        const WCHAR* startRecText = L"Record Shortcut";
        D2D1_RECT_F tR = D2D1::RectF(recBtnX, recBtnY + 8.0f, recBtnX + recBtnW, recBtnY + recBtnH);
        rt->DrawText(startRecText, (UINT32)wcslen(startRecText), m_d2d.FontCaption, tR, m_d2d.BrushText);
    }

    curY += keyCardH + 18.0f;

    // 6. Quick Presets Section
    D2D1_RECT_F presetLabelR = D2D1::RectF(m_settingsModalRect.left + padX, curY, m_settingsModalRect.right - padX, curY + 16.0f);
    const WCHAR* presetLabel = L"QUICK PRESETS";
    rt->DrawText(presetLabel, (UINT32)wcslen(presetLabel), m_d2d.FontBadge ? m_d2d.FontBadge : m_d2d.FontCaption, presetLabelR, m_d2d.BrushTextMuted);
    curY += 20.0f;

    const wchar_t* presetNames[5] = { L"Space", L"Ctrl + Space", L"Alt + Space", L"Shift + Space", L"F4" };
    float chipGap = 8.0f;
    float chipW = (keyCardW - chipGap * 4.0f) / 5.0f;
    float chipH = 32.0f;

    for (int i = 0; i < 5; ++i) {
        float chipX = m_settingsModalRect.left + padX + i * (chipW + chipGap);
        m_settingsPresetRects[i] = D2D1::RectF(chipX, curY, chipX + chipW, curY + chipH);
        D2D1_ROUNDED_RECT chipR = D2D1::RoundedRect(m_settingsPresetRects[i], 6.0f, 6.0f);

        bool isCurrentPreset = false;
        if (i == 0 && curCfg.vkCode == VK_SPACE && !curCfg.ctrl && !curCfg.alt && !curCfg.shift && !curCfg.win) isCurrentPreset = true;
        else if (i == 1 && curCfg.vkCode == VK_SPACE && curCfg.ctrl && !curCfg.alt && !curCfg.shift && !curCfg.win) isCurrentPreset = true;
        else if (i == 2 && curCfg.vkCode == VK_SPACE && !curCfg.ctrl && curCfg.alt && !curCfg.shift && !curCfg.win) isCurrentPreset = true;
        else if (i == 3 && curCfg.vkCode == VK_SPACE && !curCfg.ctrl && !curCfg.alt && curCfg.shift && !curCfg.win) isCurrentPreset = true;
        else if (i == 4 && curCfg.vkCode == VK_F4 && !curCfg.ctrl && !curCfg.alt && !curCfg.shift && !curCfg.win) isCurrentPreset = true;

        if (isCurrentPreset) {
            rt->FillRoundedRectangle(chipR, m_d2d.BrushCardSelected);
            rt->DrawRoundedRectangle(chipR, m_d2d.BrushAccent, 1.2f);
        } else if (m_hoveredButtonId == (BTN_SETTINGS_PRESET_1 + i)) {
            rt->FillRoundedRectangle(chipR, m_d2d.BrushHover);
            rt->DrawRoundedRectangle(chipR, m_d2d.BrushBorder, 1.0f);
        } else {
            rt->FillRoundedRectangle(chipR, m_d2d.BrushCard);
            rt->DrawRoundedRectangle(chipR, m_d2d.BrushBorderSubtle, 1.0f);
        }

        D2D1_RECT_F textR = D2D1::RectF(chipX, curY + 7.0f, chipX + chipW, curY + chipH);
        rt->DrawText(presetNames[i], (UINT32)wcslen(presetNames[i]), m_d2d.FontBadge ? m_d2d.FontBadge : m_d2d.FontCaption, textR,
                     isCurrentPreset ? (ID2D1Brush*)m_d2d.BrushAccent : (ID2D1Brush*)m_d2d.BrushText);
    }

    curY += chipH + 20.0f;

    // 7. System Autostart Option
    m_settingsAutostartBtn = D2D1::RectF(m_settingsModalRect.left + padX, curY, m_settingsModalRect.right - padX, curY + 40.0f);
    if (m_hoveredButtonId == BTN_SETTINGS_AUTOSTART) {
        rt->FillRoundedRectangle(D2D1::RoundedRect(m_settingsAutostartBtn, 6.0f, 6.0f), m_d2d.BrushHover);
    }

    bool autoStart = IsAutoStartEnabled();
    float boxSz = 18.0f;
    float boxX = m_settingsModalRect.left + padX + 8.0f;
    float boxY = curY + 11.0f;
    D2D1_ROUNDED_RECT chkR = D2D1::RoundedRect(D2D1::RectF(boxX, boxY, boxX + boxSz, boxY + boxSz), 4.0f, 4.0f);
    if (autoStart) {
        rt->FillRoundedRectangle(chkR, m_d2d.BrushAccent);
        rt->DrawLine(D2D1::Point2F(boxX + 4.0f, boxY + 9.0f), D2D1::Point2F(boxX + 7.5f, boxY + 13.0f), m_d2d.BrushText, 1.8f);
        rt->DrawLine(D2D1::Point2F(boxX + 7.5f, boxY + 13.0f), D2D1::Point2F(boxX + 14.0f, boxY + 5.0f), m_d2d.BrushText, 1.8f);
    } else {
        rt->FillRoundedRectangle(chkR, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(chkR, m_d2d.BrushBorder, 1.2f);
    }

    D2D1_RECT_F chkLabelR = D2D1::RectF(boxX + boxSz + 12.0f, curY + 5.0f, m_settingsModalRect.right - padX, curY + 22.0f);
    const WCHAR* chkTitle = L"Start WinPreview with Windows (Auto-start on boot)";
    rt->DrawText(chkTitle, (UINT32)wcslen(chkTitle), m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption, chkLabelR, m_d2d.BrushText);

    D2D1_RECT_F chkSubR = D2D1::RectF(boxX + boxSz + 12.0f, curY + 22.0f, m_settingsModalRect.right - padX, curY + 38.0f);
    const WCHAR* chkSub = L"Runs quietly in the background so your preview shortcut always responds";
    rt->DrawText(chkSub, (UINT32)wcslen(chkSub), m_d2d.FontBadge ? m_d2d.FontBadge : m_d2d.FontCaption, chkSubR, m_d2d.BrushTextMuted);

    curY += 46.0f;

    // 8. Bottom Footer with Reset and Done buttons
    rt->DrawLine(D2D1::Point2F(m_settingsModalRect.left + padX, curY), D2D1::Point2F(m_settingsModalRect.right - padX, curY), m_d2d.BrushBorderSubtle, 1.0f);
    curY += 14.0f;

    // Status Indicator Dot (Green)
    float dotX = m_settingsModalRect.left + padX + 8.0f;
    float dotY = curY + 14.0f;
    D2D1_ELLIPSE statusDot = D2D1::Ellipse(D2D1::Point2F(dotX, dotY), 4.0f, 4.0f);
    rt->FillEllipse(statusDot, m_d2d.BrushAccent);
    D2D1_RECT_F statusTextR = D2D1::RectF(dotX + 10.0f, curY + 6.0f, dotX + 250.0f, curY + 24.0f);
    const WCHAR* statusMsg = L"Hook Active & Ready";
    rt->DrawText(statusMsg, (UINT32)wcslen(statusMsg), m_d2d.FontBadge ? m_d2d.FontBadge : m_d2d.FontCaption, statusTextR, m_d2d.BrushAccent);

    // Reset button
    float rstW = 120.0f;
    float rstH = 32.0f;
    float rstX = m_settingsModalRect.right - padX - 100.0f - 12.0f - rstW;
    m_settingsResetBtn = D2D1::RectF(rstX, curY, rstX + rstW, curY + rstH);
    D2D1_ROUNDED_RECT rstR = D2D1::RoundedRect(m_settingsResetBtn, 6.0f, 6.0f);
    if (m_hoveredButtonId == BTN_SETTINGS_RESET) {
        rt->FillRoundedRectangle(rstR, m_d2d.BrushHover);
    } else {
        rt->FillRoundedRectangle(rstR, m_d2d.BrushCard);
    }
    rt->DrawRoundedRectangle(rstR, m_d2d.BrushBorder, 1.0f);
    D2D1_RECT_F rstTextR = D2D1::RectF(rstX, curY + 7.0f, rstX + rstW, curY + rstH);
    const WCHAR* rstText = L"Reset to Space";
    rt->DrawText(rstText, (UINT32)wcslen(rstText), m_d2d.FontCaption, rstTextR, m_d2d.BrushTextMuted);

    // Done button
    float doneW = 100.0f;
    float doneH = 32.0f;
    float doneX = m_settingsModalRect.right - padX - doneW;
    m_settingsDoneBtn = D2D1::RectF(doneX, curY, doneX + doneW, curY + doneH);
    D2D1_ROUNDED_RECT doneR = D2D1::RoundedRect(m_settingsDoneBtn, 6.0f, 6.0f);
    rt->FillRoundedRectangle(doneR, m_d2d.BrushAccent);
    D2D1_RECT_F doneTextR = D2D1::RectF(doneX, curY + 7.0f, doneX + doneW, curY + doneH);
    const WCHAR* doneText = L"Done";
    rt->DrawText(doneText, (UINT32)wcslen(doneText), m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption, doneTextR, m_d2d.BrushText);
}

bool AppWindow::CreateHudWindow() {
    if (m_hudHwnd) return true;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = HudWndProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"WinPreviewMediaHud";
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    int hudW = (int)(660.0f * m_dpiScale + 0.5f);
    int hudH = (int)(54.0f * m_dpiScale + 0.5f);
    int cornerR = (int)(26.0f * m_dpiScale + 0.5f);

    m_hudHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"WinPreviewMediaHud",
        L"",
        WS_POPUP,
        -9999, -9999, hudW, hudH,
        m_hwnd,
        NULL,
        m_hInstance,
        this
    );

    if (!m_hudHwnd) return false;
    SetWindowLongPtr(m_hudHwnd, GWLP_USERDATA, (LONG_PTR)this);
    ShowWindow(m_hudHwnd, SW_HIDE);

    HRGN hRgn = CreateRoundRectRgn(0, 0, hudW + 1, hudH + 1, cornerR, cornerR);
    SetWindowRgn(m_hudHwnd, hRgn, TRUE);

    SetLayeredWindowAttributes(m_hudHwnd, 0, 0, LWA_ALPHA);

    // Create D2D Render Target for HUD
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * m_dpiScale, 96.0f * m_dpiScale
    );
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_hudHwnd, D2D1::SizeU((UINT32)hudW, (UINT32)hudH), D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS
    );

    HRESULT hr = m_d2d.GetD2DFactory()->CreateHwndRenderTarget(rtProps, hwndProps, &m_hudRenderTarget);
    if (FAILED(hr)) return false;

    m_hudRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_hudRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    // Create HUD solid color brushes
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.094f, 0.094f, 0.106f, 0.96f), &m_hudBrushBg);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.180f, 0.180f, 0.200f, 1.0f), &m_hudBrushBorder);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.965f, 0.965f, 0.973f, 1.0f), &m_hudBrushText);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.580f, 0.580f, 0.620f, 1.0f), &m_hudBrushTextMuted);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.039f, 0.518f, 1.000f, 1.0f), &m_hudBrushAccent);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.039f, 0.518f, 1.000f, 0.35f), &m_hudBrushAccentGlow);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f), &m_hudBrushHover);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f), &m_hudBrushActive);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.165f, 0.165f, 0.180f, 1.0f), &m_hudBrushTrack);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.063f, 0.063f, 0.071f, 0.96f), &m_hudBrushTooltipBg);
    m_hudRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.240f, 0.240f, 0.260f, 1.0f), &m_hudBrushTooltipBorder);

    return true;
}

void AppWindow::DestroyHudWindow() {
    SafeRelease(m_hudBrushBg);
    SafeRelease(m_hudBrushBorder);
    SafeRelease(m_hudBrushText);
    SafeRelease(m_hudBrushTextMuted);
    SafeRelease(m_hudBrushAccent);
    SafeRelease(m_hudBrushAccentGlow);
    SafeRelease(m_hudBrushHover);
    SafeRelease(m_hudBrushActive);
    SafeRelease(m_hudBrushTrack);
    SafeRelease(m_hudBrushTooltipBg);
    SafeRelease(m_hudBrushTooltipBorder);
    SafeRelease(m_hudRenderTarget);
    if (m_hudHwnd) {
        DestroyWindow(m_hudHwnd);
        m_hudHwnd = nullptr;
    }
}

void AppWindow::UpdateHudPosition() {
    if (!m_hudHwnd || m_docType != DocumentType::Media || !IsWindowVisible(m_hwnd) || IsIconic(m_hwnd)) {
        if (m_hudHwnd && IsWindowVisible(m_hudHwnd)) ShowWindow(m_hudHwnd, SW_HIDE);
        return;
    }
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int clientW = rc.right - rc.left;
    int clientH = rc.bottom - rc.top;

    float maxHudWDip = 660.0f;
    float clientWDip = PxToDip(clientW);
    float hudWDip = (std::min)(maxHudWDip, clientWDip - 24.0f);
    if (hudWDip < 320.0f) hudWDip = (std::max)(280.0f, clientWDip - 12.0f);
    float hudHDip = 54.0f;

    int hudW = (int)(hudWDip * m_dpiScale + 0.5f);
    int hudH = (int)(hudHDip * m_dpiScale + 0.5f);

    POINT pt = { (clientW - hudW) / 2, clientH - hudH - (int)(24.0f * m_dpiScale + 0.5f) };
    ClientToScreen(m_hwnd, &pt);

    RECT currentHudRc;
    GetWindowRect(m_hudHwnd, &currentHudRc);
    int curW = currentHudRc.right - currentHudRc.left;
    int curH = currentHudRc.bottom - currentHudRc.top;

    int cornerR = (int)(26.0f * m_dpiScale + 0.5f);
    if (curW != hudW || curH != hudH) {
        SetWindowPos(m_hudHwnd, HWND_TOP, pt.x, pt.y, hudW, hudH, SWP_NOACTIVATE);
        HRGN hRgn = CreateRoundRectRgn(0, 0, hudW + 1, hudH + 1, cornerR, cornerR);
        SetWindowRgn(m_hudHwnd, hRgn, TRUE);
        if (m_hudRenderTarget) {
            m_hudRenderTarget->Resize(D2D1::SizeU((UINT32)hudW, (UINT32)hudH));
            m_hudRenderTarget->SetDpi(96.0f * m_dpiScale, 96.0f * m_dpiScale);
        }
    } else {
        SetWindowPos(m_hudHwnd, HWND_TOP, pt.x, pt.y, hudW, hudH, SWP_NOACTIVATE | SWP_NOSIZE);
    }
}

void AppWindow::HideMainWindow() {
    if (m_hudHwnd) {
        ShowWindow(m_hudHwnd, SW_HIDE);
    }
    HideUpdatePopup();
    m_hudOpacity = 0.0f;
    m_hudTargetOpacity = 0.0f;
    if (m_docType == DocumentType::Media) {
        if (m_media.IsPlaying()) {
            m_media.Pause();
        }
        m_media.ShowVideoWindow(false);
    }
    m_isScrubbing = false;
    m_isVolDragging = false;
    m_isWaveformSeeking = false;
    if (GetCapture() == m_hwnd || (m_hudHwnd && GetCapture() == m_hudHwnd)) {
        ReleaseCapture();
    }
    KillTimer(m_hwnd, 1);
    ShowWindow(m_hwnd, SW_HIDE);
}

void AppWindow::SetCursorVisibility(bool visible) {
    if (m_cursorVisible == visible) return;
    m_cursorVisible = visible;
    if (visible) {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    } else {
        SetCursor(NULL);
    }
}

void AppWindow::WakeAllUiAndCursor(const char* reason) {
    m_inactivityMs = 0;
    m_dockTargetOpacity = 1.0f;

    if (m_docType == DocumentType::Media && IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) {
        m_hudTargetOpacity = 255.0f;
        UpdateHudPosition();
        if (m_hudHwnd) {
            ShowWindow(m_hudHwnd, SW_SHOWNOACTIVATE);
            InvalidateRect(m_hudHwnd, NULL, FALSE);
        }
    } else {
        m_hudTargetOpacity = 0.0f;
        m_hudOpacity = 0.0f;
        if (m_hudHwnd && IsWindowVisible(m_hudHwnd)) {
            ShowWindow(m_hudHwnd, SW_HIDE);
        }
    }

    SetCursorVisibility(true);

    POINT curPos;
    if (GetCursorPos(&curPos)) {
        m_lastGlobalMousePos = curPos;
        POINT clPt = curPos;
        ScreenToClient(m_hwnd, &clPt);
        m_lastActiveMousePos = clPt;
    }

    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::DrawHudIconButton(const D2D1_RECT_F& rect, IconType icon, int buttonId, bool enabled) {
    if (!m_hudRenderTarget) return;
    float cx = rect.left + (rect.right - rect.left) / 2.0f;
    float cy = rect.top + (rect.bottom - rect.top) / 2.0f;

    // Center Hero Play/Pause Button
    if (buttonId == BTN_PLAY_PAUSE) {
        float heroR = std::min((rect.right - rect.left), (rect.bottom - rect.top)) / 2.0f;
        D2D1_ELLIPSE heroCircle = D2D1::Ellipse(D2D1::Point2F(cx, cy), heroR, heroR);

        if (m_hudPressedButtonId == buttonId && enabled) {
            cy += 0.5f;
            m_hudRenderTarget->FillEllipse(heroCircle, m_hudBrushAccent);
        } else if (m_hudHoveredButtonId == buttonId && enabled) {
            D2D1_ELLIPSE glowCircle = D2D1::Ellipse(D2D1::Point2F(cx, cy), heroR + 3.0f, heroR + 3.0f);
            m_hudRenderTarget->FillEllipse(glowCircle, m_hudBrushAccentGlow);
            m_hudRenderTarget->FillEllipse(heroCircle, m_hudBrushAccent);
        } else {
            m_hudRenderTarget->FillEllipse(heroCircle, m_hudBrushAccent);
        }

        ID2D1Brush* iconBrush = (ID2D1Brush*)m_hudBrushText;
        if (icon == ICON_PLAY) {
            m_d2d.DrawIconPlay(cx + 1.2f, cy, 18.0f, iconBrush, m_hudRenderTarget);
        } else {
            m_d2d.DrawIconPause(cx, cy, 18.0f, iconBrush, m_hudRenderTarget);
        }
        return;
    }

    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, 8.0f, 8.0f);
    float iconSize = 16.0f;

    bool isLoopActive = (buttonId == BTN_HUD_LOOP) && m_isLooping;

    if (m_hudPressedButtonId == buttonId && enabled) {
        m_hudRenderTarget->FillRoundedRectangle(rrect, m_hudBrushActive);
        cy += 0.5f;
    } else if (isLoopActive) {
        m_hudRenderTarget->FillRoundedRectangle(rrect, m_hudBrushActive);
        m_hudRenderTarget->DrawRoundedRectangle(rrect, m_hudBrushAccent, 1.2f);
    } else if (m_hudHoveredButtonId == buttonId && enabled) {
        m_hudRenderTarget->FillRoundedRectangle(rrect, m_hudBrushHover);
    }

    ID2D1Brush* brush = isLoopActive ? (ID2D1Brush*)m_hudBrushAccent :
                        (enabled ? (ID2D1Brush*)m_hudBrushText : (ID2D1Brush*)m_hudBrushTextMuted);

    switch (icon) {
        case ICON_PLAY:         m_d2d.DrawIconPlay(cx, cy, iconSize, brush, m_hudRenderTarget); break;
        case ICON_PAUSE:        m_d2d.DrawIconPause(cx, cy, iconSize, brush, m_hudRenderTarget); break;
        case ICON_SKIP_BACK:    m_d2d.DrawIconSkipBack(cx, cy, iconSize, brush, m_hudRenderTarget); break;
        case ICON_SKIP_FORWARD: m_d2d.DrawIconSkipForward(cx, cy, iconSize, brush, m_hudRenderTarget); break;
        case ICON_LOOP:         m_d2d.DrawIconLoop(cx, cy, iconSize, m_isLooping, brush, m_hudRenderTarget); break;
        case ICON_VOLUME:       m_d2d.DrawIconVolume(cx, cy, iconSize, m_media.IsMuted(), brush, m_hudRenderTarget); break;
        case ICON_FULLSCREEN:   m_d2d.DrawIconFullscreen(cx, cy, iconSize, m_isFullscreen, brush, m_hudRenderTarget); break;
        default: break;
    }
}

void AppWindow::RenderHud() {
    if (!m_hudRenderTarget) return;

    m_hudRenderTarget->BeginDraw();
    m_hudRenderTarget->Clear(D2D1::ColorF(0.094f, 0.094f, 0.106f, 0.96f));

    D2D1_SIZE_F sz = m_hudRenderTarget->GetSize();
    float hudW = sz.width;
    float hudH = sz.height;

    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, hudW - 0.5f, hudH - 0.5f), 26.0f, 26.0f);
    m_hudRenderTarget->DrawRoundedRectangle(rrect, m_hudBrushBorder, 1.2f);

    float btnY = (hudH - 32.0f) / 2.0f;
    float btnH = 32.0f;

    // Reset hitboxes
    m_hudBtnSkipBack = {};
    m_hudBtnPlayPause = {};
    m_hudBtnSkipForward = {};
    m_hudScrubber = {};
    m_hudBtnLoop = {};
    m_hudBtnMute = {};
    m_hudVolSlider = {};
    m_hudBtnFullscreen = {};

    float curLeftX = 14.0f;

    // 1. Grouped transport controls on left
    bool showSkipButtons = (hudW >= 420.0f);
    if (showSkipButtons) {
        m_hudBtnSkipBack = D2D1::RectF(curLeftX, btnY, curLeftX + 30.0f, btnY + btnH);
        DrawHudIconButton(m_hudBtnSkipBack, ICON_SKIP_BACK, BTN_SKIP_BACK);
        curLeftX += 34.0f;
    }

    // Play / Pause button
    m_hudBtnPlayPause = D2D1::RectF(curLeftX, btnY, curLeftX + 34.0f, btnY + btnH);
    DrawHudIconButton(m_hudBtnPlayPause, m_media.IsPlaying() ? ICON_PAUSE : ICON_PLAY, BTN_PLAY_PAUSE);
    curLeftX += 38.0f;

    if (showSkipButtons) {
        m_hudBtnSkipForward = D2D1::RectF(curLeftX, btnY, curLeftX + 30.0f, btnY + btnH);
        DrawHudIconButton(m_hudBtnSkipForward, ICON_SKIP_FORWARD, BTN_SKIP_FORWARD);
        curLeftX += 34.0f;
    }

    // Elapsed time readout
    libvlc_time_t curTime = m_media.GetTime();
    std::wstring curTimeStr = m_media.FormatTime(curTime);
    float timeTextW = 44.0f;
    D2D1_RECT_F curTimeRect = D2D1::RectF(curLeftX, btnY + 7.0f, curLeftX + timeTextW, btnY + btnH);
    m_hudRenderTarget->DrawText(curTimeStr.c_str(), (UINT32)curTimeStr.length(), m_d2d.FontCaption, curTimeRect, m_hudBrushText);
    curLeftX += timeTextW + 8.0f;

    // Right controls (laid out from right to left)
    float curRightX = hudW - 14.0f;

    // Fullscreen button
    curRightX -= 30.0f;
    m_hudBtnFullscreen = D2D1::RectF(curRightX, btnY, curRightX + 30.0f, btnY + btnH);
    DrawHudIconButton(m_hudBtnFullscreen, ICON_FULLSCREEN, BTN_FULLSCREEN);

    // Volume slider & mute
    bool showVolSlider = (hudW >= 460.0f);
    bool showMute = (hudW >= 340.0f);

    if (showVolSlider) {
        curRightX -= 8.0f;
        float volW = 50.0f;
        curRightX -= volW;
        float volX = curRightX;
        m_hudVolSlider = D2D1::RectF(volX, btnY + 4.0f, volX + volW, btnY + btnH - 4.0f);
        D2D1_ROUNDED_RECT volBg = D2D1::RoundedRect(D2D1::RectF(volX, btnY + 14.0f, volX + volW, btnY + 18.0f), 2.0f, 2.0f);
        m_hudRenderTarget->FillRoundedRectangle(volBg, m_hudBrushTrack);

        float volFrac = (float)m_media.GetVolume() / 100.0f;
        float volFill = (std::clamp)(volFrac, 0.0f, 1.0f) * volW;
        if (volFill > 0.0f) {
            D2D1_ROUNDED_RECT volFillRect = D2D1::RoundedRect(D2D1::RectF(volX, btnY + 14.0f, volX + volFill, btnY + 18.0f), 2.0f, 2.0f);
            m_hudRenderTarget->FillRoundedRectangle(volFillRect, m_hudBrushText);
            m_hudRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(volX + volFill, btnY + 16.0f), 4.5f, 4.5f), m_hudBrushText);
        }
    }

    if (showMute) {
        curRightX -= 4.0f;
        curRightX -= 30.0f;
        m_hudBtnMute = D2D1::RectF(curRightX, btnY, curRightX + 30.0f, btnY + btnH);
        DrawHudIconButton(m_hudBtnMute, ICON_VOLUME, BTN_MUTE);
    }

    if (hudW >= 520.0f) {
        curRightX -= 4.0f;
        curRightX -= 30.0f;
        m_hudBtnLoop = D2D1::RectF(curRightX, btnY, curRightX + 30.0f, btnY + btnH);
        DrawHudIconButton(m_hudBtnLoop, ICON_LOOP, BTN_HUD_LOOP);
    }

    // Total Duration readout
    libvlc_time_t totalLen = m_media.GetLength();
    std::wstring totalTimeStr = m_media.FormatTime(totalLen);
    curRightX -= (timeTextW + 8.0f);
    D2D1_RECT_F totalTimeRect = D2D1::RectF(curRightX, btnY + 7.0f, curRightX + timeTextW, btnY + btnH);
    m_hudRenderTarget->DrawText(totalTimeStr.c_str(), (UINT32)totalTimeStr.length(), m_d2d.FontCaption, totalTimeRect, m_hudBrushTextMuted);

    // Scrubber rail
    float scrubX = curLeftX + 4.0f;
    float scrubRight = curRightX - 8.0f;
    float scrubW = scrubRight - scrubX;
    float scrubY = hudH / 2.0f;

    if (scrubW >= 40.0f) {
        m_hudScrubber = D2D1::RectF(scrubX, scrubY - 12.0f, scrubX + scrubW, scrubY + 12.0f);

        bool isHoveringScrubber = (m_hudHoveredButtonId == 100 || m_isScrubbing);
        float railH = isHoveringScrubber ? 6.0f : 4.5f;

        // Background track rail
        D2D1_ROUNDED_RECT bgTrack = D2D1::RoundedRect(
            D2D1::RectF(scrubX, scrubY - railH/2, scrubX + scrubW, scrubY + railH/2), railH/2, railH/2
        );
        m_hudRenderTarget->FillRoundedRectangle(bgTrack, m_hudBrushTrack);

        // Active progress line
        float pos = m_media.GetPosition();
        float progW = (std::clamp)(pos, 0.0f, 1.0f) * scrubW;
        if (progW > 0.0f) {
            D2D1_ROUNDED_RECT progTrack = D2D1::RoundedRect(
                D2D1::RectF(scrubX, scrubY - railH/2, scrubX + progW, scrubY + railH/2), railH/2, railH/2
            );
            m_hudRenderTarget->FillRoundedRectangle(progTrack, m_hudBrushAccent);

            // Thumb knob with Apple-style glow
            float thumbR = isHoveringScrubber ? 7.0f : 5.5f;
            D2D1_POINT_2F thumbPt = D2D1::Point2F(scrubX + progW, scrubY);
            if (isHoveringScrubber) {
                m_hudRenderTarget->FillEllipse(D2D1::Ellipse(thumbPt, thumbR + 3.5f, thumbR + 3.5f), m_hudBrushAccentGlow);
            }
            m_hudRenderTarget->FillEllipse(D2D1::Ellipse(thumbPt, thumbR, thumbR), m_hudBrushText);
        }

        // Interactive Seek Time Preview Tooltip
        if ((m_hudHoveredButtonId == 100 || m_isScrubbing) && totalLen > 0) {
            float seekFrac = (std::clamp)((m_hudMousePos.x - scrubX) / scrubW, 0.0f, 1.0f);
            libvlc_time_t seekTime = (libvlc_time_t)(seekFrac * (float)totalLen);
            std::wstring seekStr = m_media.FormatTime(seekTime);
            float tipW = 48.0f;
            float tipH = 20.0f;
            float tipX = (std::clamp)(m_hudMousePos.x - tipW / 2.0f, scrubX, scrubRight - tipW);
            float tipY = scrubY - 25.0f;
            D2D1_ROUNDED_RECT tipR = D2D1::RoundedRect(D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH), 5.0f, 5.0f);
            if (m_hudBrushTooltipBg) m_hudRenderTarget->FillRoundedRectangle(tipR, m_hudBrushTooltipBg);
            if (m_hudBrushTooltipBorder) m_hudRenderTarget->DrawRoundedRectangle(tipR, m_hudBrushTooltipBorder, 1.0f);
            D2D1_RECT_F textR = D2D1::RectF(tipX, tipY + 1.0f, tipX + tipW, tipY + tipH);
            IDWriteTextFormat* tipFont = m_d2d.FontBadge ? m_d2d.FontBadge : m_d2d.FontCaption;
            m_hudRenderTarget->DrawText(seekStr.c_str(), (UINT32)seekStr.length(), tipFont, textR, m_hudBrushText);
        }
    }

    m_hudRenderTarget->EndDraw();
}

LRESULT CALLBACK AppWindow::HudWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* app = (AppWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (app) return app->HandleHudMessage(msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT AppWindow::HandleHudMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(m_hudHwnd, &ps);
            RenderHud();
            EndPaint(m_hudHwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!m_isHudMouseTracking) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hudHwnd, 0 };
                if (TrackMouseEvent(&tme)) {
                    m_isHudMouseTracking = true;
                }
            }

            POINT currentPos;
            GetCursorPos(&currentPos);
            int dx = std::abs(currentPos.x - m_lastGlobalMousePos.x);
            int dy = std::abs(currentPos.y - m_lastGlobalMousePos.y);
            if (dx >= 2 || dy >= 2) {
                m_lastGlobalMousePos = currentPos;
                WakeAllUiAndCursor("HUD_MOUSEMOVE");
            }

            POINT physPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            D2D1_POINT_2F pt = ClientToDip(physPt);
            m_hudMousePos = pt;
            int oldHover = m_hudHoveredButtonId;
            m_hudHoveredButtonId = 0;

            auto hit = [&](const D2D1_RECT_F& r) {
                return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
            };

            if (hit(m_hudBtnPlayPause)) m_hudHoveredButtonId = BTN_PLAY_PAUSE;
            else if (hit(m_hudBtnSkipBack)) m_hudHoveredButtonId = BTN_SKIP_BACK;
            else if (hit(m_hudBtnSkipForward)) m_hudHoveredButtonId = BTN_SKIP_FORWARD;
            else if (hit(m_hudBtnLoop)) m_hudHoveredButtonId = BTN_HUD_LOOP;
            else if (hit(m_hudBtnMute)) m_hudHoveredButtonId = BTN_MUTE;
            else if (hit(m_hudBtnFullscreen)) m_hudHoveredButtonId = BTN_FULLSCREEN;
            else if (hit(m_hudScrubber)) m_hudHoveredButtonId = 100;
            else if (hit(m_hudVolSlider)) m_hudHoveredButtonId = 101;

            if (m_isScrubbing) {
                float frac = (float)(pt.x - m_hudScrubber.left) / (m_hudScrubber.right - m_hudScrubber.left);
                m_media.Seek(std::clamp(frac, 0.0f, 1.0f));
            }
            if (m_isVolDragging) {
                float frac = (float)(pt.x - m_hudVolSlider.left) / (m_hudVolSlider.right - m_hudVolSlider.left);
                m_media.SetVolume((int)(std::clamp(frac, 0.0f, 1.0f) * 100.0f));
            }

            if (oldHover != m_hudHoveredButtonId || m_isScrubbing || m_isVolDragging) {
                InvalidateRect(m_hudHwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            m_isHudMouseTracking = false;
            m_hudHoveredButtonId = 0;
            InvalidateRect(m_hudHwnd, NULL, FALSE);
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (m_hudHoveredButtonId != 0 || m_isScrubbing || m_isVolDragging) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                } else {
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }
            }
            return DefWindowProc(m_hudHwnd, msg, wParam, lParam);
        }

        case WM_LBUTTONDOWN: {
            SetCapture(m_hudHwnd);
            WakeAllUiAndCursor();

            POINT physPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            D2D1_POINT_2F pt = ClientToDip(physPt);
            auto hit = [&](const D2D1_RECT_F& r) {
                return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
            };

            if (hit(m_hudBtnPlayPause)) {
                m_hudPressedButtonId = BTN_PLAY_PAUSE;
                m_media.TogglePlayPause();
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (hit(m_hudBtnSkipBack)) {
                m_hudPressedButtonId = BTN_SKIP_BACK;
                m_media.SeekSeconds(-5);
            } else if (hit(m_hudBtnSkipForward)) {
                m_hudPressedButtonId = BTN_SKIP_FORWARD;
                m_media.SeekSeconds(5);
            } else if (hit(m_hudBtnLoop)) {
                m_hudPressedButtonId = BTN_HUD_LOOP;
                m_isLooping = !m_isLooping;
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (hit(m_hudBtnMute)) {
                m_hudPressedButtonId = BTN_MUTE;
                m_media.ToggleMute();
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (hit(m_hudBtnFullscreen)) {
                m_hudPressedButtonId = BTN_FULLSCREEN;
                ToggleFullscreen();
            } else if (hit(m_hudScrubber)) {
                m_isScrubbing = true;
                float frac = (float)(pt.x - m_hudScrubber.left) / (m_hudScrubber.right - m_hudScrubber.left);
                m_media.Seek(std::clamp(frac, 0.0f, 1.0f));
            } else if (hit(m_hudVolSlider)) {
                m_isVolDragging = true;
                float frac = (float)(pt.x - m_hudVolSlider.left) / (m_hudVolSlider.right - m_hudVolSlider.left);
                m_media.SetVolume((int)(std::clamp(frac, 0.0f, 1.0f) * 100.0f));
            }

            InvalidateRect(m_hudHwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            ReleaseCapture();
            m_isScrubbing = false;
            m_isVolDragging = false;
            m_hudPressedButtonId = 0;
            InvalidateRect(m_hudHwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            WakeAllUiAndCursor();
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            m_media.SetVolume(m_media.GetVolume() + (delta > 0 ? 5 : -5));
            InvalidateRect(m_hudHwnd, NULL, FALSE);
            return 0;
        }
    }
    return DefWindowProc(m_hudHwnd, msg, wParam, lParam);
}

bool AppWindow::CreateUpdatePopupWindow() {
    if (m_updateHwnd) return true;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = UpdatePopupWndProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"WinPreviewUpdatePopup";
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    int notifW = (int)(248.0f * m_dpiScale + 0.5f);
    int notifH = (int)(34.0f * m_dpiScale + 0.5f);
    int cornerR = (int)(17.0f * m_dpiScale + 0.5f);

    m_updateHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"WinPreviewUpdatePopup",
        L"",
        WS_POPUP,
        -9999, -9999, notifW, notifH,
        m_hwnd,
        NULL,
        m_hInstance,
        this
    );

    if (!m_updateHwnd) return false;
    SetWindowLongPtr(m_updateHwnd, GWLP_USERDATA, (LONG_PTR)this);
    ShowWindow(m_updateHwnd, SW_HIDE);

    HRGN hRgn = CreateRoundRectRgn(0, 0, notifW + 1, notifH + 1, cornerR, cornerR);
    SetWindowRgn(m_updateHwnd, hRgn, TRUE);
    SetLayeredWindowAttributes(m_updateHwnd, 0, 255, LWA_ALPHA);

    // Create D2D Render Target for Update Popup
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * m_dpiScale, 96.0f * m_dpiScale
    );
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_updateHwnd, D2D1::SizeU((UINT32)notifW, (UINT32)notifH), D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS
    );

    HRESULT hr = m_d2d.GetD2DFactory()->CreateHwndRenderTarget(rtProps, hwndProps, &m_updateRenderTarget);
    if (FAILED(hr)) return false;

    m_updateRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_updateRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    m_updateRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.11f, 0.96f), &m_updateBrushBg);
    m_updateRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.42f, 0.85f, 0.55f), &m_updateBrushBorder);
    m_updateRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.98f, 1.0f), &m_updateBrushText);
    m_updateRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.60f, 0.60f, 0.65f, 1.0f), &m_updateBrushTextMuted);
    m_updateRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.42f, 0.92f, 1.0f), &m_updateBrushBtn);
    m_updateRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.55f, 0.98f, 1.0f), &m_updateBrushBtnHover);
    m_updateRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.65f, 1.0f, 1.0f), &m_updateBrushDot);
    m_updateRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.20f, 0.25f, 1.0f), &m_updateBrushTrack);

    return true;
}

void AppWindow::DestroyUpdatePopupWindow() {
    SafeRelease(m_updateBrushBg);
    SafeRelease(m_updateBrushBorder);
    SafeRelease(m_updateBrushText);
    SafeRelease(m_updateBrushTextMuted);
    SafeRelease(m_updateBrushBtn);
    SafeRelease(m_updateBrushBtnHover);
    SafeRelease(m_updateBrushDot);
    SafeRelease(m_updateBrushTrack);
    SafeRelease(m_updateRenderTarget);

    if (m_updateHwnd) {
        DestroyWindow(m_updateHwnd);
        m_updateHwnd = nullptr;
    }
}

void AppWindow::UpdatePopupPosition() {
    if (!m_updateHwnd || !IsWindowVisible(m_hwnd) || IsIconic(m_hwnd)) {
        if (m_updateHwnd && IsWindowVisible(m_updateHwnd)) ShowWindow(m_updateHwnd, SW_HIDE);
        return;
    }

    UpdateState state = m_updater.GetState();
    if (state != UpdateState::UpdateAvailable && state != UpdateState::Downloading && state != UpdateState::Downloaded) {
        if (IsWindowVisible(m_updateHwnd)) ShowWindow(m_updateHwnd, SW_HIDE);
        return;
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int clientW = rc.right - rc.left;
    int clientH = rc.bottom - rc.top;

    float notifWDip = 248.0f;
    float clientWDip = PxToDip(clientW);
    if (clientWDip < 480.0f) {
        notifWDip = 110.0f; // Mini compact badge
    } else if (clientWDip < 660.0f) {
        notifWDip = 180.0f; // Medium compact pill
    }

    float notifHDip = 34.0f;
    int notifW = (int)(notifWDip * m_dpiScale + 0.5f);
    int notifH = (int)(notifHDip * m_dpiScale + 0.5f);
    int cornerR = (int)(17.0f * m_dpiScale + 0.5f);

    int marginX = (int)(16.0f * m_dpiScale + 0.5f);
    int marginY = (int)(14.0f * m_dpiScale + 0.5f);

    POINT pt = { clientW - notifW - marginX, marginY };
    ClientToScreen(m_hwnd, &pt);

    RECT curRc;
    GetWindowRect(m_updateHwnd, &curRc);
    int curW = curRc.right - curRc.left;
    int curH = curRc.bottom - curRc.top;

    if (curW != notifW || curH != notifH) {
        SetWindowPos(m_updateHwnd, HWND_TOP, pt.x, pt.y, notifW, notifH, SWP_NOACTIVATE);
        HRGN hRgn = CreateRoundRectRgn(0, 0, notifW + 1, notifH + 1, cornerR, cornerR);
        SetWindowRgn(m_updateHwnd, hRgn, TRUE);
        if (m_updateRenderTarget) {
            m_updateRenderTarget->Resize(D2D1::SizeU((UINT32)notifW, (UINT32)notifH));
            m_updateRenderTarget->SetDpi(96.0f * m_dpiScale, 96.0f * m_dpiScale);
        }
    } else {
        SetWindowPos(m_updateHwnd, HWND_TOP, pt.x, pt.y, notifW, notifH, SWP_NOACTIVATE | SWP_NOSIZE);
    }
}

void AppWindow::ShowUpdatePopup() {
    if (!m_updateHwnd) {
        CreateUpdatePopupWindow();
    }
    if (!m_updateHwnd) return;

    UpdateState state = m_updater.GetState();
    if (state != UpdateState::UpdateAvailable && state != UpdateState::Downloading && state != UpdateState::Downloaded && state != UpdateState::Failed) {
        HideUpdatePopup();
        return;
    }

    if (IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) {
        UpdatePopupPosition();
        ShowWindow(m_updateHwnd, SW_SHOWNOACTIVATE);
        InvalidateRect(m_updateHwnd, NULL, FALSE);
    }
}

void AppWindow::HideUpdatePopup() {
    if (m_updateHwnd && IsWindowVisible(m_updateHwnd)) {
        ShowWindow(m_updateHwnd, SW_HIDE);
    }
}

void AppWindow::RenderUpdatePopup() {
    if (!m_updateRenderTarget || !m_updateHwnd) return;

    RECT rc;
    GetClientRect(m_updateHwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    float wDip = PxToDip(w);
    float hDip = PxToDip(h);

    m_updateRenderTarget->BeginDraw();
    m_updateRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    UpdateState state = m_updater.GetState();
    const UpdateInfo& info = m_updater.GetUpdateInfo();

    float cornerR = hDip / 2.0f;
    D2D1_RECT_F fullRect = D2D1::RectF(0.0f, 0.0f, wDip, hDip);
    D2D1_ROUNDED_RECT rPill = D2D1::RoundedRect(fullRect, cornerR, cornerR);

    // 1. Frosted Acrylic Surface
    if (m_updateBrushBg) {
        m_updateRenderTarget->FillRoundedRectangle(rPill, m_updateBrushBg);
    }

    // 2. Hairline Accent Border
    if (m_updateBrushBorder) {
        D2D1_COLOR_F bColor = (state == UpdateState::Downloaded)
            ? D2D1::ColorF(0.12f, 0.75f, 0.42f, 0.65f)
            : D2D1::ColorF(0.24f, 0.45f, 0.90f, 0.65f);
        m_updateBrushBorder->SetColor(bColor);
        m_updateRenderTarget->DrawRoundedRectangle(rPill, m_updateBrushBorder, 1.0f);
    }

    // 3. Status Dot Indicator
    float dotX = (wDip >= 220.0f) ? 14.0f : ((wDip >= 160.0f) ? 12.0f : 10.0f);
    D2D1_POINT_2F dotCenter = D2D1::Point2F(dotX, hDip / 2.0f);
    if (m_updateBrushDot) {
        D2D1_COLOR_F dotColor = (state == UpdateState::Downloaded)
            ? D2D1::ColorF(0.12f, 0.85f, 0.45f, 1.0f)
            : ((state == UpdateState::Downloading) ? D2D1::ColorF(0.95f, 0.65f, 0.15f, 1.0f)
            : ((state == UpdateState::Failed) ? D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f) : D2D1::ColorF(0.22f, 0.65f, 1.0f, 1.0f)));
        m_updateBrushDot->SetColor(dotColor);
        m_updateRenderTarget->FillEllipse(D2D1::Ellipse(dotCenter, 3.5f, 3.5f), m_updateBrushDot);
    }

    IDWriteTextFormat* fontText = m_d2d.FontCaption ? m_d2d.FontCaption : m_d2d.FontButton;

    // 4. Responsive Content
    if (state == UpdateState::UpdateAvailable) {
        std::wstring label = (wDip >= 220.0f) ? (L"Update v" + info.remoteVersion) : (L"v" + info.remoteVersion);
        float textLeft = (wDip >= 220.0f) ? 24.0f : ((wDip >= 160.0f) ? 22.0f : 18.0f);
        float textRight = (wDip >= 220.0f) ? (wDip - 88.0f) : ((wDip >= 160.0f) ? (wDip - 74.0f) : (wDip - 46.0f));
        D2D1_RECT_F textRect = D2D1::RectF(textLeft, 0.0f, textRight, hDip);
        if (m_updateBrushText && fontText) {
            m_updateRenderTarget->DrawText(label.c_str(), (UINT32)label.length(), fontText, textRect, m_updateBrushText);
        }

        // Action Pill Button [ Update / ↑ ]
        float btnW = (wDip >= 220.0f) ? 56.0f : ((wDip >= 160.0f) ? 46.0f : 24.0f);
        float btnRight = (wDip >= 220.0f) ? (wDip - 28.0f) : ((wDip >= 160.0f) ? (wDip - 24.0f) : (wDip - 20.0f));
        m_updateBtnRect = D2D1::RectF(btnRight - btnW, 5.0f, btnRight, hDip - 5.0f);
        D2D1_ROUNDED_RECT rBtn = D2D1::RoundedRect(m_updateBtnRect, 12.0f, 12.0f);
        ID2D1SolidColorBrush* bBrush = m_updateBtnHover ? m_updateBrushBtnHover : m_updateBrushBtn;
        if (bBrush) {
            m_updateRenderTarget->FillRoundedRectangle(rBtn, bBrush);
        }

        std::wstring btnLabel = (wDip >= 160.0f) ? L"Update" : L"↑";
        if (m_updateBrushText && m_d2d.FontButton) {
            m_updateRenderTarget->DrawText(btnLabel.c_str(), (UINT32)btnLabel.length(), m_d2d.FontButton, m_updateBtnRect, m_updateBrushText);
        }

        // Dismiss Button [ ✕ ]
        float closeLeft = (wDip >= 220.0f) ? (wDip - 24.0f) : ((wDip >= 160.0f) ? (wDip - 22.0f) : (wDip - 18.0f));
        float closeRight = (wDip >= 220.0f) ? (wDip - 6.0f) : ((wDip >= 160.0f) ? (wDip - 4.0f) : (wDip - 4.0f));
        m_updateCloseBtnRect = D2D1::RectF(closeLeft, 5.0f, closeRight, hDip - 5.0f);
        std::wstring closeLabel = L"✕";
        ID2D1SolidColorBrush* cBrush = m_updateCloseHover ? m_updateBrushText : m_updateBrushTextMuted;
        if (cBrush && m_d2d.FontButton) {
            m_updateRenderTarget->DrawText(closeLabel.c_str(), (UINT32)closeLabel.length(), m_d2d.FontButton, m_updateCloseBtnRect, cBrush);
        }
    } else if (state == UpdateState::Downloading) {
        int pct = m_updater.GetDownloadProgress();
        std::wstring dlText = (wDip >= 200.0f) ? (L"Downloading... " + std::to_wstring(pct) + L"%") : (std::to_wstring(pct) + L"%");
        float textLeft = (wDip >= 200.0f) ? 24.0f : 20.0f;
        D2D1_RECT_F textRect = D2D1::RectF(textLeft, 0.0f, wDip - 12.0f, hDip - 5.0f);
        if (m_updateBrushText && fontText) {
            m_updateRenderTarget->DrawText(dlText.c_str(), (UINT32)dlText.length(), fontText, textRect, m_updateBrushText);
        }

        // Inset Progress Bar Track along bottom of pill
        D2D1_RECT_F trackRect = D2D1::RectF(14.0f, hDip - 5.5f, wDip - 14.0f, hDip - 3.0f);
        D2D1_ROUNDED_RECT rTrack = D2D1::RoundedRect(trackRect, 1.25f, 1.25f);
        if (m_updateBrushTrack) {
            m_updateRenderTarget->FillRoundedRectangle(rTrack, m_updateBrushTrack);
        }
        float fillW = (trackRect.right - trackRect.left) * (std::max(1, pct) / 100.0f);
        D2D1_RECT_F fillRect = D2D1::RectF(trackRect.left, trackRect.top, trackRect.left + fillW, trackRect.bottom);
        D2D1_ROUNDED_RECT rFill = D2D1::RoundedRect(fillRect, 1.25f, 1.25f);
        if (m_updateBrushBtnHover) {
            m_updateRenderTarget->FillRoundedRectangle(rFill, m_updateBrushBtnHover);
        }
    } else if (state == UpdateState::Downloaded) {
        std::wstring title = (wDip >= 200.0f) ? L"Update Ready" : L"Ready";
        float textLeft = (wDip >= 220.0f) ? 24.0f : ((wDip >= 160.0f) ? 22.0f : 18.0f);
        float textRight = (wDip >= 220.0f) ? (wDip - 88.0f) : ((wDip >= 160.0f) ? (wDip - 80.0f) : (wDip - 46.0f));
        D2D1_RECT_F textRect = D2D1::RectF(textLeft, 0.0f, textRight, hDip);
        if (m_updateBrushText && fontText) {
            m_updateRenderTarget->DrawText(title.c_str(), (UINT32)title.length(), fontText, textRect, m_updateBrushText);
        }

        // Action Pill Button [ Restart / ✓ ]
        float btnW = (wDip >= 220.0f) ? 58.0f : ((wDip >= 160.0f) ? 54.0f : 24.0f);
        float btnRight = (wDip >= 220.0f) ? (wDip - 28.0f) : ((wDip >= 160.0f) ? (wDip - 24.0f) : (wDip - 20.0f));
        m_updateBtnRect = D2D1::RectF(btnRight - btnW, 5.0f, btnRight, hDip - 5.0f);
        D2D1_ROUNDED_RECT rBtn = D2D1::RoundedRect(m_updateBtnRect, 12.0f, 12.0f);
        ID2D1SolidColorBrush* bBrush = nullptr;
        D2D1_COLOR_F rColor = m_updateBtnHover ? D2D1::ColorF(0.12f, 0.78f, 0.44f, 1.0f) : D2D1::ColorF(0.08f, 0.65f, 0.36f, 1.0f);
        m_updateRenderTarget->CreateSolidColorBrush(rColor, &bBrush);
        if (bBrush) {
            m_updateRenderTarget->FillRoundedRectangle(rBtn, bBrush);
            bBrush->Release();
        }

        std::wstring btnLabel = (wDip >= 160.0f) ? L"Restart" : L"✓";
        if (m_updateBrushText && m_d2d.FontButton) {
            m_updateRenderTarget->DrawText(btnLabel.c_str(), (UINT32)btnLabel.length(), m_d2d.FontButton, m_updateBtnRect, m_updateBrushText);
        }

        // Dismiss Button [ ✕ ]
        float closeLeft = (wDip >= 220.0f) ? (wDip - 24.0f) : ((wDip >= 160.0f) ? (wDip - 22.0f) : (wDip - 18.0f));
        float closeRight = (wDip >= 220.0f) ? (wDip - 6.0f) : ((wDip >= 160.0f) ? (wDip - 4.0f) : (wDip - 4.0f));
        m_updateCloseBtnRect = D2D1::RectF(closeLeft, 5.0f, closeRight, hDip - 5.0f);
        std::wstring closeLabel = L"✕";
        ID2D1SolidColorBrush* cBrush = m_updateCloseHover ? m_updateBrushText : m_updateBrushTextMuted;
        if (cBrush && m_d2d.FontButton) {
            m_updateRenderTarget->DrawText(closeLabel.c_str(), (UINT32)closeLabel.length(), m_d2d.FontButton, m_updateCloseBtnRect, cBrush);
        }
    } else if (state == UpdateState::Failed) {
        std::wstring label = (wDip >= 220.0f) ? L"Download failed" : (wDip >= 160.0f ? L"Failed" : L"!");
        float textLeft = (wDip >= 220.0f) ? 24.0f : ((wDip >= 160.0f) ? 22.0f : 18.0f);
        float textRight = (wDip >= 220.0f) ? (wDip - 88.0f) : ((wDip >= 160.0f) ? (wDip - 74.0f) : (wDip - 46.0f));
        D2D1_RECT_F textRect = D2D1::RectF(textLeft, 0.0f, textRight, hDip);
        if (m_updateBrushText && fontText) {
            m_updateRenderTarget->DrawText(label.c_str(), (UINT32)label.length(), fontText, textRect, m_updateBrushText);
        }

        // Action Pill Button [ Retry / ↺ ]
        float btnW = (wDip >= 220.0f) ? 56.0f : ((wDip >= 160.0f) ? 46.0f : 24.0f);
        float btnRight = (wDip >= 220.0f) ? (wDip - 28.0f) : ((wDip >= 160.0f) ? (wDip - 24.0f) : (wDip - 20.0f));
        m_updateBtnRect = D2D1::RectF(btnRight - btnW, 5.0f, btnRight, hDip - 5.0f);
        D2D1_ROUNDED_RECT rBtn = D2D1::RoundedRect(m_updateBtnRect, 12.0f, 12.0f);
        ID2D1SolidColorBrush* bBrush = m_updateBtnHover ? m_updateBrushBtnHover : m_updateBrushBtn;
        if (bBrush) {
            m_updateRenderTarget->FillRoundedRectangle(rBtn, bBrush);
        }

        std::wstring btnLabel = (wDip >= 160.0f) ? L"Retry" : L"↺";
        if (m_updateBrushText && m_d2d.FontButton) {
            m_updateRenderTarget->DrawText(btnLabel.c_str(), (UINT32)btnLabel.length(), m_d2d.FontButton, m_updateBtnRect, m_updateBrushText);
        }

        // Dismiss Button [ ✕ ]
        float closeLeft = (wDip >= 220.0f) ? (wDip - 24.0f) : ((wDip >= 160.0f) ? (wDip - 22.0f) : (wDip - 18.0f));
        float closeRight = (wDip >= 220.0f) ? (wDip - 6.0f) : ((wDip >= 160.0f) ? (wDip - 4.0f) : (wDip - 4.0f));
        m_updateCloseBtnRect = D2D1::RectF(closeLeft, 5.0f, closeRight, hDip - 5.0f);
        std::wstring closeLabel = L"✕";
        ID2D1SolidColorBrush* cBrush = m_updateCloseHover ? m_updateBrushText : m_updateBrushTextMuted;
        if (cBrush && m_d2d.FontButton) {
            m_updateRenderTarget->DrawText(closeLabel.c_str(), (UINT32)closeLabel.length(), m_d2d.FontButton, m_updateCloseBtnRect, cBrush);
        }
    }

    m_updateRenderTarget->EndDraw();
}

LRESULT CALLBACK AppWindow::UpdatePopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* self = (AppWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!self) {
        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            self = (AppWindow*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            self->RenderUpdatePopup();
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);

            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            float xDip = self->PxToDip(pt.x);
            float yDip = self->PxToDip(pt.y);

            bool btnHov = (xDip >= self->m_updateBtnRect.left && xDip <= self->m_updateBtnRect.right &&
                           yDip >= self->m_updateBtnRect.top && yDip <= self->m_updateBtnRect.bottom);
            bool clsHov = (xDip >= self->m_updateCloseBtnRect.left && xDip <= self->m_updateCloseBtnRect.right &&
                           yDip >= self->m_updateCloseBtnRect.top && yDip <= self->m_updateCloseBtnRect.bottom);

            if (btnHov != self->m_updateBtnHover || clsHov != self->m_updateCloseHover) {
                self->m_updateBtnHover = btnHov;
                self->m_updateCloseHover = clsHov;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (self->m_updateBtnHover || self->m_updateCloseHover) {
                self->m_updateBtnHover = false;
                self->m_updateCloseHover = false;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_SETCURSOR: {
            if (self->m_updateBtnHover || self->m_updateCloseHover) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            float xDip = self->PxToDip(pt.x);
            float yDip = self->PxToDip(pt.y);

            UpdateState state = self->m_updater.GetState();

            if (xDip >= self->m_updateBtnRect.left && xDip <= self->m_updateBtnRect.right &&
                yDip >= self->m_updateBtnRect.top && yDip <= self->m_updateBtnRect.bottom) {

                if (state == UpdateState::UpdateAvailable || state == UpdateState::Failed) {
                    self->m_updater.DownloadUpdateAsync(
                        [self](int pct) {
                            if (self->m_updateHwnd) InvalidateRect(self->m_updateHwnd, NULL, FALSE);
                        },
                        [self](bool success, const std::wstring& path) {
                            if (self->m_updateHwnd) InvalidateRect(self->m_updateHwnd, NULL, FALSE);
                            if (!success) {
                                self->ShowToast(L"Download failed. Click Retry in banner.");
                            }
                        }
                    );
                } else if (state == UpdateState::Downloaded) {
                    self->m_updater.LaunchInstallerAndExit(self->m_hwnd);
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (xDip >= self->m_updateCloseBtnRect.left && xDip <= self->m_updateCloseBtnRect.right &&
                yDip >= self->m_updateCloseBtnRect.top && yDip <= self->m_updateCloseBtnRect.bottom) {
                self->m_updater.Dismiss();
                self->HideUpdatePopup();
                return 0;
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}




LRESULT AppWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_USER + 555: {
            ShowUpdatePopup();
            return 0;
        }

        case WM_GETICON: {
            if (wParam == ICON_BIG || wParam == 2 /* ICON_SMALL2 */) {
                return (LRESULT)(m_hIconBig ? m_hIconBig : m_hIconSmall);
            }
            return (LRESULT)(m_hIconSmall ? m_hIconSmall : m_hIconBig);
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = 340;
            mmi->ptMinTrackSize.y = 240;
            return 0;
        }

        case WM_DPICHANGED: {
            RECT* const prcNewWindow = (RECT*)lParam;
            SetWindowPos(m_hwnd,
                NULL,
                prcNewWindow->left,
                prcNewWindow->top,
                prcNewWindow->right - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            UINT dpi = HIWORD(wParam);
            if (dpi > 0) {
                m_dpiScale = (float)dpi / 96.0f;
                m_d2d.UpdateDpi(dpi);
                if (m_hudRenderTarget) {
                    m_hudRenderTarget->SetDpi((float)dpi, (float)dpi);
                }
            }
            UpdateLayout();
            UpdateHudPosition();
            return 0;
        }

        case WM_SETCURSOR: {
            if (!m_cursorVisible && LOWORD(lParam) == HTCLIENT) {
                SetCursor(NULL);
                return TRUE;
            }

            if (m_docType == DocumentType::Media && !m_isAudioOnly) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }

            if (LOWORD(lParam) == HTCLIENT) {
                POINT physPt;
                GetCursorPos(&physPt);
                ScreenToClient(m_hwnd, &physPt);
                D2D1_POINT_2F pt = ClientToDip(physPt);

                auto hitDock = [&](const D2D1_RECT_F& r) {
                    return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
                };

                if (m_hoveredButtonId != BTN_NONE || m_hoveredThumbnailIndex >= 0) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }

                if (m_docType == DocumentType::Media && m_isAudioOnly) {
                    if (hitDock(m_audioArtworkRect) || hitDock(m_audioWaveformRect)) {
                        SetCursor(LoadCursor(NULL, IDC_HAND));
                        return TRUE;
                    }
                }

                if (m_isCropActive && m_docType == DocumentType::Image && m_imageWidth > 0 && m_imageHeight > 0) {
                    D2D1_RECT_F canvasRect = GetCanvasRectDip();
                    float targetW = (float)m_imageWidth * m_zoom;
                    float targetH = (float)m_imageHeight * m_zoom;
                    float canvasCenterX = canvasRect.left + (canvasRect.right - canvasRect.left) / 2.0f;
                    float canvasCenterY = canvasRect.top + (canvasRect.bottom - canvasRect.top) / 2.0f;
                    float destLeft = canvasCenterX - (targetW / 2.0f) + m_panX;
                    float destTop = canvasCenterY - (targetH / 2.0f) + m_panY;
                    float cLeft = destLeft + (m_cropRect.left / (float)m_imageWidth) * targetW;
                    float cTop = destTop + (m_cropRect.top / (float)m_imageHeight) * targetH;
                    float cRight = destLeft + (m_cropRect.right / (float)m_imageWidth) * targetW;
                    float cBottom = destTop + (m_cropRect.bottom / (float)m_imageHeight) * targetH;

                    auto nearPt = [&](float x, float y) {
                        return std::abs(pt.x - x) <= 8.0f && std::abs(pt.y - y) <= 8.0f;
                    };

                    if (nearPt(cLeft, cTop) || nearPt(cRight, cBottom)) {
                        SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
                        return TRUE;
                    } else if (nearPt(cRight, cTop) || nearPt(cLeft, cBottom)) {
                        SetCursor(LoadCursor(NULL, IDC_SIZENESW));
                        return TRUE;
                    } else if (pt.x >= cLeft && pt.x <= cRight && pt.y >= cTop && pt.y <= cBottom) {
                        SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                        return TRUE;
                    }
                }

                if (m_docType == DocumentType::Pdf && !hitDock(m_topDockRect) && (!m_showSidebar || pt.x >= m_sidebarWidth)) {
                    if (m_isSelectingText) {
                        SetCursor(LoadCursor(NULL, IDC_IBEAM));
                        return TRUE;
                    }
                    D2D1_RECT_F canvasRect = GetCanvasRectDip();
                    D2D1_POINT_2F pagePt = CanvasToPageCoords(pt.x, pt.y, canvasRect);
                    int charIdx = m_pdf.GetCharIndexAtPos(pagePt.x, pagePt.y, 8.0, 8.0);
                    if (charIdx >= 0) {
                        SetCursor(LoadCursor(NULL, IDC_IBEAM));
                        return TRUE;
                    }
                }

                if (m_isDragging) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                    return TRUE;
                }

                if (m_hoveredButtonId != BTN_NONE || m_hoveredThumbnailIndex >= 0) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }

                if (hitDock(m_topDockRect)) {
                    SetCursor(LoadCursor(NULL, IDC_ARROW));
                    return TRUE;
                }

                if (m_showSidebar && pt.x < m_sidebarWidth) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }

                if (m_docType == DocumentType::Text || (m_docType == DocumentType::Spreadsheet && !m_text.IsTableView())) {
                    D2D1_SIZE_F sz = GetClientSizeDip();
                    float statusH = 24.0f * m_dpiScale;
                    float headerH = m_text.GetHeaderTopPadding(m_dpiScale);
                    float gutterW = m_text.GetGutterWidth(m_dpiScale);

                    // Over bottom status bar
                    if (pt.y >= sz.height - statusH) {
                        SetCursor(LoadCursor(NULL, IDC_ARROW));
                        return TRUE;
                    }
                    // Over line numbers gutter
                    if (pt.x < gutterW + 6.0f * m_dpiScale) {
                        SetCursor(LoadCursor(NULL, IDC_ARROW));
                        return TRUE;
                    }
                    // Over top header padding
                    if (pt.y < headerH) {
                        SetCursor(LoadCursor(NULL, IDC_ARROW));
                        return TRUE;
                    }
                    // Over vertical scrollbar
                    if (pt.x >= sz.width - 12.0f * m_dpiScale) {
                        SetCursor(LoadCursor(NULL, IDC_ARROW));
                        return TRUE;
                    }
                    // Over editor text editing canvas: crisp I-Beam cursor!
                    SetCursor(LoadCursor(NULL, IDC_IBEAM));
                    return TRUE;
                }
            }
            return DefWindowProc(m_hwnd, msg, wParam, lParam);
        }

        case WM_MOVE: {
            UpdateHudPosition();
            UpdatePopupPosition();
            return 0;
        }

        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
                HideUpdatePopup();
                m_hudOpacity = 0.0f;
                m_hudTargetOpacity = 0.0f;
                if (m_docType == DocumentType::Media && m_media.IsPlaying()) {
                    m_media.Pause();
                }
                return 0;
            }
            if (m_panX == 0.0f && m_panY == 0.0f) {
                FitToWindow();
            }
            UpdateLayout();
            UpdateHudPosition();
            UpdatePopupPosition();
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(m_hwnd, &ps);
            Render();
            EndPaint(m_hwnd, &ps);
            return 0;
        }

        case WM_PRINTCLIENT: {
            Render((HDC)wParam);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                SetCursorVisibility(true);
                HWND hNewActive = (HWND)lParam;
                if (hNewActive != m_hudHwnd && m_hudHwnd && IsWindowVisible(m_hudHwnd)) {
                    SetWindowPos(m_hudHwnd, m_hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            } else {
                WakeAllUiAndCursor();
                if (m_docType == DocumentType::Media && m_hudHwnd && IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) {
                    UpdateHudPosition();
                    SetWindowPos(m_hudHwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            return 0;
        }

        case WM_APP + 101: /* WM_APP_VIDEO_MOUSEMOVE from VideoWndProc */ {
            POINT curPos;
            GetCursorPos(&curPos);
            int dx = std::abs(curPos.x - m_lastGlobalMousePos.x);
            int dy = std::abs(curPos.y - m_lastGlobalMousePos.y);
            if (dx >= 2 || dy >= 2) {
                m_lastGlobalMousePos = curPos;
                WakeAllUiAndCursor("VIDEO_MOUSEMOVE");
            }
            return 0;
        }

        case WM_APP + 200: /* GetDockOpacityMilli (e.g. 1000 = 1.0f, 0 = 0.0f) */ {
            return (LRESULT)(m_dockOpacity * 1000.0f);
        }

        case WM_APP + 201: /* GetCursorVisible */ {
            return (LRESULT)(m_cursorVisible ? 1 : 0);
        }

        case WM_APP + 202: /* GetInactivityMs */ {
            return (LRESULT)m_inactivityMs;
        }

        case WM_TIMER: {
            if (wParam == TIMER_PERIODIC_UPDATE) {
                CheckForUpdates(false);
                return 0;
            }

            if (wParam == TIMER_EXPLORER_NAV) {
                KillTimer(m_hwnd, TIMER_EXPLORER_NAV);
                std::wstring selectedPath;
                if (ExplorerHook::GetSelectedExplorerPath(selectedPath) && !selectedPath.empty()) {
                    if (selectedPath != m_currentFilePath) {
                        OpenFile(selectedPath);
                        ShowWindow(m_hwnd, SW_SHOW);
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }

            if (!IsWindowVisible(m_hwnd) || IsIconic(m_hwnd)) {
                if (m_hudHwnd && IsWindowVisible(m_hudHwnd)) {
                    ShowWindow(m_hudHwnd, SW_HIDE);
                }
                return 0;
            }

            // Micro-tooltip hover delay and smooth fade (zero idle CPU once complete)
            bool isDockBtn = (m_hoveredButtonId >= BTN_SIDEBAR && m_hoveredButtonId <= BTN_TOOLBAR_FS) ||
                             m_hoveredButtonId == BTN_SEARCH || m_hoveredButtonId == BTN_CROP ||
                             m_hoveredButtonId == BTN_INFO || m_hoveredButtonId == BTN_OPEN_WITH ||
                             m_hoveredButtonId == BTN_SETTINGS || m_hoveredButtonId == BTN_OVERFLOW_MENU ||
                             (m_hoveredButtonId >= BTN_SAVE_FILE && m_hoveredButtonId <= BTN_TABLE_VIEW);
            bool canShowTooltip = (m_hoveredButtonId != BTN_NONE) && (!isDockBtn || m_dockOpacity > 0.5f);

            if (canShowTooltip) {
                m_hoverTimeMs += 30;
                // Fast swap: if a tooltip was already active on an adjacent button, show immediately without waiting 400ms (Emil Kowalski)
                if ((m_tooltipAlpha > 0.4f || m_hoverTimeMs > 400) && m_tooltipAlpha < 1.0f) {
                    m_tooltipAlpha += 0.35f;
                    if (m_tooltipAlpha > 1.0f) m_tooltipAlpha = 1.0f;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            } else {
                if (m_tooltipAlpha > 0.0f) {
                    m_tooltipAlpha -= 0.25f;
                    if (m_tooltipAlpha < 0.0f) m_tooltipAlpha = 0.0f;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
                m_hoverTimeMs = 0;
            }

            // High-precision elapsed time calculation for accurate 3-second timer
            ULONGLONG now = GetTickCount64();
            if (m_lastTimerTick == 0) m_lastTimerTick = now;
            int elapsedMs = (int)(now - m_lastTimerTick);
            m_lastTimerTick = now;
            if (elapsedMs < 1) elapsedMs = 1;
            if (elapsedMs > 200) elapsedMs = 30;

            // 1. Determine whether cursor is currently over our app window
            POINT curPos;
            GetCursorPos(&curPos);
            RECT clientRc;
            GetClientRect(m_hwnd, &clientRc);
            POINT p1 = { clientRc.left, clientRc.top };
            POINT p2 = { clientRc.right, clientRc.bottom };
            ClientToScreen(m_hwnd, &p1);
            ClientToScreen(m_hwnd, &p2);
            RECT screenRc = { p1.x, p1.y, p2.x, p2.y };
            bool isCursorOverApp = PtInRect(&screenRc, curPos);
            if (m_hudHwnd) {
                RECT hudRc = {};
                if (GetWindowRect(m_hudHwnd, &hudRc) && PtInRect(&hudRc, curPos)) {
                    isCursorOverApp = true;
                }
            }

            // 2. Global fallback movement detection with deadzone (>= 4 physical pixels)
            int gdx = std::abs(curPos.x - m_lastGlobalMousePos.x);
            int gdy = std::abs(curPos.y - m_lastGlobalMousePos.y);
            if (isCursorOverApp && (gdx >= 4 || gdy >= 4)) {
                m_lastGlobalMousePos = curPos;
                POINT clPt = curPos;
                ScreenToClient(m_hwnd, &clPt);
                m_lastActiveMousePos = clPt;
                WakeAllUiAndCursor("WM_TIMER_FALLBACK");
            }

            // 3. Check active dragging or mouse button down
            bool isActivelyInteracting = (
                m_isDragging ||
                (m_isCropActive && m_cropDragHandle > 0) ||
                m_isSelectingText ||
                m_isScrubbing ||
                m_isVolDragging ||
                ((GetCapture() == m_hwnd || (m_hudHwnd && GetCapture() == m_hudHwnd)) && ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0))
            );

            // 4. Inactivity & Auto-Hide state machine
            if (isActivelyInteracting || m_showOverflowMenu || m_showInspector || m_showSearch || m_showSettings) {
                // Actively holding mouse button down, dragging, or menu/modal open: keep 100% visible
                m_inactivityMs = 0;
                m_dockTargetOpacity = 1.0f;
                if (m_docType == DocumentType::Media) m_hudTargetOpacity = 255.0f;
                SetCursorVisibility(true);
            } else if (m_docType == DocumentType::Media && (!m_media.IsPlaying() || m_isAudioOnly)) {
                // Video paused, stopped, ended, or audio: keep permanently visible
                m_inactivityMs = 0;
                m_dockTargetOpacity = 1.0f;
                m_hudTargetOpacity = 255.0f;
            } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                // Text Editor & Spreadsheets: keep permanently visible!
                m_inactivityMs = 0;
                m_dockTargetOpacity = 1.0f;
                SetCursorVisibility(true);
            } else if (m_docType != DocumentType::None) {
                // Active document (Image, PDF, or Playing Video): track exact 3-second inactivity
                m_inactivityMs += elapsedMs;
                if (m_inactivityMs >= 3000) {
                    m_dockTargetOpacity = 0.0f;
                    if (m_docType == DocumentType::Media) m_hudTargetOpacity = 0.0f;
                    if (isCursorOverApp) {
                        if (m_cursorVisible) {
                            SetCursorVisibility(false);
                        }
                    } else {
                        if (!m_cursorVisible) {
                            SetCursorVisibility(true);
                        }
                    }
                }
            } else {
                // None / Empty state: keep permanently visible
                m_inactivityMs = 0;
                m_dockTargetOpacity = 1.0f;
                if (m_docType == DocumentType::Media) m_hudTargetOpacity = 255.0f;
                if (!m_cursorVisible) SetCursorVisibility(true);
            }

            // Smooth fade transition for top dock bar (Images & PDFs)
            bool isDockFading = (std::abs(m_dockOpacity - m_dockTargetOpacity) > 0.005f);
            if (isDockFading) {
                if (m_dockOpacity > m_dockTargetOpacity) {
                    // Smooth decelerating ease-out fade out over ~300ms
                    m_dockOpacity -= (m_dockOpacity - m_dockTargetOpacity) * 0.22f + 0.015f;
                    if (m_dockOpacity <= 0.005f) {
                        m_dockOpacity = 0.0f;
                    }
                } else {
                    // Refined smooth ease-out reveal over ~200ms (NOT instant!)
                    m_dockOpacity += (m_dockTargetOpacity - m_dockOpacity) * 0.32f + 0.025f;
                    if (m_dockOpacity >= 0.995f) {
                        m_dockOpacity = 1.0f;
                    }
                }
                InvalidateRect(m_hwnd, NULL, FALSE);
            }

            bool isHudFading = false;
            // Smooth fade transition for media HUD (Videos & Audio)
            if (m_docType == DocumentType::Media && IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) {
                isHudFading = (std::abs(m_hudOpacity - m_hudTargetOpacity) > 0.5f);
                if (isHudFading) {
                    if (m_hudOpacity > m_hudTargetOpacity) {
                        m_hudOpacity -= (m_hudOpacity - m_hudTargetOpacity) * 0.22f + 1.5f;
                        if (m_hudOpacity <= 1.0f) {
                            m_hudOpacity = 0.0f;
                        }
                    } else {
                        m_hudOpacity += (m_hudTargetOpacity - m_hudOpacity) * 0.32f + 3.0f;
                        if (m_hudOpacity >= 253.0f) {
                            m_hudOpacity = 255.0f;
                        }
                    }

                    BYTE alpha = (BYTE)(std::clamp(m_hudOpacity, 0.0f, 255.0f));
                    if (m_hudHwnd) {
                        SetLayeredWindowAttributes(m_hudHwnd, 0, alpha, LWA_ALPHA);
                        if (alpha > 0) ShowWindow(m_hudHwnd, SW_SHOWNOACTIVATE);
                        else ShowWindow(m_hudHwnd, SW_HIDE);
                    }
                }
            } else {
                if (m_hudHwnd && IsWindowVisible(m_hudHwnd)) {
                    ShowWindow(m_hudHwnd, SW_HIDE);
                }
                m_hudOpacity = 0.0f;
                m_hudTargetOpacity = 0.0f;
            }

            if (m_docType == DocumentType::Media) {
                // Adapt video window size if needed
                if (!m_isAudioOnly && !m_hasAdaptedVideoWindow) {
                    unsigned int vw = 0, vh = 0;
                    if (m_media.GetVideoDimensions(&vw, &vh) && vw > 0 && vh > 0) {
                        m_videoWidth = (int)vw;
                        m_videoHeight = (int)vh;
                        m_hasAdaptedVideoWindow = true;
                        AdaptWindowToMedia(m_videoWidth, m_videoHeight);
                    }
                }

                // Loop playback handling
                if (m_isLooping && !m_isScrubbing && m_media.IsEnded()) {
                    m_media.Restart();
                }

                // Check ended state transition to trigger HUD repaint
                bool isEndedNow = m_media.IsEnded();
                bool endedStateChanged = (isEndedNow != m_wasEnded);
                m_wasEnded = isEndedNow;

                // Repaint HUD only when actively playing, scrubbing, fading, or state changed (0% CPU when static)
                if (m_hudHwnd && (m_media.IsPlaying() || m_isScrubbing || isHudFading || endedStateChanged)) {
                    InvalidateRect(m_hudHwnd, NULL, FALSE);
                }

                // For audio files, rotate vinyl and repaint while playing or transitioning
                if (m_isAudioOnly) {
                    bool isPlayingAudio = m_media.IsPlaying();
                    float targetSlide = isPlayingAudio ? 58.0f : 20.0f;
                    bool isTransitioning = std::abs(targetSlide - m_audioVinylSlide) > 0.5f;
                    if (isPlayingAudio) {
                        m_audioDiscAngle += 2.2f;
                        if (m_audioDiscAngle >= 360.0f) m_audioDiscAngle -= 360.0f;
                        m_audioAnimTime += (float)elapsedMs * 0.0042f;
                        if (m_audioAnimTime >= 10000.0f) m_audioAnimTime -= 10000.0f;
                    }
                    bool barsSettling = false;
                    if (!isPlayingAudio) {
                        for (int i = 0; i < 64; ++i) {
                            if (m_audioVisualizerBars[i] > 14.0f) {
                                barsSettling = true;
                                break;
                            }
                        }
                    }
                    if (isPlayingAudio || isTransitioning || barsSettling) {
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                }
            }

            // Toast animation entrance & decay
            if (m_toastOpacity > 0.0f) {
                ULONGLONG elapsedToast = now - m_toastStartTime;
                if (elapsedToast < 220) {
                    // Smooth cubic ease entrance frame
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (elapsedToast > 1500) {
                    m_toastOpacity -= 0.06f;
                    if (m_toastOpacity <= 0.0f) m_toastOpacity = 0.0f;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }

            bool isAnimating = false;
            float dt = (float)elapsedMs / 1000.0f;
            if (dt < 0.001f) dt = 0.001f;
            if (dt > 0.1f) dt = 0.033f;

            // 1. Text caret blink update
            if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                if (m_text.UpdateAnimation(dt)) {
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }

            // 2. Viewport direct sync (Images, PDF, Office, SVG)
            if (m_docType == DocumentType::Image || m_docType == DocumentType::Pdf ||
                m_docType == DocumentType::Office || m_docType == DocumentType::Svg) {
                if (m_zoom != m_targetZoom || m_panX != m_targetPanX || m_panY != m_targetPanY) {
                    m_zoom = m_targetZoom;
                    m_panX = m_targetPanX;
                    m_panY = m_targetPanY;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }

            // 3. Sidebar scroll sync
            if (m_showSidebar) {
                if (m_sidebarScrollY != m_targetSidebarScrollY || m_outlineScrollY != m_targetOutlineScrollY) {
                    m_sidebarScrollY = m_targetSidebarScrollY;
                    m_outlineScrollY = m_targetOutlineScrollY;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }

            // 4. Overflow menu scroll sync
            if (m_showOverflowMenu) {
                if (m_overflowScrollY != m_targetOverflowScrollY) {
                    m_overflowScrollY = m_targetOverflowScrollY;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }

            m_inspectorAnim = m_showInspector ? 1.0f : 0.0f;
            m_overflowMenuAnim = m_showOverflowMenu ? 1.0f : 0.0f;
            m_searchAnim = m_showSearch ? 1.0f : 0.0f;
            m_settingsAnim = m_showSettings ? 1.0f : 0.0f;
            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            WCHAR filePath[MAX_PATH];
            if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH)) {
                OpenFile(filePath);
            }
            DragFinish(hDrop);
            return 0;
        }

        case WM_SHOWWINDOW: {
            if (!wParam) {
                // Window is being hidden: ensure HUD is hidden, media paused, timer stopped
                if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
                HideUpdatePopup();
                m_hudOpacity = 0.0f;
                m_hudTargetOpacity = 0.0f;
                if (m_docType == DocumentType::Media) {
                    if (m_media.IsPlaying()) m_media.Pause();
                    m_media.ShowVideoWindow(false);
                }
                m_isScrubbing = false;
                m_isVolDragging = false;
                m_isWaveformSeeking = false;
                KillTimer(m_hwnd, 1);
            } else {
                // Window is being shown: ensure device resources exist, start 30ms animation timer, sync HUD
                if (!m_d2d.GetRenderTarget()) {
                    m_d2d.CreateDeviceResources(m_hwnd);
                }
                SetTimer(m_hwnd, 1, 30, NULL);
                if (m_docType == DocumentType::Media) {
                    UpdateHudPosition();
                }
                if (m_updater.GetUpdateInfo().isAvailable) {
                    ShowUpdatePopup();
                }
            }
            return 0;
        }

        case WM_COPYDATA: {
            COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
            if (cds && cds->dwData == 1 && cds->lpData) {
                const wchar_t* path = (const wchar_t*)cds->lpData;
                OpenFile(path);
                if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                else ShowWindow(m_hwnd, SW_SHOW);

                HWND hForeground = GetForegroundWindow();
                DWORD foregroundThreadId = hForeground ? GetWindowThreadProcessId(hForeground, NULL) : 0;
                DWORD currentThreadId = GetCurrentThreadId();

                if (foregroundThreadId != currentThreadId && foregroundThreadId != 0) {
                    AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);
                    SetForegroundWindow(m_hwnd);
                    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW);
                    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW);
                    BringWindowToTop(m_hwnd);
                    SetFocus(m_hwnd);
                    AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
                } else {
                    SetForegroundWindow(m_hwnd);
                    BringWindowToTop(m_hwnd);
                    SetFocus(m_hwnd);
                }
                InvalidateRect(m_hwnd, NULL, FALSE);
                return TRUE;
            }
            return FALSE;
        }

        case WM_HOTKEY: {
            if (wParam == ExplorerHook::HOTKEY_ID) {
                PostMessageW(m_hwnd, ExplorerHook::WM_WINPREVIEW_EXPLORER_SPACE, 0, 0);
                return 0;
            }
            break;
        }

        case ExplorerHook::WM_WINPREVIEW_CLOSE: {
            HideMainWindow();
            return 0;
        }

        case WM_WTSSESSION_CHANGE: {
            if (wParam == 7 /* WTS_SESSION_LOCK */) {
                HideMainWindow();
            } else if (wParam == 8 /* WTS_SESSION_UNLOCK */ || wParam == 5 /* WTS_SESSION_LOGON */) {
                ExplorerHook::EnsureHookActive(m_hwnd);
                ExplorerHook::ReapplyRegisteredHotkey(m_hwnd);
            }
            return 0;
        }

        case WM_POWERBROADCAST: {
            if (wParam == 4 /* PBT_APMRESUMEAUTOMATIC */ || wParam == 7 /* PBT_APMRESUMESUSPEND */) {
                ExplorerHook::EnsureHookActive(m_hwnd);
                ExplorerHook::ReapplyRegisteredHotkey(m_hwnd);
            }
            return TRUE;
        }

        case ExplorerHook::WM_WINPREVIEW_EXPLORER_SPACE: {
            DWORD now = GetTickCount();
            if (now - m_lastHookToggleTime < 180) {
                return 0; // Debounce rapid repeated triggers
            }
            m_lastHookToggleTime = now;

            auto forceForeground = [this]() {
                if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                else ShowWindow(m_hwnd, SW_SHOW);

                HWND hForeground = GetForegroundWindow();
                DWORD foregroundThreadId = hForeground ? GetWindowThreadProcessId(hForeground, NULL) : 0;
                DWORD currentThreadId = GetCurrentThreadId();

                if (foregroundThreadId != currentThreadId && foregroundThreadId != 0) {
                    AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);
                    SetForegroundWindow(m_hwnd);
                    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW);
                    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW);
                    BringWindowToTop(m_hwnd);
                    SetFocus(m_hwnd);
                    AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
                } else {
                    SetForegroundWindow(m_hwnd);
                    BringWindowToTop(m_hwnd);
                    SetFocus(m_hwnd);
                }
            };

            std::wstring selectedPath;
            bool hasSelection = ExplorerHook::GetSelectedExplorerPath(selectedPath) && !selectedPath.empty();

            if (IsWindowVisible(m_hwnd)) {
                // If preview is visible, tapping hotkey on the same file or empty selection toggles preview off
                if (!hasSelection || _wcsicmp(selectedPath.c_str(), m_currentFilePath.c_str()) == 0) {
                    HideMainWindow();
                    return 0;
                }
                // Otherwise a new/different file was selected in Explorer: switch to previewing that new file!
                OpenFile(selectedPath);
                forceForeground();
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            // Window was hidden: open selected file
            if (hasSelection) {
                OpenFile(selectedPath);
                forceForeground();
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else {
                HotkeyConfig cfg = ExplorerHook::GetHotkeyConfig();
                if (cfg.vkCode == VK_SPACE && !cfg.ctrl && !cfg.alt && !cfg.shift && !cfg.win) {
                    ExplorerHook::PassSynthesizedSpace();
                } else {
                    // Custom hotkey pressed without selection: show clean Welcome screen
                    ShowWindow(m_hwnd, SW_SHOW);
                    forceForeground();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case ExplorerHook::WM_WINPREVIEW_OPEN_FILE: {
            m_lastHookToggleTime = GetTickCount();
            WCHAR* pPath = (WCHAR*)wParam;
            if (pPath) {
                std::wstring pathStr = pPath;
                delete[] pPath;
                OpenFile(pathStr);
                if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                else ShowWindow(m_hwnd, SW_SHOW);

                HWND hForeground = GetForegroundWindow();
                DWORD foregroundThreadId = hForeground ? GetWindowThreadProcessId(hForeground, NULL) : 0;
                DWORD currentThreadId = GetCurrentThreadId();

                if (foregroundThreadId != currentThreadId && foregroundThreadId != 0) {
                    AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);
                    SetForegroundWindow(m_hwnd);
                    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW);
                    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW);
                    BringWindowToTop(m_hwnd);
                    SetFocus(m_hwnd);
                    AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
                } else {
                    SetForegroundWindow(m_hwnd);
                    BringWindowToTop(m_hwnd);
                    SetFocus(m_hwnd);
                }
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
            return 0;
        }

        case ExplorerHook::WM_WINPREVIEW_QUERY_PATH: {
            const wchar_t* queryPath = (const wchar_t*)lParam;
            if (queryPath && !m_currentFilePath.empty()) {
                if (_wcsicmp(m_currentFilePath.c_str(), queryPath) == 0) {
                    return 1; // Same file is currently loaded
                }
            }
            return 0;
        }

        case ExplorerHook::WM_WINPREVIEW_EXPLORER_NAV: {
            if (wParam == 0) {
                // Query: Can Spacebar close WinPreview?
                if (m_showSettings || m_docType == DocumentType::Text) {
                    return 0; // In settings modal or text editor, don't close WinPreview on Space!
                }
                return 1; // Can close on Spacebar
            } else if (wParam == 1) {
                // Explorer selection changed via arrow keys (live navigation)
                // Set a 30ms debounce timer so Explorer has time to shift its selection in its shell view!
                SetTimer(m_hwnd, TIMER_EXPLORER_NAV, 30, NULL);
            }
            return 0;
        }

        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Open WinPreview");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SETTINGS, L"Keybind Settings...\tCtrl+,");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                bool autoStart = IsAutoStartEnabled();
                AppendMenuW(hMenu, MF_STRING | (autoStart ? MF_CHECKED : MF_UNCHECKED), IDM_TRAY_AUTOSTART, L"Start with Windows");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit WinPreview");

                SetForegroundWindow(m_hwnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hwnd, NULL);
                DestroyMenu(hMenu);
            } else if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                if (IsWindowVisible(m_hwnd)) {
                    HideMainWindow();
                } else {
                    ShowWindow(m_hwnd, SW_SHOW);
                    SetForegroundWindow(m_hwnd);
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            WORD cmdId = LOWORD(wParam);
            if (cmdId == 9999) {
                HideMainWindow();
                return 0;
            } else if (cmdId == 1001) {
                WakeAllUiAndCursor();
                m_media.TogglePlayPause();
            } else if (cmdId == 1002) {
                WakeAllUiAndCursor();
                ToggleFullscreen();
            } else if (cmdId == IDM_TRAY_OPEN) {
                if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                else ShowWindow(m_hwnd, SW_SHOW);
                SetForegroundWindow(m_hwnd);
                BringWindowToTop(m_hwnd);
                SetFocus(m_hwnd);
            } else if (cmdId == IDM_TRAY_SETTINGS) {
                if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                else ShowWindow(m_hwnd, SW_SHOW);
                SetForegroundWindow(m_hwnd);
                BringWindowToTop(m_hwnd);
                SetFocus(m_hwnd);
                if (!m_showSettings) {
                    ToggleSettings();
                }
            } else if (cmdId == IDM_TRAY_AUTOSTART) {
                SetAutoStartEnabled(!IsAutoStartEnabled());
            } else if (cmdId == IDM_TRAY_EXIT) {
                RemoveTrayIcon();
                ExplorerHook::UninstallHook();
                DestroyWindow(m_hwnd);
            } else if (cmdId == BTN_SAVE_FILE) {
                SaveCurrentTextFile();
            } else if (cmdId == BTN_UNDO) {
                if (m_text.Undo()) { UpdateWindowTitle(); InvalidateRect(m_hwnd, NULL, FALSE); }
            } else if (cmdId == BTN_REDO) {
                if (m_text.Redo()) { UpdateWindowTitle(); InvalidateRect(m_hwnd, NULL, FALSE); }
            } else if (cmdId == BTN_CUT) {
                if (m_text.Cut(m_hwnd)) { UpdateWindowTitle(); ShowToast(L"Cut to clipboard"); InvalidateRect(m_hwnd, NULL, FALSE); }
            } else if (cmdId == BTN_COPY_TEXT) {
                if (m_docType == DocumentType::Spreadsheet && m_text.IsTableView()) {
                    CopyTextToClipboard(m_hwnd, m_text.GetAllText());
                    ShowToast(L"Table copied to clipboard!");
                } else {
                    if (m_text.Copy(m_hwnd)) { ShowToast(m_text.HasSelection() ? L"Copied selection" : L"Copied line"); }
                }
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (cmdId == BTN_PASTE) {
                if (m_text.Paste(m_hwnd)) { UpdateWindowTitle(); ShowToast(L"Pasted"); InvalidateRect(m_hwnd, NULL, FALSE); }
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            WakeAllUiAndCursor();
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT physPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(m_hwnd, &physPt);
            D2D1_POINT_2F pt = ClientToDip(physPt);

            if (m_showOverflowMenu &&
                pt.x >= m_overflowMenuRect.left && pt.x <= m_overflowMenuRect.right &&
                pt.y >= m_overflowMenuRect.top && pt.y <= m_overflowMenuRect.bottom) {
                float renderH = m_overflowMenuRect.bottom - m_overflowMenuRect.top;
                if (m_overflowTotalH > renderH) {
                    float maxScroll = m_overflowTotalH - renderH;
                    m_targetOverflowScrollY += (delta > 0 ? -45.0f : 45.0f);
                    m_targetOverflowScrollY = (std::clamp)(m_targetOverflowScrollY, 0.0f, maxScroll);
                    m_overflowScrollY = m_targetOverflowScrollY;
                    m_overflowHoveredIndex = GetOverflowItemIndexAt(pt);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }
            }

            if (m_showSidebar && pt.x < m_sidebarWidth) {
                D2D1_SIZE_F clientSz = GetClientSizeDip();
                float availH = clientSz.height;
                if (m_sidebarTab == 0) {
                    float totalH = 54.0f + m_thumbnails.size() * (136.0f + 12.0f);
                    float minScroll = availH - totalH;
                    if (minScroll > 0.0f) minScroll = 0.0f;
                    m_targetSidebarScrollY += (delta > 0 ? 80.0f : -80.0f);
                    m_targetSidebarScrollY = (std::clamp)(m_targetSidebarScrollY, minScroll, 0.0f);
                    m_sidebarScrollY = m_targetSidebarScrollY;
                } else {
                    float totalH = 54.0f + m_outline.size() * 31.0f;
                    float minScroll = availH - totalH;
                    if (minScroll > 0.0f) minScroll = 0.0f;
                    m_targetOutlineScrollY += (delta > 0 ? 70.0f : -70.0f);
                    m_targetOutlineScrollY = (std::clamp)(m_targetOutlineScrollY, minScroll, 0.0f);
                    m_outlineScrollY = m_targetOutlineScrollY;
                }
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            if (m_docType == DocumentType::Media) {
                WakeHudAndCursor();
                int vol = m_media.GetVolume() + (delta > 0 ? 5 : -5);
                m_media.SetVolume(vol);
                if (m_hudHwnd) InvalidateRect(m_hudHwnd, NULL, FALSE);
                return 0;
            }

            if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (delta > 0) m_text.ZoomIn();
                    else m_text.ZoomOut();
                } else if (GetKeyState(VK_SHIFT) & 0x8000) {
                    m_text.Scroll(0.0f, (float)delta * 0.8f);
                } else {
                    m_text.Scroll(-(float)delta * 0.8f, 0.0f);
                }
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            if (m_docType == DocumentType::Font) {
                m_font.Scroll((float)delta * 0.75f);
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            if (m_docType == DocumentType::Folder) {
                m_folder.Scroll((float)delta * 0.75f);
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            if (m_docType == DocumentType::Markdown || m_docType == DocumentType::Notebook) {
                m_md.Scroll((float)delta * 0.75f);
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            if (m_docType == DocumentType::Binary || m_docType == DocumentType::Shortcut || m_docType == DocumentType::Certificate) {
                m_system.Scroll((float)delta * 0.75f);
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            D2D1_RECT_F canvasRect = GetCanvasRectDip();
            float canvasCenterX = canvasRect.left + (canvasRect.right - canvasRect.left) / 2.0f;
            float canvasCenterY = canvasRect.top + (canvasRect.bottom - canvasRect.top) / 2.0f;

            // Physical focal-point preserving zoom calculation
            float cursorDocX = (pt.x - canvasCenterX - m_panX) / (m_zoom > 0.001f ? m_zoom : 1.0f);
            float cursorDocY = (pt.y - canvasCenterY - m_panY) / (m_zoom > 0.001f ? m_zoom : 1.0f);

            float factor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
            m_zoom = (std::clamp)(m_zoom * factor, 0.05f, 25.0f);
            m_targetZoom = m_zoom;

            // Anchor pan so the document pixel directly under the mouse stays pinned
            m_panX = pt.x - canvasCenterX - cursorDocX * m_zoom;
            m_panY = pt.y - canvasCenterY - cursorDocY * m_zoom;
            m_targetPanX = m_panX;
            m_targetPanY = m_panY;
            m_isMomentumActive = false;
            m_dragVelX = 0.0f;
            m_dragVelY = 0.0f;

            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEHWHEEL: {
            WakeAllUiAndCursor();
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                m_text.Scroll(0.0f, (float)delta * 0.8f);
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (m_docType == DocumentType::Image || m_docType == DocumentType::Pdf || m_docType == DocumentType::Office || m_docType == DocumentType::Svg) {
                m_panX -= (float)delta * 0.6f;
                m_targetPanX = m_panX;
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            SetCapture(m_hwnd);
            POINT physPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            D2D1_POINT_2F pt = ClientToDip(physPt);
            m_lastMousePos = pt;

            WakeAllUiAndCursor();

            auto hit = [&](const D2D1_RECT_F& r) {
                return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
            };

            // -0.5 In-App Update Banner clicks
            UpdateState updateState = m_updater.GetState();
            if (updateState == UpdateState::UpdateAvailable || updateState == UpdateState::Downloaded) {
                if (hit(m_updateActionBtnRect)) {
                    if (updateState == UpdateState::UpdateAvailable) {
                        m_updater.DownloadUpdateAsync(
                            [this](int pct) {
                                InvalidateRect(m_hwnd, NULL, FALSE);
                            },
                            [this](bool success, const std::wstring& path) {
                                InvalidateRect(m_hwnd, NULL, FALSE);
                                if (!success) {
                                    ShowToast(L"Failed to download update.");
                                }
                            }
                        );
                    } else if (updateState == UpdateState::Downloaded) {
                        bool launched = m_updater.LaunchInstallerAndExit(m_hwnd);
                        if (!launched) {
                            ShowToast(L"Test Verified! (In production, launches installer)");
                            InvalidateRect(m_hwnd, NULL, FALSE);
                        }
                    }
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }
                if (hit(m_updateDismissBtnRect)) {
                    m_updater.Dismiss();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }
            }

            // -0.8 Overflow Menu clicks & dismiss
            if (m_showOverflowMenu) {
                if (hit(m_overflowMenuRect)) {
                    HandleOverflowMenuClick(pt);
                    return 0;
                } else if (!hit(m_btnOverflowRect)) {
                    m_showOverflowMenu = false;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }
            }

            // -1. Preferences & Keybind settings modal clicks
            if (m_showSettings) {
                if (hit(m_settingsCloseBtn) || hit(m_settingsDoneBtn)) {
                    ToggleSettings();
                } else if (hit(m_settingsRecordBtn)) {
                    m_isRecordingKeybind = !m_isRecordingKeybind;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_settingsResetBtn)) {
                    HotkeyConfig defCfg;
                    defCfg.name = L"Space";
                    defCfg.vkCode = VK_SPACE;
                    defCfg.ctrl = false;
                    defCfg.alt = false;
                    defCfg.shift = false;
                    defCfg.win = false;
                    ExplorerHook::SetHotkeyConfig(defCfg);
                    ExplorerHook::SaveHotkeyConfig();
                    ExplorerHook::ReapplyRegisteredHotkey(m_hwnd);
                    m_isRecordingKeybind = false;
                    ShowToast(L"Keybind reset to Space");
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_settingsAutostartBtn)) {
                    SetAutoStartEnabled(!IsAutoStartEnabled());
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else {
                    bool hitPreset = false;
                    for (int i = 0; i < 5; ++i) {
                        if (hit(m_settingsPresetRects[i])) {
                            HotkeyConfig cfg;
                            if (i == 0) { cfg.name = L"Space"; cfg.vkCode = VK_SPACE; }
                            else if (i == 1) { cfg.name = L"Ctrl + Space"; cfg.vkCode = VK_SPACE; cfg.ctrl = true; }
                            else if (i == 2) { cfg.name = L"Alt + Space"; cfg.vkCode = VK_SPACE; cfg.alt = true; }
                            else if (i == 3) { cfg.name = L"Shift + Space"; cfg.vkCode = VK_SPACE; cfg.shift = true; }
                            else if (i == 4) { cfg.name = L"F4"; cfg.vkCode = VK_F4; }

                            ExplorerHook::SetHotkeyConfig(cfg);
                            ExplorerHook::SaveHotkeyConfig();
                            ExplorerHook::ReapplyRegisteredHotkey(m_hwnd);
                            m_isRecordingKeybind = false;
                            std::wstring msg = L"Keybind set to " + cfg.name;
                            ShowToast(msg.c_str());
                            InvalidateRect(m_hwnd, NULL, FALSE);
                            hitPreset = true;
                            break;
                        }
                    }
                    if (!hitPreset && !hit(m_settingsModalRect)) {
                        ToggleSettings();
                    }
                }
                return 0;
            }

            // Empty State Keybind Button
            if (m_docType == DocumentType::None && hit(m_emptyStateKeybindBtn)) {
                ToggleSettings();
                return 0;
            }

            // Unsupported State Action Buttons
            if (m_docType == DocumentType::Unsupported) {
                if (hit(m_unsupportedOpenBtnRect)) {
                    ShellExecuteW(NULL, L"open", m_currentFilePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    HideMainWindow();
                    return 0;
                }
                if (hit(m_unsupportedRevealBtnRect)) {
                    std::wstring params = L"/select,\"" + m_currentFilePath + L"\"";
                    ShellExecuteW(NULL, L"open", L"explorer.exe", params.c_str(), NULL, SW_SHOWNORMAL);
                    HideMainWindow();
                    return 0;
                }
                if (hit(m_unsupportedCopyBtnRect)) {
                    CopyTextToClipboard(m_hwnd, m_currentFilePath);
                    ShowToast(L"Copied file path");
                    return 0;
                }
            }

            // System Engine Action Buttons
            if (m_docType == DocumentType::Binary || m_docType == DocumentType::Shortcut || m_docType == DocumentType::Certificate) {
                if (hit(m_system.GetPrimaryBtnRect())) {
                    m_system.ExecutePrimaryAction();
                    HideMainWindow();
                    return 0;
                }
                if (hit(m_system.GetSecondaryBtnRect())) {
                    m_system.ExecuteSecondaryAction();
                    HideMainWindow();
                    return 0;
                }
            }

            // 0. Search bar and inspector clicks
            if (m_showSearch && hit(m_searchBarRect)) {
                if (hit(m_searchPrevBtn)) PrevSearchMatch();
                else if (hit(m_searchNextBtn)) NextSearchMatch();
                else if (hit(m_searchCloseBtn)) ToggleSearch();
                return 0;
            }

            if (m_showInspector && hit(m_inspectorRect)) {
                if (hit(m_inspectorCloseRect)) {
                    m_pressedButtonId = BTN_INSPECTOR_CLOSE;
                    ToggleInspector();
                    return 0;
                }
                if (hit(m_inspectorCopyRect)) {
                    m_pressedButtonId = BTN_INSPECTOR_COPY;
                    CopyInspectorMetadata();
                    return 0;
                }
                return 0;
            }

            // 1. Toolbar dock clicks
            if (m_dockOpacity > 0.1f && hit(m_topDockRect)) {
                if (hit(m_btnSidebarRect)) {
                    m_pressedButtonId = BTN_SIDEBAR;
                    m_showSidebar = !m_showSidebar;
                    UpdateLayout();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_btnPrevPageRect)) {
                    m_pressedButtonId = BTN_PREV_PAGE;
                    PrevPage();
                } else if (hit(m_btnNextPageRect)) {
                    m_pressedButtonId = BTN_NEXT_PAGE;
                    NextPage();
                } else if (hit(m_btnZoomOutRect)) {
                    m_pressedButtonId = BTN_ZOOM_OUT;
                    ZoomOut();
                } else if (hit(m_btnZoomResetRect)) {
                    m_pressedButtonId = BTN_ZOOM_RESET;
                    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.ResetZoom();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    } else {
                        ResetZoom();
                    }
                } else if (hit(m_btnZoomInRect)) {
                    m_pressedButtonId = BTN_ZOOM_IN;
                    ZoomIn();
                } else if (hit(m_btnZoomFitRect)) {
                    m_pressedButtonId = BTN_ZOOM_FIT;
                    FitToWindow();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_btnRotateLeftRect)) {
                    m_pressedButtonId = BTN_ROTATE_L;
                    Rotate(-90);
                } else if (hit(m_btnRotateRightRect)) {
                    m_pressedButtonId = BTN_ROTATE_R;
                    Rotate(90);
                } else if (hit(m_btnToolbarFsRect)) {
                    m_pressedButtonId = BTN_TOOLBAR_FS;
                    ToggleFullscreen();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_btnSearchRect)) {
                    m_pressedButtonId = BTN_SEARCH;
                    ToggleSearch();
                } else if (hit(m_btnCropRect)) {
                    m_pressedButtonId = BTN_CROP;
                    if (m_hasDetectedContentBounds) {
                        m_trimBlackBars = !m_trimBlackBars;
                        int adaptW = (int)m_imageWidth;
                        int adaptH = (int)m_imageHeight;
                        if (m_trimBlackBars) {
                            adaptW = (int)(m_contentBounds.right - m_contentBounds.left + 0.5f);
                            adaptH = (int)(m_contentBounds.bottom - m_contentBounds.top + 0.5f);
                        }
                        AdaptWindowToMedia(adaptW, adaptH);
                        FitToWindow();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    } else {
                        ToggleCrop();
                    }
                } else if (hit(m_btnInfoRect)) {
                    m_pressedButtonId = BTN_INFO;
                    ToggleInspector();
                } else if (hit(m_btnSaveTextRect)) {
                    m_pressedButtonId = BTN_SAVE_FILE;
                    SaveCurrentTextFile();
                } else if (hit(m_btnUndoRect)) {
                    m_pressedButtonId = BTN_UNDO;
                    if (m_text.Undo()) {
                        UpdateWindowTitle();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                } else if (hit(m_btnRedoRect)) {
                    m_pressedButtonId = BTN_REDO;
                    if (m_text.Redo()) {
                        UpdateWindowTitle();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                } else if (hit(m_btnCutTextRect)) {
                    m_pressedButtonId = BTN_CUT;
                    if (m_text.Cut(m_hwnd)) {
                        UpdateWindowTitle();
                        ShowToast(L"Cut to clipboard");
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                } else if (hit(m_btnCopyTextRect)) {
                    m_pressedButtonId = BTN_COPY_TEXT;
                    if (m_docType == DocumentType::Spreadsheet && m_text.IsTableView()) {
                        CopyTextToClipboard(m_hwnd, m_text.GetAllText());
                        ShowToast(L"Table copied to clipboard!");
                    } else {
                        if (m_text.Copy(m_hwnd)) {
                            ShowToast(m_text.HasSelection() ? L"Copied selection" : L"Copied line");
                        }
                    }
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_btnPasteTextRect)) {
                    m_pressedButtonId = BTN_PASTE;
                    if (m_text.Paste(m_hwnd)) {
                        UpdateWindowTitle();
                        ShowToast(L"Pasted");
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                } else if (hit(m_btnWrapTextRect)) {
                    m_pressedButtonId = BTN_WRAP_TEXT;
                    m_text.ToggleWordWrap();
                    ShowToast(m_text.IsWordWrap() ? L"Word Wrap: ON" : L"Word Wrap: OFF");
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_btnTableViewRect)) {
                    m_pressedButtonId = BTN_TABLE_VIEW;
                    m_text.ToggleTableView();
                    ShowToast(m_text.IsTableView() ? L"Grid View" : L"Raw Text View");
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_btnOpenWithRect)) {
                    m_pressedButtonId = BTN_OPEN_WITH;
                    OpenCurrentFileInDefaultApp();
                } else if (hit(m_btnSettingsRect)) {
                    m_pressedButtonId = BTN_SETTINGS;
                    ToggleSettings();
                } else if (hit(m_btnOverflowRect)) {
                    m_pressedButtonId = BTN_OVERFLOW_MENU;
                    m_showOverflowMenu = !m_showOverflowMenu;
                    m_overflowScrollY = 0.0f;
                    m_overflowHoveredIndex = -1;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
                return 0;
            }

            // 2. Sidebar clicks (tab switching, page selection, or outline jump)
            if (m_showSidebar && pt.x < m_sidebarWidth) {
                if (hit(m_tabThumbsRect)) {
                    m_sidebarTab = 0;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (hit(m_tabOutlineRect)) {
                    m_sidebarTab = 1;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }

                if (m_sidebarTab == 0) {
                    for (const auto& item : m_thumbnails) {
                        if (pt.x >= item.rect.left && pt.x <= item.rect.right &&
                            pt.y >= item.rect.top && pt.y <= item.rect.bottom) {
                            SetPage(item.pageIndex);
                            break;
                        }
                    }
                } else {
                    float startY = 54.0f + m_outlineScrollY;
                    for (const auto& item : m_outline) {
                        float itemH = 28.0f;
                        if (pt.y >= startY && pt.y <= startY + itemH) {
                            SetPage(item.pageIndex);
                            break;
                        }
                        startY += itemH + 3.0f;
                    }
                }
                return 0;
            }

            // 3. Media canvas click
            if (m_docType == DocumentType::Media) {
                WakeHudAndCursor();
                if (m_isAudioOnly && hit(m_audioWaveformRect)) {
                    m_isWaveformSeeking = true;
                    SetCapture(m_hwnd);
                    float frac = (pt.x - m_audioWaveformRect.left) / (m_audioWaveformRect.right - m_audioWaveformRect.left);
                    m_media.Seek(std::clamp(frac, 0.0f, 1.0f));
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    if (m_hudHwnd) InvalidateRect(m_hudHwnd, NULL, FALSE);
                    return 0;
                }
                m_media.TogglePlayPause();
                InvalidateRect(m_hwnd, NULL, FALSE);
                if (m_hudHwnd) InvalidateRect(m_hudHwnd, NULL, FALSE);
                return 0;
            }

            // 3b. Text & Spreadsheet scrollbar, status bar, and canvas text interaction
            if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                D2D1_RECT_F canvasRect = GetCanvasRectDip();

                // 1. Scrollbar drag check
                if (m_text.HandleMouseDown(pt.x, pt.y, canvasRect)) {
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }

                // 2. Status bar click check (Spaces toggle, Word Wrap toggle, Save modified)
                if (m_text.HandleStatusBarClick(pt.x, pt.y, canvasRect, m_dpiScale)) {
                    UpdateWindowTitle();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }

                // 3. Text canvas click (caret placement, shift+click range selection)
                if (m_docType == DocumentType::Text || !m_text.IsTableView()) {
                    bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    m_text.HandleClick(pt.x, pt.y, canvasRect, shiftHeld, m_dpiScale);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }
                return 0;
            }

            // 4. Crop handles drag
            if (m_isCropActive && m_docType == DocumentType::Image && m_imageWidth > 0 && m_imageHeight > 0) {
                D2D1_RECT_F canvasRect = GetCanvasRectDip();
                float targetW = (float)m_imageWidth * m_zoom;
                float targetH = (float)m_imageHeight * m_zoom;
                float canvasCenterX = canvasRect.left + (canvasRect.right - canvasRect.left) / 2.0f;
                float canvasCenterY = canvasRect.top + (canvasRect.bottom - canvasRect.top) / 2.0f;
                float destLeft = canvasCenterX - (targetW / 2.0f) + m_panX;
                float destTop = canvasCenterY - (targetH / 2.0f) + m_panY;
                float cLeft = destLeft + (m_cropRect.left / (float)m_imageWidth) * targetW;
                float cTop = destTop + (m_cropRect.top / (float)m_imageHeight) * targetH;
                float cRight = destLeft + (m_cropRect.right / (float)m_imageWidth) * targetW;
                float cBottom = destTop + (m_cropRect.bottom / (float)m_imageHeight) * targetH;

                auto nearPt = [&](float x, float y) {
                    return std::abs(pt.x - x) <= 10.0f && std::abs(pt.y - y) <= 10.0f;
                };

                if (nearPt(cLeft, cTop)) m_cropDragHandle = 1;
                else if (nearPt(cRight, cTop)) m_cropDragHandle = 2;
                else if (nearPt(cRight, cBottom)) m_cropDragHandle = 3;
                else if (nearPt(cLeft, cBottom)) m_cropDragHandle = 4;
                else if (pt.x >= cLeft && pt.x <= cRight && pt.y >= cTop && pt.y <= cBottom) m_cropDragHandle = 5;

                if (m_cropDragHandle > 0) {
                    m_lastCropMouse = pt;
                    return 0;
                }
            }

            // 5. PDF Text Selection
            if (m_docType == DocumentType::Pdf) {
                D2D1_RECT_F canvasRect = GetCanvasRectDip();
                D2D1_POINT_2F pagePt = CanvasToPageCoords(pt.x, pt.y, canvasRect);
                int charIdx = m_pdf.GetCharIndexAtPos(pagePt.x, pagePt.y, 8.0, 8.0);
                if (charIdx >= 0) {
                    m_isSelectingText = true;
                    m_selStartIndex = charIdx;
                    m_selEndIndex = charIdx;
                    m_isDragging = false;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else {
                    if (m_selStartIndex >= 0) {
                        m_selStartIndex = -1;
                        m_selEndIndex = -1;
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                }
            }

            // 6. Canvas pan drag
            m_isDragging = true;
            m_isMomentumActive = false;
            m_dragVelX = 0.0f;
            m_dragVelY = 0.0f;
            m_targetPanX = m_panX;
            m_targetPanY = m_panY;
            return 0;
        }

        case WM_MOUSEMOVE: {
            POINT physPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            D2D1_POINT_2F pt = ClientToDip(physPt);

            if (!m_isMouseTracking) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
                if (TrackMouseEvent(&tme)) {
                    m_isMouseTracking = true;
                }
            }

            int dx = std::abs(physPt.x - m_lastActiveMousePos.x);
            int dy = std::abs(physPt.y - m_lastActiveMousePos.y);

            if (dx >= 2 || dy >= 2) {
                m_lastActiveMousePos = physPt;
                GetCursorPos(&m_lastGlobalMousePos);
                WakeAllUiAndCursor("WM_MOUSEMOVE");
            }

            // Crop dragging
            if (m_isCropActive && m_cropDragHandle > 0 && m_imageWidth > 0 && m_imageHeight > 0) {
                float dx = (pt.x - m_lastCropMouse.x) / m_zoom;
                float dy = (pt.y - m_lastCropMouse.y) / m_zoom;
                switch (m_cropDragHandle) {
                    case 1: m_cropRect.left += dx; m_cropRect.top += dy; break;
                    case 2: m_cropRect.right += dx; m_cropRect.top += dy; break;
                    case 3: m_cropRect.right += dx; m_cropRect.bottom += dy; break;
                    case 4: m_cropRect.left += dx; m_cropRect.bottom += dy; break;
                    case 5:
                        m_cropRect.left += dx; m_cropRect.right += dx;
                        m_cropRect.top += dy; m_cropRect.bottom += dy;
                        break;
                }
                m_cropRect.left = (std::clamp)(m_cropRect.left, 0.0f, (float)m_imageWidth);
                m_cropRect.right = (std::clamp)(m_cropRect.right, 0.0f, (float)m_imageWidth);
                m_cropRect.top = (std::clamp)(m_cropRect.top, 0.0f, (float)m_imageHeight);
                m_cropRect.bottom = (std::clamp)(m_cropRect.bottom, 0.0f, (float)m_imageHeight);
                m_lastCropMouse = pt;
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            // Text selection dragging
            if (m_isSelectingText && m_docType == DocumentType::Pdf) {
                D2D1_RECT_F canvasRect = GetCanvasRectDip();
                D2D1_POINT_2F pagePt = CanvasToPageCoords(pt.x, pt.y, canvasRect);
                int charIdx = m_pdf.GetCharIndexAtPos(pagePt.x, pagePt.y, 16.0, 16.0);
                if (charIdx >= 0 && charIdx != m_selEndIndex) {
                    m_selEndIndex = charIdx;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
                return 0;
            }

            // Audio waveform scrubbing & hover tracking
            if (m_docType == DocumentType::Media && m_isAudioOnly) {
                float width = m_audioWaveformRect.right - m_audioWaveformRect.left;
                if (m_isWaveformSeeking) {
                    if (width > 0.0f) {
                        float frac = (pt.x - m_audioWaveformRect.left) / width;
                        m_media.Seek((std::clamp)(frac, 0.0f, 1.0f));
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        if (m_hudHwnd) InvalidateRect(m_hudHwnd, NULL, FALSE);
                    }
                    return 0;
                }
                float oldHover = m_audioHoverFrac;
                if (width > 0.0f && pt.x >= m_audioWaveformRect.left && pt.x <= m_audioWaveformRect.right &&
                    pt.y >= m_audioWaveformRect.top && pt.y <= m_audioWaveformRect.bottom) {
                    m_audioHoverFrac = (pt.x - m_audioWaveformRect.left) / width;
                } else {
                    m_audioHoverFrac = -1.0f;
                }
                if (std::abs(m_audioHoverFrac - oldHover) > 0.02f) {
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }


            // Text / Spreadsheet scrollbar dragging or text drag selection
            if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                D2D1_RECT_F canvasRect = GetCanvasRectDip();
                if (m_text.IsDraggingScrollbar()) {
                    if (m_text.HandleMouseMove(pt.x, pt.y, canvasRect)) {
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (m_text.IsSelectingText()) {
                    m_text.HandleDrag(pt.x, pt.y, canvasRect, m_dpiScale);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }
            }

            // Hover tracking
            int oldHover = m_hoveredButtonId;
            m_hoveredButtonId = BTN_NONE;

            auto hit = [&](const D2D1_RECT_F& r) {
                return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
            };

            UpdateState updateState = m_updater.GetState();
            if (updateState == UpdateState::UpdateAvailable || updateState == UpdateState::Downloaded) {
                bool actHover = hit(m_updateActionBtnRect);
                bool disHover = hit(m_updateDismissBtnRect);
                if (actHover != m_updateActionBtnHover || disHover != m_updateDismissBtnHover) {
                    m_updateActionBtnHover = actHover;
                    m_updateDismissBtnHover = disHover;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
                if (actHover || disHover) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                }
            }

            if (m_dockOpacity > 0.1f) {
                if (hit(m_btnSidebarRect)) m_hoveredButtonId = BTN_SIDEBAR;
                else if (hit(m_btnPrevPageRect)) m_hoveredButtonId = BTN_PREV_PAGE;
                else if (hit(m_btnNextPageRect)) m_hoveredButtonId = BTN_NEXT_PAGE;
                else if (hit(m_btnZoomOutRect)) m_hoveredButtonId = BTN_ZOOM_OUT;
                else if (hit(m_btnZoomResetRect)) m_hoveredButtonId = BTN_ZOOM_RESET;
                else if (hit(m_btnZoomInRect)) m_hoveredButtonId = BTN_ZOOM_IN;
                else if (hit(m_btnZoomFitRect)) m_hoveredButtonId = BTN_ZOOM_FIT;
                else if (hit(m_btnRotateLeftRect)) m_hoveredButtonId = BTN_ROTATE_L;
                else if (hit(m_btnRotateRightRect)) m_hoveredButtonId = BTN_ROTATE_R;
                else if (hit(m_btnToolbarFsRect)) m_hoveredButtonId = BTN_TOOLBAR_FS;
                else if (hit(m_btnSearchRect)) m_hoveredButtonId = BTN_SEARCH;
                else if (hit(m_btnCropRect)) m_hoveredButtonId = BTN_CROP;
                else if (hit(m_btnInfoRect)) m_hoveredButtonId = BTN_INFO;
                else if (hit(m_btnSaveTextRect)) m_hoveredButtonId = BTN_SAVE_FILE;
                else if (hit(m_btnUndoRect)) m_hoveredButtonId = BTN_UNDO;
                else if (hit(m_btnRedoRect)) m_hoveredButtonId = BTN_REDO;
                else if (hit(m_btnCutTextRect)) m_hoveredButtonId = BTN_CUT;
                else if (hit(m_btnCopyTextRect)) m_hoveredButtonId = BTN_COPY_TEXT;
                else if (hit(m_btnPasteTextRect)) m_hoveredButtonId = BTN_PASTE;
                else if (hit(m_btnWrapTextRect)) m_hoveredButtonId = BTN_WRAP_TEXT;
                else if (hit(m_btnTableViewRect)) m_hoveredButtonId = BTN_TABLE_VIEW;
                else if (hit(m_btnOpenWithRect)) m_hoveredButtonId = BTN_OPEN_WITH;
                else if (hit(m_btnSettingsRect)) m_hoveredButtonId = BTN_SETTINGS;
                else if (hit(m_btnOverflowRect)) m_hoveredButtonId = BTN_OVERFLOW_MENU;
            }
            if (m_showOverflowMenu) {
                int oldHover = m_overflowHoveredIndex;
                m_overflowHoveredIndex = GetOverflowItemIndexAt(pt);
                if (m_overflowHoveredIndex != oldHover) {
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }
            if (m_docType == DocumentType::None && hit(m_emptyStateKeybindBtn)) {
                m_hoveredButtonId = BTN_SETTINGS;
            }
            if (m_docType == DocumentType::Unsupported) {
                if (hit(m_unsupportedOpenBtnRect)) m_hoveredButtonId = BTN_UNSUPPORTED_OPEN;
                else if (hit(m_unsupportedRevealBtnRect)) m_hoveredButtonId = BTN_UNSUPPORTED_REVEAL;
                else if (hit(m_unsupportedCopyBtnRect)) m_hoveredButtonId = BTN_UNSUPPORTED_COPY;
            }
            if (m_docType == DocumentType::Binary || m_docType == DocumentType::Shortcut || m_docType == DocumentType::Certificate) {
                if (hit(m_system.GetPrimaryBtnRect())) m_hoveredButtonId = BTN_SYSTEM_PRIMARY;
                else if (hit(m_system.GetSecondaryBtnRect())) m_hoveredButtonId = BTN_SYSTEM_SECONDARY;
            }
            if (hit(m_tabThumbsRect)) m_hoveredButtonId = BTN_SIDEBAR_TAB_THUMBS;
            else if (hit(m_tabOutlineRect)) m_hoveredButtonId = BTN_SIDEBAR_TAB_OUTLINE;
            else if (hit(m_searchPrevBtn)) m_hoveredButtonId = BTN_SEARCH_PREV;
            else if (hit(m_searchNextBtn)) m_hoveredButtonId = BTN_SEARCH_NEXT;
            else if (hit(m_searchCloseBtn)) m_hoveredButtonId = BTN_SEARCH_CLOSE;

            if (m_showInspector) {
                if (hit(m_inspectorCloseRect)) m_hoveredButtonId = BTN_INSPECTOR_CLOSE;
                else if (hit(m_inspectorCopyRect)) m_hoveredButtonId = BTN_INSPECTOR_COPY;
            }

            if (m_showSettings) {
                if (hit(m_settingsCloseBtn)) m_hoveredButtonId = BTN_SETTINGS_CLOSE;
                else if (hit(m_settingsRecordBtn)) m_hoveredButtonId = BTN_SETTINGS_RECORD;
                else if (hit(m_settingsResetBtn)) m_hoveredButtonId = BTN_SETTINGS_RESET;
                else if (hit(m_settingsDoneBtn)) m_hoveredButtonId = BTN_SETTINGS_DONE;
                else if (hit(m_settingsAutostartBtn)) m_hoveredButtonId = BTN_SETTINGS_AUTOSTART;
                else {
                    for (int i = 0; i < 5; ++i) {
                        if (hit(m_settingsPresetRects[i])) {
                            m_hoveredButtonId = BTN_SETTINGS_PRESET_1 + i;
                            break;
                        }
                    }
                }
            }

            int oldThumbHover = m_hoveredThumbnailIndex;
            m_hoveredThumbnailIndex = -1;
            if (m_showSidebar && m_sidebarTab == 0 && pt.x < m_sidebarWidth) {
                for (size_t i = 0; i < m_thumbnails.size(); i++) {
                    const auto& item = m_thumbnails[i];
                    if (pt.x >= item.rect.left && pt.x <= item.rect.right &&
                        pt.y >= item.rect.top && pt.y <= item.rect.bottom) {
                        m_hoveredThumbnailIndex = (int)i;
                        break;
                    }
                }
            }

            if (oldHover != m_hoveredButtonId || oldThumbHover != m_hoveredThumbnailIndex) {
                InvalidateRect(m_hwnd, NULL, FALSE);
            }

            if (m_isDragging) {
                float dx = pt.x - m_lastMousePos.x;
                float dy = pt.y - m_lastMousePos.y;
                m_panX += dx;
                m_panY += dy;
                m_targetPanX = m_panX;
                m_targetPanY = m_panY;
                m_lastMousePos = pt;
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            m_isMouseTracking = false;
            SetCursorVisibility(true);
            m_hoveredButtonId = BTN_NONE;
            m_hoveredThumbnailIndex = -1;
            m_audioHoverFrac = -1.0f;
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            ReleaseCapture();
            m_isDragging = false;
            m_isMomentumActive = false;
            m_dragVelX = 0.0f;
            m_dragVelY = 0.0f;
            m_isScrubbing = false;
            m_isWaveformSeeking = false;
            m_isSelectingText = false;
            m_cropDragHandle = 0;
            m_pressedButtonId = BTN_NONE;
            m_text.HandleMouseUp();
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            POINT physPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            D2D1_POINT_2F pt = ClientToDip(physPt);
            auto hit = [&](const D2D1_RECT_F& r) {
                return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
            };
            if (!hit(m_topDockRect)) {
                if (m_docType == DocumentType::Text || (m_docType == DocumentType::Spreadsheet && !m_text.IsTableView())) {
                    D2D1_RECT_F canvasRect = GetCanvasRectDip();
                    m_text.HandleDoubleClick(pt.x, pt.y, canvasRect, m_dpiScale);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (m_docType == DocumentType::Media) {
                    ToggleFullscreen();
                } else {
                    if (std::abs(m_zoom - 1.0f) < 0.1f) {
                        FitToWindow();
                    } else {
                        ResetZoom();
                    }
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_RBUTTONDOWN: {
            WakeAllUiAndCursor("MOUSE_CLICK");
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT physPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            POINT screenPt = physPt;
            ClientToScreen(m_hwnd, &screenPt);
            ShowContextMenu(screenPt.x, screenPt.y);
            return 0;
        }

        case WM_CONTEXTMENU: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (x == -1 && y == -1) {
                RECT rc;
                GetClientRect(m_hwnd, &rc);
                POINT pt = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
                ClientToScreen(m_hwnd, &pt);
                x = pt.x;
                y = pt.y;
            }
            ShowContextMenu(x, y);
            return 0;
        }

        case WM_MBUTTONDOWN: {
            WakeAllUiAndCursor("MOUSE_CLICK");
            break;
        }

        case WM_CHAR: {
            WakeAllUiAndCursor();
            if (m_showSearch) {
                if (wParam == VK_BACK) {
                    if (!m_searchQuery.empty()) {
                        m_searchQuery.pop_back();
                        ExecuteSearch();
                    }
                } else if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
                    // Handled in WM_KEYDOWN
                } else if (wParam >= 32) {
                    m_searchQuery.push_back((WCHAR)wParam);
                    ExecuteSearch();
                }
                return 0;
            }

            if (m_docType == DocumentType::Text || (m_docType == DocumentType::Spreadsheet && !m_text.IsTableView())) {
                wchar_t ch = (wchar_t)wParam;
                if (ch == 19) { // Ctrl+S
                    SaveCurrentTextFile();
                    return 0;
                } else if (ch == 26) { // Ctrl+Z
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShift ? m_text.Redo() : m_text.Undo()) {
                        UpdateWindowTitle();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (ch == 25) { // Ctrl+Y
                    if (m_text.Redo()) {
                        UpdateWindowTitle();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (ch == 1) { // Ctrl+A
                    m_text.SelectAll();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (ch == 24) { // Ctrl+X
                    if (m_text.Cut(m_hwnd)) {
                        UpdateWindowTitle();
                        ShowToast(L"Cut to clipboard");
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (ch == 3) { // Ctrl+C
                    if (m_text.Copy(m_hwnd)) {
                        ShowToast(m_text.HasSelection() ? L"Copied selection" : L"Copied line");
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (ch == 22) { // Ctrl+V
                    if (m_text.Paste(m_hwnd)) {
                        UpdateWindowTitle();
                        ShowToast(L"Pasted");
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (ch >= 32 || ch == L'\t') {
                    if (m_text.InsertChar(ch)) {
                        UpdateWindowTitle();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                }
            }
            break;
        }

        case WM_SYSKEYDOWN:
        case WM_KEYDOWN: {
            WakeAllUiAndCursor();

            bool isCtrl = ((GetKeyState(VK_CONTROL) & 0x8000) != 0) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
            bool isShift = ((GetKeyState(VK_SHIFT) & 0x8000) != 0) || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
            bool isAlt = ((GetKeyState(VK_MENU) & 0x8000) != 0) || ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) || (msg == WM_SYSKEYDOWN);

            // -1. Keybind Recording Mode (Interactive Key Capture)
            if (m_isRecordingKeybind) {
                if (wParam == VK_CONTROL || wParam == VK_SHIFT || wParam == VK_MENU || wParam == VK_LWIN || wParam == VK_RWIN) {
                    return 0; // Pure modifier key down, wait for actual combination key
                }
                if (wParam == VK_ESCAPE) {
                    m_isRecordingKeybind = false;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }

                HotkeyConfig newCfg;
                newCfg.ctrl = isCtrl;
                newCfg.alt = isAlt;
                newCfg.shift = isShift;
                newCfg.win = ((GetKeyState(VK_LWIN) & 0x8000) != 0) || ((GetKeyState(VK_RWIN) & 0x8000) != 0);
                newCfg.vkCode = (UINT)wParam;

                std::wstring name;
                if (newCfg.ctrl) name += L"Ctrl + ";
                if (newCfg.alt) name += L"Alt + ";
                if (newCfg.shift) name += L"Shift + ";
                if (newCfg.win) name += L"Win + ";

                if (wParam == VK_SPACE) name += L"Space";
                else if (wParam >= VK_F1 && wParam <= VK_F24) name += L"F" + std::to_wstring(wParam - VK_F1 + 1);
                else if (wParam >= 'A' && wParam <= 'Z') name += (wchar_t)wParam;
                else if (wParam >= '0' && wParam <= '9') name += (wchar_t)wParam;
                else if (wParam == VK_TAB) name += L"Tab";
                else if (wParam == VK_OEM_3) name += L"`";
                else if (wParam == VK_RETURN) name += L"Enter";
                else {
                    WCHAR keyName[64] = {0};
                    UINT scanCode = MapVirtualKeyW((UINT)wParam, MAPVK_VK_TO_VSC);
                    if (scanCode > 0 && GetKeyNameTextW(scanCode << 16, keyName, 64) > 0) {
                        name += keyName;
                    } else {
                        name += L"Key " + std::to_wstring(wParam);
                    }
                }

                newCfg.name = name;
                ExplorerHook::SetHotkeyConfig(newCfg);
                ExplorerHook::SaveHotkeyConfig();
                ExplorerHook::ReapplyRegisteredHotkey(m_hwnd);
                m_isRecordingKeybind = false;
                std::wstring toast = L"Keybind set to " + name;
                ShowToast(toast.c_str());
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            // -0.5 Overflow Menu Escape
            if (m_showOverflowMenu && wParam == VK_ESCAPE) {
                m_showOverflowMenu = false;
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }

            // 0. Settings Modal Escape
            if (m_showSettings && wParam == VK_ESCAPE) {
                ToggleSettings();
                return 0;
            }

            // 0.1 Inspector Escape
            if (m_showInspector && wParam == VK_ESCAPE) {
                ToggleInspector();
                return 0;
            }

            // 1. Ctrl+, to open/toggle Settings Modal
            if (isCtrl && (wParam == VK_OEM_COMMA || wParam == ',' || wParam == 0xBC)) {
                ToggleSettings();
                return 0;
            }

            // If settings modal is open, consume all other key presses so background document is unaffected
            if (m_showSettings) {
                return 0;
            }

            // Global shortcut: Ctrl+Shift+C -> Copy Full File Path
            if (isCtrl && isShift && (wParam == 'C' || wParam == 'c')) {
                if (!m_currentFilePath.empty()) {
                    CopyTextToClipboard(m_hwnd, m_currentFilePath);
                    ShowToast(L"Copied file path");
                    return 0;
                }
            }

            // Global shortcut: Ctrl+O -> Open in Default System App
            if (isCtrl && !isShift && !isAlt && (wParam == 'O' || wParam == 'o')) {
                if (!m_currentFilePath.empty()) {
                    ShellExecuteW(NULL, L"open", m_currentFilePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    HideMainWindow();
                    return 0;
                }
            }

            // Global shortcut: F5 or Ctrl+R -> Reload current preview
            if ((wParam == VK_F5) || (isCtrl && !isShift && !isAlt && (wParam == 'R' || wParam == 'r'))) {
                if (!m_currentFilePath.empty()) {
                    std::wstring cur = m_currentFilePath;
                    OpenFile(cur);
                    ShowToast(L"Reloaded preview");
                    return 0;
                }
            }

            // Global shortcut: Ctrl+Shift+U -> Simulate update notification (Test UX)
            if (isCtrl && isShift && !isAlt && (wParam == 'U' || wParam == 'u')) {
                m_updater.SimulateUpdateForTesting();
                ShowToast(L"Simulated Update: v1.1.0 Ready to Test");
                ShowUpdatePopup();
                return 0;
            }

            // Global shortcut: Ctrl+U -> Check for updates (Production network query)
            if (isCtrl && !isShift && !isAlt && (wParam == 'U' || wParam == 'u')) {
                CheckForUpdates(true);
                return 0;
            }

            // =========================================================================
            // TEXT & SPREADSHEET EDITOR SHORTCUTS & NAVIGATION (MINI VS CODE)
            // =========================================================================
            if (m_docType == DocumentType::Text || (m_docType == DocumentType::Spreadsheet && !m_text.IsTableView())) {
                if (isCtrl) {
                    if (wParam == 'S') {
                        SaveCurrentTextFile();
                        return 0;
                    } else if (wParam == 'Z') {
                        if (isShift) {
                            if (m_text.Redo()) {
                                UpdateWindowTitle();
                                InvalidateRect(m_hwnd, NULL, FALSE);
                            }
                        } else {
                            if (m_text.Undo()) {
                                UpdateWindowTitle();
                                InvalidateRect(m_hwnd, NULL, FALSE);
                            }
                        }
                        return 0;
                    } else if (wParam == 'Y') {
                        if (m_text.Redo()) {
                            UpdateWindowTitle();
                            InvalidateRect(m_hwnd, NULL, FALSE);
                        }
                        return 0;
                    } else if (wParam == 'A') {
                        m_text.SelectAll();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    } else if (wParam == 'X') {
                        if (m_text.Cut(m_hwnd)) {
                            UpdateWindowTitle();
                            ShowToast(L"Cut to clipboard");
                            InvalidateRect(m_hwnd, NULL, FALSE);
                        }
                        return 0;
                    } else if (wParam == 'C') {
                        if (m_text.Copy(m_hwnd)) {
                            ShowToast(m_text.HasSelection() ? L"Copied selection" : L"Copied line");
                            InvalidateRect(m_hwnd, NULL, FALSE);
                        }
                        return 0;
                    } else if (wParam == 'V') {
                        if (m_text.Paste(m_hwnd)) {
                            UpdateWindowTitle();
                            ShowToast(L"Pasted");
                            InvalidateRect(m_hwnd, NULL, FALSE);
                        }
                        return 0;
                    } else if (wParam == 'I') {
                        ToggleInspector();
                        return 0;
                    }
                }

                // Alt+Z toggles word wrap in VS Code!
                if (isAlt && (wParam == 'Z' || wParam == 'z')) {
                    m_text.ToggleWordWrap();
                    ShowToast(m_text.IsWordWrap() ? L"Word Wrap: ON" : L"Word Wrap: OFF");
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                }

                // Alt+Left / Alt+Right navigates files even while editing text
                if (isAlt && wParam == VK_LEFT) {
                    NavigateFolder(-1);
                    return 0;
                } else if (isAlt && wParam == VK_RIGHT) {
                    NavigateFolder(1);
                    return 0;
                }

                // Keyboard editing keys
                if (wParam == VK_RETURN) {
                    if (isCtrl) {
                        OpenCurrentFileInDefaultApp();
                        return 0;
                    }
                    if (m_text.InsertNewline()) {
                        UpdateWindowTitle();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (wParam == VK_BACK) {
                    if (m_text.DeleteChar(true)) {
                        UpdateWindowTitle();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (wParam == VK_DELETE) {
                    if (m_text.DeleteChar(false)) {
                        UpdateWindowTitle();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (wParam == VK_TAB) {
                    if (m_text.Indent(isShift)) {
                        UpdateWindowTitle();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    }
                    return 0;
                } else if (wParam == VK_LEFT) {
                    if (isCtrl) m_text.MoveWord(-1, isShift);
                    else m_text.MoveCursor(0, -1, isShift);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (wParam == VK_RIGHT) {
                    if (isCtrl) m_text.MoveWord(1, isShift);
                    else m_text.MoveCursor(0, 1, isShift);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (wParam == VK_UP) {
                    m_text.MoveCursor(-1, 0, isShift);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (wParam == VK_DOWN) {
                    m_text.MoveCursor(1, 0, isShift);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (wParam == VK_PRIOR) {
                    m_text.PageUp(600.0f);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (wParam == VK_NEXT) {
                    m_text.PageDown(600.0f);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (wParam == VK_HOME) {
                    m_text.MoveHome(isShift);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (wParam == VK_END) {
                    m_text.MoveEnd(isShift);
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (wParam == VK_F11) {
                    ToggleFullscreen();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                    return 0;
                } else if (wParam == VK_ESCAPE) {
                    if (m_text.HasSelection()) {
                        m_text.ClearSelection();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (m_showInspector) {
                        ToggleInspector();
                        return 0;
                    }
                    if (m_isFullscreen) {
                        ToggleFullscreen();
                        return 0;
                    }
                    HideMainWindow();
                    return 0;
                }
            } else if (m_docType == DocumentType::Spreadsheet && m_text.IsTableView()) {
                if (isCtrl && wParam == 'C') {
                    CopyTextToClipboard(m_hwnd, m_text.GetAllText());
                    ShowToast(L"Table copied to clipboard!");
                    return 0;
                }
            }

            if (isCtrl) {
                if (wParam == 'C' || wParam == 'c') {
                    if (m_docType == DocumentType::Pdf) {
                        CopySelectedText();
                        return 0;
                    } else if (!m_currentFilePath.empty()) {
                        CopyTextToClipboard(m_hwnd, m_currentFilePath);
                        ShowToast(L"Copied file path");
                        return 0;
                    }
                } else if (wParam == 'F' || wParam == 'f') {
                    ToggleSearch();
                    return 0;
                } else if (wParam == 'I' || wParam == 'i') {
                    ToggleInspector();
                    return 0;
                } else if (wParam == 'S' || wParam == 's') {
                    SaveImageFile(isShift);
                    return 0;
                }
            }

            switch (wParam) {
                case VK_SPACE:
                    if (m_docType == DocumentType::Media) {
                        m_media.TogglePlayPause();
                    } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.PageDown(600.0f);
                    } else {
                        NextPage();
                    }
                    break;
                case VK_RETURN:
                    if (m_docType == DocumentType::Unsupported) {
                        ShellExecuteW(NULL, L"open", m_currentFilePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                        HideMainWindow();
                        return 0;
                    }
                    if (m_docType == DocumentType::Binary || m_docType == DocumentType::Shortcut || m_docType == DocumentType::Certificate) {
                        m_system.ExecutePrimaryAction();
                        HideMainWindow();
                        return 0;
                    }
                    if (m_showSearch) {
                        if (isShift) PrevSearchMatch();
                        else NextSearchMatch();
                        return 0;
                    } else if (m_isCropActive) {
                        ApplyCrop();
                        return 0;
                    }
                    OpenCurrentFileInDefaultApp();
                    return 0;
                case 'C':
                    if (m_docType == DocumentType::Image && !isCtrl) {
                        ToggleCrop();
                        return 0;
                    }
                    break;
                case VK_LEFT:
                    if (isAlt) {
                        NavigateFolder(-1);
                        return 0;
                    }
                    if (m_docType == DocumentType::Media) {
                        m_media.SeekSeconds(-5);
                    } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.Scroll(0.0f, -48.0f);
                    } else {
                        PrevPage();
                    }
                    break;
                case VK_RIGHT:
                    if (isAlt) {
                        NavigateFolder(1);
                        return 0;
                    }
                    if (m_docType == DocumentType::Media) {
                        m_media.SeekSeconds(5);
                    } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.Scroll(0.0f, 48.0f);
                    } else {
                        NextPage();
                    }
                    break;
                case VK_PRIOR: // Page Up
                    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.PageUp(600.0f);
                    } else {
                        PrevPage();
                    }
                    break;
                case VK_NEXT: // Page Down
                    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.PageDown(600.0f);
                    } else {
                        NextPage();
                    }
                    break;
                case VK_HOME:
                    if (m_docType == DocumentType::Pdf) {
                        SetPage(0);
                    } else if (m_docType == DocumentType::Media) {
                        m_media.Seek(0.0f);
                    } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.ScrollToTop();
                    }
                    break;
                case VK_END:
                    if (m_docType == DocumentType::Pdf && m_totalPages > 0) {
                        SetPage(m_totalPages - 1);
                    } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.ScrollToBottom(600.0f);
                    }
                    break;
                case VK_BACK:
                    PrevPage();
                    break;
                case VK_UP:
                    if (m_docType == DocumentType::Media) {
                        m_media.SetVolume(m_media.GetVolume() + 5);
                    } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.Scroll(-48.0f, 0.0f);
                    } else {
                        PrevPage();
                    }
                    break;
                case VK_DOWN:
                    if (m_docType == DocumentType::Media) {
                        m_media.SetVolume(m_media.GetVolume() - 5);
                    } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.Scroll(48.0f, 0.0f);
                    } else {
                        NextPage();
                    }
                    break;
                case 'M':
                    if (m_docType == DocumentType::Media) {
                        m_media.ToggleMute();
                        if (m_hudHwnd) InvalidateRect(m_hudHwnd, NULL, FALSE);
                    }
                    break;
                case 'R':
                    Rotate(90);
                    break;
                case 'L':
                    if (m_docType == DocumentType::Media) {
                        m_isLooping = !m_isLooping;
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    } else {
                        Rotate(-90);
                    }
                    break;
                case 'W':
                    if (m_docType == DocumentType::Text && !isCtrl) {
                        m_text.ToggleWordWrap();
                        ShowToast(m_text.IsWordWrap() ? L"Word Wrap: ON" : L"Word Wrap: OFF");
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    }
                    break;
                case 'T':
                    if (isCtrl && m_docType == DocumentType::Image && m_hasDetectedContentBounds) {
                        m_trimBlackBars = !m_trimBlackBars;
                        int adaptW = (int)m_imageWidth;
                        int adaptH = (int)m_imageHeight;
                        if (m_trimBlackBars) {
                            adaptW = (int)(m_contentBounds.right - m_contentBounds.left + 0.5f);
                            adaptH = (int)(m_contentBounds.bottom - m_contentBounds.top + 0.5f);
                        }
                        AdaptWindowToMedia(adaptW, adaptH);
                        FitToWindow();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                    } else if (m_docType == DocumentType::Spreadsheet && !isCtrl) {
                        m_text.ToggleTableView();
                        ShowToast(m_text.IsTableView() ? L"Grid View" : L"Raw Text View");
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    }
                    break;
                case 'I':
                    ToggleInspector();
                    break;
                case 'S':
                    if (m_docType == DocumentType::Pdf && m_totalPages > 1 && !isCtrl) {
                        m_showSidebar = !m_showSidebar;
                        UpdateLayout();
                    }
                    break;
                case 'F':
                case VK_F11:
                    if (!isCtrl) {
                        ToggleFullscreen();
                    }
                    break;
                case '0':
                    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.ResetZoom();
                    } else {
                        FitToWindow();
                    }
                    break;
                case '1':
                    ResetZoom();
                    break;
                case VK_OEM_PLUS:
                case VK_ADD:
                    ZoomIn();
                    break;
                case VK_OEM_MINUS:
                case VK_SUBTRACT:
                    ZoomOut();
                    break;
                case VK_ESCAPE:
                    if (m_showSearch) {
                        ToggleSearch();
                    } else if (m_isCropActive) {
                        CancelCrop();
                    } else if (m_showInspector) {
                        ToggleInspector();
                    } else if (m_isFullscreen) {
                        ToggleFullscreen();
                    } else {
                        HideMainWindow();
                    }
                    break;
            }
            if (msg == WM_SYSKEYDOWN) {
                return DefWindowProc(m_hwnd, msg, wParam, lParam);
            }
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        case WM_CLOSE: {
            HideMainWindow();
            return 0; // Keep resident in background for instant spacebar preview!
        }

        case WM_QUERYENDSESSION: {
            return TRUE;
        }

        case WM_ENDSESSION: {
            if (wParam) {
                RemoveTrayIcon();
                ExplorerHook::UninstallHook();
                DestroyHudWindow();
                DestroyUpdatePopupWindow();
                ExitProcess(0);
            }
            return 0;
        }

        case WM_DESTROY: {
            RemoveTrayIcon();
            ExplorerHook::UninstallHook();
            DestroyHudWindow();
            DestroyUpdatePopupWindow();
            PostQuitMessage(0);
            return 0;
        }
    }

    if (msg == g_wmTaskbarCreated && g_wmTaskbarCreated != 0) {
        InitTrayIcon(false);
        ExplorerHook::EnsureHookActive(m_hwnd);
        return 0;
    }

    return DefWindowProc(m_hwnd, msg, wParam, lParam);
}

void AppWindow::InitTrayIcon(bool showNotification) {
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | (showNotification ? NIF_INFO : 0);
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(101), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!m_nid.hIcon) m_nid.hIcon = (HICON)GetClassLongPtrW(m_hwnd, GCLP_HICONSM);
    if (!m_nid.hIcon) m_nid.hIcon = (HICON)GetClassLongPtrW(m_hwnd, GCLP_HICON);
    if (!m_nid.hIcon) m_nid.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(101));
    wcscpy_s(m_nid.szTip, L"WinPreview (Spacebar to preview)");
    if (showNotification) {
        wcscpy_s(m_nid.szInfoTitle, L"WinPreview Ready");
        wcscpy_s(m_nid.szInfo, L"WinPreview is running in background. Select any file in File Explorer or Desktop and press Spacebar to preview!");
        m_nid.dwInfoFlags = NIIF_INFO;
    }
    if (!Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
    m_hasTrayIcon = true;
}

void AppWindow::RemoveTrayIcon() {
    if (m_hasTrayIcon) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_hasTrayIcon = false;
    }
}

void AppWindow::ShowTrayNotification(const wchar_t* title, const wchar_t* msg) {
    if (!m_hasTrayIcon) return;
    m_nid.uFlags |= NIF_INFO;
    wcscpy_s(m_nid.szInfoTitle, title ? title : L"WinPreview");
    wcscpy_s(m_nid.szInfo, msg ? msg : L"");
    m_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

bool AppWindow::IsAutoStartEnabled() const {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        LONG res = RegQueryValueExW(hKey, L"WinPreview", NULL, &type, NULL, NULL);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void AppWindow::SetAutoStartEnabled(bool enable) {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR exePath[MAX_PATH] = {};
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            WCHAR cmd[MAX_PATH + 32] = {};
            swprintf_s(cmd, L"\"%s\" --background", exePath);
            RegSetValueExW(hKey, L"WinPreview", 0, REG_SZ, (const BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKey, L"WinPreview");
        }
        RegCloseKey(hKey);
    }
}

} // namespace WinPreview
