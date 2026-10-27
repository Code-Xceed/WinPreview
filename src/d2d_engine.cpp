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
      BrushCrosshair(nullptr), BrushBorderHighlight(nullptr), BrushTextDim(nullptr),
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
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.000f, 1.000f, 1.000f, 0.22f), &BrushCrosshair);       // Corner crosshair marks (+)
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
    SafeRelease(BrushCrosshair);
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
// VECTOR ICON DRAWING IMPLEMENTATIONS (Resolution-Independent Direct2D)
// =========================================================================

void D2DEngine::DrawIconSidebar(float cx, float cy, float size, bool isOpen, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float w = 6.8f;
    float h = 5.2f;
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(D2D1::RectF(cx - w, cy - h, cx + w, cy + h), 2.0f, 2.0f);
    target->DrawRoundedRectangle(rrect, brush, 1.5f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - w + 4.2f, cy - h), D2D1::Point2F(cx - w + 4.2f, cy + h), brush, 1.4f, RoundStrokeStyle);

    if (isOpen) {
        D2D1_ROUNDED_RECT pane = D2D1::RoundedRect(D2D1::RectF(cx - w + 0.8f, cy - h + 0.8f, cx - w + 3.8f, cy + h - 0.8f), 1.2f, 1.2f);
        target->FillRoundedRectangle(pane, BrushCardSelected ? (ID2D1Brush*)BrushCardSelected : brush);
        target->DrawLine(D2D1::Point2F(cx - 0.5f, cy - 2.0f), D2D1::Point2F(cx + 4.5f, cy - 2.0f), brush, 1.2f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(cx - 0.5f, cy + 2.0f), D2D1::Point2F(cx + 4.5f, cy + 2.0f), brush, 1.2f, RoundStrokeStyle);
    }
}

void D2DEngine::DrawIconChevronLeft(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float s = 4.5f;
    target->DrawLine(D2D1::Point2F(cx + 1.8f, cy - s), D2D1::Point2F(cx - 2.2f, cy), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - 2.2f, cy), D2D1::Point2F(cx + 1.8f, cy + s), brush, 1.6f, RoundStrokeStyle);
}

void D2DEngine::DrawIconChevronRight(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float s = 4.5f;
    target->DrawLine(D2D1::Point2F(cx - 1.8f, cy - s), D2D1::Point2F(cx + 2.2f, cy), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx + 2.2f, cy), D2D1::Point2F(cx - 1.8f, cy + s), brush, 1.6f, RoundStrokeStyle);
}

void D2DEngine::DrawIconZoomIn(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float s = 4.5f;
    target->DrawLine(D2D1::Point2F(cx - s, cy), D2D1::Point2F(cx + s, cy), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx, cy - s), D2D1::Point2F(cx, cy + s), brush, 1.6f, RoundStrokeStyle);
}

void D2DEngine::DrawIconZoomOut(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float s = 4.5f;
    target->DrawLine(D2D1::Point2F(cx - s, cy), D2D1::Point2F(cx + s, cy), brush, 1.6f, RoundStrokeStyle);
}

void D2DEngine::DrawIconFit(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    // Outer frame
    float w = 6.8f;
    float h = 5.2f;
    D2D1_ROUNDED_RECT frame = D2D1::RoundedRect(D2D1::RectF(cx - w, cy - h, cx + w, cy + h), 2.0f, 2.0f);
    target->DrawRoundedRectangle(frame, brush, 1.5f, RoundStrokeStyle);

    // Center divider guide
    target->DrawLine(D2D1::Point2F(cx, cy - 3.2f), D2D1::Point2F(cx, cy + 3.2f), brush, 1.0f, RoundStrokeStyle);

    // Left fit arrow pointing left
    target->DrawLine(D2D1::Point2F(cx - 1.0f, cy), D2D1::Point2F(cx - 4.5f, cy), brush, 1.4f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - 3.0f, cy - 1.8f), D2D1::Point2F(cx - 4.5f, cy), brush, 1.4f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - 3.0f, cy + 1.8f), D2D1::Point2F(cx - 4.5f, cy), brush, 1.4f, RoundStrokeStyle);

    // Right fit arrow pointing right
    target->DrawLine(D2D1::Point2F(cx + 1.0f, cy), D2D1::Point2F(cx + 4.5f, cy), brush, 1.4f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx + 3.0f, cy - 1.8f), D2D1::Point2F(cx + 4.5f, cy), brush, 1.4f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx + 3.0f, cy + 1.8f), D2D1::Point2F(cx + 4.5f, cy), brush, 1.4f, RoundStrokeStyle);
}

