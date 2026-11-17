#include "comic_engine.h"
#include "archive_engine.h"
#include "d2d_engine.h"
#include "wic_decoder.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace WinPreview {
namespace {

bool IsImageEntry(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    static const wchar_t* kExts[] = { L".jpg", L".jpeg", L".jpe", L".png", L".webp", L".gif",
                                      L".bmp", L".avif", L".heic", L".heif", L".tif", L".tiff" };
    for (const wchar_t* e : kExts) {
        if (ext == e) return true;
    }
    return false;
}

// "page2.jpg" sorts before "page10.jpg".
bool NaturalLess(const std::wstring& a, const std::wstring& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (iswdigit(a[i]) && iswdigit(b[j])) {
            unsigned long long na = 0, nb = 0;
            while (i < a.size() && iswdigit(a[i])) na = na * 10 + (a[i++] - L'0');
            while (j < b.size() && iswdigit(b[j])) nb = nb * 10 + (b[j++] - L'0');
            if (na != nb) return na < nb;
        } else {
            wchar_t ca = (wchar_t)towlower(a[i]);
            wchar_t cb = (wchar_t)towlower(b[j]);
            if (ca != cb) return ca < cb;
            ++i;
            ++j;
        }
    }
    return a.size() < b.size();
}

} // namespace

ComicEngine::ComicEngine() {}

ComicEngine::~ComicEngine() {
    Close();
}

void ComicEngine::ReleasePage() {
    if (m_bitmap) {
        m_bitmap->Release();
        m_bitmap = nullptr;
    }
    m_pageWidth = 0;
    m_pageHeight = 0;
}

void ComicEngine::Close() {
    ReleasePage();
    m_isLoaded = false;
    m_archivePath.clear();
    m_title.clear();
    m_errorText.clear();
    m_pages.clear();
    m_currentPage = 0;
    if (!m_pageFile.empty()) {
        DeleteFileW(m_pageFile.c_str());
        m_pageFile.clear();
    }
    if (!m_tempDir.empty()) {
        RemoveDirectoryW(m_tempDir.c_str());
        m_tempDir.clear();
    }
}

bool ComicEngine::LoadArchive(const std::wstring& filePath) {
    Close();
    m_archivePath = filePath;

    size_t slash = filePath.find_last_of(L"\\/");
    m_title = (slash == std::wstring::npos) ? filePath : filePath.substr(slash + 1);

    ArchiveEngine archive;
    std::vector<ArchiveEntryInfo> entries;
    std::wstring displayName, error;
    ArchiveResult result = archive.Load(filePath, entries, displayName, error);
    if (result != ArchiveResult::Ok) {
        m_errorText = error.empty() ? L"This comic could not be read." : error;
        return false;
    }

    for (const auto& entry : entries) {
        if (entry.isDirectory) continue;
        if (!IsImageEntry(entry.path)) continue;
        m_pages.push_back(entry.path);
    }
    if (m_pages.empty()) {
        m_errorText = L"No image pages in this comic.";
        return false;
    }
    std::sort(m_pages.begin(), m_pages.end(), NaturalLess);

    // Page 1 is extracted up front; decoding happens in Render where the WIC
    // factory and render target are available.
    m_isLoaded = true;
    if (!ExtractPage(0)) {
        m_errorText = L"The first page could not be extracted.";
        return false;
    }
    return true;
}

bool ComicEngine::ExtractPage(int index) {
    if (index < 0 || index >= (int)m_pages.size()) return false;

    if (m_tempDir.empty()) {
        WCHAR temp[MAX_PATH] = {};
        if (!GetTempPathW(MAX_PATH, temp)) return false;
        std::wstring root = std::wstring(temp) + L"WinPreview-comics";
        CreateDirectoryW(root.c_str(), NULL);
        m_tempDir = root + L"\\" + std::to_wstring(GetTickCount64());
        CreateDirectoryW(m_tempDir.c_str(), NULL);
    }
    if (!m_pageFile.empty()) {
        DeleteFileW(m_pageFile.c_str());
        m_pageFile.clear();
    }
    ReleasePage();
    m_currentPage = index;

    const std::wstring& entry = m_pages[(size_t)index];
    std::string bytes;
    std::wstring error;
    ArchiveEngine archive;
    if (!archive.ReadEntry(m_archivePath, entry, 96u * 1024 * 1024, bytes, error)) {
        m_errorText = L"Page " + std::to_wstring(index + 1) + L" could not be read.";
        return false;
    }

    size_t dot = entry.find_last_of(L'.');
    std::wstring ext = (dot == std::wstring::npos) ? L".img" : entry.substr(dot);
    m_pageFile = m_tempDir + L"\\page" + ext;

    HANDLE hFile = CreateFileW(m_pageFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        m_pageFile.clear();
        return false;
    }
    DWORD written = 0;
    WriteFile(hFile, bytes.data(), (DWORD)bytes.size(), &written, NULL);
    CloseHandle(hFile);
    if (written != bytes.size()) {
        DeleteFileW(m_pageFile.c_str());
        m_pageFile.clear();
        return false;
    }
    m_errorText.clear();
    return true;
}

