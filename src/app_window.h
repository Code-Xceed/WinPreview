#pragma once
#include "d2d_engine.h"
#include "wic_decoder.h"
#include "pdf_engine.h"
#include "media_engine.h"
#include "text_engine.h"
#include "svg_engine.h"
#include "font_engine.h"
#include "folder_engine.h"
#include "markdown_engine.h"
#include "system_engine.h"
#include "explorer_hook.h"
#include "update_engine.h"
#include <string>
#include <vector>

namespace WinPreview {

enum class DocumentType {
    None,
    Image,
    Pdf,
    Media,
    Text,
    Spreadsheet,
    Office,
    Svg,
    Font,
    Folder,
    Markdown,
    Notebook,
    Binary,
    Shortcut,
    Certificate,
    Unsupported
};

struct ThumbnailItem {
    int pageIndex = 0;
    std::wstring label;
    ID2D1Bitmap* bitmap = nullptr;
    D2D1_RECT_F rect = {};
};

enum IconType {
    ICON_SIDEBAR,
    ICON_CHEVRON_LEFT,
    ICON_CHEVRON_RIGHT,
    ICON_ZOOM_OUT,
    ICON_ZOOM_IN,
    ICON_ZOOM_FIT,
    ICON_ROTATE_LEFT,
    ICON_ROTATE_RIGHT,
    ICON_PLAY,
    ICON_PAUSE,
    ICON_VOLUME,
    ICON_FULLSCREEN,
    ICON_SKIP_BACK,
    ICON_SKIP_FORWARD,
    ICON_LOOP,
    ICON_FILE_PDF,
    ICON_FILE_IMAGE,
    ICON_FILE_VIDEO,
    ICON_SEARCH,
    ICON_CROP,
    ICON_INFO,
    ICON_OUTLINE,
    ICON_COPY,
    ICON_WRAP,
    ICON_TABLE,
    ICON_SAVE,
    ICON_UNDO,
    ICON_REDO,
    ICON_CUT,
    ICON_PASTE,
    ICON_SETTINGS,
    ICON_OPEN_WITH,
    ICON_MORE
};

enum ContextMenuCommand {
    IDM_CTX_UNDO = 1001,
    IDM_CTX_REDO,
    IDM_CTX_CUT,
    IDM_CTX_COPY,
    IDM_CTX_PASTE,
    IDM_CTX_SELECT_ALL,
    IDM_CTX_SAVE,
    IDM_CTX_WORD_WRAP,
    IDM_CTX_TABLE_VIEW,
    IDM_CTX_ZOOM_IN,
    IDM_CTX_ZOOM_OUT,
    IDM_CTX_ZOOM_RESET,
    IDM_CTX_ROTATE_LEFT,
    IDM_CTX_ROTATE_RIGHT,
    IDM_CTX_FULLSCREEN,
    IDM_CTX_INFO,
    IDM_CTX_SETTINGS,
    IDM_CTX_CHECK_UPDATES,
    IDM_CTX_SIMULATE_UPDATE
};

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow, bool showTrayNotification = false);
    bool OpenFile(const std::wstring& filePath);
    bool SaveScreenshot(const std::wstring& outPath);
    void SetShowOverflowMenu(bool show) { m_showOverflowMenu = show; }
    void RunMessageLoop();

    HWND GetHwnd() const { return m_hwnd; }