void D2DEngine::DrawIconRotateLeft(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float r = 5.2f;
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            // Start at bottom-right (about 45 deg)
            sink->BeginFigure(D2D1::Point2F(cx + 3.8f, cy + 3.8f), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(cx - 2.6f, cy - 4.5f),
                D2D1::SizeF(r, r),
                0.0f,
                D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                D2D1_ARC_SIZE_LARGE
            ));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            target->DrawGeometry(path, brush, 1.6f, RoundStrokeStyle);
        }
        path->Release();
    }
    // Arrowhead at (cx - 2.6f, cy - 4.5f) pointing down-left
    float ax = cx - 2.6f;
    float ay = cy - 4.5f;
    target->DrawLine(D2D1::Point2F(ax - 2.8f, ay + 0.5f), D2D1::Point2F(ax, ay), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(ax + 0.5f, ay - 2.8f), D2D1::Point2F(ax, ay), brush, 1.6f, RoundStrokeStyle);
}

void D2DEngine::DrawIconRotateRight(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float r = 5.2f;
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            // Start at bottom-left
            sink->BeginFigure(D2D1::Point2F(cx - 3.8f, cy + 3.8f), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(cx + 2.6f, cy - 4.5f),
                D2D1::SizeF(r, r),
                0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                D2D1_ARC_SIZE_LARGE
            ));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            target->DrawGeometry(path, brush, 1.6f, RoundStrokeStyle);
        }
        path->Release();
    }
    // Arrowhead at (cx + 2.6f, cy - 4.5f) pointing down-right
    float ax = cx + 2.6f;
    float ay = cy - 4.5f;
    target->DrawLine(D2D1::Point2F(ax + 2.8f, ay + 0.5f), D2D1::Point2F(ax, ay), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(ax - 0.5f, ay - 2.8f), D2D1::Point2F(ax, ay), brush, 1.6f, RoundStrokeStyle);
}

void D2DEngine::DrawIconPlay(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            float ox = cx + 0.8f; // Optical center shift
            sink->BeginFigure(D2D1::Point2F(ox - 3.8f, cy - 5.5f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(ox + 5.2f, cy));
            sink->AddLine(D2D1::Point2F(ox - 3.8f, cy + 5.5f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            target->FillGeometry(path, brush);
            target->DrawGeometry(path, brush, 1.4f, RoundStrokeStyle); // Round vertex caps
        }
        path->Release();
    }
}

void D2DEngine::DrawIconPause(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float h = 5.5f;
    float gap = 2.8f;
    D2D1_ROUNDED_RECT bar1 = D2D1::RoundedRect(D2D1::RectF(cx - gap - 2.0f, cy - h, cx - gap + 0.5f, cy + h), 1.2f, 1.2f);
    D2D1_ROUNDED_RECT bar2 = D2D1::RoundedRect(D2D1::RectF(cx + gap - 0.5f, cy - h, cx + gap + 2.0f, cy + h), 1.2f, 1.2f);
    target->FillRoundedRectangle(bar1, brush);
    target->FillRoundedRectangle(bar2, brush);
}

void D2DEngine::DrawIconVolume(float cx, float cy, float size, bool isMuted, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            sink->BeginFigure(D2D1::Point2F(cx - 5.5f, cy - 2.5f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(cx - 2.8f, cy - 2.5f));
            sink->AddLine(D2D1::Point2F(cx + 0.8f, cy - 5.5f));
            sink->AddLine(D2D1::Point2F(cx + 0.8f, cy + 5.5f));
            sink->AddLine(D2D1::Point2F(cx - 2.8f, cy + 2.5f));
            sink->AddLine(D2D1::Point2F(cx - 5.5f, cy + 2.5f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            target->FillGeometry(path, brush);
        }
        path->Release();
    }
    if (isMuted) {
        float x1 = cx + 3.2f, x2 = cx + 7.2f;
        target->DrawLine(D2D1::Point2F(x1, cy - 2.5f), D2D1::Point2F(x2, cy + 2.5f), brush, 1.6f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(x2, cy - 2.5f), D2D1::Point2F(x1, cy + 2.5f), brush, 1.6f, RoundStrokeStyle);
    } else {
        ID2D1PathGeometry* wave = nullptr;
        m_d2dFactory->CreatePathGeometry(&wave);
        if (wave) {
            ID2D1GeometrySink* sink = nullptr;
            wave->Open(&sink);
            if (sink) {
                sink->BeginFigure(D2D1::Point2F(cx + 3.0f, cy - 3.2f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + 3.0f, cy + 3.2f), D2D1::SizeF(3.6f, 3.6f), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);

                sink->BeginFigure(D2D1::Point2F(cx + 5.2f, cy - 5.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + 5.2f, cy + 5.0f), D2D1::SizeF(6.0f, 6.0f), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                sink->Release();
                target->DrawGeometry(wave, brush, 1.5f, RoundStrokeStyle);
            }
            wave->Release();
        }
    }
}

void D2DEngine::DrawIconFullscreen(float cx, float cy, float size, bool isFullscreen, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    if (!isFullscreen) {
        // Outward expansion corner brackets (Enter Fullscreen)
        float r = 5.5f;
        float arm = 3.2f;
        // Top-left
        target->DrawLine(D2D1::Point2F(cx - r, cy - r + arm), D2D1::Point2F(cx - r, cy - r), brush, 1.6f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx - r + arm, cy - r), brush, 1.6f, RoundStrokeStyle);
        // Top-right
        target->DrawLine(D2D1::Point2F(cx + r - arm, cy - r), D2D1::Point2F(cx + r, cy - r), brush, 1.6f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx + r, cy - r + arm), brush, 1.6f, RoundStrokeStyle);
        // Bottom-left
        target->DrawLine(D2D1::Point2F(cx - r, cy + r - arm), D2D1::Point2F(cx - r, cy + r), brush, 1.6f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(cx - r, cy + r), D2D1::Point2F(cx - r + arm, cy + r), brush, 1.6f, RoundStrokeStyle);
        // Bottom-right
        target->DrawLine(D2D1::Point2F(cx + r - arm, cy + r), D2D1::Point2F(cx + r, cy + r), brush, 1.6f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(cx + r, cy + r), D2D1::Point2F(cx + r, cy + r - arm), brush, 1.6f, RoundStrokeStyle);
    } else {
        // Inward collapse corner brackets (Exit Fullscreen)
        float inR = 1.6f;
        float outR = 5.5f;
        // Top-left
        target->DrawLine(D2D1::Point2F(cx - inR, cy - outR), D2D1::Point2F(cx - inR, cy - inR), brush, 1.6f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(cx - inR, cy - inR), D2D1::Point2F(cx - outR, cy - inR), brush, 1.6f, RoundStrokeStyle);
        // Top-right
        target->DrawLine(D2D1::Point2F(cx + inR, cy - outR), D2D1::Point2F(cx + inR, cy - inR), brush, 1.6f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(cx + inR, cy - inR), D2D1::Point2F(cx + outR, cy - inR), brush, 1.6f, RoundStrokeStyle);
        // Bottom-left
        target->DrawLine(D2D1::Point2F(cx - inR, cy + outR), D2D1::Point2F(cx - inR, cy + inR), brush, 1.6f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(cx - inR, cy + inR), D2D1::Point2F(cx - outR, cy + inR), brush, 1.6f, RoundStrokeStyle);
        // Bottom-right
        target->DrawLine(D2D1::Point2F(cx + inR, cy + outR), D2D1::Point2F(cx + inR, cy + inR), brush, 1.6f, RoundStrokeStyle);
        target->DrawLine(D2D1::Point2F(cx + inR, cy + inR), D2D1::Point2F(cx + outR, cy + inR), brush, 1.6f, RoundStrokeStyle);
    }
}

void D2DEngine::DrawIconSkipBack(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float s = 4.2f;
    // Left chevron
    target->DrawLine(D2D1::Point2F(cx - 0.5f, cy - s), D2D1::Point2F(cx - 4.5f, cy), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - 4.5f, cy), D2D1::Point2F(cx - 0.5f, cy + s), brush, 1.6f, RoundStrokeStyle);
    // Right chevron
    target->DrawLine(D2D1::Point2F(cx + 4.5f, cy - s), D2D1::Point2F(cx + 0.5f, cy), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx + 0.5f, cy), D2D1::Point2F(cx + 4.5f, cy + s), brush, 1.6f, RoundStrokeStyle);
}

