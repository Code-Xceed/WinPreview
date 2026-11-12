#include "d2d_engine.h"
#include <iostream>
#include <cmath>

namespace WinPreview {

D2DEngine::D2DEngine()
    : BrushBg(nullptr), BrushToolbar(nullptr), BrushSidebar(nullptr),
      BrushBorder(nullptr), BrushBorderSubtle(nullptr), BrushText(nullptr),
      BrushTextMuted(nullptr), BrushAccent(nullptr), BrushAccentGlow(nullptr),
      BrushHover(nullptr), BrushActive(nullptr), BrushCard(nullptr),
      BrushCardSelected(nullptr), BrushGlassPill(nullptr),
      BrushShadowDark(nullptr), BrushShadowSoft(nullptr),
      BrushSelection(nullptr), BrushHighlight(nullptr), BrushActiveMatch(nullptr),
      BrushCropOverlay(nullptr), BrushAudioWavePlayed(nullptr), BrushAudioWaveUnplayed(nullptr),
      BrushAudioWavePlayhead(nullptr), BrushAudioSubtitle(nullptr),
      BrushGreen(nullptr), BrushAmber(nullptr), BrushRed(nullptr),
      BrushBorderHighlight(nullptr), BrushTextDim(nullptr),
      BrushChipBg(nullptr), BrushChipBorder(nullptr), BrushCrimson(nullptr), BrushGinkgo(nullptr),
      FontDisplay(nullptr), FontDisplayCenter(nullptr),
      FontHero(nullptr), FontHeroCenter(nullptr), FontTitle(nullptr), FontTitleCenter(nullptr),
      FontBody(nullptr), FontBodyCenter(nullptr), FontCaption(nullptr), FontCaptionCenter(nullptr),
      FontBadge(nullptr), FontMono(nullptr), FontButton(nullptr), FontCaptionRight(nullptr),
      FontMenu(nullptr), FontTimer(nullptr), FontUtilityMono(nullptr), FontUtilityMonoCenter(nullptr),
      RoundStrokeStyle(nullptr)
{
}

D2DEngine::~D2DEngine() {
    DiscardDeviceResources();
    for (auto* geometry : m_iconGeometries) {
        if (geometry) geometry->Release();
    }
    m_iconGeometries.clear();
    SafeRelease(FontDisplay);
    SafeRelease(FontDisplayCenter);
    SafeRelease(FontHero);
    SafeRelease(FontHeroCenter);
    SafeRelease(FontTitle);
    SafeRelease(FontTitleCenter);
    SafeRelease(FontBody);
    SafeRelease(FontBodyCenter);
    SafeRelease(FontCaption);
    SafeRelease(FontCaptionCenter);
    SafeRelease(FontBadge);
    SafeRelease(FontMono);
    SafeRelease(FontButton);
    SafeRelease(FontCaptionRight);
    SafeRelease(FontMenu);
    SafeRelease(FontTimer);
    SafeRelease(FontUtilityMono);
    SafeRelease(FontUtilityMonoCenter);
    SafeRelease(RoundStrokeStyle);
    SafeRelease(m_dwriteFactory);
    SafeRelease(m_wicFactory);
    SafeRelease(m_d2dFactory);
}

bool D2DEngine::Initialize() {
    // Direct2D Factory
    D2D1_FACTORY_OPTIONS options = {};
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, (void**)&m_d2dFactory);
    if (FAILED(hr)) return false;

    // Create Apple-standard rounded stroke style (for clean, non-jagged vector icons)
    D2D1_STROKE_STYLE_PROPERTIES strokeProps = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND
    );
    m_d2dFactory->CreateStrokeStyle(&strokeProps, NULL, 0, &RoundStrokeStyle);

    // DirectWrite Factory
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&m_dwriteFactory);
    if (FAILED(hr)) return false;

    // WIC Factory
    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wicFactory));
    if (FAILED(hr)) return false;

    CreateFonts();
    return true;
}

