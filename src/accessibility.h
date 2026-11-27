#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <uiautomationcore.h>
#include <string>

namespace WinPreview {

class UiaDocument;

// The preview surface is drawn with Direct2D, so there is no native control
// tree for a screen reader. This exposes the window as one UI Automation
// document: name (file), value (format + page/position), help text and
// notifications when the page changes. UI Automation is the modern path;
// its MSAA bridge also covers older clients.
class Accessibility {
public:
    Accessibility();
    ~Accessibility();

    void Attach(HWND hwnd);
    void Detach();

    // WM_GETOBJECT entry point (UiaRootObjectId / OBJID_CLIENT).
    LRESULT HandleGetObject(WPARAM wParam, LPARAM lParam);

    void SetIdentity(const std::wstring& name, const std::wstring& value,
                     const std::wstring& help);
    void SetValue(const std::wstring& value);
    void SetHelp(const std::wstring& help);

    HWND Window() const { return m_hwnd; }
    long LastObjectResult() const { return m_lastObjectResult; }
    const std::wstring& Name() const { return m_name; }
    const std::wstring& Value() const { return m_value; }
    const std::wstring& Help() const { return m_help; }

private:
    void Announce(const std::wstring& text);

    HWND m_hwnd = nullptr;
    UiaDocument* m_document = nullptr;
    std::wstring m_name;
    std::wstring m_value;
    std::wstring m_help;
    long m_lastObjectResult = 0;
};

} // namespace WinPreview