void D2DEngine::DrawIconSkipForward(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float s = 4.2f;
    // Left chevron
    target->DrawLine(D2D1::Point2F(cx - 4.5f, cy - s), D2D1::Point2F(cx - 0.5f, cy), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - 0.5f, cy), D2D1::Point2F(cx - 4.5f, cy + s), brush, 1.6f, RoundStrokeStyle);
    // Right chevron
    target->DrawLine(D2D1::Point2F(cx + 0.5f, cy - s), D2D1::Point2F(cx + 4.5f, cy), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx + 4.5f, cy), D2D1::Point2F(cx + 0.5f, cy + s), brush, 1.6f, RoundStrokeStyle);
}

void D2DEngine::DrawIconLoop(float cx, float cy, float size, bool active, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float w = 5.5f;
    float h = 3.2f;
    float r = 2.4f;
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            // Top arrow arc (left side curves up, then goes right)
            sink->BeginFigure(D2D1::Point2F(cx - w, cy + h - r), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - w + r, cy - h), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(D2D1::Point2F(cx + w, cy - h));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);

            // Bottom arrow arc (right side curves down, then goes left)
            sink->BeginFigure(D2D1::Point2F(cx + w, cy - h + r), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + w - r, cy + h), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(D2D1::Point2F(cx - w, cy + h));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);

            sink->Close();
            sink->Release();
            target->DrawGeometry(path, brush, active ? 1.8f : 1.5f, RoundStrokeStyle);
        }
        path->Release();
    }
    // Arrowheads:
    // Top arrow points right at (cx + w, cy - h)
    target->DrawLine(D2D1::Point2F(cx + w - 2.6f, cy - h - 2.4f), D2D1::Point2F(cx + w, cy - h), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx + w - 2.6f, cy - h + 2.4f), D2D1::Point2F(cx + w, cy - h), brush, 1.6f, RoundStrokeStyle);
    // Bottom arrow points left at (cx - w, cy + h)
    target->DrawLine(D2D1::Point2F(cx - w + 2.6f, cy + h - 2.4f), D2D1::Point2F(cx - w, cy + h), brush, 1.6f, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - w + 2.6f, cy + h + 2.4f), D2D1::Point2F(cx - w, cy + h), brush, 1.6f, RoundStrokeStyle);
}

