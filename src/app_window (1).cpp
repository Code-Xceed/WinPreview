#include "app_window.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <shlwapi.h>
#include <commdlg.h>
#include <propsys.h>
#include <propkey.h>

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
    BTN_PASTE = 35
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

AppWindow::~AppWindow() {
    SetCursorVisibility(true);
    DestroyHudWindow();
    SafeRelease(m_imageBitmap);
    SafeRelease(m_pdfPageBitmap);
    SafeRelease(m_audioAlbumArt);
    SafeRelease(m_officeBitmap);
    m_text.Close();
    SafeRelease(m_dockLayer);
}

bool AppWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    // Register Window Class
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // D2D paints everything
    wc.lpszClassName = L"WinPreviewMainWindow";
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    RegisterClassExW(&wc);

    // Initial window size
    int width = 1240;
    int height = 820;
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

    if (!m_hwnd) return false;

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
    if (!m_d2d.Initialize()) return false;

    // Create floating media HUD window
    CreateHudWindow();

    // Start 30ms high-fidelity animation and auto-hide timer
    SetTimer(m_hwnd, 1, 30, NULL);

    // Show Window immediately (<25ms cold launch)
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

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

    // Close previous
    m_media.CloseMedia();
    SafeRelease(m_imageBitmap);
    SafeRelease(m_pdfPageBitmap);
    SafeRelease(m_audioAlbumArt);
    SafeRelease(m_officeBitmap);
    m_text.Close();
    ClearThumbnails();
    m_docType = DocumentType::None;
    m_isAudioOnly = false;
    m_hasAdaptedVideoWindow = false;
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_hudOpacity = 0.0f;
    m_hudTargetOpacity = 0.0f;
    m_audioArtist.clear();
    m_audioAlbum.clear();
    m_audioDiscAngle = 0.0f;
    m_isAudioSeeking = false;
    for (int i = 0; i < 32; ++i) m_audioVisualizerBars[i] = 0.0f;
    if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);

    // Reset feature states
    m_showSearch = false;
    m_searchQuery.clear();
    m_searchMatches.clear();
    m_currentMatchIndex = -1;
    m_isCropActive = false;
    m_cropDragHandle = 0;
    m_selStartIndex = -1;
    m_selEndIndex = -1;
    m_isSelectingText = false;
    m_showInspector = false;
    m_sidebarTab = 0;
    m_sidebarScrollY = 0.0f;
    m_outlineScrollY = 0.0f;
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

    bool success = false;

    // Helper lambdas for extension matching
    auto isTextExt = [](const std::wstring& e) {
        return (e == L".txt" || e == L".md" || e == L".markdown" || e == L".log" || e == L".rtf" ||
                e == L".ini" || e == L".cfg" || e == L".conf" || e == L".env" || e == L".toml" ||
                e == L".json" || e == L".xml" || e == L".xaml" || e == L".html" || e == L".htm" ||
                e == L".css" || e == L".scss" || e == L".sass" || e == L".less" || e == L".yaml" || e == L".yml" ||
                e == L".js" || e == L".mjs" || e == L".cjs" || e == L".jsx" || e == L".ts" || e == L".mts" || e == L".tsx" ||
                e == L".py" || e == L".cpp" || e == L".cxx" || e == L".cc" || e == L".c" || e == L".h" || e == L".hpp" || e == L".hxx" ||
                e == L".cs" || e == L".rs" || e == L".go" || e == L".java" || e == L".kt" || e == L".kts" ||
                e == L".php" || e == L".rb" || e == L".sh" || e == L".bash" || e == L".zsh" || e == L".bat" || e == L".cmd" ||
                e == L".ps1" || e == L".psm1" || e == L".sql" || e == L".lua" || e == L".r" || e == L".swift" || e == L".dart" ||
                e == L".zig" || e == L".diff" || e == L".patch");
    };

    auto isSpreadsheetExt = [](const std::wstring& e) {
        return (e == L".csv" || e == L".tsv" || e == L".tab");
    };

    auto isOfficeExt = [](const std::wstring& e) {
        return (e == L".pptx" || e == L".ppt" || e == L".xlsx" || e == L".xls" ||
                e == L".docx" || e == L".doc" || e == L".key" || e == L".numbers" || e == L".pages" ||
                e == L".odp" || e == L".ods" || e == L".odt");
    };

    // 1. PDF
    if (ext == L".pdf") {
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
    else if (ext == L".mp4" || ext == L".mkv" || ext == L".avi" || ext == L".mov" ||
             ext == L".webm" || ext == L".flv" || ext == L".wmv" || ext == L".m4v" ||
             ext == L".mp3" || ext == L".wav" || ext == L".flac" || ext == L".aac" || 
             ext == L".m4a" || ext == L".ogg" || ext == L".wma" || ext == L".ts") {
        m_isAudioOnly = (ext == L".mp3" || ext == L".wav" || ext == L".flac" || 
                         ext == L".aac" || ext == L".m4a" || ext == L".ogg" || ext == L".wma");
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
                typedef HRESULT (WINAPI *PFN_SHGetPropertyStoreFromParsingName)(PCWSTR, IBindCtx*, DWORD, REFIID, void**);
                HMODULE hPropSys = LoadLibraryW(L"propsys.dll");
                if (hPropSys) {
                    auto pfn = (PFN_SHGetPropertyStoreFromParsingName)GetProcAddress(hPropSys, "SHGetPropertyStoreFromParsingName");
                    if (pfn) {
                        IPropertyStore* pStore = nullptr;
                        if (SUCCEEDED(pfn(filePath.c_str(), NULL, GPS_DEFAULT, IID_PPV_ARGS(&pStore))) && pStore) {
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
                    }
                    FreeLibrary(hPropSys);
                }

                AdaptWindowToMedia(540, 500);
            } else {
                unsigned int vw = 0, vh = 0;
                if (m_media.GetVideoDimensions(&vw, &vh) && vw > 0 && vh > 0) {
                    m_videoWidth = (int)vw;
                    m_videoHeight = (int)vh;
                    m_hasAdaptedVideoWindow = true;
                    AdaptWindowToMedia(m_videoWidth, m_videoHeight);
                } else {
                    typedef HRESULT (WINAPI *PFN_SHGetPropertyStoreFromParsingName)(PCWSTR, IBindCtx*, DWORD, REFIID, void**);
                    HMODULE hPropSys = LoadLibraryW(L"propsys.dll");
                    if (hPropSys) {
                        auto pfn = (PFN_SHGetPropertyStoreFromParsingName)GetProcAddress(hPropSys, "SHGetPropertyStoreFromParsingName");
                        if (pfn) {
                            IPropertyStore* pStore = nullptr;
                            HRESULT hr = pfn(filePath.c_str(), NULL, GPS_DEFAULT, IID_PPV_ARGS(&pStore));
                            if (SUCCEEDED(hr) && pStore) {
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
                        FreeLibrary(hPropSys);
                    }
                }
            }

            m_lastTimerTick = GetTickCount64();
            GetCursorPos(&m_lastGlobalMousePos);
            WakeHudAndCursor();
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
    else if (isTextExt(ext)) {
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
    // 5. Presentation Slides & Office Documents (PPTX, XLSX, DOCX)
    else if (isOfficeExt(ext)) {
        if (WicDecoder::LoadShellThumbnail(m_d2d.GetWICFactory(), m_d2d.GetRenderTarget(), filePath, 1280, &m_officeBitmap)) {
            m_docType = DocumentType::Office;
            m_showSidebar = false;
            if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);

            D2D1_SIZE_F sz = m_officeBitmap->GetSize();
            m_officeWidth = (UINT)sz.width;
            m_officeHeight = (UINT)sz.height;

            if (ext == L".pptx" || ext == L".ppt" || ext == L".key") {
                m_formatBadgeText = L"PRESENTATION SLIDES • 16:9";
            } else if (ext == L".xlsx" || ext == L".xls") {
                m_formatBadgeText = L"EXCEL WORKBOOK";
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
    // 6. Image (Default via WIC)
    else {
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
        m_dockOpacity = 1.0f;
        WakeAllUiAndCursor();
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

void AppWindow::NavigateFolder(int delta) {
    if (m_currentFilePath.empty() || m_docType != DocumentType::Image) return;

    size_t lastSlash = m_currentFilePath.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) return;

    std::wstring folder = m_currentFilePath.substr(0, lastSlash);
    std::wstring searchPattern = folder + L"\\*.*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<std::wstring> imageFiles;

    auto isImageExt = [](const std::wstring& fn) {
        size_t dot = fn.find_last_of(L'.');
        if (dot == std::wstring::npos) return false;
        std::wstring ext = fn.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".webp" ||
                ext == L".bmp" || ext == L".gif" || ext == L".tif" || ext == L".tiff" ||
                ext == L".ico" || ext == L".heic" || ext == L".heif");
    };

    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring filename = ffd.cFileName;
            if (isImageExt(filename)) {
                imageFiles.push_back(folder + L"\\" + filename);
            }
        }
    } while (FindNextFileW(hFind, &ffd) != 0);

    FindClose(hFind);

    if (imageFiles.size() <= 1) return;

    std::sort(imageFiles.begin(), imageFiles.end(), [](const std::wstring& a, const std::wstring& b) {
        return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
    });

    int curIdx = -1;
    for (size_t i = 0; i < imageFiles.size(); i++) {
        if (_wcsicmp(imageFiles[i].c_str(), m_currentFilePath.c_str()) == 0) {
            curIdx = (int)i;
            break;
        }
    }

    if (curIdx >= 0) {
        int nextIdx = (curIdx + delta + (int)imageFiles.size()) % (int)imageFiles.size();
        OpenFile(imageFiles[nextIdx]);
    }
}

void AppWindow::NextPage() {
    if (m_docType == DocumentType::Pdf && m_currentPage + 1 < m_totalPages) {
        SetPage(m_currentPage + 1);
    } else if (m_docType == DocumentType::Image) {
        NavigateFolder(1);
    }
}

void AppWindow::PrevPage() {
    if (m_docType == DocumentType::Pdf && m_currentPage > 0) {
        SetPage(m_currentPage - 1);
    } else if (m_docType == DocumentType::Image) {
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
        m_zoom *= 1.25f;
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ZoomOut() {
    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
        m_text.ZoomOut();
    } else {
        m_zoom *= 0.8f;
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void AppWindow::ResetZoom() {
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;
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
    bool isWrapActive = (buttonId == BTN_WRAP_TEXT) && m_text.IsWordWrap();
    bool isTableActive = (buttonId == BTN_TABLE_VIEW) && m_text.IsTableView();
    bool isSaveActive = (buttonId == BTN_SAVE_FILE) && m_text.IsModified();

    bool isStateActive = isLoopActive || isCropActive || isSearchActive || isInfoActive || isWrapActive || isTableActive || isSaveActive;

    // Active (press) state: tactile scale down + active fill
    if (m_pressedButtonId == buttonId && enabled) {
        rt->FillRoundedRectangle(rrect, m_d2d.BrushActive);
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
        case ICON_OUTLINE:        m_d2d.DrawIconOutline(cx, cy, iconSize, brush); break;
        case ICON_COPY:           m_d2d.DrawIconCopy(cx, cy, iconSize, brush); break;
        case ICON_WRAP:           m_d2d.DrawIconWrap(cx, cy, iconSize, m_text.IsWordWrap(), brush); break;
        case ICON_TABLE:          m_d2d.DrawIconTable(cx, cy, iconSize, m_text.IsTableView(), brush); break;
        case ICON_SAVE:           m_d2d.DrawIconSave(cx, cy, iconSize, m_text.IsModified(), brush); break;
        case ICON_UNDO:           m_d2d.DrawIconUndo(cx, cy, iconSize, brush); break;
        case ICON_REDO:           m_d2d.DrawIconRedo(cx, cy, iconSize, brush); break;
        case ICON_CUT:            m_d2d.DrawIconCut(cx, cy, iconSize, brush); break;
        case ICON_PASTE:          m_d2d.DrawIconPaste(cx, cy, iconSize, brush); break;
    }
}

void AppWindow::DrawTextButton(const D2D1_RECT_F& rect, const WCHAR* label, int buttonId, bool enabled) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, 6.0f, 6.0f);

    if (m_pressedButtonId == buttonId && enabled) {
        rt->FillRoundedRectangle(rrect, m_d2d.BrushActive);
    } else if (m_hoveredButtonId == buttonId && enabled) {
        rt->FillRoundedRectangle(rrect, m_d2d.BrushHover);
        rt->DrawRoundedRectangle(rrect, m_d2d.BrushBorderSubtle, 1.0f);
    }

    ID2D1Brush* brush = enabled ? (ID2D1Brush*)m_d2d.BrushText : (ID2D1Brush*)m_d2d.BrushTextMuted;
    IDWriteTextFormat* font = m_d2d.FontButton ? m_d2d.FontButton : m_d2d.FontCaption;
    rt->DrawText(label, (UINT32)wcslen(label), font, rect, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

const WCHAR* AppWindow::GetButtonTooltip(int buttonId) const {
    switch (buttonId) {
        case BTN_SIDEBAR: return m_showSidebar ? L"Hide Sidebar (S)" : L"Show Sidebar (S)";
        case BTN_PREV_PAGE: return L"Previous Page (←)";
        case BTN_NEXT_PAGE: return L"Next Page (→)";
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
        case BTN_CROP:
            if (m_hasDetectedContentBounds) {
                return m_trimBlackBars ? L"Show Full Canvas (Include Black Bars) (Ctrl+T)" : L"Trim Black Bars (Fit Content) (Ctrl+T)";
            }
            return m_isCropActive ? L"Commit Crop (Enter)" : L"Crop Image (C)";
        case BTN_SIDEBAR_TAB_THUMBS: return L"Thumbnails View";
        case BTN_SIDEBAR_TAB_OUTLINE: return L"Table of Contents Outline";
        case BTN_SEARCH_PREV: return L"Previous Match (Shift+Enter)";
        case BTN_SEARCH_NEXT: return L"Next Match (Enter)";
        case BTN_SEARCH_CLOSE: return L"Close Search (Esc)";
        case BTN_SAVE_FILE: return m_text.IsModified() ? L"Save Changes (Ctrl+S)" : L"File Saved (Ctrl+S)";
        case BTN_UNDO: return L"Undo (Ctrl+Z)";
        case BTN_REDO: return L"Redo (Ctrl+Y)";
        case BTN_CUT: return L"Cut Selection (Ctrl+X)";
        case BTN_COPY_TEXT: return (m_docType == DocumentType::Spreadsheet && m_text.IsTableView()) ? L"Copy Table (Ctrl+C)" : (m_text.HasSelection() ? L"Copy Selection (Ctrl+C)" : L"Copy Line (Ctrl+C)");
        case BTN_PASTE: return L"Paste (Ctrl+V)";
        case BTN_WRAP_TEXT: return m_text.IsWordWrap() ? L"Disable Word Wrap (W)" : L"Enable Word Wrap (W)";
        case BTN_TABLE_VIEW: return m_text.IsTableView() ? L"Switch to Raw Text Editor (T)" : L"Switch to Table Grid View (T)";
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
        case BTN_SIDEBAR_TAB_THUMBS: return m_tabThumbsRect;
        case BTN_SIDEBAR_TAB_OUTLINE: return m_tabOutlineRect;
        case BTN_SEARCH_PREV: return m_searchPrevBtn;
        case BTN_SEARCH_NEXT: return m_searchNextBtn;
        case BTN_SEARCH_CLOSE: return m_searchCloseBtn;
        case BTN_SAVE_FILE: return m_btnSaveTextRect;
        case BTN_UNDO: return m_btnUndoRect;
        case BTN_REDO: return m_btnRedoRect;
        case BTN_CUT: return m_btnCutTextRect;
        case BTN_COPY_TEXT: return m_btnCopyTextRect;
        case BTN_PASTE: return m_btnPasteTextRect;
        case BTN_WRAP_TEXT: return m_btnWrapTextRect;
        case BTN_TABLE_VIEW: return m_btnTableViewRect;
        default: return D2D1::RectF(0, 0, 0, 0);
    }
}

void AppWindow::Render() {
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

    // 2. Render Canvas or Empty State
    if (m_docType == DocumentType::None) {
        RenderEmptyState(canvasRect);
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
         m_docType == DocumentType::Office) && m_dockOpacity > 0.005f) {
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
    }

    // 5. Render Floating Overlays (Search Bar & Inspector, rendered on top cleanly below dock)
    if (m_showSearch && m_docType == DocumentType::Pdf) {
        RenderSearchBar(canvasRect);
    }
    if (m_showInspector) {
        RenderInspector(canvasRect);
    }

    // 6. Render Micro-tooltip if active
    if (m_tooltipAlpha > 0.05f && m_hoveredButtonId != BTN_NONE && m_dockOpacity > 0.5f) {
        const WCHAR* tipText = GetButtonTooltip(m_hoveredButtonId);
        if (tipText) {
            D2D1_RECT_F btnRect = GetButtonRect(m_hoveredButtonId);
            float anchorX = btnRect.left + (btnRect.right - btnRect.left) / 2.0f;
            float anchorY = btnRect.bottom + 4.0f;
            m_d2d.DrawMicroTooltip(anchorX, anchorY, tipText, m_tooltipAlpha);
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

    if (m_docType != DocumentType::Image && m_docType != DocumentType::Pdf &&
        m_docType != DocumentType::Text && m_docType != DocumentType::Spreadsheet &&
        m_docType != DocumentType::Office) return;
    if (m_dockOpacity <= 0.005f) return;

    float totalW = rect.right - rect.left;
    float dockH = 38.0f;
    float dockRadius = dockH / 2.0f; // 19.0f smooth circular pill caps
    float dockY = 12.0f;
    float btnH = 30.0f;
    float btnY = dockY + (dockH - btnH) / 2.0f;

    // Dynamic width calculation based on active mode
    float dockW = 0.0f;
    bool showPdfNav = (m_docType == DocumentType::Pdf && m_totalPages > 1);
    bool showRotate = true;

    if (m_docType == DocumentType::Image) {
        // Image Dock: [ - | 54% | + ] | [ <-> ] | [ ↺  ↻ ] | [ ⯐ ] [ ⓘ ] [ ⛶ ]
        dockW = 357.0f;
        if (totalW < 370.0f) {
            dockW = 295.0f;
            showRotate = false;
        }
    } else if (m_docType == DocumentType::Pdf) {
        if (showPdfNav) {
            // Multi-page PDF: [ Sidebar ] | [ ◀  1 / N  ▶ ] | [ - | 100% | + ] | [ <-> ] | [ ↺  ↻ ] | [ 🔍 ] [ ⓘ ] [ ⛶ ]
            dockW = 530.0f;
            if (totalW < 540.0f) {
                dockW -= 79.0f;
                showRotate = false;
            }
        } else {
            // Single-page PDF: [ - | 100% | + ] | [ <-> ] | [ ↺  ↻ ] | [ 🔍 ] [ ⓘ ] [ ⛶ ]
            dockW = 357.0f;
            if (totalW < 370.0f) {
                dockW = 295.0f;
                showRotate = false;
            }
        }
    } else if (m_docType == DocumentType::Text) {
        // Text Mini VS Code: [ 💾 Save ] [ ↶ Undo ] [ ↷ Redo ] | [ ✂ Cut ] [ 📋 Copy ] [ 📋 Paste ] | [ - | 100% | + ] | [ ↵ Wrap ] [ ⓘ ] [ ⛶ ]
        dockW = 442.0f;
        if (totalW < 460.0f) {
            dockW = 345.0f;
        }
    } else if (m_docType == DocumentType::Spreadsheet) {
        // Spreadsheet: [ 💾 Save ] [ ↶ Undo ] [ ↷ Redo ] | [ 📋 Copy ] [ 📋 Paste ] | [ - | 100% | + ] | [ ⊞ Table ] [ ⓘ ] [ ⛶ ]
        dockW = 442.0f;
        if (totalW < 460.0f) {
            dockW = 345.0f;
        }
    } else if (m_docType == DocumentType::Office) {
        // Office: [ - | 100% | + ] | [ <-> ] | [ ↺  ↻ ] | [ ⓘ ] [ ⛶ ]
        dockW = 320.0f;
    }
    float availLeft = (m_showSidebar && m_docType == DocumentType::Pdf) ? m_sidebarWidth : 0.0f;
    float availW = totalW - availLeft;
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
        // --- Segment 1: Inset Zoom Capsule [ - | 54% | + ] ---
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

        // Inner Divider 1
        float div1X = curX + zoomBtnW;
        rt->DrawLine(D2D1::Point2F(div1X, capY + 4.0f), D2D1::Point2F(div1X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

        // Zoom Percent Readout [54%]
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

        // Inner Divider 2
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

        // --- Divider ---
        drawDivider(curX + 8.0f);
        curX += 17.0f;

        // --- Segment 2: Fit to Window [ <-> ] ---
        m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
        curX += 30.0f;

        // --- Segment 3: Rotation [ ↺  ↻ ] ---
        if (showRotate) {
            drawDivider(curX + 8.0f);
            curX += 17.0f;

            m_btnRotateLeftRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnRotateLeftRect, ICON_ROTATE_LEFT, BTN_ROTATE_L, true);
            curX += 32.0f;

            m_btnRotateRightRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnRotateRightRect, ICON_ROTATE_RIGHT, BTN_ROTATE_R, true);
            curX += 30.0f;
        }

        // --- Divider ---
        drawDivider(curX + 8.0f);
        curX += 17.0f;

        // --- Segment 4: Actions [ ⯐ ] [ ⓘ ] [ ⛶ ] ---
        m_btnCropRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnCropRect, ICON_CROP, BTN_CROP, true);
        curX += 34.0f;

        m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
        curX += 34.0f;

        m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
    }
    // =========================================================================
    // DOCK RENDERING: PDF MODE
    // =========================================================================
    else if (m_docType == DocumentType::Pdf) {
        if (showPdfNav) {
            // Sidebar Toggle
            m_btnSidebarRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnSidebarRect, ICON_SIDEBAR, BTN_SIDEBAR, true);
            curX += 30.0f;

            drawDivider(curX + 8.0f);
            curX += 17.0f;

            // Page Nav Capsule: [ ◀  1 / N  ▶ ]
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
        }

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

        float div1X = curX + zoomBtnW;
        rt->DrawLine(D2D1::Point2F(div1X, capY + 4.0f), D2D1::Point2F(div1X, capY + capH - 4.0f), m_d2d.BrushBorder, 1.0f);

        // Zoom Readout
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

        // Fit to Window
        m_btnZoomFitRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnZoomFitRect, ICON_ZOOM_FIT, BTN_ZOOM_FIT, true);
        curX += 30.0f;

        // Rotate Left & Right
        if (showRotate) {
            drawDivider(curX + 8.0f);
            curX += 17.0f;

            m_btnRotateLeftRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnRotateLeftRect, ICON_ROTATE_LEFT, BTN_ROTATE_L, true);
            curX += 32.0f;

            m_btnRotateRightRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnRotateRightRect, ICON_ROTATE_RIGHT, BTN_ROTATE_R, true);
            curX += 30.0f;
        }

        drawDivider(curX + 8.0f);
        curX += 17.0f;

        // Search in document
        m_btnSearchRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnSearchRect, ICON_SEARCH, BTN_SEARCH, true);
        curX += 34.0f;

        // Info / Inspector
        m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
        curX += 34.0f;

        // Fullscreen
        m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
    }
    // =========================================================================
    // DOCK RENDERING: TEXT & CODE MODE
    // =========================================================================
    else if (m_docType == DocumentType::Text) {
        bool isCompact = (totalW < 460.0f);

        // Segment 1: Save & Undo/Redo [ 💾 ] [ ↶ ] [ ↷ ]
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

        // Segment 2: Clipboard Actions [ ✂ ] [ 📋 ] [ 📋 ]
        if (!isCompact) {
            m_btnCutTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnCutTextRect, ICON_CUT, BTN_CUT, true);
            curX += 30.0f;
        }

        m_btnCopyTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnCopyTextRect, ICON_COPY, BTN_COPY_TEXT, true);
        curX += 30.0f;

        if (!isCompact) {
            m_btnPasteTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnPasteTextRect, ICON_PASTE, BTN_PASTE, true);
            curX += 30.0f;
        }

        drawDivider(curX + 8.0f);
        curX += 17.0f;

        // Segment 3: Inset Font Zoom Capsule [ - | 100% | + ]
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

        // Font Percent Readout [100%]
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

        // Segment 4: Wrap Toggle Button
        m_btnWrapTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnWrapTextRect, ICON_WRAP, BTN_WRAP_TEXT, true);
        curX += 34.0f;

        // Info / Inspector
        if (!isCompact) {
            m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
            curX += 34.0f;
        }

        // Fullscreen
        m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
    }
    // =========================================================================
    // DOCK RENDERING: SPREADSHEET MODE
    // =========================================================================
    else if (m_docType == DocumentType::Spreadsheet) {
        bool isCompact = (totalW < 460.0f);

        // Segment 1: Save & Undo/Redo [ 💾 ] [ ↶ ] [ ↷ ]
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

        // Segment 2: Copy & Paste
        m_btnCopyTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnCopyTextRect, ICON_COPY, BTN_COPY_TEXT, true);
        curX += 30.0f;

        if (!isCompact) {
            m_btnPasteTextRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnPasteTextRect, ICON_PASTE, BTN_PASTE, true);
            curX += 30.0f;
        }

        drawDivider(curX + 8.0f);
        curX += 17.0f;

        // Segment 3: Inset Font Zoom Capsule [ - | 100% | + ]
        float capH = 28.0f;
        float capY = dockY + (dockH - capH) / 2.0f;
        float zoomBtnW = 27.0f;
        float zoomTextW = 50.0f;
        float totalCapW = zoomBtnW + 1.0f + zoomTextW + 1.0f + zoomBtnW;

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

        // Font Percent Readout [100%]
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

        // Segment 3: Table / Raw Grid Toggle Button
        m_btnTableViewRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnTableViewRect, ICON_TABLE, BTN_TABLE_VIEW, true);
        curX += 34.0f;

        // Info / Inspector
        if (!isCompact) {
            m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
            DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
            curX += 34.0f;
        }

        // Fullscreen
        m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
    }
    // =========================================================================
    // DOCK RENDERING: OFFICE / PRESENTATION SLIDE MODE
    // =========================================================================
    else if (m_docType == DocumentType::Office) {
        // Segment 1: Inset Zoom Capsule [ - | 100% | + ]
        float capH = 28.0f;
        float capY = dockY + (dockH - capH) / 2.0f;
        float zoomBtnW = 27.0f;
        float zoomTextW = 50.0f;
        float totalCapW = zoomBtnW + 1.0f + zoomTextW + 1.0f + zoomBtnW;

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

        // Zoom Percent Readout [100%]
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

        // Info / Inspector
        m_btnInfoRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnInfoRect, ICON_INFO, BTN_INFO, true);
        curX += 34.0f;

        // Fullscreen
        m_btnToolbarFsRect = D2D1::RectF(curX, btnY, curX + 30.0f, btnY + btnH);
        DrawIconButton(m_btnToolbarFsRect, ICON_FULLSCREEN, BTN_TOOLBAR_FS, true);
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

    float cx = rect.left + (rect.right - rect.left) / 2.0f;
    float cy = rect.top + (rect.bottom - rect.top) / 2.0f - 40.0f;

    // 1. Central preview illustration card
    float cardW = 180.0f;
    float cardH = 120.0f;
    D2D1_ROUNDED_RECT cardR = D2D1::RoundedRect(D2D1::RectF(cx - cardW/2, cy - cardH/2 - 50.0f, cx + cardW/2, cy + cardH/2 - 50.0f), 12.0f, 12.0f);
    rt->FillRoundedRectangle(cardR, m_d2d.BrushCard);
    rt->DrawRoundedRectangle(cardR, m_d2d.BrushAccentGlow, 1.5f);

    // Icon inside card
    D2D1_ELLIPSE iconCircle = D2D1::Ellipse(D2D1::Point2F(cx, cy - 50.0f), 28.0f, 28.0f);
    rt->FillEllipse(iconCircle, m_d2d.BrushAccentGlow);
    m_d2d.DrawIconFit(cx, cy - 50.0f, 24.0f, m_d2d.BrushAccent);

    // 2. Hero Title
    const WCHAR* title = L"WinPreview";
    D2D1_RECT_F titleRect = D2D1::RectF(cx - 200.0f, cy + 40.0f, cx + 200.0f, cy + 80.0f);
    rt->DrawText(title, (UINT32)wcslen(title), m_d2d.FontHero, titleRect, m_d2d.BrushText);

    // 3. Subtitle
    const WCHAR* subtitle = L"Drag & drop any image, document, or video to preview instantly";
    D2D1_RECT_F subRect = D2D1::RectF(cx - 280.0f, cy + 82.0f, cx + 280.0f, cy + 110.0f);
    rt->DrawText(subtitle, (UINT32)wcslen(subtitle), m_d2d.FontBody, subRect, m_d2d.BrushTextMuted);

    // 4. Keyboard Shortcuts Bar at bottom
    float barY = rect.bottom - 70.0f;
    float barW = 680.0f;
    float barH = 42.0f;
    D2D1_ROUNDED_RECT barR = D2D1::RoundedRect(D2D1::RectF(cx - barW/2, barY, cx + barW/2, barY + barH), 21.0f, 21.0f);
    rt->FillRoundedRectangle(barR, m_d2d.BrushCard);
    rt->DrawRoundedRectangle(barR, m_d2d.BrushBorderSubtle, 1.0f);

    const WCHAR* hints = L"Space : Play / Next   •   ← / → : Seek / Pages   •   Ctrl + Wheel : Zoom   •   R / L : Rotate   •   F : Fullscreen";
    D2D1_RECT_F hintTextRect = D2D1::RectF(cx - barW/2 + 10.0f, barY + 12.0f, cx + barW/2 - 10.0f, barY + barH);
    rt->DrawText(hints, (UINT32)wcslen(hints), m_d2d.FontCaption, hintTextRect, m_d2d.BrushTextMuted);
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

    float cx = rect.left + (rect.right - rect.left) / 2.0f;
    float toastY = rect.bottom - 46.0f;
    float toastW = 190.0f;
    float toastH = 34.0f;
    D2D1_RECT_F toastRect = D2D1::RectF(cx - toastW / 2.0f, toastY - toastH / 2.0f, cx + toastW / 2.0f, toastY + toastH / 2.0f);
    D2D1_ROUNDED_RECT rToast = D2D1::RoundedRect(toastRect, 17.0f, 17.0f);

    m_d2d.BrushShadowDark->SetOpacity(m_toastOpacity * 0.45f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(toastRect.left, toastRect.top + 2.0f, toastRect.right, toastRect.bottom + 5.0f), 17.0f, 17.0f), m_d2d.BrushShadowDark);

    m_d2d.BrushToolbar->SetOpacity(m_toastOpacity * 0.96f);
    rt->FillRoundedRectangle(rToast, m_d2d.BrushToolbar);

    m_d2d.BrushBorder->SetOpacity(m_toastOpacity * 0.85f);
    rt->DrawRoundedRectangle(rToast, m_d2d.BrushBorder, 1.0f);

    m_d2d.BrushText->SetOpacity(m_toastOpacity);
    rt->DrawText(m_toastMessage.c_str(), (UINT32)m_toastMessage.length(), m_d2d.FontButton, toastRect, m_d2d.BrushText);

    m_d2d.BrushShadowDark->SetOpacity(0.40f);
    m_d2d.BrushToolbar->SetOpacity(1.0f);
    m_d2d.BrushBorder->SetOpacity(1.0f);
    m_d2d.BrushText->SetOpacity(1.0f);
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
        DestroyMenu(hMenu);
        return;
    }

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
    }
}

