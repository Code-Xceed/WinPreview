#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <string>
#include <vector>

namespace WinPreview {

class D2DEngine;

// Renders .rtf through the system RichEdit control (the same formatted-text
// engine WordPad uses), lays the document out in pages, and paints the current
// page as a paper sheet. Raw markup is never shown.
class RtfEngine {
public:
    RtfEngine();
    ~RtfEngine();

    bool Load(HWND parent, const std::wstring& filePath);
    void Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale);

    void Close();

    bool IsLoaded() const { return m_loaded; }
    int GetPageCount() const { return (int)m_pageStarts.size(); }
    int GetPage() const { return m_page; }
    void NextPage();
    void PrevPage();

    std::wstring GetFormatBadge() const;
    std::wstring GetPlainText() const { return m_plainText; }
    unsigned long long GetFileSize() const { return m_fileSize; }

private:
    bool EnsureLayout(int widthPx, int heightPx, ID2D1RenderTarget* rt);
    void BuildPages();
    void RenderPage(ID2D1RenderTarget* rt);
    LONG FormatRange(HDC dc, const RECT& pageTwips, const RECT& textTwips, LONG cpMin, LONG cpMax);
    void ReleaseSurface();
    void ExtractText();

    bool m_loaded = false;
    HWND m_parent = nullptr;
    HWND m_rich = nullptr;

    HDC m_memDc = nullptr;
    HBITMAP m_dib = nullptr;
    HGDIOBJ m_oldBitmap = nullptr;
    void* m_bits = nullptr;

    int m_pageWidthPx = 0;
    int m_pageHeightPx = 0;
    int m_pageWidthTwips = 0;
    int m_pageHeightTwips = 0;
    int m_dpi = 96;

    std::vector<LONG> m_pageStarts;
    int m_page = 0;
    int m_renderedPage = -1;

    ID2D1Bitmap* m_pageBitmap = nullptr;

    std::wstring m_filePath;
    std::wstring m_plainText;
    unsigned long long m_fileSize = 0;
};

} // namespace WinPreview