void D2DEngine::DrawIconFilePdf(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float w = size * 0.35f;
    float h = size * 0.48f;
    float fold = w * 0.45f;
    D2D1_ROUNDED_RECT doc = D2D1::RoundedRect(D2D1::RectF(cx - w, cy - h, cx + w, cy + h), 2.0f, 2.0f);
    target->DrawRoundedRectangle(doc, brush, 1.4f);
    target->DrawLine(D2D1::Point2F(cx + w - fold, cy - h), D2D1::Point2F(cx + w, cy - h + fold), brush, 1.4f);
}

void D2DEngine::DrawIconFileImage(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float s = size * 0.42f;
    D2D1_ROUNDED_RECT frame = D2D1::RoundedRect(D2D1::RectF(cx - s, cy - s * 0.8f, cx + s, cy + s * 0.8f), 2.5f, 2.5f);
    target->DrawRoundedRectangle(frame, brush, 1.4f);
    // Sun
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - s * 0.4f, cy - s * 0.25f), 2.0f, 2.0f), brush);
    // Mountain
    target->DrawLine(D2D1::Point2F(cx - s * 0.7f, cy + s * 0.5f), D2D1::Point2F(cx - s * 0.1f, cy - s * 0.1f), brush, 1.4f);
    target->DrawLine(D2D1::Point2F(cx - s * 0.1f, cy - s * 0.1f), D2D1::Point2F(cx + s * 0.3f, cy + s * 0.3f), brush, 1.4f);
    target->DrawLine(D2D1::Point2F(cx + s * 0.3f, cy + s * 0.3f), D2D1::Point2F(cx + s * 0.7f, cy - s * 0.0f), brush, 1.4f);
}

void D2DEngine::DrawIconFileVideo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;
    float s = size * 0.42f;
    D2D1_ROUNDED_RECT frame = D2D1::RoundedRect(D2D1::RectF(cx - s, cy - s * 0.7f, cx + s, cy + s * 0.7f), 3.0f, 3.0f);
    target->DrawRoundedRectangle(frame, brush, 1.4f);
    // Inner play triangle
    DrawIconPlay(cx + 0.5f, cy, size * 0.55f, brush, target);
}

void D2DEngine::DrawIconMusic(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 18.0f;
    float x_off = 0.9675f * s;
    float y_off = 1.108f * s;

    D2D1_MATRIX_3X2_F baseTransform;
    target->GetTransform(&baseTransform);

    // Note heads: angled ovals at -22 deg (standard musical notation)
    D2D1_POINT_2F leftCenter = D2D1::Point2F(cx + x_off - 5.28f * s, cy + y_off + 3.84f * s);
    D2D1_POINT_2F rightCenter = D2D1::Point2F(cx + x_off + 3.36f * s, cy + y_off + 1.80f * s);

    // Draw left note head
    target->SetTransform(D2D1::Matrix3x2F::Rotation(-22.0f, leftCenter) * baseTransform);
    target->FillEllipse(D2D1::Ellipse(leftCenter, 3.36f * s, 2.30f * s), brush);

    // Draw right note head
    target->SetTransform(D2D1::Matrix3x2F::Rotation(-22.0f, rightCenter) * baseTransform);
    target->FillEllipse(D2D1::Ellipse(rightCenter, 3.36f * s, 2.30f * s), brush);

    // Restore base transform for stems and connecting beam
    target->SetTransform(baseTransform);

    // Vertical stems connecting flush to note head tangents
    float stemW = 1.35f * s;
    float stem1X = cx + x_off - 2.83f * s;
    float stem2X = cx + x_off + 5.81f * s;
    float topY1 = cy + y_off - 6.00f * s;
    float topY2 = cy + y_off - 8.52f * s;
    float beamH = 2.20f * s;

    // Draw vertical stems
    target->DrawLine(D2D1::Point2F(stem1X, leftCenter.y), D2D1::Point2F(stem1X, topY1 + 0.5f * s), brush, stemW);
    target->DrawLine(D2D1::Point2F(stem2X, rightCenter.y), D2D1::Point2F(stem2X, topY2 + 0.5f * s), brush, stemW);

    // Parallelogram connecting beam: authentic musical notation with vertical cut ends
    ID2D1PathGeometry* beamPath = nullptr;
    m_d2dFactory->CreatePathGeometry(&beamPath);
    if (beamPath) {
        ID2D1GeometrySink* sink = nullptr;
        beamPath->Open(&sink);
        if (sink) {
            float halfW = stemW * 0.5f;
            sink->BeginFigure(D2D1::Point2F(stem1X - halfW, topY1), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(stem2X + halfW, topY2));
            sink->AddLine(D2D1::Point2F(stem2X + halfW, topY2 + beamH));
            sink->AddLine(D2D1::Point2F(stem1X - halfW, topY1 + beamH));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            target->FillGeometry(beamPath, brush);
        }
        beamPath->Release();
    }
}

