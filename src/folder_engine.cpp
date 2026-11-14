#include "folder_engine.h"
#include "d2d_engine.h"
#include <algorithm>
#include <cmath>

namespace WinPreview {

#pragma pack(push, 1)
struct ZipEocd {
    uint32_t signature;           // 0x06054b50
    uint16_t diskNumber;
    uint16_t startDisk;
    uint16_t totalEntriesThisDisk;
    uint16_t totalEntries;
    uint32_t centralDirSize;
    uint32_t centralDirOffset;
    uint16_t commentLength;
};

struct ZipCdHeader {
    uint32_t signature;           // 0x02014b50
    uint16_t versionMadeBy;
    uint16_t versionNeeded;
    uint16_t bitFlag;
    uint16_t compressionMethod;
    uint16_t lastModTime;
    uint16_t lastModDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t fileNameLength;
    uint16_t extraFieldLength;
    uint16_t fileCommentLength;
    uint16_t diskNumberStart;
    uint16_t internalFileAttributes;
    uint32_t externalFileAttributes;
    uint32_t relativeOffsetOfLocalHeader;
};
#pragma pack(pop)

static std::wstring FormatSize(UINT64 bytes) {
    WCHAR buf[64];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        swprintf_s(buf, L"%0.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        swprintf_s(buf, L"%0.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        swprintf_s(buf, L"%0.1f KB", (double)bytes / 1024.0);
    } else {
        swprintf_s(buf, L"%llu B", bytes);
    }
    return buf;
}

static std::wstring FormatFileTime(const FILETIME& ft) {
    FILETIME localFt = {};
    FileTimeToLocalFileTime(&ft, &localFt);
    SYSTEMTIME st = {};
    FileTimeToSystemTime(&localFt, &st);
    WCHAR buf[64];
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

static std::wstring LowerExtensionOf(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext;
}

static std::wstring ToLowerCopy(const std::wstring& text) {
    std::wstring out = text;
    std::transform(out.begin(), out.end(), out.begin(), ::towlower);
    return out;
}

static std::wstring ParentPath(const std::wstring& path) {
    size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

static std::wstring LeafName(const std::wstring& path) {
    size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& leaf) {
    return dir.empty() ? leaf : dir + L"\\" + leaf;
}

FolderEngine::FolderEngine() {}

FolderEngine::~FolderEngine() {
    Close();
}

void FolderEngine::Close() {
    m_isLoaded = false;
    m_isArchive = false;
    m_archiveTypeName.clear();
    m_folderPath.clear();
    m_folderName.clear();
    m_fileCount = 0;
    m_dirCount = 0;
    m_totalSizeBytes = 0;
    m_totalCompressedBytes = 0;
    m_items.clear();
    m_allEntries.clear();
    m_directoryPaths.clear();
    m_currentDir.clear();
    m_search.clear();
    m_selected = -1;
    m_hover = -1;
    m_hasError = false;
    m_passwordProtected = false;
    m_encryptedEntries = false;
    m_errorText.clear();
    m_rowRects.clear();
    m_crumbRects.clear();
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
    m_contentHeight = 0.0f;
    m_archive.Cleanup();
}

bool FolderEngine::LoadFromDirectory(const std::wstring& folderPath) {
    Close();

    DWORD attrs = GetFileAttributesW(folderPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    m_folderPath = folderPath;
    size_t slash = folderPath.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash + 1 < folderPath.length()) {
        m_folderName = folderPath.substr(slash + 1);
    } else {
        m_folderName = folderPath;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(folderPath.c_str(), GetFileExInfoStandard, &fad)) {
        m_lastWriteTime = fad.ftLastWriteTime;
    }

    std::wstring searchPattern = folderPath;
    if (searchPattern.back() != L'\\' && searchPattern.back() != L'/') {
        searchPattern += L'\\';
    }
    searchPattern += L'*';

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }

            FolderItemEntry entry = {};
            entry.name = fd.cFileName;
            entry.isDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            entry.lastWriteTime = fd.ftLastWriteTime;
            entry.formattedDate = FormatFileTime(fd.ftLastWriteTime);

            if (entry.isDirectory) {
                m_dirCount++;
                entry.formattedSize = L"—";
                entry.extension = L"Folder";
            } else {
                m_fileCount++;
                entry.sizeBytes = ((UINT64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                m_totalSizeBytes += entry.sizeBytes;
                entry.formattedSize = FormatSize(entry.sizeBytes);

                size_t dot = entry.name.find_last_of(L'.');
                if (dot != std::wstring::npos) {
                    entry.extension = entry.name.substr(dot);
                    std::transform(entry.extension.begin(), entry.extension.end(), entry.extension.begin(), ::towupper);
                } else {
                    entry.extension = L"FILE";
                }
            }

            m_items.push_back(entry);
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    // Sort: directories first (alphabetical), then files (alphabetical)
    std::sort(m_items.begin(), m_items.end(), [](const FolderItemEntry& a, const FolderItemEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    m_isLoaded = true;
    return true;
}

bool FolderEngine::LoadFromArchive(const std::wstring& archivePath) {
    Close();

    m_isArchive = true;
    m_folderPath = archivePath;

    std::wstring ext = ToLowerCopy(LowerExtensionOf(archivePath));

    if (ext == L".apk") m_archiveTypeName = L"APK PACKAGE";
    else if (ext == L".jar" || ext == L".war" || ext == L".aar" || ext == L".ear") m_archiveTypeName = L"JAVA ARCHIVE";
    else if (ext == L".nupkg") m_archiveTypeName = L"NUGET PACKAGE";
    else if (ext == L".msix" || ext == L".appx") m_archiveTypeName = L"APP PACKAGE";
    else if (ext == L".whl") m_archiveTypeName = L"PYTHON WHEEL";
    else if (ext == L".vsix") m_archiveTypeName = L"VS EXTENSION";
    else if (ext == L".cbz" || ext == L".cbr") m_archiveTypeName = L"COMIC ARCHIVE";
    else if (ext == L".epub") m_archiveTypeName = L"EPUB EBOOK";
    else if (ext == L".xpi" || ext == L".crx") m_archiveTypeName = L"BROWSER EXTENSION";
    else if (ext == L".7z") m_archiveTypeName = L"7-ZIP ARCHIVE";
    else if (ext == L".rar") m_archiveTypeName = L"RAR ARCHIVE";
    else if (ext == L".tar") m_archiveTypeName = L"TAR ARCHIVE";
    else if (ext == L".gz" || ext == L".gzip" || ext == L".tgz") m_archiveTypeName = L"GZIP ARCHIVE";
    else if (ext == L".bz2" || ext == L".bzip2" || ext == L".tbz2") m_archiveTypeName = L"BZIP2 ARCHIVE";
    else if (ext == L".xz" || ext == L".txz") m_archiveTypeName = L"XZ ARCHIVE";
    else if (ext == L".zst") m_archiveTypeName = L"ZSTD ARCHIVE";
    else if (ext == L".iso" || ext == L".img") m_archiveTypeName = L"DISC IMAGE";
    else if (ext == L".cab") m_archiveTypeName = L"CABINET ARCHIVE";
    else if (ext == L".lzh" || ext == L".lha") m_archiveTypeName = L"LZH ARCHIVE";
    else if (ext == L".z" || ext == L".lzma") m_archiveTypeName = L"COMPRESSED FILE";
    else m_archiveTypeName = L"ZIP ARCHIVE";

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(archivePath.c_str(), GetFileExInfoStandard, &fad)) {
        m_lastWriteTime = fad.ftLastWriteTime;
    }

    std::vector<ArchiveEntryInfo> entries;
    std::wstring displayName;
    std::wstring errorText;
    ArchiveResult result = m_archive.Load(archivePath, entries, displayName, errorText);
    m_folderName = displayName;

    if (result == ArchiveResult::Encrypted) {
        m_hasError = true;
        m_passwordProtected = true;
        m_errorText = errorText.empty() ? L"This archive is password protected." : errorText;
        m_isLoaded = true;
        return true;
    }
    if (result == ArchiveResult::Failed) {
        m_hasError = true;
        m_errorText = errorText.empty() ? L"This archive could not be read." : errorText;
        m_isLoaded = true;
        return true;
    }
    if (result == ArchiveResult::Unsupported) {
        m_hasError = true;
        m_errorText = errorText.empty() ? L"This archive format is not supported." : errorText;
        m_isLoaded = true;
        return true;
    }

    m_totalCompressedBytes = 0;
    for (const auto& raw : entries) {
        FolderItemEntry entry = {};
        entry.path = raw.path;
        entry.name = LeafName(raw.path);
        entry.isDirectory = raw.isDirectory;
        entry.encrypted = raw.encrypted;
        entry.sizeBytes = raw.sizeBytes;
        entry.compressedSizeBytes = raw.packedBytes;
        entry.lastWriteTime = raw.modified;
        entry.formattedDate = raw.modified.dwLowDateTime || raw.modified.dwHighDateTime
                                   ? FormatFileTime(raw.modified) : L"—";

        if (entry.isDirectory) {
            m_dirCount++;
            m_directoryPaths.push_back(raw.path);
            entry.formattedSize = L"—";
            entry.extension = L"Folder";
        } else {
            m_fileCount++;
            m_totalSizeBytes += raw.sizeBytes;
            m_totalCompressedBytes += raw.packedBytes;
            entry.compressedSizeBytes = raw.packedBytes;
            entry.formattedSize = FormatSize(raw.sizeBytes);
            size_t dot = entry.name.find_last_of(L'.');
            if (dot != std::wstring::npos) {
                entry.extension = entry.name.substr(dot);
                std::transform(entry.extension.begin(), entry.extension.end(), entry.extension.begin(), ::towupper);
            } else {
                entry.extension = L"FILE";
            }
            if (entry.encrypted) m_encryptedEntries = true;
        }
        m_allEntries.push_back(entry);
    }

    // Synthesize directories that the archive only implies through file paths.
    std::vector<std::wstring> implied;
    for (const auto& entry : entries) {
        std::wstring dir = ParentPath(entry.path);
        while (!dir.empty()) {
            implied.push_back(dir);
            dir = ParentPath(dir);
        }
    }
    for (const auto& dir : implied) {
        auto it = std::find_if(m_directoryPaths.begin(), m_directoryPaths.end(),
                               [&](const std::wstring& existing) { return _wcsicmp(existing.c_str(), dir.c_str()) == 0; });
        if (it == m_directoryPaths.end()) {
            m_directoryPaths.push_back(dir);
            FolderItemEntry entry = {};
            entry.path = dir;
            entry.name = LeafName(dir);
            entry.isDirectory = true;
            entry.formattedSize = L"—";
            entry.formattedDate = L"—";
            entry.extension = L"Folder";
            m_allEntries.push_back(entry);
            m_dirCount++;
        }
    }

    std::sort(m_allEntries.begin(), m_allEntries.end(), [](const FolderItemEntry& a, const FolderItemEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
        return _wcsicmp(a.path.c_str(), b.path.c_str()) < 0;
    });

    if (m_encryptedEntries) m_passwordProtected = true;

    RebuildVisibleItems();
    m_isLoaded = true;
    (void)errorText;  // truncation notes are not surfaced as an error state
    return true;
}

std::wstring FolderEngine::BreadcrumbRoot() const {
    return m_folderName;
}

// Breadcrumb segments: index 0 is the archive root, then one per path component.
size_t FolderEngine::BreadcrumbCount() const {
    if (m_currentDir.empty()) return 0;
    size_t count = 2;  // root + first component
    for (wchar_t ch : m_currentDir) {
        if (ch == L'\\') ++count;
    }
    return count;
}

std::wstring FolderEngine::BreadcrumbDirectory(size_t index) const {
    if (index == 0) return L"";
    size_t seen = 1;
    for (size_t i = 0; i < m_currentDir.size(); ++i) {
        if (m_currentDir[i] == L'\\') {
            if (seen == index) return m_currentDir.substr(0, i);
            ++seen;
        }
    }
    return m_currentDir;
}

void FolderEngine::RebuildVisibleItems() {
    m_items.clear();
    m_rowRects.clear();
    m_crumbRects.clear();

    if (!m_hasError) {
        if (!m_search.empty()) {
            const std::wstring needle = ToLowerCopy(m_search);
            for (const auto& entry : m_allEntries) {
                if (entry.path.empty()) continue;
                if (ToLowerCopy(entry.path).find(needle) == std::wstring::npos) continue;
                FolderItemEntry hit = entry;
                hit.name = entry.path;
                m_items.push_back(hit);
            }
        } else {
            if (!m_currentDir.empty()) {
                FolderItemEntry up = {};
                up.name = L"..";
                up.path = ParentPath(m_currentDir);
                up.isDirectory = true;
                up.formattedSize = L"—";
                up.formattedDate = L"—";
                up.extension = L"UP";
                m_items.push_back(up);
            }
            for (const auto& entry : m_allEntries) {
                if (_wcsicmp(ParentPath(entry.path).c_str(), m_currentDir.c_str()) != 0) continue;
                m_items.push_back(entry);
            }
        }
        std::sort(m_items.begin(), m_items.end(), [](const FolderItemEntry& a, const FolderItemEntry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        if (!m_currentDir.empty() && m_search.empty() && !m_items.empty()) {
            // Keep ".." first even after sorting.
            auto it = std::find_if(m_items.begin(), m_items.end(),
                                   [](const FolderItemEntry& e) { return e.name == L".."; });
            if (it != m_items.end()) {
                FolderItemEntry up = *it;
                m_items.erase(it);
                m_items.insert(m_items.begin(), up);
            }
        }
    }

    m_selected = -1;
    m_hover = -1;
    ResetScroll();
}

void FolderEngine::ResetScroll() {
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
}

bool FolderEngine::EnterFolder(size_t index) {
    if (m_hasError || index >= m_items.size()) return false;
    const FolderItemEntry& item = m_items[index];
    if (!item.isDirectory) return false;
    if (item.name == L"..") return GoUp();
    m_currentDir = item.path;
    m_search.clear();
    RebuildVisibleItems();
    return true;
}

bool FolderEngine::GoUp() {
    if (m_hasError) return false;
    if (!m_search.empty()) {
        m_search.clear();
        RebuildVisibleItems();
        return true;
    }
    if (m_currentDir.empty()) return false;
    m_currentDir = ParentPath(m_currentDir);
    RebuildVisibleItems();
    return true;
}

bool FolderEngine::NavigateTo(const std::wstring& directory) {
    if (m_hasError) return false;
    if (_wcsicmp(directory.c_str(), m_currentDir.c_str()) == 0) return true;
    m_currentDir = directory;
    m_search.clear();
    RebuildVisibleItems();
    return true;
}

void FolderEngine::SetSelection(int index) {
    if (index < 0 || index >= (int)m_items.size()) {
        m_selected = -1;
        return;
    }
    m_selected = index;
}

void FolderEngine::SetSearch(const std::wstring& query) {
    if (query == m_search) return;
    m_search = query;
    m_selected = -1;
    RebuildVisibleItems();
}

std::wstring FolderEngine::GetSelectedPath() const {
    if (m_selected < 0 || m_selected >= (int)m_items.size()) return m_folderPath;
    const FolderItemEntry& item = m_items[m_selected];
    if (item.name == L"..") return m_folderPath;
    if (m_isArchive) return m_folderPath + L"\\" + item.path;  // virtual path for copy
    return m_folderPath + L"\\" + item.name;
}

// ---------------------------------------------------------------------------
// Layout / hit testing
// ---------------------------------------------------------------------------

// Row geometry is derived from the same constants Render uses. Hit testing must
// not depend on a paint having happened yet: a click can arrive before the first
// frame of a freshly opened archive.
int FolderEngine::HitTestRow(float x, float y, const D2D1_RECT_F& canvasRect) const {
    if (!m_isLoaded || m_hasError || m_items.empty()) return -1;

    float marginX = (std::max)(36.0f, ((canvasRect.right - canvasRect.left) - 920.0f) / 2.0f);
    float contentW = (canvasRect.right - canvasRect.left) - marginX * 2.0f;
    if (x < marginX || x > marginX + contentW) return -1;

    float listTop = canvasRect.top + 32.0f + 72.0f + 36.0f + 16.0f;
    if (m_isArchive && (!m_currentDir.empty() || !m_search.empty())) listTop += 36.0f + 12.0f;
    listTop += 26.0f + 8.0f;

    const float rowH = 34.0f;
    const float rowY = y - (listTop + m_scrollY);
    if (rowY < 0.0f) return -1;
    const int index = (int)(rowY / rowH);
    if (index < 0 || index >= (int)m_items.size()) return -1;
    return index;
}

int FolderEngine::HitTestBreadcrumb(float x, float y) const {
    for (const auto& crumb : m_crumbRects) {
        if (x >= crumb.left && x <= crumb.right) return (int)crumb.index;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void FolderEngine::Scroll(float dy) {
    float maxScroll = (std::max)(0.0f, m_contentHeight - 400.0f);
    m_scrollY += dy;
    if (m_scrollY > 0.0f) m_scrollY = 0.0f;
    if (m_scrollY < -maxScroll) m_scrollY = -maxScroll;
    m_targetScrollY = m_scrollY;
}

bool FolderEngine::UpdateAnimation(float dt) {
    if (m_scrollY != m_targetScrollY) {
        m_scrollY = m_targetScrollY;
        return true;
    }
    return false;
}

void FolderEngine::Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale) {
    if (!rt || !m_isLoaded) return;

    m_rowRects.clear();
    m_crumbRects.clear();

    float clientW = targetRect.right - targetRect.left;
    float marginX = (std::max)(36.0f, (clientW - 920.0f) / 2.0f);
    float contentW = clientW - marginX * 2.0f;

    float curY = targetRect.top + 32.0f + m_scrollY;

    // --- 1. Header hero ---
    {
        float iconSize = 44.0f;
        float iconX = marginX + 2.0f;
        float iconY = curY + 2.0f;

        D2D1_ROUNDED_RECT iconBox = D2D1::RoundedRect(D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize), 10.0f, 10.0f);
        rt->FillRoundedRectangle(iconBox, d2d.BrushCard);
        rt->DrawRoundedRectangle(iconBox, d2d.BrushBorder, 1.0f);
        if (m_isArchive) {
            d2d.DrawIconFileDocument(iconX + iconSize * 0.5f, iconY + iconSize * 0.5f, 22.0f, d2d.BrushAccent, rt);
        } else {
            d2d.DrawIconFolder(iconX + iconSize * 0.5f, iconY + iconSize * 0.5f, 24.0f, d2d.BrushAccent, rt);
        }

        float textX = iconX + iconSize + 18.0f;
        float titleW = contentW - iconSize - 20.0f;

        IDWriteTextLayout* tl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(m_folderName.c_str(), (UINT32)m_folderName.length(),
            d2d.FontHero ? d2d.FontHero : d2d.FontTitle, titleW, 36.0f, &tl);
        if (tl) {
            rt->DrawTextLayout(D2D1::Point2F(textX, curY), tl, d2d.BrushText);
            tl->Release();
        }

        IDWriteTextLayout* pl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(m_folderPath.c_str(), (UINT32)m_folderPath.length(),
            d2d.FontCaption ? d2d.FontCaption : d2d.FontBody, titleW, 20.0f, &pl);
        if (pl) {
            rt->DrawTextLayout(D2D1::Point2F(textX, curY + 36.0f), pl, d2d.BrushTextMuted);
            pl->Release();
        }

        curY += 72.0f;

        float badgeY = curY;
        auto drawPill = [&](const std::wstring& txt, float x, ID2D1SolidColorBrush* bg, ID2D1SolidColorBrush* fg, bool isAccent = false) -> float {
            float pw = 18.0f + (float)txt.length() * 7.2f;
            D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(x, badgeY, x + pw, badgeY + 24.0f), 6.0f, 6.0f);
            rt->FillRoundedRectangle(r, bg ? bg : d2d.BrushChipBg);
            rt->DrawRoundedRectangle(r, isAccent ? (ID2D1Brush*)d2d.BrushAccent : (d2d.BrushChipBorder ? (ID2D1Brush*)d2d.BrushChipBorder : (ID2D1Brush*)d2d.BrushBorder), 1.0f);

            IDWriteTextFormat* badgeFont = d2d.FontUtilityMono ? d2d.FontUtilityMono : d2d.FontBadge;
            IDWriteTextLayout* btl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(txt.c_str(), (UINT32)txt.length(), badgeFont, pw, 24.0f, &btl);
            if (btl) {
                btl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                btl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(x, badgeY), btl, fg ? fg : d2d.BrushText);
                btl->Release();
            }
            return pw + 8.0f;
        };

        float bx = marginX;
        ID2D1SolidColorBrush* chipBg = d2d.BrushChipBg ? d2d.BrushChipBg : d2d.BrushCard;
        auto plural = [](UINT32 count) { return count == 1 ? L"" : L"S"; };
        if (m_isArchive) {
            bx += drawPill(m_archiveTypeName, bx, chipBg, d2d.BrushAccent, true);
            WCHAR countStr[80];
            swprintf_s(countStr, L"%u FILE%s • %u SUBFOLDER%s", m_fileCount, plural(m_fileCount),
                       m_dirCount, plural(m_dirCount));
            bx += drawPill(countStr, bx, chipBg, d2d.BrushText);
            bx += drawPill(L"UNCOMPRESSED: " + FormatSize(m_totalSizeBytes), bx, chipBg, d2d.BrushTextMuted);
            if (m_passwordProtected) {
                drawPill(L"PASSWORD PROTECTED", bx, d2d.BrushAmber, d2d.BrushText);
            } else {
                drawPill(L"COMPRESSED: " + FormatSize(m_totalCompressedBytes), bx, chipBg, d2d.BrushGreen);
            }
        } else {
            bx += drawPill(L"DIRECTORY", bx, chipBg, d2d.BrushAccent, true);
            WCHAR countStr[80];
            swprintf_s(countStr, L"%u FILE%s • %u SUBFOLDER%s", m_fileCount, plural(m_fileCount),
                       m_dirCount, plural(m_dirCount));
            bx += drawPill(countStr, bx, chipBg, d2d.BrushText);
            bx += drawPill(FormatSize(m_totalSizeBytes), bx, chipBg, d2d.BrushTextMuted);
            bx += drawPill(FormatFileTime(m_lastWriteTime), bx, chipBg, d2d.BrushTextMuted);
        }

        curY += 36.0f;
    }

    rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY), d2d.BrushBorderSubtle, 1.0f);
    curY += 16.0f;

    // --- 2. Error / password state ---
    if (m_hasError) {
        D2D1_ROUNDED_RECT card = D2D1::RoundedRect(
            D2D1::RectF(marginX, curY + 24.0f, marginX + contentW, curY + 148.0f), 12.0f, 12.0f);
        rt->FillRoundedRectangle(card, d2d.BrushCard);
        rt->DrawRoundedRectangle(card, d2d.BrushBorder, 1.0f);

        const wchar_t* title = m_passwordProtected ? L"Password protected" : L"Can't open this archive";
        IDWriteTextLayout* tl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(title, (UINT32)wcslen(title),
            d2d.FontTitle ? d2d.FontTitle : d2d.FontBody, contentW - 48.0f, 30.0f, &tl);
        if (tl) {
            rt->DrawTextLayout(D2D1::Point2F(marginX + 24.0f, curY + 44.0f), tl, d2d.BrushText);
            tl->Release();
        }

        IDWriteTextLayout* bl = nullptr;
        std::wstring body = m_errorText;
        if (m_passwordProtected) {
            body += L"\nWinPreview can list an archive only when it isn't set to hide file names.";
        }
        d2d.GetDWriteFactory()->CreateTextLayout(body.c_str(), (UINT32)body.length(),
            d2d.FontBody, contentW - 48.0f, 60.0f, &bl);
        if (bl) {
            bl->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            rt->DrawTextLayout(D2D1::Point2F(marginX + 24.0f, curY + 80.0f), bl, d2d.BrushTextMuted);
            bl->Release();
        }

        m_contentHeight = curY + 160.0f;
        return;
    }

    // --- 3. Breadcrumb (archive navigation) ---
    if (m_isArchive && (!m_currentDir.empty() || !m_search.empty())) {
        float crumbX = marginX;
        float crumbY = curY;
        float fontSize = 12.0f;

        auto drawCrumb = [&](const std::wstring& text, size_t index, bool last) {
            IDWriteTextLayout* cl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(text.c_str(), (UINT32)text.length(),
                d2d.FontBody, 460.0f, 22.0f, &cl);
            float width = 0.0f;
            if (cl) {
                DWRITE_TEXT_METRICS metrics = {};
                cl->GetMetrics(&metrics);
                width = metrics.widthIncludingTrailingWhitespace;
                cl->Release();
            }
            if (width < 12.0f) width = (float)text.length() * 7.0f;

            D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(
                D2D1::RectF(crumbX, crumbY, crumbX + width + 16.0f, crumbY + 24.0f), 6.0f, 6.0f);
            rt->FillRoundedRectangle(pill, last ? d2d.BrushCard : d2d.BrushChipBg);
            rt->DrawRoundedRectangle(pill, last ? d2d.BrushAccent : d2d.BrushBorder, 1.0f);

            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(text.c_str(), (UINT32)text.length(),
                d2d.FontBody, width + 12.0f, 24.0f, &tl);
            if (tl) {
                tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(crumbX + 8.0f, crumbY), tl,
                                   last ? d2d.BrushText : d2d.BrushTextMuted);
                tl->Release();
            }
            CrumbRect crumb;
            crumb.left = crumbX;
            crumb.right = crumbX + width + 16.0f;
            crumb.index = index;
            m_crumbRects.push_back(crumb);

            crumbX += width + 24.0f;
        };

        drawCrumb(m_folderName, 0, m_currentDir.empty());
        for (size_t i = 1; i < BreadcrumbCount(); ++i) {
            std::wstring dir = BreadcrumbDirectory(i);
            std::wstring label = LeafName(dir);
            drawCrumb(label, i, i + 1 == BreadcrumbCount());
        }
        if (!m_search.empty()) {
            drawCrumb(L"\u201c" + m_search + L"\u201d", (size_t)-1, true);
        }

        curY += 36.0f;
        rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY), d2d.BrushBorderSubtle, 1.0f);
        curY += 12.0f;
    }

