#include "rtf_engine.h"
#include "d2d_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <richedit.h>

namespace WinPreview {

namespace {

// The control lives in Msftedit.dll (RichEdit 4.1). Loading the DLL registers
// the RICHEDIT50W window class on first use.
bool EnsureRichEditLoaded() {
    static bool attempted = false;
    static bool available = false;
    if (!attempted) {
        attempted = true;
        HMODULE module = LoadLibraryW(L"Msftedit.dll");
        available = module != nullptr;
    }
    return available;
}

struct StreamInData {
    const char* data = nullptr;
    size_t size = 0;
    size_t offset = 0;
};

DWORD CALLBACK StreamInCallback(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG* written) {
    StreamInData* stream = reinterpret_cast<StreamInData*>(cookie);
    size_t remaining = stream->size > stream->offset ? stream->size - stream->offset : 0;
    size_t take = (std::min)((size_t)count, remaining);
    if (take > 0) memcpy(buffer, stream->data + stream->offset, take);
    stream->offset += take;
    *written = (LONG)take;
    return 0;
}

bool LooksLikeRtf(const std::vector<char>& data) {
    size_t start = 0;
    // Skip a UTF-8 BOM if an editor wrote one.
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        start = 3;
    }
    return data.size() >= start + 5 && memcmp(data.data() + start, "{\\rtf", 5) == 0;
}

} // namespace

RtfEngine::RtfEngine() {}

RtfEngine::~RtfEngine() {
    Close();
}

void RtfEngine::ReleaseSurface() {
    if (m_memDc && m_oldBitmap) {
        SelectObject(m_memDc, m_oldBitmap);
        m_oldBitmap = nullptr;
    }
    if (m_dib) {
        DeleteObject(m_dib);
        m_dib = nullptr;
    }
    if (m_memDc) {
        DeleteDC(m_memDc);
        m_memDc = nullptr;
    }
    m_bits = nullptr;
    m_pageWidthPx = 0;
    m_pageHeightPx = 0;
    m_pageWidthTwips = 0;
    m_pageHeightTwips = 0;
}

void RtfEngine::Close() {
    SafeRelease(m_pageBitmap);
    ReleaseSurface();
    if (m_rich) {
        DestroyWindow(m_rich);
        m_rich = nullptr;
    }
    m_parent = nullptr;
    m_pageStarts.clear();
    m_page = 0;
    m_renderedPage = -1;
    m_loaded = false;
    m_plainText.clear();
    m_filePath.clear();
    m_fileSize = 0;
}

bool RtfEngine::Load(HWND parent, const std::wstring& filePath) {
    Close();
    if (!EnsureRichEditLoaded() || !parent) return false;

    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > (64ll << 20)) {
        CloseHandle(file);
        return false;
    }

    std::vector<char> data((size_t)size.QuadPart);
    DWORD totalRead = 0;
    bool ok = ReadFile(file, data.data(), (DWORD)data.size(), &totalRead, nullptr) != FALSE &&
              totalRead == data.size();
    CloseHandle(file);
    if (!ok) return false;

    // Anything that is not RTF is left to the plain-text viewer.
    if (!LooksLikeRtf(data)) return false;

    m_rich = CreateWindowExW(0, L"RICHEDIT50W", L"",
                             WS_CHILD | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL,
                             0, 0, 160, 160, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_rich) return false;
    m_parent = parent;

    SendMessageW(m_rich, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(255, 255, 255));
    SendMessageW(m_rich, EM_SETEVENTMASK, 0, 0);
    SendMessageW(m_rich, EM_SETUNDOLIMIT, 0, 0);

    // RTF without an explicit colour assumes the window text colour; the page
    // is white, so the default must be black regardless of the app theme.
    CHARFORMAT2W format = {};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = RGB(0, 0, 0);
    SendMessageW(m_rich, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&format);

    StreamInData stream{data.data(), data.size(), 0};
    EDITSTREAM editStream = {};
    editStream.dwCookie = (DWORD_PTR)&stream;
    editStream.pfnCallback = StreamInCallback;
    SendMessageW(m_rich, EM_STREAMIN, SF_RTF, (LPARAM)&editStream);

    if (editStream.dwError != 0 && GetWindowTextLengthW(m_rich) == 0) {
        Close();
        return false;
    }

    m_filePath = filePath;
    m_fileSize = (unsigned long long)data.size();
    ExtractText();
    m_pageStarts.push_back(0);
    m_loaded = true;
    return true;
}