void D2DEngine::DrawIconCopy(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    // Back document sheet (top-left bracket with folded corner)
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            sink->BeginFigure(D2D1::Point2F(cx + 2.2f * s, cy - 5.5f * s), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(cx - 4.2f * s, cy - 5.5f * s));
            sink->AddLine(D2D1::Point2F(cx - 5.5f * s, cy - 4.2f * s));
            sink->AddLine(D2D1::Point2F(cx - 5.5f * s, cy + 2.5f * s));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            target->DrawGeometry(path, brush, 1.4f * s, RoundStrokeStyle);
        }
        path->Release();
    }

    // Front document sheet
    D2D1_ROUNDED_RECT front = D2D1::RoundedRect(
        D2D1::RectF(cx - 2.8f * s, cy - 2.8f * s, cx + 5.5f * s, cy + 5.8f * s),
        1.6f * s, 1.6f * s
    );
    target->DrawRoundedRectangle(front, brush, 1.4f * s, RoundStrokeStyle);
}

void D2DEngine::DrawIconWrap(float cx, float cy, float size, bool isWrapped, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    // Top text guide line
    target->DrawLine(D2D1::Point2F(cx - 5.5f * s, cy - 4.5f * s), D2D1::Point2F(cx + 5.5f * s, cy - 4.5f * s), brush, 1.4f * s, RoundStrokeStyle);

    // Return arrow: straight, 180° curve, straight return, arrowhead
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            sink->BeginFigure(D2D1::Point2F(cx - 5.5f * s, cy), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(cx + 2.5f * s, cy));
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(cx + 2.5f * s, cy + 4.5f * s),
                D2D1::SizeF(2.25f * s, 2.25f * s),
                0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                D2D1_ARC_SIZE_SMALL
            ));
            sink->AddLine(D2D1::Point2F(cx - 2.5f * s, cy + 4.5f * s));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            target->DrawGeometry(path, brush, 1.4f * s, RoundStrokeStyle);
        }
        path->Release();
    }

    // Arrowhead pointing left at (cx - 2.5f * s, cy + 4.5f * s)
    float ax = cx - 2.5f * s;
    float ay = cy + 4.5f * s;
    target->DrawLine(D2D1::Point2F(ax + 2.4f * s, ay - 2.2f * s), D2D1::Point2F(ax, ay), brush, 1.4f * s, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(ax + 2.4f * s, ay + 2.2f * s), D2D1::Point2F(ax, ay), brush, 1.4f * s, RoundStrokeStyle);
}

void D2DEngine::DrawIconTable(float cx, float cy, float size, bool isTable, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    float w = 5.8f * s;
    float h = 5.0f * s;

    // Outer grid container
    D2D1_ROUNDED_RECT outer = D2D1::RoundedRect(D2D1::RectF(cx - w, cy - h, cx + w, cy + h), 1.8f * s, 1.8f * s);
    target->DrawRoundedRectangle(outer, brush, 1.4f * s, RoundStrokeStyle);

    // Header divider line
    target->DrawLine(D2D1::Point2F(cx - w, cy - 1.5f * s), D2D1::Point2F(cx + w, cy - 1.5f * s), brush, 1.3f * s, RoundStrokeStyle);

    // Column divider line
    target->DrawLine(D2D1::Point2F(cx - 1.0f * s, cy - h), D2D1::Point2F(cx - 1.0f * s, cy + h), brush, 1.3f * s, RoundStrokeStyle);
}

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
    float padV = 5.0f;
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

void D2DEngine::DrawIconSearch(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float scale = size / 24.0f;
    float r = 4.2f * scale;
    float circleX = cx - 1.5f * scale;
    float circleY = cy - 1.5f * scale;

    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(circleX, circleY), r, r), brush, 1.5f * scale);
    target->DrawLine(
        D2D1::Point2F(circleX + 3.0f * scale, circleY + 3.0f * scale),
        D2D1::Point2F(cx + 6.0f * scale, cy + 6.0f * scale),
        brush, 1.6f * scale, RoundStrokeStyle
    );
}