std::wstring ComicEngine::CurrentEntryName() const {
    if (m_currentPage < 0 || m_currentPage >= (int)m_pages.size()) return L"";
    return m_pages[(size_t)m_currentPage];
}

bool ComicEngine::GoToPage(int index) {
    if (index < 0 || index >= (int)m_pages.size()) return false;
    if (index == m_currentPage && (m_bitmap || !m_pageFile.empty())) return true;
    return ExtractPage(index);
}

bool ComicEngine::UpdateAnimation(float dt) {
    (void)dt;
    return false;
}

std::wstring ComicEngine::GetFormatBadge() const {
    WCHAR buffer[128];
    swprintf_s(buffer, L"COMIC • %d PAGES", (int)m_pages.size());
    return buffer;
}

void ComicEngine::Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& rect, float dpiScale) {
    (void)dpiScale;
    if (!rt || !m_isLoaded) return;

    float width = rect.right - rect.left;
    float height = rect.bottom - rect.top;

    // Decode lazily once a render target exists.
    if (!m_bitmap && !m_pageFile.empty()) {
        WicDecoder::LoadImageFromFile(d2d.GetWICFactory(), d2d.GetRenderTarget(),
                                      m_pageFile, &m_bitmap, &m_pageWidth, &m_pageHeight);
    }
    if (!m_bitmap || m_pageWidth == 0 || m_pageHeight == 0) {
        const wchar_t* message = m_errorText.empty() ? L"No page to show." : m_errorText.c_str();
        rt->DrawText(message, (UINT32)wcslen(message), d2d.FontBody,
                     D2D1::RectF(rect.left + 40.0f, rect.top + 80.0f, rect.right - 40.0f, rect.bottom),
                     d2d.BrushTextMuted);
        return;
    }

    // Fit the page to the window. Small pages are allowed to grow (up to 4x) so
    // a low-resolution scan still fills the reading area. The bottom 44px are
    // reserved for the page counter so a tall page never covers it.
    const float topPad = 56.0f;
    const float bottomPad = 44.0f;
    float usableH = (std::max)(60.0f, height - topPad - bottomPad);
    float scale = (std::min)((width - 48.0f) / (float)m_pageWidth, usableH / (float)m_pageHeight);
    if (scale > 4.0f) scale = 4.0f;
    if (scale <= 0.0f) scale = 0.01f;

    float drawW = (float)m_pageWidth * scale;
    float drawH = (float)m_pageHeight * scale;
    float left = rect.left + (width - drawW) / 2.0f;
    float top = rect.top + topPad + (usableH - drawH) / 2.0f;

    // Soft shadow behind the page.
    D2D1_RECT_F shadow = D2D1::RectF(left + 4.0f, top + 8.0f, left + drawW + 4.0f, top + drawH + 8.0f);
    rt->FillRectangle(shadow, d2d.BrushShadowSoft ? d2d.BrushShadowSoft : d2d.BrushCard);

    rt->DrawBitmap(m_bitmap, D2D1::RectF(left, top, left + drawW, top + drawH), 1.0f,
                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

    WCHAR counter[64];
    swprintf_s(counter, L"PAGE %d / %d", m_currentPage + 1, (int)m_pages.size());
    IDWriteTextLayout* layout = nullptr;
    d2d.GetDWriteFactory()->CreateTextLayout(counter, (UINT32)wcslen(counter),
        d2d.FontBadge ? d2d.FontBadge : d2d.FontCaption, 240.0f, 20.0f, &layout);
    if (layout) {
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextLayout(D2D1::Point2F(rect.left + width / 2.0f - 120.0f, rect.bottom - 34.0f),
                           layout, d2d.BrushTextMuted);
        layout->Release();
    }
}

} // namespace WinPreview
