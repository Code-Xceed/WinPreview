#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <string>

namespace WinPreview {

// Browser-grade HTML rendering through the installed WebView2 Evergreen
// runtime. Page script is disabled: a previewer shows the document, it does not
// run code from a downloaded file. When the runtime or loader is unavailable,
// Create fails and the caller uses the dependency-free reader view instead.
class HtmlView {
public:
    HtmlView();
    ~HtmlView();

    bool Create(HWND owner, const std::wstring& filePath, float dpiScale);
    void SetBounds(const D2D1_RECT_F& contentRectDip, float dpiScale);
    void Close();

    bool IsActive() const { return m_active; }
    // The rendered page's text (host-initiated, not page script) for Copy all.
    bool GetPageText(std::wstring& text);
    // Writes the rendered page to a PNG (WebView2 CapturePreview). Used by the
    // --html-capture test flag; it captures the page only, never the desktop.
    bool CapturePreview(const std::wstring& path);
    HWND HostWindow() const { return m_host; }
    const std::wstring& RuntimeVersion() const { return m_runtimeVersion; }

private:
    struct Impl;

    bool m_active = false;
    HWND m_owner = nullptr;
    HWND m_host = nullptr;
    float m_dpiScale = 1.0f;
    std::wstring m_runtimeVersion;
    Impl* m_impl = nullptr;
};

} // namespace WinPreview