void D2DEngine::DrawIconCrop(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float scale = size / 24.0f;
    float arm = 5.0f * scale;
    float stem = 2.0f * scale;

    // Top-left crop mark
    target->DrawLine(D2D1::Point2F(cx - arm - stem, cy - arm), D2D1::Point2F(cx + arm, cy - arm), brush, 1.4f * scale, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - arm, cy - arm - stem), D2D1::Point2F(cx - arm, cy + arm), brush, 1.4f * scale, RoundStrokeStyle);

    // Bottom-right crop mark
    target->DrawLine(D2D1::Point2F(cx + arm + stem, cy + arm), D2D1::Point2F(cx - arm, cy + arm), brush, 1.4f * scale, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx + arm, cy + arm + stem), D2D1::Point2F(cx + arm, cy - arm), brush, 1.4f * scale, RoundStrokeStyle);
}

void D2DEngine::DrawIconInfo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float scale = size / 24.0f;
    float r = 6.0f * scale;

    // Outer circle
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush, 1.4f * scale);

    // Dot
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy - 2.8f * scale), 1.0f * scale, 1.0f * scale), brush);

    // Vertical stem
    target->DrawLine(
        D2D1::Point2F(cx, cy - 0.5f * scale),
        D2D1::Point2F(cx, cy + 3.2f * scale),
        brush, 1.4f * scale, RoundStrokeStyle
    );
}

void D2DEngine::DrawIconOutline(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float scale = size / 24.0f;
    float leftDotX = cx - 5.5f * scale;
    float lineStartX = cx - 2.2f * scale;
    float lineEndX = cx + 6.0f * scale;

    float yPositions[3] = { cy - 4.2f * scale, cy, cy + 4.2f * scale };

    for (int i = 0; i < 3; ++i) {
        float y = yPositions[i];
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(leftDotX, y), 1.1f * scale, 1.1f * scale), brush);
        target->DrawLine(
            D2D1::Point2F(lineStartX, y),
            D2D1::Point2F(lineEndX, y),
            brush, 1.4f * scale, RoundStrokeStyle
        );
    }
}

void D2DEngine::DrawIconSave(float cx, float cy, float size, bool isDirty, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    float w = 5.8f * s;
    float h = 6.2f * s;

    // Floppy disk body with chamfered top-right bevel
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            sink->BeginFigure(D2D1::Point2F(cx - w, cy - h), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(cx + w - 2.5f * s, cy - h));
            sink->AddLine(D2D1::Point2F(cx + w, cy - h + 2.5f * s));
            sink->AddLine(D2D1::Point2F(cx + w, cy + h));
            sink->AddLine(D2D1::Point2F(cx - w, cy + h));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            target->DrawGeometry(path, brush, 1.4f * s, RoundStrokeStyle);
        }
        path->Release();
    }

    // Top shutter slider
    D2D1_RECT_F shutter = D2D1::RectF(cx - w * 0.45f, cy - h, cx + w * 0.35f, cy - h * 0.30f);
    target->DrawRectangle(shutter, brush, 1.2f * s);

    // Bottom paper label
    D2D1_RECT_F label = D2D1::RectF(cx - w * 0.65f, cy + h * 0.15f, cx + w * 0.65f, cy + h * 0.85f);
    target->DrawRectangle(label, brush, 1.2f * s);

    // Modified dot indicator if dirty
    if (isDirty) {
        ID2D1Brush* dotBrush = BrushAccent ? (ID2D1Brush*)BrushAccent : brush;
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + w + 1.2f * s, cy - h - 1.2f * s), 2.2f * s, 2.2f * s), dotBrush);
    }
}

void D2DEngine::DrawIconUndo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    float r = 4.8f * s;

    // Smooth circular return arc
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            sink->BeginFigure(D2D1::Point2F(cx + 4.8f * s, cy + 4.5f * s), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(cx - 2.0f * s, cy - 2.8f * s),
                D2D1::SizeF(r, r),
                0.0f,
                D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                D2D1_ARC_SIZE_LARGE
            ));
            sink->AddLine(D2D1::Point2F(cx - 5.5f * s, cy - 2.8f * s));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            target->DrawGeometry(path, brush, 1.4f * s, RoundStrokeStyle);
        }
        path->Release();
    }

    // Left arrowhead
    float ax = cx - 5.5f * s;
    float ay = cy - 2.8f * s;
    target->DrawLine(D2D1::Point2F(ax + 2.8f * s, ay - 2.6f * s), D2D1::Point2F(ax, ay), brush, 1.4f * s, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(ax + 2.8f * s, ay + 2.6f * s), D2D1::Point2F(ax, ay), brush, 1.4f * s, RoundStrokeStyle);
}