void AppWindow::RenderCanvas(const D2D1_RECT_F& rect) {
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();

    // 1. Text & Code Documents
    if (m_docType == DocumentType::Text) {
        m_text.RenderText(rt, m_d2d, rect, m_dpiScale);
        if (m_toastOpacity > 0.01f) {
            RenderToast(rect);
        }
        return;
    }

    // 2. Tabular Spreadsheets (CSV / TSV)
    if (m_docType == DocumentType::Spreadsheet) {
        if (m_text.IsTableView()) {
            m_text.RenderSpreadsheet(rt, m_d2d, rect, m_dpiScale);
        } else {
            m_text.RenderText(rt, m_d2d, rect, m_dpiScale);
        }
        if (m_toastOpacity > 0.01f) {
            RenderToast(rect);
        }
        return;
    }

    // 3. Audio & Video
    if (m_docType == DocumentType::Media) {
        if (!m_isAudioOnly) {
            return; // Child HWND handles video surface
        }

        // --- Enhanced Apple Music / Vinyl Hybrid Audio Preview ---
        float cx = rect.left + (rect.right - rect.left) / 2.0f;
        float cy = rect.top + (rect.bottom - rect.top) / 2.0f;

        float cardW = 440.0f;
        float cardH = 430.0f;
        D2D1_RECT_F cardRect = D2D1::RectF(cx - cardW / 2.0f, cy - cardH / 2.0f, cx + cardW / 2.0f, cy + cardH / 2.0f);
        m_audioCardRect = cardRect;
        D2D1_ROUNDED_RECT cardR = D2D1::RoundedRect(cardRect, 20.0f, 20.0f);

        // Soft ambient drop shadow
        D2D1_ROUNDED_RECT ambShadow = D2D1::RoundedRect(
            D2D1::RectF(cardRect.left - 10.0f, cardRect.top + 6.0f, cardRect.right + 10.0f, cardRect.bottom + 26.0f), 24.0f, 24.0f);
        rt->FillRoundedRectangle(ambShadow, m_d2d.BrushShadowSoft);

        // Frosted Card body & subtle border
        rt->FillRoundedRectangle(cardR, m_d2d.BrushCard);
        rt->DrawRoundedRectangle(cardR, m_d2d.BrushBorderSubtle, 1.2f);

        bool isPlaying = m_media.IsPlaying();
        float mediaY = cy - 80.0f;

        // --- Visual Center: Album Art or Animated Spinning Vinyl Record ---
        if (m_audioAlbumArt) {
            // Spinning vinyl disc peeking out when playing
            if (isPlaying) {
                float vinylX = cx + 38.0f;
                float vinylR = 62.0f;
                D2D1::Matrix3x2F oldXform;
                rt->GetTransform(&oldXform);
                rt->SetTransform(D2D1::Matrix3x2F::Rotation(m_audioDiscAngle, D2D1::Point2F(vinylX, mediaY)) * oldXform);

                // Vinyl outer body
                D2D1_ELLIPSE discOuter = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), vinylR, vinylR);
                rt->FillEllipse(discOuter, m_d2d.BrushBg);
                rt->DrawEllipse(discOuter, m_d2d.BrushBorder, 1.2f);

                // Grooves
                D2D1_ELLIPSE discG1 = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), vinylR - 14.0f, vinylR - 14.0f);
                D2D1_ELLIPSE discG2 = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), vinylR - 26.0f, vinylR - 26.0f);
                rt->DrawEllipse(discG1, m_d2d.BrushBorderSubtle, 0.8f);
                rt->DrawEllipse(discG2, m_d2d.BrushBorderSubtle, 0.8f);

                // Center core
                D2D1_ELLIPSE discCore = D2D1::Ellipse(D2D1::Point2F(vinylX, mediaY), 20.0f, 20.0f);
                rt->FillEllipse(discCore, m_d2d.BrushAccentGlow);
                rt->DrawEllipse(discCore, m_d2d.BrushAccent, 1.0f);

                rt->SetTransform(oldXform);
            }

            // Front Album Art Card
            float artSize = 144.0f;
            float artX = isPlaying ? (cx - 20.0f) : cx;
            D2D1_RECT_F artRect = D2D1::RectF(artX - artSize / 2.0f, mediaY - artSize / 2.0f, artX + artSize / 2.0f, mediaY + artSize / 2.0f);
            D2D1_ROUNDED_RECT rArt = D2D1::RoundedRect(artRect, 12.0f, 12.0f);

            // Album shadow
            D2D1_ROUNDED_RECT artShadow = D2D1::RoundedRect(
                D2D1::RectF(artRect.left - 4.0f, artRect.top + 2.0f, artRect.right + 4.0f, artRect.bottom + 12.0f), 14.0f, 14.0f);
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
        } else {
            // Full Procedural Animated Vinyl Disc
            float discR = 70.0f;
            D2D1::Matrix3x2F oldXform;
            rt->GetTransform(&oldXform);
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(m_audioDiscAngle, D2D1::Point2F(cx, mediaY)) * oldXform);

            D2D1_ELLIPSE discOuter = D2D1::Ellipse(D2D1::Point2F(cx, mediaY), discR, discR);
            rt->FillEllipse(discOuter, m_d2d.BrushBg);
            rt->DrawEllipse(discOuter, m_d2d.BrushBorder, 1.5f);

            // Concentric grooves
            for (float r = 24.0f; r < discR - 8.0f; r += 10.0f) {
                D2D1_ELLIPSE discRing = D2D1::Ellipse(D2D1::Point2F(cx, mediaY), r, r);
                rt->DrawEllipse(discRing, m_d2d.BrushBorderSubtle, 0.8f);
            }

            // Glowing center core with music note
            D2D1_ELLIPSE discCenter = D2D1::Ellipse(D2D1::Point2F(cx, mediaY), 24.0f, 24.0f);
            rt->FillEllipse(discCenter, m_d2d.BrushAccentGlow);
            rt->DrawEllipse(discCenter, m_d2d.BrushAccent, 1.5f);
            m_d2d.DrawIconMusic(cx, mediaY, 18.0f, m_d2d.BrushAccent);

            rt->SetTransform(oldXform);
        }

        // --- Track Title & Artist Subtitle ---
        D2D1_RECT_F titleRect = D2D1::RectF(cx - cardW / 2.0f + 20.0f, cy + 18.0f, cx + cardW / 2.0f - 20.0f, cy + 46.0f);
        rt->DrawText(m_fileName.c_str(), (UINT32)m_fileName.length(), m_d2d.FontTitle ? m_d2d.FontTitle : m_d2d.FontBody, titleRect, m_d2d.BrushText);

        std::wstring artistLine = m_audioArtist;
        if (!m_audioAlbum.empty()) {
            if (!artistLine.empty()) artistLine += L" • ";
            artistLine += m_audioAlbum;
        }
        if (artistLine.empty()) artistLine = m_formatBadgeText;

        D2D1_RECT_F artistRect = D2D1::RectF(cx - cardW / 2.0f + 20.0f, cy + 46.0f, cx + cardW / 2.0f - 20.0f, cy + 68.0f);
        rt->DrawText(artistLine.c_str(), (UINT32)artistLine.length(), m_d2d.FontCaption, artistRect, m_d2d.BrushTextMuted);

        // --- Live Animated Audio Spectrum Equalizer (28 bars) ---
        libvlc_time_t currTime = m_media.GetTime();
        float t = (float)(currTime / 1000.0) * 4.2f;
        float startX = cx - (28 * 4.0f + 27 * 3.0f) / 2.0f;
        for (int i = 0; i < 28; ++i) {
            float phase = (float)i / 28.0f;
            float wave = sinf(t * 3.2f + phase * 6.28f) * cosf(t * 1.6f + phase * 4.0f);
            float wave2 = sinf(t * 6.4f + phase * 12.0f) * 0.45f;
            float val = (wave + wave2 + 1.45f) / 2.9f;
            val = std::clamp(val, 0.0f, 1.0f);
            float targetH = isPlaying ? (4.0f + val * 24.0f) : 3.0f;
            m_audioVisualizerBars[i] += (targetH - m_audioVisualizerBars[i]) * 0.35f;

            float bx = startX + (float)i * 7.0f;
            float bh = m_audioVisualizerBars[i];
            D2D1_ROUNDED_RECT barR = D2D1::RoundedRect(
                D2D1::RectF(bx, cy + 86.0f - bh / 2.0f, bx + 4.0f, cy + 86.0f + bh / 2.0f),
                2.0f, 2.0f
            );
            rt->FillRoundedRectangle(barR, m_d2d.BrushAccent);
        }

        // --- Interactive Scrubber Bar ---
        libvlc_time_t totalLen = m_media.GetLength();
        float scrubLeft = cx - 170.0f;
        float scrubRight = cx + 170.0f;
        float scrubW = scrubRight - scrubLeft;
        float scrubY = cy + 118.0f;
        m_audioScrubberRect = D2D1::RectF(scrubLeft, scrubY - 8.0f, scrubRight, scrubY + 8.0f);

        // Background track
        D2D1_ROUNDED_RECT scrubBg = D2D1::RoundedRect(D2D1::RectF(scrubLeft, scrubY - 2.0f, scrubRight, scrubY + 2.0f), 2.0f, 2.0f);
        rt->FillRoundedRectangle(scrubBg, m_d2d.BrushBorderSubtle);

        // Progress fill
        float frac = (totalLen > 0) ? (float)currTime / (float)totalLen : 0.0f;
        frac = std::clamp(frac, 0.0f, 1.0f);
        float thumbX = scrubLeft + scrubW * frac;
        if (thumbX > scrubLeft) {
            D2D1_ROUNDED_RECT scrubFill = D2D1::RoundedRect(D2D1::RectF(scrubLeft, scrubY - 2.0f, thumbX, scrubY + 2.0f), 2.0f, 2.0f);
            rt->FillRoundedRectangle(scrubFill, m_d2d.BrushAccent);
        }

        // Glowing Thumb
        D2D1_ELLIPSE thumbCircle = D2D1::Ellipse(D2D1::Point2F(thumbX, scrubY), 5.5f, 5.5f);
        rt->FillEllipse(thumbCircle, m_d2d.BrushText);
        rt->DrawEllipse(thumbCircle, m_d2d.BrushAccent, 1.5f);

        // Elapsed & Total Time Labels
        std::wstring curTimeStr = m_media.FormatTime(currTime);
        std::wstring totTimeStr = m_media.FormatTime(totalLen);
        D2D1_RECT_F curTimeR = D2D1::RectF(scrubLeft, scrubY + 8.0f, scrubLeft + 60.0f, scrubY + 24.0f);
        D2D1_RECT_F totTimeR = D2D1::RectF(scrubRight - 60.0f, scrubY + 8.0f, scrubRight, scrubY + 24.0f);
        rt->DrawText(curTimeStr.c_str(), (UINT32)curTimeStr.length(), m_d2d.FontBadge, curTimeR, m_d2d.BrushTextMuted);
        rt->DrawText(totTimeStr.c_str(), (UINT32)totTimeStr.length(), m_d2d.FontBadge, totTimeR, m_d2d.BrushTextMuted);

        // --- Interactive Controls Bar ---
        float btnRowY = cy + 162.0f;

        // 1. Loop Button
        m_audioLoopBtnRect = D2D1::RectF(cx - 146.0f, btnRowY - 15.0f, cx - 116.0f, btnRowY + 15.0f);
        D2D1_ROUNDED_RECT rLoop = D2D1::RoundedRect(m_audioLoopBtnRect, 15.0f, 15.0f);
        if (m_isLooping) {
            rt->FillRoundedRectangle(rLoop, m_d2d.BrushCardSelected);
            rt->DrawRoundedRectangle(rLoop, m_d2d.BrushAccent, 1.0f);
        }
        m_d2d.DrawIconLoop(cx - 131.0f, btnRowY, 18.0f, m_isLooping, m_isLooping ? (ID2D1Brush*)m_d2d.BrushAccent : (ID2D1Brush*)m_d2d.BrushTextMuted);

        // 2. Previous 10s Button
        m_audioPrevBtnRect = D2D1::RectF(cx - 86.0f, btnRowY - 18.0f, cx - 50.0f, btnRowY + 18.0f);
        D2D1_ROUNDED_RECT rPrev = D2D1::RoundedRect(m_audioPrevBtnRect, 18.0f, 18.0f);
        rt->FillRoundedRectangle(rPrev, m_d2d.BrushHover);
        m_d2d.DrawIconSkipBack(cx - 68.0f, btnRowY, 20.0f, m_d2d.BrushText);

        // 3. Play / Pause Button (Hero circular button)
        m_audioPlayBtnRect = D2D1::RectF(cx - 24.0f, btnRowY - 24.0f, cx + 24.0f, btnRowY + 24.0f);
        D2D1_ELLIPSE playCircle = D2D1::Ellipse(D2D1::Point2F(cx, btnRowY), 24.0f, 24.0f);
        rt->FillEllipse(playCircle, m_d2d.BrushAccent);
        if (isPlaying) {
            m_d2d.DrawIconPause(cx, btnRowY, 20.0f, m_d2d.BrushBg);
        } else {
            m_d2d.DrawIconPlay(cx + 1.5f, btnRowY, 20.0f, m_d2d.BrushBg);
        }

        // 4. Next 10s Button
        m_audioNextBtnRect = D2D1::RectF(cx + 50.0f, btnRowY - 18.0f, cx + 86.0f, btnRowY + 18.0f);
        D2D1_ROUNDED_RECT rNext = D2D1::RoundedRect(m_audioNextBtnRect, 18.0f, 18.0f);
        rt->FillRoundedRectangle(rNext, m_d2d.BrushHover);
        m_d2d.DrawIconSkipForward(cx + 68.0f, btnRowY, 20.0f, m_d2d.BrushText);

        // 5. Volume / Mute Button
        m_audioVolumeBtnRect = D2D1::RectF(cx + 116.0f, btnRowY - 15.0f, cx + 146.0f, btnRowY + 15.0f);
        D2D1_ROUNDED_RECT rVol = D2D1::RoundedRect(m_audioVolumeBtnRect, 15.0f, 15.0f);
        if (m_media.IsMuted()) {
            rt->FillRoundedRectangle(rVol, m_d2d.BrushCardSelected);
        }
        m_d2d.DrawIconVolume(cx + 131.0f, btnRowY, 18.0f, m_media.IsMuted(), m_media.IsMuted() ? (ID2D1Brush*)m_d2d.BrushAccent : (ID2D1Brush*)m_d2d.BrushText);

        if (m_toastOpacity > 0.01f) {
            RenderToast(rect);
        }
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
        return;
    }
    ID2D1HwndRenderTarget* rt = m_d2d.GetRenderTarget();
    if (!rt) return;

    float availW = canvasRect.right - canvasRect.left;
    float cardW = (std::min)(300.0f, availW - 24.0f);
    float cardX = canvasRect.right - cardW - 14.0f;
    if (cardX < canvasRect.left + 10.0f) cardX = canvasRect.left + 10.0f;

    // Anchor below search bar if active, otherwise below top dock pill
    float dockBottom = (m_topDockRect.bottom > 0.0f) ? m_topDockRect.bottom : 0.0f;
    float cardY = (m_showSearch && m_searchBarRect.bottom > 0.0f)
        ? (m_searchBarRect.bottom + 10.0f)
        : ((dockBottom > 0.0f) ? (dockBottom + 10.0f) : (canvasRect.top + 14.0f));

    float maxH = canvasRect.bottom - cardY - 14.0f;
    float cardH = (std::min)(360.0f, (std::max)(160.0f, maxH));

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
    D2D1_RECT_F headerR = D2D1::RectF(cardX + 16.0f, cardY + 14.0f, cardX + cardW - 40.0f, cardY + 36.0f);
    rt->DrawText(L"Inspector", 9, m_d2d.FontTitle ? m_d2d.FontTitle : m_d2d.FontBody, headerR, m_d2d.BrushText);

    // Close 'x' button
    float closeX = cardX + cardW - 32.0f;
    float closeY = cardY + 12.0f;
    float cr = 4.0f;
    rt->DrawLine(D2D1::Point2F(closeX - cr, closeY - cr + 6.0f), D2D1::Point2F(closeX + cr, closeY + cr + 6.0f), m_d2d.BrushTextMuted, 1.5f);
    rt->DrawLine(D2D1::Point2F(closeX + cr, closeY - cr + 6.0f), D2D1::Point2F(closeX - cr, closeY + cr + 6.0f), m_d2d.BrushTextMuted, 1.5f);

    // Divider line
    rt->DrawLine(D2D1::Point2F(cardX + 16.0f, cardY + 44.0f), D2D1::Point2F(cardX + cardW - 16.0f, cardY + 44.0f), m_d2d.BrushBorderSubtle, 1.0f);

    float rowY = cardY + 54.0f;
    float rowH = 26.0f;
    float labelW = 100.0f;
    float valX = cardX + 16.0f + labelW;
    float valW = cardW - 32.0f - labelW;

    auto drawRow = [&](const WCHAR* label, const std::wstring& val) {
        if (val.empty()) return;
        D2D1_RECT_F lblR = D2D1::RectF(cardX + 16.0f, rowY, cardX + 16.0f + labelW, rowY + rowH);
        D2D1_RECT_F vR = D2D1::RectF(valX, rowY, valX + valW, rowY + rowH);
        rt->DrawText(label, (UINT32)wcslen(label), m_d2d.FontCaption, lblR, m_d2d.BrushTextMuted);
        rt->DrawText(val.c_str(), (UINT32)val.length(), m_d2d.FontCaption, vR, m_d2d.BrushText);
        rowY += rowH;
    };

    drawRow(L"File Name", m_fileName);

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
        WCHAR szBuf[64];
        swprintf_s(szBuf, L"%.1f KB (%lld bytes)", (float)m_text.GetFileSize() / 1024.0f, (long long)m_text.GetFileSize());
        drawRow(L"File Size", szBuf);
        drawRow(L"Encoding", m_text.GetEncodingName());
        drawRow(L"Word Wrap", m_text.IsWordWrap() ? L"Enabled" : L"Disabled");
    } else if (m_docType == DocumentType::Spreadsheet) {
        drawRow(L"Format", L"Delimited Spreadsheet");
        drawRow(L"Columns", std::to_wstring(m_text.GetColumnCount()));
        drawRow(L"Rows", std::to_wstring(m_text.GetRowCount()));
        WCHAR szBuf[64];
        swprintf_s(szBuf, L"%.1f KB (%lld bytes)", (float)m_text.GetFileSize() / 1024.0f, (long long)m_text.GetFileSize());
        drawRow(L"File Size", szBuf);
        drawRow(L"View Mode", m_text.IsTableView() ? L"Tabular Grid" : L"Raw Delimited");
    } else if (m_docType == DocumentType::Office) {
        drawRow(L"Format", m_formatBadgeText);
        WCHAR dimBuf[64];
        swprintf_s(dimBuf, L"%u × %u px", m_officeWidth, m_officeHeight);
        drawRow(L"Slide Preview", dimBuf);
    }
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
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
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
    if (!m_hudHwnd || m_docType != DocumentType::Media) {
        if (m_hudHwnd) ShowWindow(m_hudHwnd, SW_HIDE);
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

    if (m_docType == DocumentType::Media) {
        m_hudTargetOpacity = 255.0f;
        UpdateHudPosition();
        if (m_hudHwnd) {
            ShowWindow(m_hudHwnd, SW_SHOWNOACTIVATE);
            InvalidateRect(m_hudHwnd, NULL, FALSE);
        }
    } else {
        m_hudTargetOpacity = 0.0f;
        m_hudOpacity = 0.0f;
        if (m_hudHwnd) {
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
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, 6.0f, 6.0f);
    float cx = rect.left + (rect.right - rect.left) / 2.0f;
    float cy = rect.top + (rect.bottom - rect.top) / 2.0f;
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




LRESULT AppWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
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
                    if (hitDock(m_audioPlayBtnRect) || hitDock(m_audioPrevBtnRect) || hitDock(m_audioNextBtnRect) ||
                        hitDock(m_audioLoopBtnRect) || hitDock(m_audioVolumeBtnRect) || hitDock(m_audioScrubberRect)) {
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
            return 0;
        }

        case WM_SIZE: {
            if (m_panX == 0.0f && m_panY == 0.0f) {
                FitToWindow();
            }
            UpdateLayout();
            UpdateHudPosition();
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

        case WM_ERASEBKGND:
            return 1;

        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                SetCursorVisibility(true);
            } else {
                WakeAllUiAndCursor();
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
            // Micro-tooltip hover delay and smooth fade (zero idle CPU once complete)
            if (m_hoveredButtonId != BTN_NONE && m_dockOpacity > 0.5f) {
                m_hoverTimeMs += 30;
                if (m_hoverTimeMs > 400 && m_tooltipAlpha < 1.0f) {
                    m_tooltipAlpha += 0.20f;
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
            if (isActivelyInteracting) {
                // Actively holding mouse button down or dragging: keep 100% visible
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
            // Smooth fade transition for media HUD (Videos ONLY)
            if (m_docType == DocumentType::Media) {
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

                // For audio files, rotate vinyl and repaint while playing
                if (m_isAudioOnly && m_media.IsPlaying()) {
                    m_audioDiscAngle += 2.2f;
                    if (m_audioDiscAngle >= 360.0f) m_audioDiscAngle -= 360.0f;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }

            // Toast animation decay
            if (m_toastOpacity > 0.0f) {
                ULONGLONG elapsedToast = now - m_toastStartTime;
                if (elapsedToast > 1400) {
                    m_toastOpacity -= 0.08f;
                    if (m_toastOpacity <= 0.0f) m_toastOpacity = 0.0f;
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }

            // Text & Spreadsheet smooth kinetic scrolling
            if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                float dt = (float)elapsedMs / 1000.0f;
                if (m_text.UpdateAnimation(dt)) {
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }
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

        case WM_COMMAND: {
            WORD cmdId = LOWORD(wParam);
            if (cmdId == 1001) {
                WakeAllUiAndCursor();
                m_media.TogglePlayPause();
            } else if (cmdId == 1002) {
                WakeAllUiAndCursor();
                ToggleFullscreen();
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

            if (m_showSidebar && pt.x < m_sidebarWidth) {
                D2D1_SIZE_F clientSz = GetClientSizeDip();
                float availH = clientSz.height;
                if (m_sidebarTab == 0) {
                    m_sidebarScrollY += (delta > 0 ? 60.0f : -60.0f);
                    if (m_sidebarScrollY > 0.0f) m_sidebarScrollY = 0.0f;
                    float totalH = 54.0f + m_thumbnails.size() * (136.0f + 12.0f);
                    float minScroll = availH - totalH;
                    if (minScroll > 0.0f) minScroll = 0.0f;
                    if (m_sidebarScrollY < minScroll) m_sidebarScrollY = minScroll;
                } else {
                    m_outlineScrollY += (delta > 0 ? 50.0f : -50.0f);
                    if (m_outlineScrollY > 0.0f) m_outlineScrollY = 0.0f;
                    float totalH = 54.0f + m_outline.size() * 31.0f;
                    float minScroll = availH - totalH;
                    if (minScroll > 0.0f) minScroll = 0.0f;
                    if (m_outlineScrollY < minScroll) m_outlineScrollY = minScroll;
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

            D2D1_RECT_F canvasRect = GetCanvasRectDip();
            float canvasCenterX = canvasRect.left + (canvasRect.right - canvasRect.left) / 2.0f;
            float canvasCenterY = canvasRect.top + (canvasRect.bottom - canvasRect.top) / 2.0f;

            float cursorRelX = pt.x - (canvasCenterX + m_panX);
            float cursorRelY = pt.y - (canvasCenterY + m_panY);

            float factor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
            float oldZoom = m_zoom;
            m_zoom *= factor;
            if (m_zoom < 0.05f) m_zoom = 0.05f;
            if (m_zoom > 25.0f) m_zoom = 25.0f;

            float actualFactor = m_zoom / oldZoom;
            m_panX -= cursorRelX * (actualFactor - 1.0f);
            m_panY -= cursorRelY * (actualFactor - 1.0f);

            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEHWHEEL: {
            WakeAllUiAndCursor();
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                m_text.Scroll(0.0f, (float)delta * 0.8f);
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (m_docType == DocumentType::Image || m_docType == DocumentType::Pdf || m_docType == DocumentType::Office) {
                m_panX -= (float)delta * 0.5f;
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

            // 0. Search bar and inspector clicks
            if (m_showSearch && hit(m_searchBarRect)) {
                if (hit(m_searchPrevBtn)) PrevSearchMatch();
                else if (hit(m_searchNextBtn)) NextSearchMatch();
                else if (hit(m_searchCloseBtn)) ToggleSearch();
                return 0;
            }

            if (m_showInspector && hit(m_inspectorRect)) {
                float closeX = m_inspectorRect.right - 32.0f;
                float closeY = m_inspectorRect.top + 12.0f;
                if (std::abs(pt.x - closeX) <= 12.0f && std::abs(pt.y - closeY - 6.0f) <= 12.0f) {
                    ToggleInspector();
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
                    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.ZoomOut();
                    } else {
                        m_zoom *= 0.8f;
                    }
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_btnZoomResetRect)) {
                    m_pressedButtonId = BTN_ZOOM_RESET;
                    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.ResetZoom();
                    } else {
                        ResetZoom();
                    }
                    InvalidateRect(m_hwnd, NULL, FALSE);
                } else if (hit(m_btnZoomInRect)) {
                    m_pressedButtonId = BTN_ZOOM_IN;
                    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.ZoomIn();
                    } else {
                        m_zoom *= 1.25f;
                    }
                    InvalidateRect(m_hwnd, NULL, FALSE);
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
                if (m_isAudioOnly) {
                    if (hit(m_audioPlayBtnRect)) {
                        m_media.TogglePlayPause();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    } else if (hit(m_audioPrevBtnRect)) {
                        m_media.SeekSeconds(-10);
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    } else if (hit(m_audioNextBtnRect)) {
                        m_media.SeekSeconds(10);
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    } else if (hit(m_audioLoopBtnRect)) {
                        m_isLooping = !m_isLooping;
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    } else if (hit(m_audioVolumeBtnRect)) {
                        m_media.ToggleMute();
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    } else if (hit(m_audioScrubberRect)) {
                        m_isAudioSeeking = true;
                        float scrubLeft = m_audioScrubberRect.left;
                        float scrubRight = m_audioScrubberRect.right;
                        if (scrubRight > scrubLeft) {
                            float frac = (pt.x - scrubLeft) / (scrubRight - scrubLeft);
                            m_media.Seek(std::clamp(frac, 0.0f, 1.0f));
                        }
                        InvalidateRect(m_hwnd, NULL, FALSE);
                        return 0;
                    }
                }
                m_media.TogglePlayPause();
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

            // Audio seeking
            if (m_isAudioSeeking && m_docType == DocumentType::Media && m_isAudioOnly) {
                float scrubLeft = m_audioScrubberRect.left;
                float scrubRight = m_audioScrubberRect.right;
                if (scrubRight > scrubLeft) {
                    float frac = (pt.x - scrubLeft) / (scrubRight - scrubLeft);
                    m_media.Seek(std::clamp(frac, 0.0f, 1.0f));
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
                return 0;
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
            }
            if (hit(m_tabThumbsRect)) m_hoveredButtonId = BTN_SIDEBAR_TAB_THUMBS;
            else if (hit(m_tabOutlineRect)) m_hoveredButtonId = BTN_SIDEBAR_TAB_OUTLINE;
            else if (hit(m_searchPrevBtn)) m_hoveredButtonId = BTN_SEARCH_PREV;
            else if (hit(m_searchNextBtn)) m_hoveredButtonId = BTN_SEARCH_NEXT;
            else if (hit(m_searchCloseBtn)) m_hoveredButtonId = BTN_SEARCH_CLOSE;

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
                m_panX += (pt.x - m_lastMousePos.x);
                m_panY += (pt.y - m_lastMousePos.y);
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
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            ReleaseCapture();
            m_isDragging = false;
            m_isScrubbing = false;
            m_isAudioSeeking = false;
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

        case WM_KEYDOWN: {
            WakeAllUiAndCursor();

            bool isCtrl = ((GetKeyState(VK_CONTROL) & 0x8000) != 0) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
            bool isShift = ((GetKeyState(VK_SHIFT) & 0x8000) != 0) || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
            bool isAlt = ((GetKeyState(VK_MENU) & 0x8000) != 0) || ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0);

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

                // Keyboard editing keys
                if (wParam == VK_RETURN) {
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
                    PostQuitMessage(0);
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
                if (wParam == 'C') {
                    if (m_docType == DocumentType::Pdf) {
                        CopySelectedText();
                        return 0;
                    }
                } else if (wParam == 'F') {
                    ToggleSearch();
                    return 0;
                } else if (wParam == 'I') {
                    ToggleInspector();
                    return 0;
                } else if (wParam == 'S') {
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
                    if (m_showSearch) {
                        if (isShift) PrevSearchMatch();
                        else NextSearchMatch();
                        return 0;
                    } else if (m_isCropActive) {
                        ApplyCrop();
                        return 0;
                    }
                    break;
                case 'C':
                    if (m_docType == DocumentType::Image && !isCtrl) {
                        ToggleCrop();
                        return 0;
                    }
                    break;
                case VK_LEFT:
                    if (m_docType == DocumentType::Media) {
                        m_media.SeekSeconds(-5);
                    } else if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.Scroll(0.0f, -48.0f);
                    } else {
                        PrevPage();
                    }
                    break;
                case VK_RIGHT:
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
                    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.ZoomIn();
                    } else {
                        m_zoom *= 1.25f;
                    }
                    break;
                case VK_OEM_MINUS:
                case VK_SUBTRACT:
                    if (m_docType == DocumentType::Text || m_docType == DocumentType::Spreadsheet) {
                        m_text.ZoomOut();
                    } else {
                        m_zoom *= 0.8f;
                    }
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
                        PostQuitMessage(0);
                    }
                    break;
            }
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }

        case WM_DESTROY: {
            DestroyHudWindow();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(m_hwnd, msg, wParam, lParam);
}

} // namespace WinPreview