void D2DEngine::CreateFonts() {
    SafeRelease(FontDisplay);
    SafeRelease(FontDisplayCenter);
    SafeRelease(FontHero);
    SafeRelease(FontHeroCenter);
    SafeRelease(FontTitle);
    SafeRelease(FontTitleCenter);
    SafeRelease(FontBody);
    SafeRelease(FontBodyCenter);
    SafeRelease(FontCaption);
    SafeRelease(FontCaptionCenter);
    SafeRelease(FontBadge);
    SafeRelease(FontMono);
    SafeRelease(FontButton);
    SafeRelease(FontCaptionRight);
    SafeRelease(FontMenu);
    SafeRelease(FontTimer);
    SafeRelease(FontUtilityMono);
    SafeRelease(FontUtilityMonoCenter);

    if (!m_dwriteFactory) return;

    const WCHAR* fontFallback = L"Segoe UI";
    static const WCHAR* s_displayFamily = nullptr;
    static const WCHAR* s_textFamily = nullptr;
    static const WCHAR* s_smallFamily = nullptr;

    if (!s_displayFamily) {
        // Fast probe: check if Segoe UI Variable Display exists
        IDWriteTextFormat* probe = nullptr;
        if (SUCCEEDED(m_dwriteFactory->CreateTextFormat(L"Segoe UI Variable Display", NULL,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"en-us", &probe)) && probe) {
            s_displayFamily = L"Segoe UI Variable Display";
            s_textFamily = L"Segoe UI Variable Text";
            s_smallFamily = L"Segoe UI Variable Small";
            probe->Release();
        } else {
            s_displayFamily = fontFallback;
            s_textFamily = fontFallback;
            s_smallFamily = fontFallback;
        }
    }

    // DirectWrite font sizes are defined in DIPs (1/96 inch).
    // Direct2D render targets automatically scale DIPs to device pixels using their configured DPI.
    // Specifying font sizes in DIPs ensures pixel-perfect, crisp rendering without double-scaling.

    // Display / Grand Title (Zen Editorial Tech Headline)
    m_dwriteFactory->CreateTextFormat(
        s_displayFamily, NULL,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        28.0f, L"en-us", &FontDisplay);

    m_dwriteFactory->CreateTextFormat(
        s_displayFamily, NULL,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        28.0f, L"en-us", &FontDisplayCenter);
    if (FontDisplayCenter) {
        FontDisplayCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        FontDisplayCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Hero Center (for centered titles in Welcome / Unsupported screens)
    m_dwriteFactory->CreateTextFormat(
        s_displayFamily, NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        22.0f, L"en-us", &FontHeroCenter);
    if (FontHeroCenter) {
        FontHeroCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        FontHeroCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Title (Medium text optical weight)
    m_dwriteFactory->CreateTextFormat(
        s_textFamily, NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        14.0f, L"en-us", &FontTitle);
    if (FontTitle) {
        FontTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        FontTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* ellipsis = nullptr;
        m_dwriteFactory->CreateEllipsisTrimmingSign(FontTitle, &ellipsis);
        FontTitle->SetTrimming(&trimming, ellipsis);
        SafeRelease(ellipsis);
    }

    // Title Center (Medium text optical weight, centered)
    m_dwriteFactory->CreateTextFormat(
        s_textFamily, NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        14.5f, L"en-us", &FontTitleCenter);
    if (FontTitleCenter) {
        FontTitleCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        FontTitleCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        FontTitleCenter->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* ellipsis = nullptr;
        m_dwriteFactory->CreateEllipsisTrimmingSign(FontTitleCenter, &ellipsis);
        FontTitleCenter->SetTrimming(&trimming, ellipsis);
        SafeRelease(ellipsis);
    }

    // Body (Balanced text reading size)
    m_dwriteFactory->CreateTextFormat(
        s_textFamily, NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f, L"en-us", &FontBody);

    // Body Center (Balanced text reading size, centered)
    m_dwriteFactory->CreateTextFormat(
        s_textFamily, NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f, L"en-us", &FontBodyCenter);
    if (FontBodyCenter) {
        FontBodyCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        FontBodyCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Caption (Micro optical size for crisp small labels)
    m_dwriteFactory->CreateTextFormat(
        s_smallFamily, NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.5f, L"en-us", &FontCaption);
    if (FontCaption) {
        FontCaption->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        FontCaption->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Caption Center (Centered micro optical size)
    m_dwriteFactory->CreateTextFormat(
        s_smallFamily, NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.5f, L"en-us", &FontCaptionCenter);
    if (FontCaptionCenter) {
        FontCaptionCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        FontCaptionCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        FontCaptionCenter->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // Badge (Small optical size, high legibility semi-bold)
    m_dwriteFactory->CreateTextFormat(
        s_smallFamily, NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10.0f, L"en-us", &FontBadge);
    if (FontBadge) {
        FontBadge->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        FontBadge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        FontBadge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Monospace (Cascadia Code preferred, Consolas fallback - zero disk scan on startup)
    HRESULT hrMono = m_dwriteFactory->CreateTextFormat(
        L"Cascadia Code", NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.5f, L"en-us", &FontMono);
    if (FAILED(hrMono) || !FontMono) {
        m_dwriteFactory->CreateTextFormat(
            L"Consolas", NULL,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.5f, L"en-us", &FontMono);
    }
    if (FontMono) {
        FontMono->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        FontMono->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // Button label (Small optical size, centered, crisp SemiBold)
    m_dwriteFactory->CreateTextFormat(
        s_smallFamily, NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"en-us", &FontButton);
    if (FontButton) {
        FontButton->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        FontButton->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        FontButton->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Caption Right (Trailing micro optical size for keyboard shortcut hints)
    m_dwriteFactory->CreateTextFormat(
        s_smallFamily, NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f, L"en-us", &FontCaptionRight);
    if (FontCaptionRight) {
        FontCaptionRight->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        FontCaptionRight->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        FontCaptionRight->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    // Menu Item (12.0f, leading text alignment, vertical center, no-wrap with ellipsis trimming)
    m_dwriteFactory->CreateTextFormat(
        s_smallFamily, NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"en-us", &FontMenu);
    if (FontMenu) {
        FontMenu->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        FontMenu->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        FontMenu->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* ellipsis = nullptr;
        if (SUCCEEDED(m_dwriteFactory->CreateEllipsisTrimmingSign(FontMenu, &ellipsis)) && ellipsis) {
            FontMenu->SetTrimming(&trimming, ellipsis);
            ellipsis->Release();
        }
    }

    // Monospace Timer & Numeric Readouts (zero jitter during playback & counting)
    HRESULT hrTimer = m_dwriteFactory->CreateTextFormat(
        L"Cascadia Code", NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.5f, L"en-us", &FontTimer);
    if (FAILED(hrTimer) || !FontTimer) {
        m_dwriteFactory->CreateTextFormat(
            L"Consolas", NULL,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.5f, L"en-us", &FontTimer);
    }
    if (FontTimer) {
        FontTimer->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        FontTimer->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        FontTimer->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Monospace Utility Labels (Zen Editorial Tech badges, counts & metadata)
    HRESULT hrUtil = m_dwriteFactory->CreateTextFormat(
        L"Cascadia Code", NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10.5f, L"en-us", &FontUtilityMono);
    if (FAILED(hrUtil) || !FontUtilityMono) {
        m_dwriteFactory->CreateTextFormat(
            L"Consolas", NULL,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            10.5f, L"en-us", &FontUtilityMono);
    }
    if (FontUtilityMono) {
        FontUtilityMono->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        FontUtilityMono->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    HRESULT hrUtilCenter = m_dwriteFactory->CreateTextFormat(
        L"Cascadia Code", NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10.5f, L"en-us", &FontUtilityMonoCenter);
    if (FAILED(hrUtilCenter) || !FontUtilityMonoCenter) {
        m_dwriteFactory->CreateTextFormat(
            L"Consolas", NULL,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            10.5f, L"en-us", &FontUtilityMonoCenter);
    }
    if (FontUtilityMonoCenter) {
        FontUtilityMonoCenter->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        FontUtilityMonoCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        FontUtilityMonoCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void D2DEngine::UpdateDpi(UINT dpi) {
    m_dpi = dpi;
    m_dpiScale = (float)dpi / 96.0f;
    if (m_renderTarget) {
        m_renderTarget->SetDpi(96.0f * m_dpiScale, 96.0f * m_dpiScale);
    }
}

bool D2DEngine::CreateDeviceResources(HWND hwnd) {
    m_hwnd = hwnd;
    if (m_renderTarget) return true;

    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    if (size.width == 0) size.width = 1;
    if (size.height == 0) size.height = 1;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * m_dpiScale, 96.0f * m_dpiScale,
        D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        hwnd, size, D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS
    );

    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(rtProps, hwndProps, &m_renderTarget);
    if (FAILED(hr)) return false;

    m_renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    CreateBrushes();
    return true;
}

void D2DEngine::CreateBrushes() {
    if (!m_renderTarget) return;

    // Refined Obsidian & Zen Editorial Tech Dark Mode Palette
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.035f, 0.035f, 0.043f, 1.0f), &BrushBg);             // Deep Obsidian (#09090B)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.055f, 0.055f, 0.067f, 1.0f), &BrushToolbar);        // Dark Tool Bar (#0E0E11)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.047f, 0.047f, 0.059f, 1.0f), &BrushSidebar);        // Deep Sidebar (#0C0C0F)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.133f, 0.133f, 0.157f, 1.0f), &BrushBorder);         // Precision Border (#222228)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.098f, 0.098f, 0.118f, 1.0f), &BrushBorderSubtle);   // Subtle Border (#19191E)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.973f, 0.973f, 0.980f, 1.0f), &BrushText);           // Pure text (#F8F8FA)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.520f, 0.520f, 0.560f, 1.0f), &BrushTextMuted);      // Muted text (#85858F)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.380f, 0.380f, 0.420f, 1.0f), &BrushTextDim);        // Dim utility text (#61616B)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.039f, 0.518f, 1.000f, 1.0f), &BrushAccent);         // Apple Blue / Electric Cyan (#0A84FF)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.039f, 0.518f, 1.000f, 0.35f), &BrushAccentGlow);    // Accent Glow
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f), &BrushHover);               // Hover overlay
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f), &BrushActive);              // Active overlay
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.071f, 0.071f, 0.086f, 1.0f), &BrushCard);           // Elevated Card (#121216)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.039f, 0.518f, 1.000f, 0.15f), &BrushCardSelected);   // Card selected tint
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.071f, 0.071f, 0.086f, 0.92f), &BrushGlassPill);      // Frosted HUD Pill (#121216EB)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.000f, 0.000f, 0.000f, 0.45f), &BrushShadowDark);     // Hard drop shadow
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.000f, 0.000f, 0.000f, 0.18f), &BrushShadowSoft);     // Ambient soft shadow
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.000f, 0.470f, 0.830f, 0.35f), &BrushSelection);      // PDF Text Selection Blue
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.000f, 0.880f, 0.200f, 0.45f), &BrushHighlight);      // Search Match Yellow Glow
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.000f, 0.500f, 1.000f, 0.70f), &BrushActiveMatch);    // Active Match Blue Pill
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.000f, 0.000f, 0.000f, 0.65f), &BrushCropOverlay);    // Crop Background Dimmer
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.039f, 0.518f, 1.000f, 1.00f), &BrushAudioWavePlayed);   // Electric Apple Blue (#0A84FF)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.240f, 0.240f, 0.280f, 0.85f), &BrushAudioWaveUnplayed); // Clean crisp Zinc
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.220f, 0.740f, 1.000f, 1.00f), &BrushAudioWavePlayhead); // Vibrant Cyan Playhead
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.720f, 0.740f, 0.780f, 1.00f), &BrushAudioSubtitle);     // High-legibility Zinc-300
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.188f, 0.820f, 0.447f, 1.00f), &BrushGreen);             // Emerald Green (#30D158 / #10B981)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.000f, 0.624f, 0.039f, 1.00f), &BrushAmber);             // Amber Orange (#FF9F0A)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.000f, 0.271f, 0.227f, 1.00f), &BrushRed);               // Red (#FF453A)

    // Zen Editorial Tech Accents
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.000f, 1.000f, 1.000f, 0.08f), &BrushBorderHighlight);  // Top-edge light bevel
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.078f, 0.078f, 0.094f, 1.0f), &BrushChipBg);           // Tech pill surface (#141418)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.149f, 0.149f, 0.184f, 1.0f), &BrushChipBorder);       // Tech pill border (#26262F)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.882f, 0.114f, 0.282f, 1.0f), &BrushCrimson);          // Japanese Maple Crimson (#E11D48)
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.961f, 0.620f, 0.043f, 1.0f), &BrushGinkgo);           // Golden Ginkgo Amber (#F59E0B)
}

