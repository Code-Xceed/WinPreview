#pragma once
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000005
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_3.h>
#include <d2d1svg.h>
#include <string>

namespace WinPreview {

class SvgEngine {
public:
    SvgEngine();
    ~SvgEngine();

    bool LoadFromFile(ID2D1RenderTarget* rt, const std::wstring& filePath);
    void Render(ID2D1RenderTarget* rt, const D2D1_RECT_F& targetRect, float zoom, float panX, float panY);
    void Close();

    bool IsLoaded() const { return m_svgDocument != nullptr; }
    D2D1_SIZE_F GetOriginalSize() const { return m_originalSize; }
    std::wstring GetMetadataString() const;

private:
    ID2D1SvgDocument* m_svgDocument = nullptr;
    D2D1_SIZE_F m_originalSize = { 800.0f, 600.0f };
    std::wstring m_filePath;
    UINT64 m_fileSizeBytes = 0;
};

} // namespace WinPreview
