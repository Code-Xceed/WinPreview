#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <string>
#include <vector>

namespace WinPreview {

class D2DEngine;

struct FontInfo {
    std::wstring familyName;
    std::wstring styleName;
    std::wstring version;
    std::wstring copyright;
    std::wstring formatType;
    UINT32 glyphCount = 0;
    UINT16 designUnitsPerEm = 0;
    INT16 ascent = 0;
    INT16 descent = 0;
    INT16 lineGap = 0;
    INT16 capHeight = 0;
    INT16 xHeight = 0;
    UINT64 fileSizeBytes = 0;
};

class FontEngine {
public:
    FontEngine();
    ~FontEngine();

    bool LoadFromFile(IDWriteFactory* dwrite, const std::wstring& filePath);
    // TrueType collections (.ttc) hold several faces in one file.
    bool SetFace(UINT32 index);
    bool StepFace(int delta);
    UINT32 GetFaceCount() const { return m_faceCount; }
    UINT32 GetFaceIndex() const { return m_faceIndex; }
    void Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale);
    void Scroll(float dy);
    bool UpdateAnimation(float dt);
    void Close();

    bool IsLoaded() const { return m_isLoaded; }
    const FontInfo& GetInfo() const { return m_info; }
    std::wstring GetFormatBadge() const;

private:
    void ParseNameTable(const void* tableData, UINT32 tableSize);
    bool LoadFace(UINT32 index);

    bool m_isLoaded = false;
    std::wstring m_filePath;
    FontInfo m_info;
    UINT32 m_faceCount = 1;
    UINT32 m_faceIndex = 0;
    UINT64 m_fileSizeBytes = 0;
    DWRITE_FONT_FILE_TYPE m_fileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE m_faceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    float m_scrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    float m_contentHeight = 0.0f;

    IDWriteFactory* m_dwrite = nullptr;
    IDWriteFontFile* m_fontFile = nullptr;
    IDWriteFontFace* m_fontFace = nullptr;

    // Specimen text formats
    IDWriteTextFormat* m_fmtHero = nullptr;
    IDWriteTextFormat* m_fmtLarge = nullptr;
    IDWriteTextFormat* m_fmtMedium = nullptr;
    IDWriteTextFormat* m_fmtSmall = nullptr;
    IDWriteTextFormat* m_fmtMono = nullptr;
};

} // namespace WinPreview