void D2DEngine::DrawIconRedo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    float r = 4.8f * s;

    // Smooth circular return arc
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            sink->BeginFigure(D2D1::Point2F(cx - 4.8f * s, cy + 4.5f * s), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(cx + 2.0f * s, cy - 2.8f * s),
                D2D1::SizeF(r, r),
                0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                D2D1_ARC_SIZE_LARGE
            ));
            sink->AddLine(D2D1::Point2F(cx + 5.5f * s, cy - 2.8f * s));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            target->DrawGeometry(path, brush, 1.4f * s, RoundStrokeStyle);
        }
        path->Release();
    }

    // Right arrowhead
    float ax = cx + 5.5f * s;
    float ay = cy - 2.8f * s;
    target->DrawLine(D2D1::Point2F(ax - 2.8f * s, ay - 2.6f * s), D2D1::Point2F(ax, ay), brush, 1.4f * s, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(ax - 2.8f * s, ay + 2.6f * s), D2D1::Point2F(ax, ay), brush, 1.4f * s, RoundStrokeStyle);
}

void D2DEngine::DrawIconCut(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    // Symmetrical scissor handle loops
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 3.2f * s, cy + 4.0f * s), 2.2f * s, 2.2f * s), brush, 1.4f * s);
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 3.2f * s, cy + 4.0f * s), 2.2f * s, 2.2f * s), brush, 1.4f * s);

    // Blades crossing through center (cx, cy)
    target->DrawLine(D2D1::Point2F(cx - 1.8f * s, cy + 2.4f * s), D2D1::Point2F(cx + 4.8f * s, cy - 5.5f * s), brush, 1.4f * s, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx + 1.8f * s, cy + 2.4f * s), D2D1::Point2F(cx - 4.8f * s, cy - 5.5f * s), brush, 1.4f * s, RoundStrokeStyle);

    // Center pivot screw pin
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 0.2f * s), 0.9f * s, 0.9f * s), brush);
}

void D2DEngine::DrawIconPaste(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    // Board back
    D2D1_ROUNDED_RECT board = D2D1::RoundedRect(D2D1::RectF(cx - 5.0f * s, cy - 3.5f * s, cx + 5.0f * s, cy + 6.2f * s), 1.6f * s, 1.6f * s);
    target->DrawRoundedRectangle(board, brush, 1.4f * s, RoundStrokeStyle);

    // Clip at top
    D2D1_ROUNDED_RECT clip = D2D1::RoundedRect(D2D1::RectF(cx - 2.5f * s, cy - 5.8f * s, cx + 2.5f * s, cy - 2.2f * s), 1.2f * s, 1.2f * s);
    target->DrawRoundedRectangle(clip, brush, 1.4f * s, RoundStrokeStyle);

    // Document lines inside clipboard
    target->DrawLine(D2D1::Point2F(cx - 2.8f * s, cy + 0.5f * s), D2D1::Point2F(cx + 2.8f * s, cy + 0.5f * s), brush, 1.2f * s, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - 2.8f * s, cy + 3.2f * s), D2D1::Point2F(cx + 1.5f * s, cy + 3.2f * s), brush, 1.2f * s, RoundStrokeStyle);
}

void D2DEngine::DrawIconSettings(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    float innerR = 3.2f * s;
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), innerR, innerR), brush, 1.4f * s);

    // 6 gear cogs
    for (int i = 0; i < 6; ++i) {
        float angle = (float)i * 60.0f * 3.14159265f / 180.0f;
        float cosA = cosf(angle);
        float sinA = sinf(angle);
        float r1 = 4.2f * s;
        float r2 = 6.4f * s;
        target->DrawLine(
            D2D1::Point2F(cx + r1 * cosA, cy + r1 * sinA),
            D2D1::Point2F(cx + r2 * cosA, cy + r2 * sinA),
            brush, 1.8f * s, RoundStrokeStyle
        );
    }
}

void D2DEngine::DrawIconEye(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float r = size * 0.46f;
    float h = size * 0.28f;

    ID2D1PathGeometry* eye = nullptr;
    m_d2dFactory->CreatePathGeometry(&eye);
    if (eye) {
        ID2D1GeometrySink* sink = nullptr;
        eye->Open(&sink);
        if (sink) {
            // Upper eyelid curve
            sink->BeginFigure(D2D1::Point2F(cx - r, cy), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddBezier(D2D1::BezierSegment(
                D2D1::Point2F(cx - r * 0.45f, cy - h),
                D2D1::Point2F(cx + r * 0.45f, cy - h),
                D2D1::Point2F(cx + r, cy)
            ));
            // Lower eyelid curve
            sink->AddBezier(D2D1::BezierSegment(
                D2D1::Point2F(cx + r * 0.45f, cy + h),
                D2D1::Point2F(cx - r * 0.45f, cy + h),
                D2D1::Point2F(cx - r, cy)
            ));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            target->DrawGeometry(eye, brush, 1.8f * m_dpiScale, RoundStrokeStyle);
        }
        eye->Release();
    }
    // Iris / Pupil circle
    D2D1_ELLIPSE pupil = D2D1::Ellipse(D2D1::Point2F(cx, cy), size * 0.16f, size * 0.16f);
    target->FillEllipse(pupil, brush);
}

void D2DEngine::DrawIconFileDocument(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float w = size * 0.34f;
    float h = size * 0.44f;
    float fold = size * 0.15f;

    ID2D1PathGeometry* doc = nullptr;
    m_d2dFactory->CreatePathGeometry(&doc);
    if (doc) {
        ID2D1GeometrySink* sink = nullptr;
        doc->Open(&sink);
        if (sink) {
            sink->BeginFigure(D2D1::Point2F(cx - w, cy - h), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(cx + w - fold, cy - h));
            sink->AddLine(D2D1::Point2F(cx + w, cy - h + fold));
            sink->AddLine(D2D1::Point2F(cx + w, cy + h));
            sink->AddLine(D2D1::Point2F(cx - w, cy + h));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            target->DrawGeometry(doc, brush, 1.8f * m_dpiScale, RoundStrokeStyle);
        }
        doc->Release();
    }
    // Corner fold lines
    target->DrawLine(D2D1::Point2F(cx + w - fold, cy - h), D2D1::Point2F(cx + w - fold, cy - h + fold), brush, 1.5f * m_dpiScale, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx + w - fold, cy - h + fold), D2D1::Point2F(cx + w, cy - h + fold), brush, 1.5f * m_dpiScale, RoundStrokeStyle);
}

