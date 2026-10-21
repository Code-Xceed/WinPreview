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

struct FolderItemEntry {
    std::wstring name;
    bool isDirectory = false;
    UINT64 sizeBytes = 0;
    UINT64 compressedSizeBytes = 0;
    FILETIME lastWriteTime = {};
    std::wstring formattedSize;
    std::wstring formattedDate;
    std::wstring extension;
};

class FolderEngine {
public:
    FolderEngine();
    ~FolderEngine();

    bool LoadFromDirectory(const std::wstring& folderPath);
    bool LoadFromZipArchive(const std::wstring& zipPath);
    void Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale);
    void Scroll(float dy);
    bool UpdateAnimation(float dt);
    void Close();

    bool IsLoaded() const { return m_isLoaded; }
    bool IsArchive() const { return m_isArchive; }
    std::wstring GetFormatBadge() const;
    const std::wstring& GetFolderPath() const { return m_folderPath; }

private:
    bool m_isLoaded = false;
    bool m_isArchive = false;
    std::wstring m_archiveTypeName;
    std::wstring m_folderPath;
    std::wstring m_folderName;
    UINT32 m_fileCount = 0;
    UINT32 m_dirCount = 0;
    UINT64 m_totalSizeBytes = 0;
    UINT64 m_totalCompressedBytes = 0;
    FILETIME m_lastWriteTime = {};
    std::vector<FolderItemEntry> m_items;

    float m_scrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    float m_contentHeight = 0.0f;
};

} // namespace WinPreview