void D2DEngine::DiscardDeviceResources() {
    SafeRelease(BrushBg);
    SafeRelease(BrushToolbar);
    SafeRelease(BrushSidebar);
    SafeRelease(BrushBorder);
    SafeRelease(BrushBorderSubtle);
    SafeRelease(BrushText);
    SafeRelease(BrushTextMuted);
    SafeRelease(BrushTextDim);
    SafeRelease(BrushAccent);
    SafeRelease(BrushAccentGlow);
    SafeRelease(BrushHover);
    SafeRelease(BrushActive);
    SafeRelease(BrushCard);
    SafeRelease(BrushCardSelected);
    SafeRelease(BrushGlassPill);
    SafeRelease(BrushShadowDark);
    SafeRelease(BrushShadowSoft);
    SafeRelease(BrushSelection);
    SafeRelease(BrushHighlight);
    SafeRelease(BrushActiveMatch);
    SafeRelease(BrushCropOverlay);
    SafeRelease(BrushAudioWavePlayed);
    SafeRelease(BrushAudioWaveUnplayed);
    SafeRelease(BrushAudioWavePlayhead);
    SafeRelease(BrushAudioSubtitle);
    SafeRelease(BrushGreen);
    SafeRelease(BrushAmber);
    SafeRelease(BrushRed);
    SafeRelease(BrushBorderHighlight);
    SafeRelease(BrushChipBg);
    SafeRelease(BrushChipBorder);
    SafeRelease(BrushCrimson);
    SafeRelease(BrushGinkgo);
    SafeRelease(m_renderTarget);
}