    // --- 4. Table ---
    float colNameW = contentW * 0.52f;
    float colTypeW = contentW * 0.14f;
    float colSizeW = contentW * 0.14f;
    float colDateW = contentW * 0.20f;

    {
        float hY = curY;
        float colX = marginX;

        rt->DrawText(L"NAME", 4, d2d.FontBadge, D2D1::RectF(colX + 32.0f, hY, colX + colNameW, hY + 18.0f), d2d.BrushTextMuted);
        colX += colNameW;

        rt->DrawText(L"TYPE", 4, d2d.FontBadge, D2D1::RectF(colX, hY, colX + colTypeW, hY + 18.0f), d2d.BrushTextMuted);
        colX += colTypeW;

        IDWriteTextLayout* stl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(L"SIZE", 4, d2d.FontBadge, colSizeW - 12.0f, 18.0f, &stl);
        if (stl) {
            stl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            rt->DrawTextLayout(D2D1::Point2F(colX, hY), stl, d2d.BrushTextMuted);
            stl->Release();
        }
        colX += colSizeW;

        IDWriteTextLayout* dtl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(L"DATE MODIFIED", 13, d2d.FontBadge, colDateW - 12.0f, 18.0f, &dtl);
        if (dtl) {
            dtl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            rt->DrawTextLayout(D2D1::Point2F(colX, hY), dtl, d2d.BrushTextMuted);
            dtl->Release();
        }

        curY += 26.0f;
        rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY), d2d.BrushBorderSubtle, 1.0f);
        curY += 8.0f;
    }

    if (m_items.empty()) {
        std::wstring message = m_search.empty()
            ? L"This archive is empty."
            : L"Nothing in this archive matches \u201c" + m_search + L"\u201d.";
        float y = curY + 28.0f;
        rt->DrawText(message.c_str(), (UINT32)message.length(), d2d.FontBody,
                     D2D1::RectF(marginX, y, marginX + contentW, y + 40.0f), d2d.BrushTextMuted);
        m_contentHeight = y + 60.0f;
        return;
    }

    float rowH = 34.0f;
    for (size_t i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items[i];

        RowRect rect;
        rect.top = curY;
        rect.bottom = curY + rowH;
        m_rowRects.push_back(rect);

        if (curY + rowH < targetRect.top) {
            curY += rowH;
            continue;
        }
        if (curY > targetRect.bottom) {
            curY += rowH * (m_items.size() - i);
            break;
        }

        const bool selected = ((int)i == m_selected);
        const bool hovered = ((int)i == m_hover);
        if (selected || hovered) {
            ID2D1SolidColorBrush* bg = selected && d2d.BrushSelection ? d2d.BrushSelection
                                      : (d2d.BrushHover ? d2d.BrushHover : d2d.BrushCard);
            D2D1_ROUNDED_RECT rowBg = D2D1::RoundedRect(
                D2D1::RectF(marginX, curY, marginX + contentW, curY + rowH), 4.0f, 4.0f);
            rt->FillRoundedRectangle(rowBg, bg);
        } else if (i % 2 == 1) {
            D2D1_ROUNDED_RECT rowBg = D2D1::RoundedRect(
                D2D1::RectF(marginX, curY, marginX + contentW, curY + rowH), 4.0f, 4.0f);
            rt->FillRoundedRectangle(rowBg, d2d.BrushCard);
        }

        float colX = marginX;
        float glyphCenterY = curY + rowH / 2.0f;
        if (item.isDirectory) {
            d2d.DrawIconFolder(colX + 14.0f, glyphCenterY, 15.0f, d2d.BrushAccent, rt);
        } else {
            d2d.DrawIconFileDocument(colX + 14.0f, glyphCenterY, 15.0f, d2d.BrushTextMuted, rt);
        }

        IDWriteTextLayout* ntl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(item.name.c_str(), (UINT32)item.name.length(),
            d2d.FontBody, colNameW - 32.0f, rowH, &ntl);
        if (ntl) {
            ntl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ntl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            ntl->SetTrimming(&trimming, nullptr);
            rt->DrawTextLayout(D2D1::Point2F(colX + 30.0f, curY), ntl, d2d.BrushText);
            ntl->Release();
        }
        colX += colNameW;

        IDWriteTextLayout* ttl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(item.extension.c_str(), (UINT32)item.extension.length(),
            d2d.FontCaption, colTypeW - 8.0f, rowH, &ttl);
        if (ttl) {
            ttl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(colX, curY), ttl, d2d.BrushTextMuted);
            ttl->Release();
        }
        colX += colTypeW;

        IDWriteTextLayout* stl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(item.formattedSize.c_str(), (UINT32)item.formattedSize.length(),
            d2d.FontMono ? d2d.FontMono : d2d.FontCaption, colSizeW - 12.0f, rowH, &stl);
        if (stl) {
            stl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            stl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(colX, curY), stl, d2d.BrushText);
            stl->Release();
        }
        colX += colSizeW;

        IDWriteTextLayout* dtl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(item.formattedDate.c_str(), (UINT32)item.formattedDate.length(),
            d2d.FontMono ? d2d.FontMono : d2d.FontCaption, colDateW - 12.0f, rowH, &dtl);
        if (dtl) {
            dtl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            dtl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(colX, curY), dtl, d2d.BrushTextMuted);
            dtl->Release();
        }

        curY += rowH;
    }

    m_contentHeight = curY - (targetRect.top + 32.0f + m_scrollY);
}

std::wstring FolderEngine::GetFormatBadge() const {
    WCHAR buf[160];
    if (m_isArchive) {
        if (m_hasError) {
            swprintf_s(buf, L"%s • %s", m_archiveTypeName.c_str(),
                       m_passwordProtected ? L"PASSWORD PROTECTED" : L"UNREADABLE");
        } else {
            swprintf_s(buf, L"%s • %u ITEMS • %s (COMPRESSED: %s)%s",
                m_archiveTypeName.c_str(), m_fileCount + m_dirCount,
                FormatSize(m_totalSizeBytes).c_str(), FormatSize(m_totalCompressedBytes).c_str(),
                m_passwordProtected ? L" • PASSWORD PROTECTED" : L"");
        }
    } else {
        swprintf_s(buf, L"DIRECTORY • %u ITEMS • %s", m_fileCount + m_dirCount, FormatSize(m_totalSizeBytes).c_str());
    }
    return buf;
}

} // namespace WinPreview