void D2DEngine::DrawIconExternalLink(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    float strokeW = 1.6f * s;

    // 1. Box with open top-right corner
    ID2D1PathGeometry* path = nullptr;
    m_d2dFactory->CreatePathGeometry(&path);
    if (path) {
        ID2D1GeometrySink* sink = nullptr;
        path->Open(&sink);
        if (sink) {
            sink->BeginFigure(D2D1::Point2F(cx + 1.2f * s, cy - 6.2f * s), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(D2D1::Point2F(cx - 6.2f * s, cy - 6.2f * s));
            sink->AddLine(D2D1::Point2F(cx - 6.2f * s, cy + 6.2f * s));
            sink->AddLine(D2D1::Point2F(cx + 6.2f * s, cy + 6.2f * s));
            sink->AddLine(D2D1::Point2F(cx + 6.2f * s, cy - 1.2f * s));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            target->DrawGeometry(path, brush, strokeW, RoundStrokeStyle);
        }
        path->Release();
    }

    // 2. Diagonal Arrow pointing to top-right
    D2D1_POINT_2F arrowTip = D2D1::Point2F(cx + 6.2f * s, cy - 6.2f * s);
    D2D1_POINT_2F arrowBase = D2D1::Point2F(cx - 0.5f * s, cy + 0.5f * s);
    target->DrawLine(arrowBase, arrowTip, brush, strokeW, RoundStrokeStyle);

    // 3. Arrowhead barbs
    target->DrawLine(D2D1::Point2F(arrowTip.x - 4.5f * s, arrowTip.y), arrowTip, brush, strokeW, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(arrowTip.x, arrowTip.y + 4.5f * s), arrowTip, brush, strokeW, RoundStrokeStyle);
}

void D2DEngine::DrawIconHamburger(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    float s = size / 24.0f;
    float halfW = 6.0f * s;
    float strokeW = 1.7f * s;
    float yOffset = 4.6f * s;

    // Three horizontal lines with rounded caps
    target->DrawLine(D2D1::Point2F(cx - halfW, cy - yOffset), D2D1::Point2F(cx + halfW, cy - yOffset), brush, strokeW, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - halfW, cy),           D2D1::Point2F(cx + halfW, cy),           brush, strokeW, RoundStrokeStyle);
    target->DrawLine(D2D1::Point2F(cx - halfW, cy + yOffset), D2D1::Point2F(cx + halfW, cy + yOffset), brush, strokeW, RoundStrokeStyle);
}

// =========================================================================
// ZEN EDITORIAL TECH DRAWING HELPERS
// =========================================================================

void D2DEngine::DrawCornerCrosshairs(const D2D1_RECT_F& rect, float armLength, float strokeWidth, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target) return;
    ID2D1Brush* brush = BrushCrosshair ? BrushCrosshair : BrushBorder;
    if (!brush) return;

    float s = m_dpiScale;
    float len = armLength * s;
    float sw = strokeWidth * s;

    // 4 Corner registration crosshairs (+)
    D2D1_POINT_2F corners[4] = {
        D2D1::Point2F(rect.left, rect.top),       // Top-Left
        D2D1::Point2F(rect.right, rect.top),      // Top-Right
        D2D1::Point2F(rect.left, rect.bottom),    // Bottom-Left
        D2D1::Point2F(rect.right, rect.bottom)    // Bottom-Right
    };

    for (int i = 0; i < 4; ++i) {
        float cx = corners[i].x;
        float cy = corners[i].y;
        target->DrawLine(D2D1::Point2F(cx - len, cy), D2D1::Point2F(cx + len, cy), brush, sw);
        target->DrawLine(D2D1::Point2F(cx, cy - len), D2D1::Point2F(cx, cy + len), brush, sw);
    }
}

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