private:
    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;

    D2DEngine m_d2d;
    PdfEngine m_pdf;
    MediaEngine m_media;
    TextEngine m_text;
    SvgEngine m_svg;
    FontEngine m_font;
    FolderEngine m_folder;
    MarkdownEngine m_md;
    SystemEngine m_system;
    bool m_markdownRichMode = true;

    // Current State
    DocumentType m_docType = DocumentType::None;
    std::wstring m_currentFilePath;
    std::wstring m_fileName;
    std::wstring m_fileInfoText;
    std::wstring m_formatBadgeText;
    bool m_isAudioOnly = false;

    // Image document
    ID2D1Bitmap* m_imageBitmap = nullptr;
    UINT m_imageWidth = 0;
    UINT m_imageHeight = 0;
    D2D1_RECT_F m_contentBounds = {};
    bool m_hasDetectedContentBounds = false;
    bool m_trimBlackBars = true;
    bool m_flipH = false;
    bool m_flipV = false;
    ImageMetadata m_imageMeta = {};

    // Office / Presentation Slide document
    ID2D1Bitmap* m_officeBitmap = nullptr;
    UINT m_officeWidth = 0;
    UINT m_officeHeight = 0;

    // Audio Rich UI & Animation
    ID2D1Bitmap* m_audioAlbumArt = nullptr;
    float m_audioDiscAngle = 0.0f;
    float m_audioAnimTime = 0.0f;
    float m_audioVisualizerBars[64] = {};
    float m_audioVisualizerBottom[64] = {};
    std::wstring m_audioArtist;
    std::wstring m_audioAlbum;
    D2D1_RECT_F m_audioArtworkRect = {};
    D2D1_RECT_F m_audioWaveformRect = {};
    bool m_isWaveformSeeking = false;
    float m_audioVinylSlide = 0.0f;
    float m_audioHoverFrac = -1.0f;

    // Text & Spreadsheet UI
    D2D1_RECT_F m_btnSaveTextRect = {};
    D2D1_RECT_F m_btnUndoRect = {};
    D2D1_RECT_F m_btnRedoRect = {};
    D2D1_RECT_F m_btnCutTextRect = {};
    D2D1_RECT_F m_btnCopyTextRect = {};
    D2D1_RECT_F m_btnPasteTextRect = {};
    D2D1_RECT_F m_btnWrapTextRect = {};
    D2D1_RECT_F m_btnTableViewRect = {};
    float m_toastOpacity = 0.0f;
    std::wstring m_toastMessage;
    ULONGLONG m_toastStartTime = 0;
    void ShowToast(const std::wstring& msg);
    void RenderToast(const D2D1_RECT_F& rect);
    void UpdateWindowTitle();
    void SaveCurrentTextFile();

    // In-App Auto-Update System
    UpdateEngine m_updater;
    D2D1_RECT_F m_updateBannerRect = {};
    D2D1_RECT_F m_updateActionBtnRect = {};
    D2D1_RECT_F m_updateDismissBtnRect = {};
    bool m_updateActionBtnHover = false;
    bool m_updateDismissBtnHover = false;
    ULONGLONG m_lastUpdateCheckTick = 0;
    void RenderUpdateBanner(const D2D1_RECT_F& rect);
    void CheckForUpdates(bool isUserInitiated);
    void CheckUpdateOnPreviewOpen();

    // PDF document
    int m_currentPage = 0;
    int m_totalPages = 0;
    ID2D1Bitmap* m_pdfPageBitmap = nullptr;
    float m_pdfPageWidth = 0.0f;
    float m_pdfPageHeight = 0.0f;

    // Viewport transform
    float m_zoom = 1.0f;
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    int m_rotation = 0; // 0, 90, 180, 270
    float m_dpiScale = 1.0f;

    // Sidebar & Layout
    bool m_showSidebar = true;
    int m_sidebarTab = 0; // 0: Thumbnails, 1: Outline
    float m_sidebarWidth = 220.0f;
    float m_sidebarScrollY = 0.0f;
    float m_outlineScrollY = 0.0f;
    std::vector<ThumbnailItem> m_thumbnails;
    std::vector<PdfOutlineItem> m_outline;

    // Text Selection (PDF)
    int m_selStartIndex = -1;
    int m_selEndIndex = -1;
    bool m_isSelectingText = false;
    void CopySelectedText();

    // In-Document Search (Ctrl+F)
    bool m_showSearch = false;
    std::wstring m_searchQuery;
    std::vector<PdfSearchMatch> m_searchMatches;
    int m_currentMatchIndex = -1;
    D2D1_RECT_F m_searchBarRect = {};
    D2D1_RECT_F m_searchPrevBtn = {};
    D2D1_RECT_F m_searchNextBtn = {};
    D2D1_RECT_F m_searchCloseBtn = {};
    void ToggleSearch();
    void ExecuteSearch();
    void NextSearchMatch();
    void PrevSearchMatch();

    // Image Crop & Manipulation
    bool m_isCropActive = false;
    D2D1_RECT_F m_cropRect = {}; // Image coordinate space
    int m_cropDragHandle = 0;    // 0: none, 1: LT, 2: RT, 3: RB, 4: LB, 5: body
    D2D1_POINT_2F m_lastCropMouse = {};
    void ToggleCrop();
    void ApplyCrop();
    void CancelCrop();
    void SaveImageFile(bool promptSaveAs);

    // Inspector HUD (Ctrl+I)
    bool m_showInspector = false;
    D2D1_RECT_F m_inspectorRect = {};
    void ToggleInspector();

    // Preferences & Keybind Settings Modal (Ctrl+,)
    bool m_showSettings = false;
    bool m_isRecordingKeybind = false;
    D2D1_RECT_F m_settingsModalRect = {};
    D2D1_RECT_F m_settingsCloseBtn = {};
    D2D1_RECT_F m_settingsRecordBtn = {};
    D2D1_RECT_F m_settingsResetBtn = {};
    D2D1_RECT_F m_settingsDoneBtn = {};
    D2D1_RECT_F m_settingsAutostartBtn = {};
    D2D1_RECT_F m_settingsPresetRects[5] = {};
    D2D1_RECT_F m_emptyStateKeybindBtn = {};
    void ToggleSettings();
    void RenderSettingsModal(const D2D1_RECT_F& canvasRect);
    void TogglePreviewFromHook();
    DWORD m_lastHookToggleTime = 0;

    // Unsupported File Inspector state
    std::wstring m_unsupportedTypeName;
    std::wstring m_unsupportedSizeStr;
    std::wstring m_unsupportedDateStr;
    std::wstring m_unsupportedHexDump;
    ID2D1Bitmap* m_unsupportedIconBitmap = nullptr;
    ID2D1Bitmap* m_appLogoBitmap = nullptr;
    D2D1_RECT_F m_unsupportedOpenBtnRect = {};
    D2D1_RECT_F m_unsupportedRevealBtnRect = {};
    D2D1_RECT_F m_unsupportedCopyBtnRect = {};
    void RenderUnsupportedState(const D2D1_RECT_F& rect);
    void LoadUnsupportedFileInfo(const std::wstring& filePath);

    // Mouse Interaction
    bool m_isDragging = false;
    D2D1_POINT_2F m_lastMousePos = {};
    bool m_isScrubbing = false;
    bool m_isVolDragging = false;
    bool m_isFullscreen = false;
    bool m_isLooping = false;
    bool m_wasEnded = false;
    bool m_cursorVisible = true;
    bool m_isMouseTracking = false;
    POINT m_lastGlobalMousePos = {};
    POINT m_lastActiveMousePos = { -9999, -9999 };
    ULONGLONG m_lastTimerTick = 0;
    RECT m_preFullscreenRect = {};
    LONG m_preFullscreenStyle = 0;

    // Top Floating Dock & Toolbar buttons hitboxes
    D2D1_RECT_F m_topDockRect = {};
    D2D1_RECT_F m_btnSidebarRect = {};
    D2D1_RECT_F m_btnPrevPageRect = {};
    D2D1_RECT_F m_btnNextPageRect = {};
    D2D1_RECT_F m_btnZoomOutRect = {};
    D2D1_RECT_F m_btnZoomResetRect = {};
    D2D1_RECT_F m_btnZoomInRect = {};
    D2D1_RECT_F m_btnZoomFitRect = {};
    D2D1_RECT_F m_btnRotateLeftRect = {};
    D2D1_RECT_F m_btnRotateRightRect = {};
    D2D1_RECT_F m_btnToolbarPlayPauseRect = {};
    D2D1_RECT_F m_btnLoopRect = {};
    D2D1_RECT_F m_btnToolbarMuteRect = {};
    D2D1_RECT_F m_btnToolbarFsRect = {};
    D2D1_RECT_F m_btnSearchRect = {};
    D2D1_RECT_F m_btnCropRect = {};
    D2D1_RECT_F m_btnInfoRect = {};
    D2D1_RECT_F m_btnOpenWithRect = {};
    D2D1_RECT_F m_btnSettingsRect = {};
    D2D1_RECT_F m_btnOverflowRect = {};
    D2D1_RECT_F m_overflowMenuRect = {};
    bool m_showOverflowMenu = false;
    int m_overflowHoveredIndex = -1;
    D2D1_RECT_F m_tabThumbsRect = {};
    D2D1_RECT_F m_tabOutlineRect = {};
    std::wstring m_openWithAppName;
    mutable std::wstring m_openWithTooltip;

    // Media HUD Window (Owned Layered Floating Window)
    HWND m_hudHwnd = nullptr;
    ID2D1HwndRenderTarget* m_hudRenderTarget = nullptr;
    ID2D1SolidColorBrush* m_hudBrushBg = nullptr;
    ID2D1SolidColorBrush* m_hudBrushBorder = nullptr;
    ID2D1SolidColorBrush* m_hudBrushText = nullptr;
    ID2D1SolidColorBrush* m_hudBrushTextMuted = nullptr;
    ID2D1SolidColorBrush* m_hudBrushAccent = nullptr;
    ID2D1SolidColorBrush* m_hudBrushAccentGlow = nullptr;
    ID2D1SolidColorBrush* m_hudBrushHover = nullptr;
    ID2D1SolidColorBrush* m_hudBrushActive = nullptr;
    ID2D1SolidColorBrush* m_hudBrushTrack = nullptr;
    ID2D1SolidColorBrush* m_hudBrushTooltipBg = nullptr;
    ID2D1SolidColorBrush* m_hudBrushTooltipBorder = nullptr;

    // Top-Right Minimal Update Popup Overlay (Guaranteed on top of all media)
    HWND m_updateHwnd = nullptr;
    ID2D1HwndRenderTarget* m_updateRenderTarget = nullptr;
    ID2D1SolidColorBrush* m_updateBrushBg = nullptr;
    ID2D1SolidColorBrush* m_updateBrushBorder = nullptr;
    ID2D1SolidColorBrush* m_updateBrushText = nullptr;
    ID2D1SolidColorBrush* m_updateBrushTextMuted = nullptr;
    ID2D1SolidColorBrush* m_updateBrushBtn = nullptr;
    ID2D1SolidColorBrush* m_updateBrushBtnHover = nullptr;
    ID2D1SolidColorBrush* m_updateBrushDot = nullptr;
    ID2D1SolidColorBrush* m_updateBrushTrack = nullptr;

    D2D1_RECT_F m_updateBtnRect = {};
    D2D1_RECT_F m_updateCloseBtnRect = {};
    bool m_updateBtnHover = false;
    bool m_updateCloseHover = false;

    bool CreateUpdatePopupWindow();
    void DestroyUpdatePopupWindow();
    void UpdatePopupPosition();
    void ShowUpdatePopup();
    void HideUpdatePopup();
    void RenderUpdatePopup();
    static LRESULT CALLBACK UpdatePopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);






    float m_hudOpacity = 0.0f;
    float m_hudTargetOpacity = 0.0f;
    float m_dockOpacity = 1.0f;
    float m_dockTargetOpacity = 1.0f;
    ID2D1Layer* m_dockLayer = nullptr;
    int m_inactivityMs = 0;

    // HUD buttons hitboxes (local coordinates in HUD)
    D2D1_RECT_F m_hudBtnPlayPause = {};
    D2D1_RECT_F m_hudBtnSkipBack = {};
    D2D1_RECT_F m_hudBtnSkipForward = {};
    D2D1_RECT_F m_hudScrubber = {};
    D2D1_RECT_F m_hudBtnLoop = {};
    D2D1_RECT_F m_hudBtnMute = {};
    D2D1_RECT_F m_hudVolSlider = {};
    D2D1_RECT_F m_hudBtnFullscreen = {};
    D2D1_POINT_2F m_hudMousePos = {};
    bool m_isHudMouseTracking = false;

    // UI hover & pressed state
    int m_hoveredButtonId = 0;
    int m_pressedButtonId = 0;
    int m_hudHoveredButtonId = 0;
    int m_hudPressedButtonId = 0;
    int m_hoveredThumbnailIndex = -1;

    // Tooltip state
    int m_hoverTimeMs = 0;
    float m_tooltipAlpha = 0.0f;
    const WCHAR* GetButtonTooltip(int buttonId) const;
    D2D1_RECT_F GetButtonRect(int buttonId) const;

    // Rendering methods
    void Render(HDC outHdc = nullptr);
    void RenderToolbar(const D2D1_RECT_F& rect);
    void RenderSidebar(const D2D1_RECT_F& rect);
    void RenderCanvas(const D2D1_RECT_F& rect);
    void RenderEmptyState(const D2D1_RECT_F& rect);
    void RenderSearchBar(const D2D1_RECT_F& canvasRect);
    void RenderCropOverlay(const D2D1_RECT_F& canvasRect);
    void RenderInspector(const D2D1_RECT_F& canvasRect);
    void DrawIconButton(const D2D1_RECT_F& rect, IconType icon, int buttonId, bool enabled = true);
    void DrawTextButton(const D2D1_RECT_F& rect, const WCHAR* label, int buttonId, bool enabled = true);

    // Overflow Menu for Responsive Compact Toolbars
    struct OverflowMenuItem {
        int id = 0;
        IconType icon = ICON_MORE;
        std::wstring label;
        std::wstring shortcut;
        bool isDivider = false;
        bool isChecked = false;
        bool isEnabled = true;
    };
    std::vector<OverflowMenuItem> GetCurrentOverflowItems() const;
    void RenderOverflowMenu(const D2D1_RECT_F& canvasRect);
    void DrawMenuIcon(float cx, float cy, float size, IconType icon, ID2D1Brush* brush);
    int GetOverflowItemIndexAt(const D2D1_POINT_2F& pt) const;
    void HandleOverflowMenuClick(const D2D1_POINT_2F& pt);
    void ExecuteOverflowCommand(int buttonId);

    // Media HUD & UI wake methods
    bool CreateHudWindow();
    void DestroyHudWindow();
    void UpdateHudPosition();
    void RenderHud();
    void DrawHudIconButton(const D2D1_RECT_F& rect, IconType icon, int buttonId, bool enabled = true);
    void SetCursorVisibility(bool visible);
    void WakeAllUiAndCursor(const char* reason = "unknown");
    void WakeHudAndCursor() { WakeAllUiAndCursor("WakeHud"); }


    // Layout updates
    void UpdateLayout();
    void FitToWindow();
    void ZoomIn();
    void ZoomOut();
    void ResetZoom();
    void NextPage();
    void PrevPage();
    void SetPage(int page);
    void Rotate(int angleDelta);
    void ToggleFullscreen();
    void ShowContextMenu(int screenX, int screenY);
    void NavigateFolder(int delta);
    void OpenCurrentFileInDefaultApp();
    void AdaptWindowToMedia(int mediaW, int mediaH);
    void HideMainWindow();

    bool m_hasAdaptedVideoWindow = false;
    int m_videoWidth = 0;
    int m_videoHeight = 0;

    // Thumbnails & Outline
    void LoadThumbnails();
    void ClearThumbnails();
    void LoadOutline();

    // Coordinate and DPI conversion
    inline D2D1_POINT_2F ClientToDip(POINT pt) const {
        float scale = (m_dpiScale > 0.05f) ? m_dpiScale : 1.0f;
        return D2D1::Point2F((float)pt.x / scale, (float)pt.y / scale);
    }
    inline D2D1_POINT_2F ClientToDip(int x, int y) const {
        float scale = (m_dpiScale > 0.05f) ? m_dpiScale : 1.0f;
        return D2D1::Point2F((float)x / scale, (float)y / scale);
    }
    inline float DipToPx(float dip) const {
        float scale = (m_dpiScale > 0.05f) ? m_dpiScale : 1.0f;
        return dip * scale;
    }
    inline float PxToDip(int px) const {
        float scale = (m_dpiScale > 0.05f) ? m_dpiScale : 1.0f;
        return (float)px / scale;
    }
    D2D1_SIZE_F GetClientSizeDip() const;
    D2D1_RECT_F GetCanvasRectDip() const;
    POINT WindowToCanvas(const POINT& pt, const D2D1_RECT_F& canvasRect);
    D2D1_POINT_2F CanvasToPageCoords(float canvasX, float canvasY, const D2D1_RECT_F& canvasRect);

    // System Tray & Background Residency
    static const UINT TIMER_EXPLORER_NAV = 7001;
    static const UINT TIMER_PERIODIC_UPDATE = 7002;
    static const UINT WM_TRAYICON = WM_USER + 200;
    static const UINT IDM_TRAY_OPEN = 2001;
    static const UINT IDM_TRAY_AUTOSTART = 2002;
    static const UINT IDM_TRAY_EXIT = 2003;
    static const UINT IDM_TRAY_SETTINGS = 2004;

    void InitTrayIcon(bool showNotification = false);
    void RemoveTrayIcon();
    void ShowTrayNotification(const wchar_t* title, const wchar_t* msg);
    bool IsAutoStartEnabled() const;
    void SetAutoStartEnabled(bool enable);

    NOTIFYICONDATAW m_nid = {};
    bool m_hasTrayIcon = false;
    HICON m_hIconBig = NULL;
    HICON m_hIconSmall = NULL;

    // Message handlers
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK HudWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleHudMessage(UINT msg, WPARAM wParam, LPARAM lParam);

};

} // namespace WinPreview
