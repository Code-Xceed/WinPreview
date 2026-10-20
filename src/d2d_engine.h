#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>

namespace WinPreview {

template<typename T>
void SafeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

class D2DEngine {
public:
    D2DEngine();
    ~D2DEngine();

    bool Initialize();
    bool CreateDeviceResources(HWND hwnd);
    void DiscardDeviceResources();

    void Resize(UINT width, UINT height);
    void BeginDraw();
    HRESULT EndDraw();

    ID2D1Factory1* GetD2DFactory() const { return m_d2dFactory; }
    ID2D1HwndRenderTarget* GetRenderTarget() const { return m_renderTarget; }
    IDWriteFactory* GetDWriteFactory() const { return m_dwriteFactory; }
    IWICImagingFactory* GetWICFactory() const { return m_wicFactory; }

    // Color brushes
    ID2D1SolidColorBrush* BrushBg = nullptr;
    ID2D1SolidColorBrush* BrushToolbar = nullptr;
    ID2D1SolidColorBrush* BrushSidebar = nullptr;
    ID2D1SolidColorBrush* BrushBorder = nullptr;
    ID2D1SolidColorBrush* BrushBorderSubtle = nullptr;
    ID2D1SolidColorBrush* BrushText = nullptr;
    ID2D1SolidColorBrush* BrushTextMuted = nullptr;
    ID2D1SolidColorBrush* BrushAccent = nullptr;
    ID2D1SolidColorBrush* BrushAccentGlow = nullptr;
    ID2D1SolidColorBrush* BrushHover = nullptr;
    ID2D1SolidColorBrush* BrushActive = nullptr;
    ID2D1SolidColorBrush* BrushCard = nullptr;
    ID2D1SolidColorBrush* BrushCardSelected = nullptr;
    ID2D1SolidColorBrush* BrushGlassPill = nullptr;
    ID2D1SolidColorBrush* BrushShadowDark = nullptr;
    ID2D1SolidColorBrush* BrushShadowSoft = nullptr;
    ID2D1SolidColorBrush* BrushSelection = nullptr;
    ID2D1SolidColorBrush* BrushHighlight = nullptr;
    ID2D1SolidColorBrush* BrushActiveMatch = nullptr;
    ID2D1SolidColorBrush* BrushCropOverlay = nullptr;
    ID2D1SolidColorBrush* BrushAudioWavePlayed = nullptr;
    ID2D1SolidColorBrush* BrushAudioWaveUnplayed = nullptr;
    ID2D1SolidColorBrush* BrushAudioWavePlayhead = nullptr;
    ID2D1SolidColorBrush* BrushAudioSubtitle = nullptr;
    ID2D1SolidColorBrush* BrushGreen = nullptr;
    ID2D1SolidColorBrush* BrushAmber = nullptr;
    ID2D1SolidColorBrush* BrushRed = nullptr;

    // Text formats
    IDWriteTextFormat* FontHero = nullptr;
    IDWriteTextFormat* FontHeroCenter = nullptr;
    IDWriteTextFormat* FontTitle = nullptr;
    IDWriteTextFormat* FontTitleCenter = nullptr;
    IDWriteTextFormat* FontBody = nullptr;
    IDWriteTextFormat* FontBodyCenter = nullptr;
    IDWriteTextFormat* FontCaption = nullptr;
    IDWriteTextFormat* FontCaptionCenter = nullptr;
    IDWriteTextFormat* FontBadge = nullptr;
    IDWriteTextFormat* FontMono = nullptr;
    IDWriteTextFormat* FontButton = nullptr;
    IDWriteTextFormat* FontCaptionRight = nullptr;
    IDWriteTextFormat* FontMenu = nullptr;

    // Stroke styles
    ID2D1StrokeStyle* RoundStrokeStyle = nullptr;

    // Vector Icon Drawing Helpers (Resolution-Independent)
    void DrawIconSidebar(float cx, float cy, float size, bool isOpen, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconChevronLeft(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconChevronRight(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconZoomIn(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconZoomOut(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconFit(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconRotateLeft(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconRotateRight(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconPlay(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconPause(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconVolume(float cx, float cy, float size, bool isMuted, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconFullscreen(float cx, float cy, float size, bool isFullscreen, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconSkipBack(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconSkipForward(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconLoop(float cx, float cy, float size, bool active, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconMusic(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconFilePdf(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconFileImage(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconFileVideo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconSearch(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconCrop(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconInfo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconOutline(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconCopy(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconWrap(float cx, float cy, float size, bool isWrapped, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconTable(float cx, float cy, float size, bool isTable, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconSave(float cx, float cy, float size, bool isDirty, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconUndo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconRedo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconCut(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconPaste(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconSettings(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconEye(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconFileDocument(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconExternalLink(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);
    void DrawIconHamburger(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt = nullptr);

    // Micro-Tooltip Renderer (Smooth Alpha, Direct2D Anti-aliased)
    void DrawMicroTooltip(float centerX, float topY, const WCHAR* text, float alpha, bool above = false);

    float GetDpiScale() const { return m_dpiScale; }
    void UpdateDpi(UINT dpi);

private:
    ID2D1Factory1* m_d2dFactory = nullptr;
    ID2D1HwndRenderTarget* m_renderTarget = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    IWICImagingFactory* m_wicFactory = nullptr;

    HWND m_hwnd = nullptr;
    float m_dpiScale = 1.0f;
    UINT m_dpi = 96;

    void CreateBrushes();
    void CreateFonts();
};

} // namespace WinPreview