void D2DEngine::Resize(UINT width, UINT height) {
    if (m_renderTarget) {
        D2D1_SIZE_U size = D2D1::SizeU(width, height);
        m_renderTarget->Resize(size);
    }
}

void D2DEngine::BeginDraw() {
    if (m_renderTarget) {
        m_renderTarget->BeginDraw();
    }
}

HRESULT D2DEngine::EndDraw() {
    if (!m_renderTarget) return S_OK;
    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        CreateDeviceResources(m_hwnd);
    }
    return hr;
}

// =========================================================================
// Vector icons are rendered from Lucide-derived assets in icon_engine.cpp.

void D2DEngine::DrawMicroTooltip(float centerX, float topY, const WCHAR* text, float alpha, bool above) {
    if (!m_renderTarget || !text || alpha <= 0.01f) return;

    UINT32 len = (UINT32)wcslen(text);
    if (len == 0) return;

    IDWriteTextLayout* layout = nullptr;
    m_dwriteFactory->CreateTextLayout(text, len, FontCaption, 320.0f, 24.0f, &layout);
    if (!layout) return;

    DWRITE_TEXT_METRICS tm;
    layout->GetMetrics(&tm);
    float textW = tm.width;
    SafeRelease(layout);

    float padH = 10.0f;
    float pillW = textW + padH * 2.0f;
    float pillH = 24.0f;

    float left = std::floor(centerX - pillW / 2.0f + 0.5f);
    float top = std::floor((above ? (topY - pillH - 8.0f) : (topY + 8.0f)) + 0.5f);
    D2D1_RECT_F pillRect = D2D1::RectF(left, top, left + pillW, top + pillH);
    D2D1_ROUNDED_RECT rpill = D2D1::RoundedRect(pillRect, 6.0f, 6.0f);

    // Drop shadow
    D2D1_ROUNDED_RECT rshadow = D2D1::RoundedRect(
        D2D1::RectF(pillRect.left, pillRect.top + 2.0f, pillRect.right, pillRect.bottom + 4.0f), 6.0f, 6.0f
    );
    BrushShadowDark->SetOpacity(alpha * 0.45f);
    m_renderTarget->FillRoundedRectangle(rshadow, BrushShadowDark);

    // Frosted Pill background
    BrushToolbar->SetOpacity(alpha * 0.96f);
    m_renderTarget->FillRoundedRectangle(rpill, BrushToolbar);

    // Border
    BrushBorder->SetOpacity(alpha * 0.90f);
    m_renderTarget->DrawRoundedRectangle(rpill, BrushBorder, 1.0f);

    // Text (optically centered horizontally and vertically)
    D2D1_RECT_F textRect = D2D1::RectF(pillRect.left, pillRect.top + 1.0f, pillRect.right, pillRect.bottom);
    BrushText->SetOpacity(alpha);
    m_renderTarget->DrawText(text, len, FontCaptionCenter ? FontCaptionCenter : FontCaption, textRect, BrushText);

    // Reset opacities
    BrushShadowDark->SetOpacity(0.40f);
    BrushToolbar->SetOpacity(1.0f);
    BrushBorder->SetOpacity(1.0f);
    BrushText->SetOpacity(1.0f);
}