void RtfEngine::ExtractText() {
    m_plainText.clear();
    if (!m_rich) return;
    int length = GetWindowTextLengthW(m_rich);
    if (length <= 0) return;
    // Guard against pathological documents in the clipboard path.
    if (length > 4 * 1024 * 1024) length = 4 * 1024 * 1024;

    std::wstring buffer((size_t)length + 1, L'\0');
    int copied = GetWindowTextW(m_rich, buffer.data(), length + 1);
    if (copied <= 0) return;
    buffer.resize((size_t)copied);

    m_plainText.reserve(buffer.size() + 32);
    for (size_t i = 0; i < buffer.size(); ++i) {
        wchar_t c = buffer[i];
        if (c == L'\r') {
            m_plainText += L"\r\n";
            if (i + 1 < buffer.size() && buffer[i + 1] == L'\n') ++i;
        } else {
            m_plainText += c;
        }
    }
}

LONG RtfEngine::FormatRange(HDC dc, const RECT& pageTwips, const RECT& textTwips,
                            LONG cpMin, LONG cpMax) {
    FORMATRANGE range = {};
    range.hdcTarget = dc;
    range.hdc = dc;
    range.rcPage = pageTwips;
    range.rc = textTwips;
    range.chrg.cpMin = cpMin;
    range.chrg.cpMax = cpMax;
    LONG result = (LONG)SendMessageW(m_rich, EM_FORMATRANGE, TRUE, (LPARAM)&range);
    SendMessageW(m_rich, EM_FORMATRANGE, FALSE, 0);
    return result;
}

bool RtfEngine::EnsureLayout(int widthPx, int heightPx, ID2D1RenderTarget* rt) {
    if (!m_rich || widthPx < 40 || heightPx < 40) return false;

    HDC screen = GetDC(m_parent);
    if (!screen) return false;
    int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(m_parent, screen);
    if (dpi <= 0) dpi = 96;

    if (m_memDc && widthPx == m_pageWidthPx && heightPx == m_pageHeightPx && dpi == m_dpi) {
        return true;
    }

    SafeRelease(m_pageBitmap);
    m_renderedPage = -1;
    ReleaseSurface();

    HDC screenDc = GetDC(m_parent);
    m_memDc = CreateCompatibleDC(screenDc);
    ReleaseDC(m_parent, screenDc);
    if (!m_memDc) return false;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = widthPx;
    info.bmiHeader.biHeight = -heightPx;  // top-down DIB
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    m_dib = CreateDIBSection(m_memDc, &info, DIB_RGB_COLORS, &m_bits, nullptr, 0);
    if (!m_dib || !m_bits) {
        ReleaseSurface();
        return false;
    }
    m_oldBitmap = SelectObject(m_memDc, m_dib);
    m_pageWidthPx = widthPx;
    m_pageHeightPx = heightPx;
    m_dpi = dpi;
    m_pageWidthTwips = MulDiv(widthPx, 1440, dpi);
    m_pageHeightTwips = MulDiv(heightPx, 1440, dpi);

    (void)rt;
    BuildPages();
    return true;
}

void RtfEngine::BuildPages() {
    m_pageStarts.clear();
    m_pageStarts.push_back(0);
    m_renderedPage = -1;
    if (!m_rich || !m_memDc) return;

    RECT pageTwips = { 0, 0, m_pageWidthTwips, m_pageHeightTwips };
    // ~0.6 in margins, matching a document page rather than a code editor.
    RECT textTwips = { MulDiv(m_dpi, 9, 16), MulDiv(m_dpi, 7, 16),
                       m_pageWidthTwips - MulDiv(m_dpi, 9, 16),
                       m_pageHeightTwips - MulDiv(m_dpi, 7, 16) };

    LONG total = (LONG)GetWindowTextLengthW(m_rich);
    LONG cp = 0;
    while (cp < total && m_pageStarts.size() < 400) {
        LONG last = FormatRange(m_memDc, pageTwips, textTwips, cp, -1);
        if (last <= cp) break;
        cp = last;
        if (cp < total) m_pageStarts.push_back(cp);
    }

    if (m_page >= (int)m_pageStarts.size()) m_page = (int)m_pageStarts.size() - 1;
    if (m_page < 0) m_page = 0;
}

