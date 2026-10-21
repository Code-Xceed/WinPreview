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
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
    m_contentHeight = 0.0f;
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

bool FolderEngine::LoadFromZipArchive(const std::wstring& zipPath) {
    Close();

    m_isArchive = true;
    m_folderPath = zipPath;

    size_t slash = zipPath.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash + 1 < zipPath.length()) {
        m_folderName = zipPath.substr(slash + 1);
    } else {
        m_folderName = zipPath;
    }

    std::wstring ext;
    size_t dot = zipPath.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext = zipPath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }

    if (ext == L".apk") m_archiveTypeName = L"APK PACKAGE";
    else if (ext == L".jar") m_archiveTypeName = L"JAR ARCHIVE";
    else if (ext == L".nupkg") m_archiveTypeName = L"NUGET PACKAGE";
    else if (ext == L".msix" || ext == L".appx") m_archiveTypeName = L"APP PACKAGE";
    else if (ext == L".whl") m_archiveTypeName = L"PYTHON WHEEL";
    else if (ext == L".vsix") m_archiveTypeName = L"VS EXTENSION";
    else if (ext == L".cbz") m_archiveTypeName = L"COMIC ARCHIVE";
    else if (ext == L".epub") m_archiveTypeName = L"EPUB EBOOK";
    else if (ext == L".xpi" || ext == L".crx") m_archiveTypeName = L"BROWSER EXTENSION";
    else if (ext == L".war" || ext == L".aar" || ext == L".ear") m_archiveTypeName = L"JAVA ARCHIVE";
    else m_archiveTypeName = L"ZIP ARCHIVE";

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(zipPath.c_str(), GetFileExInfoStandard, &fad)) {
        m_lastWriteTime = fad.ftLastWriteTime;
    }

    HANDLE hFile = CreateFileW(zipPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart < sizeof(ZipEocd)) {
        CloseHandle(hFile);
        return false;
    }

    UINT64 fileSize = liSize.QuadPart;
    DWORD scanSize = (DWORD)(std::min)(fileSize, (UINT64)65557);
    std::vector<BYTE> buf(scanSize);

    LARGE_INTEGER liSeek;
    liSeek.QuadPart = fileSize - scanSize;
    SetFilePointerEx(hFile, liSeek, NULL, FILE_BEGIN);

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buf.data(), scanSize, &bytesRead, NULL) || bytesRead < sizeof(ZipEocd)) {
        CloseHandle(hFile);
        return false;
    }

    // Find EOCD signature from end of buffer
    int eocdPos = -1;
    for (int i = (int)bytesRead - (int)sizeof(ZipEocd); i >= 0; --i) {
        if (*(uint32_t*)(buf.data() + i) == 0x06054b50) {
            eocdPos = i;
            break;
        }
    }

    if (eocdPos < 0) {
        CloseHandle(hFile);
        return false;
    }

    ZipEocd* pEocd = (ZipEocd*)(buf.data() + eocdPos);
    uint32_t totalEntries = pEocd->totalEntries;
    uint32_t cdOffset = pEocd->centralDirOffset;

    if (totalEntries == 0 || (UINT64)cdOffset >= fileSize) {
        CloseHandle(hFile);
        return false;
    }

    liSeek.QuadPart = cdOffset;
    SetFilePointerEx(hFile, liSeek, NULL, FILE_BEGIN);

    for (uint32_t i = 0; i < totalEntries && i < 20000; ++i) {
        ZipCdHeader cd = {};
        if (!ReadFile(hFile, &cd, sizeof(cd), &bytesRead, NULL) || bytesRead != sizeof(cd) || cd.signature != 0x02014b50) {
            break;
        }

        std::string rawName(cd.fileNameLength, '\0');
        if (cd.fileNameLength > 0) {
            ReadFile(hFile, &rawName[0], cd.fileNameLength, &bytesRead, NULL);
        }

        if (cd.extraFieldLength > 0 || cd.fileCommentLength > 0) {
            LARGE_INTEGER skipLi;
            skipLi.QuadPart = cd.extraFieldLength + cd.fileCommentLength;
            SetFilePointerEx(hFile, skipLi, NULL, FILE_CURRENT);
        }

        UINT codePage = (cd.bitFlag & (1 << 11)) ? CP_UTF8 : CP_ACP;
        int wlen = MultiByteToWideChar(codePage, 0, rawName.c_str(), (int)rawName.length(), NULL, 0);
        std::wstring wname(wlen, L'\0');
        if (wlen > 0) {
            MultiByteToWideChar(codePage, 0, rawName.c_str(), (int)rawName.length(), &wname[0], wlen);
        }

        FolderItemEntry entry = {};
        entry.name = wname;
        entry.sizeBytes = cd.uncompressedSize;
        entry.compressedSizeBytes = cd.compressedSize;

        bool isDir = (!wname.empty() && (wname.back() == L'/' || wname.back() == L'\\')) ||
                     ((cd.externalFileAttributes & 0x10) != 0);

        if (isDir) {
            entry.isDirectory = true;
            m_dirCount++;
            entry.formattedSize = L"—";
            entry.extension = L"Folder";
            if (!entry.name.empty() && (entry.name.back() == L'/' || entry.name.back() == L'\\')) {
                entry.name.pop_back();
            }
        } else {
            entry.isDirectory = false;
            m_fileCount++;
            m_totalSizeBytes += entry.sizeBytes;
            m_totalCompressedBytes += entry.compressedSizeBytes;
            entry.formattedSize = FormatSize(entry.sizeBytes);

            size_t dotPos = entry.name.find_last_of(L'.');
            if (dotPos != std::wstring::npos) {
                entry.extension = entry.name.substr(dotPos);
                std::transform(entry.extension.begin(), entry.extension.end(), entry.extension.begin(), ::towupper);
            } else {
                entry.extension = L"FILE";
            }
        }

        SYSTEMTIME st = {};
        st.wYear = ((cd.lastModDate >> 9) & 0x7F) + 1980;
        st.wMonth = (std::max)(1, (int)((cd.lastModDate >> 5) & 0x0F));
        st.wDay = (std::max)(1, (int)(cd.lastModDate & 0x1F));
        st.wHour = (cd.lastModTime >> 11) & 0x1F;
        st.wMinute = (cd.lastModTime >> 5) & 0x3F;
        st.wSecond = (cd.lastModTime & 0x1F) * 2;
        FILETIME ft = {};
        SystemTimeToFileTime(&st, &ft);
        entry.lastWriteTime = ft;
        entry.formattedDate = FormatFileTime(ft);

        m_items.push_back(entry);
    }

    CloseHandle(hFile);

    std::sort(m_items.begin(), m_items.end(), [](const FolderItemEntry& a, const FolderItemEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    m_isLoaded = true;
    return true;
}

void FolderEngine::Scroll(float dy) {
    float maxScroll = (std::max)(0.0f, m_contentHeight - 400.0f);
    m_targetScrollY += dy;
    if (m_targetScrollY > 0.0f) m_targetScrollY = 0.0f;
    if (m_targetScrollY < -maxScroll) m_targetScrollY = -maxScroll;
}

bool FolderEngine::UpdateAnimation(float dt) {
    float dy = m_targetScrollY - m_scrollY;
    if (std::abs(dy) > 0.25f) {
        float safeDt = (std::max)(0.001f, (std::min)(0.1f, dt));
        float decay = 1.0f - std::exp(-22.0f * safeDt);
        m_scrollY += dy * decay;
        return true;
    } else if (m_scrollY != m_targetScrollY) {
        m_scrollY = m_targetScrollY;
        return true;
    }
    return false;
}

void FolderEngine::Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale) {
    if (!rt || !m_isLoaded) return;

    float clientW = targetRect.right - targetRect.left;
    float clientH = targetRect.bottom - targetRect.top;
    float marginX = (std::max)(36.0f, (clientW - 920.0f) / 2.0f);
    float contentW = clientW - marginX * 2.0f;

    float curY = targetRect.top + 32.0f + m_scrollY;

    // --- 1. Folder Header Hero ---
    {
        float iconSize = 44.0f;
        float iconX = marginX + 2.0f;
        float iconY = curY + 2.0f;

        // Folder back plate
        D2D1_ROUNDED_RECT backPlate = D2D1::RoundedRect(D2D1::RectF(iconX, iconY + 6.0f, iconX + iconSize, iconY + iconSize), 5.0f, 5.0f);
        rt->FillRoundedRectangle(backPlate, d2d.BrushAccent);
        // Folder tab
        D2D1_ROUNDED_RECT folderTab = D2D1::RoundedRect(D2D1::RectF(iconX, iconY, iconX + iconSize * 0.45f, iconY + 12.0f), 3.0f, 3.0f);
        rt->FillRoundedRectangle(folderTab, d2d.BrushAccent);

        // Title and path
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

        // Badges
        float badgeY = curY;
        auto drawPill = [&](const std::wstring& txt, float x, ID2D1SolidColorBrush* bg, ID2D1SolidColorBrush* fg) -> float {
            float pw = 18.0f + (float)txt.length() * 7.2f;
            D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(x, badgeY, x + pw, badgeY + 22.0f), 11.0f, 11.0f);
            rt->FillRoundedRectangle(r, bg);
            rt->DrawRoundedRectangle(r, d2d.BrushBorder, 1.0f);

            IDWriteTextLayout* btl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(txt.c_str(), (UINT32)txt.length(), d2d.FontBadge, pw, 22.0f, &btl);
            if (btl) {
                btl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                btl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(x, badgeY), btl, fg);
                btl->Release();
            }
            return pw + 8.0f;
        };

        float bx = marginX;
        if (m_isArchive) {
            bx += drawPill(m_archiveTypeName, bx, d2d.BrushCard, d2d.BrushAccent);
            WCHAR countStr[64];
            swprintf_s(countStr, L"%u FILES • %u SUBFOLDERS", m_fileCount, m_dirCount);
            bx += drawPill(countStr, bx, d2d.BrushCard, d2d.BrushText);
            bx += drawPill(L"UNCOMPRESSED: " + FormatSize(m_totalSizeBytes), bx, d2d.BrushCard, d2d.BrushTextMuted);
            bx += drawPill(L"COMPRESSED: " + FormatSize(m_totalCompressedBytes), bx, d2d.BrushCard, d2d.BrushGreen);
        } else {
            bx += drawPill(L"DIRECTORY", bx, d2d.BrushCard, d2d.BrushAccent);
            WCHAR countStr[64];
            swprintf_s(countStr, L"%u FILES • %u SUBFOLDERS", m_fileCount, m_dirCount);
            bx += drawPill(countStr, bx, d2d.BrushCard, d2d.BrushText);
            bx += drawPill(FormatSize(m_totalSizeBytes), bx, d2d.BrushCard, d2d.BrushTextMuted);
            bx += drawPill(FormatFileTime(m_lastWriteTime), bx, d2d.BrushCard, d2d.BrushTextMuted);
        }

        curY += 36.0f;
    }

    // Divider
    rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY), d2d.BrushBorderSubtle, 1.0f);
    curY += 16.0f;

    // --- 2. File Listing Table ---
    float colNameW = contentW * 0.52f;
    float colTypeW = contentW * 0.14f;
    float colSizeW = contentW * 0.14f;
    float colDateW = contentW * 0.20f;

    // Table Header
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

    // Rows
    float rowH = 34.0f;
    for (size_t i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items[i];
        if (curY + rowH < targetRect.top) {
            curY += rowH;
            continue;
        }
        if (curY > targetRect.bottom) {
            curY += rowH * (m_items.size() - i);
            break;
        }

        // Alternating row background
        if (i % 2 == 1) {
            D2D1_ROUNDED_RECT rowBg = D2D1::RoundedRect(D2D1::RectF(marginX, curY, marginX + contentW, curY + rowH), 4.0f, 4.0f);
            rt->FillRoundedRectangle(rowBg, d2d.BrushCard);
        }

        float colX = marginX;

        // Icon
        float glyphCenterY = curY + rowH / 2.0f;
        if (item.isDirectory) {
            D2D1_ROUNDED_RECT bp = D2D1::RoundedRect(D2D1::RectF(colX + 6.0f, glyphCenterY - 6.0f, colX + 22.0f, glyphCenterY + 6.0f), 2.0f, 2.0f);
            rt->FillRoundedRectangle(bp, d2d.BrushAccent);
        } else {
            d2d.DrawIconFileDocument(colX + 14.0f, glyphCenterY, 14.0f, d2d.BrushTextMuted);
        }

        // Name
        IDWriteTextLayout* ntl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(item.name.c_str(), (UINT32)item.name.length(),
            d2d.FontBody, colNameW - 32.0f, rowH, &ntl);
        if (ntl) {
            ntl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(colX + 30.0f, curY), ntl, d2d.BrushText);
            ntl->Release();
        }
        colX += colNameW;

        // Type
        IDWriteTextLayout* ttl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(item.extension.c_str(), (UINT32)item.extension.length(),
            d2d.FontCaption, colTypeW - 8.0f, rowH, &ttl);
        if (ttl) {
            ttl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(colX, curY), ttl, d2d.BrushTextMuted);
            ttl->Release();
        }
        colX += colTypeW;

        // Size
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

        // Date
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
    WCHAR buf[128];
    if (m_isArchive) {
        swprintf_s(buf, L"%s • %u ITEMS • %s (COMPRESSED: %s)",
            m_archiveTypeName.c_str(), m_fileCount + m_dirCount,
            FormatSize(m_totalSizeBytes).c_str(), FormatSize(m_totalCompressedBytes).c_str());
    } else {
        swprintf_s(buf, L"DIRECTORY • %u ITEMS • %s", m_fileCount + m_dirCount, FormatSize(m_totalSizeBytes).c_str());
    }
    return buf;
}

} // namespace WinPreview