// ZEN EDITORIAL TECH DRAWING HELPERS
// =========================================================================

void D2DEngine::DrawTechChip(const D2D1_RECT_F& rect, const wchar_t* label, const wchar_t* countOrTag, bool active, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !label) return;

    float radius = (rect.bottom - rect.top) * 0.5f;
    if (radius > 12.0f * m_dpiScale) radius = 12.0f * m_dpiScale;
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, radius, radius);

    // Chip Background
    if (active && BrushAccent) {
        D2D1_COLOR_F c = D2D1::ColorF(0.039f, 0.518f, 1.000f, 0.18f);
        ID2D1SolidColorBrush* activeBg = nullptr;
        if (SUCCEEDED(target->CreateSolidColorBrush(c, &activeBg)) && activeBg) {
            target->FillRoundedRectangle(rrect, activeBg);
            activeBg->Release();
        }
    } else if (BrushChipBg) {
        target->FillRoundedRectangle(rrect, BrushChipBg);
    }

    // 1px Border
    ID2D1Brush* borderBrush = active ? BrushAccent : (BrushChipBorder ? BrushChipBorder : BrushBorderSubtle);
    if (borderBrush) {
        target->DrawRoundedRectangle(rrect, borderBrush, 1.0f * m_dpiScale);
    }

    // Typography
    IDWriteTextFormat* font = FontUtilityMono ? FontUtilityMono : FontCaption;
    ID2D1Brush* textBrush = active ? (BrushAccent ? BrushAccent : BrushText) : (BrushText ? BrushText : BrushTextMuted);

    if (countOrTag && countOrTag[0] != L'\0') {
        float padding = 8.0f * m_dpiScale;
        D2D1_RECT_F labelRect = D2D1::RectF(rect.left + padding, rect.top, rect.right - 28.0f * m_dpiScale, rect.bottom);
        target->DrawText(label, (UINT32)wcslen(label), font, labelRect, textBrush);

        float badgeW = 20.0f * m_dpiScale;
        float badgeH = (rect.bottom - rect.top) - 6.0f * m_dpiScale;
        float badgeX = rect.right - badgeW - 4.0f * m_dpiScale;
        float badgeY = rect.top + 3.0f * m_dpiScale;
        D2D1_ROUNDED_RECT badgeRRect = D2D1::RoundedRect(
            D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH),
            badgeH * 0.5f, badgeH * 0.5f
        );
        if (BrushBorder) {
            target->FillRoundedRectangle(badgeRRect, BrushBorder);
        }
        ID2D1Brush* countBrush = BrushTextMuted ? BrushTextMuted : textBrush;
        IDWriteTextFormat* countFont = FontUtilityMonoCenter ? FontUtilityMonoCenter : font;
        target->DrawText(countOrTag, (UINT32)wcslen(countOrTag), countFont,
            D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH), countBrush);
    } else {
        IDWriteTextFormat* centerFont = FontUtilityMonoCenter ? FontUtilityMonoCenter : FontButton;
        target->DrawText(label, (UINT32)wcslen(label), centerFont, rect, textBrush);
    }
}

} // namespace WinPreview