void RtfEngine::RenderPage(ID2D1RenderTarget* rt) {
    if (!rt || !m_rich || !m_memDc || !m_bits) return;

    RECT full = { 0, 0, m_pageWidthPx, m_pageHeightPx };
    HBRUSH paper = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(m_memDc, &full, paper);
    DeleteObject(paper);

    SetBkMode(m_memDc, TRANSPARENT);
    SetTextColor(m_memDc, RGB(0, 0, 0));

    RECT pageTwips = { 0, 0, m_pageWidthTwips, m_pageHeightTwips };
    RECT textTwips = { MulDiv(m_dpi, 9, 16), MulDiv(m_dpi, 7, 16),
                       m_pageWidthTwips - MulDiv(m_dpi, 9, 16),
                       m_pageHeightTwips - MulDiv(m_dpi, 7, 16) };

    if (m_page < 0) m_page = 0;
    if (m_page >= (int)m_pageStarts.size()) m_page = (int)m_pageStarts.size() - 1;
    LONG start = m_pageStarts.empty() ? 0 : m_pageStarts[(size_t)m_page];
    LONG end = (m_page + 1 < (int)m_pageStarts.size()) ? m_pageStarts[(size_t)m_page + 1] : -1;
    FormatRange(m_memDc, pageTwips, textTwips, start, end);

    SafeRelease(m_pageBitmap);
    D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    rt->CreateBitmap(D2D1::SizeU((UINT32)m_pageWidthPx, (UINT32)m_pageHeightPx),
                     m_bits, (UINT32)m_pageWidthPx * 4, properties, &m_pageBitmap);
    m_renderedPage = m_page;
}

void RtfEngine::Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect,
                       float dpiScale) {
    if (!rt || !m_loaded) return;

    float availW = targetRect.right - targetRect.left;
    float availH = targetRect.bottom - targetRect.top;
    if (availW < 120.0f || availH < 120.0f) return;

    // Letter proportions, fitted between the floating dock and the bottom edge.
    const float kAspect = 8.5f / 11.0f;
    const float contentTop = targetRect.top + 92.0f;
    const float contentBottom = targetRect.bottom - 30.0f;
    float areaH = (std::max)(180.0f, contentBottom - contentTop);
    float pageW = (std::min)(availW - 72.0f, areaH * kAspect);
    pageW = (std::max)(220.0f, pageW);
    float pageH = pageW / kAspect;
    if (pageH > areaH) {
        pageH = areaH;
        pageW = pageH * kAspect;
    }

    int widthPx = (int)(pageW * dpiScale + 0.5f);
    int heightPx = (int)(pageH * dpiScale + 0.5f);
    if (!EnsureLayout(widthPx, heightPx, rt)) return;
    if (m_renderedPage != m_page || !m_pageBitmap) RenderPage(rt);
    if (!m_pageBitmap) return;

    float x = targetRect.left + (availW - pageW) / 2.0f;
    float y = contentTop + (areaH - pageH) / 2.0f;

    // Paper sheet: soft offset shadow, white face, hairline edge.
    D2D1_ROUNDED_RECT shadow = D2D1::RoundedRect(
        D2D1::RectF(x + 1.0f, y + 3.0f, x + pageW + 1.0f, y + pageH + 3.0f), 3.0f, 3.0f);
    if (d2d.BrushShadowSoft) rt->FillRoundedRectangle(shadow, d2d.BrushShadowSoft);
    D2D1_ROUNDED_RECT paper = D2D1::RoundedRect(D2D1::RectF(x, y, x + pageW, y + pageH), 3.0f, 3.0f);
    // The bitmap itself is filled white, so it is the paper surface.
    rt->DrawBitmap(m_pageBitmap, D2D1::RectF(x, y, x + pageW, y + pageH));
    rt->DrawRoundedRectangle(paper, d2d.BrushBorderSubtle, 1.0f);

    // Page chip, only when it fits under the sheet.
    if (GetPageCount() > 1) {
        float chipY = y + pageH + 10.0f;
        if (chipY < targetRect.bottom - 18.0f) {
            WCHAR label[96];
            swprintf_s(label, L"Page %d / %d  •  Page Up / Page Down", m_page + 1,
                       (int)m_pageStarts.size());
            IDWriteTextLayout* layout = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(label, (UINT32)wcslen(label),
                d2d.FontCaption, pageW, 18.0f, &layout);
            if (layout) {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(x, chipY), layout, d2d.BrushTextMuted);
                layout->Release();
            }
        }
    }
}

void RtfEngine::NextPage() {
    if (m_page + 1 < (int)m_pageStarts.size()) {
        ++m_page;
        m_renderedPage = -1;
    }
}

void RtfEngine::PrevPage() {
    if (m_page > 0) {
        --m_page;
        m_renderedPage = -1;
    }
}

std::wstring RtfEngine::GetFormatBadge() const {
    WCHAR buffer[128];
    int pages = (int)m_pageStarts.size();
    if (pages <= 0) pages = 1;
    swprintf_s(buffer, L"RTF • FORMATTED • PAGE %d / %d", m_page + 1, pages);
    return buffer;
}

} // namespace WinPreview
