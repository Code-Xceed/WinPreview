#pragma once
#ifndef WINPREVIEW_COMIC_ENGINE_H
#define WINPREVIEW_COMIC_ENGINE_H

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

// Ordered image pages from a comic archive (.cbz / .cbr). Pages are extracted
// one at a time into %TEMP% and decoded with WIC, so a 200-page comic costs one
// bitmap in memory instead of 200.
class ComicEngine {
public:
    ComicEngine();
    ~ComicEngine();

    bool LoadArchive(const std::wstring& filePath);
    void Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& rect, float dpiScale);
    bool UpdateAnimation(float dt);
    void Close();

    bool IsLoaded() const { return m_isLoaded; }
    int PageCount() const { return (int)m_pages.size(); }
    int CurrentPage() const { return m_currentPage; }
    bool GoToPage(int index);
    bool NextPage() { return GoToPage(m_currentPage + 1); }
    bool PreviousPage() { return GoToPage(m_currentPage - 1); }
    const std::wstring& GetTitle() const { return m_title; }
    const std::wstring& GetErrorText() const { return m_errorText; }
    std::wstring GetFormatBadge() const;
    std::wstring CurrentEntryName() const;

private:
    bool ExtractPage(int index);
    void ReleasePage();

    bool m_isLoaded = false;
    std::wstring m_archivePath;
    std::wstring m_title;
    std::wstring m_errorText;
    std::vector<std::wstring> m_pages;   // entry names in reading order
    int m_currentPage = 0;

    ID2D1Bitmap* m_bitmap = nullptr;
    UINT m_pageWidth = 0;
    UINT m_pageHeight = 0;
    std::wstring m_tempDir;
    std::wstring m_pageFile;
};

} // namespace WinPreview

#endif // WINPREVIEW_COMIC_ENGINE_H
