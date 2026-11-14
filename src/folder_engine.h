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
#include "archive_engine.h"

namespace WinPreview {

class D2DEngine;

struct FolderItemEntry {
    std::wstring name;
    std::wstring path;              // archive-relative path; empty for a filesystem listing
    bool isDirectory = false;
    bool encrypted = false;
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
    bool LoadFromArchive(const std::wstring& archivePath);
    void Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale);
    void Scroll(float dy);
    bool UpdateAnimation(float dt);
    void Close();

    bool IsLoaded() const { return m_isLoaded; }
    bool IsArchive() const { return m_isArchive; }
    std::wstring GetFormatBadge() const;
    const std::wstring& GetFolderPath() const { return m_folderPath; }
    const std::wstring& GetDisplayName() const { return m_folderName; }

    // Archive state
    bool HasError() const { return m_hasError; }
    bool IsPasswordProtected() const { return m_passwordProtected; }
    const std::wstring& GetErrorText() const { return m_errorText; }
    bool HasEncryptedEntries() const { return m_encryptedEntries; }

    // Archive navigation
    const std::wstring& GetCurrentDir() const { return m_currentDir; }
    const std::wstring& GetSearchQuery() const { return m_search; }
    const std::vector<FolderItemEntry>& GetItems() const { return m_items; }
    bool EnterFolder(size_t index);
    bool GoUp();
    bool NavigateTo(const std::wstring& directory);
    void SetSelection(int index);
    int GetSelection() const { return m_selected; }
    void SetHover(int index) { m_hover = index; }
    int GetHover() const { return m_hover; }
    void SetSearch(const std::wstring& query);
    std::wstring GetSelectedPath() const;

    // Hit testing against the last rendered frame (client coordinates).
    int HitTestRow(float x, float y, const D2D1_RECT_F& canvasRect) const;
    int HitTestBreadcrumb(float x, float y) const;
    std::wstring BreadcrumbDirectory(size_t index) const;
    size_t BreadcrumbCount() const;

private:
    void RebuildVisibleItems();
    void ResetScroll();
    std::wstring BreadcrumbRoot() const;

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

    // Archive tree
    ArchiveEngine m_archive;
    std::vector<FolderItemEntry> m_allEntries;
    std::vector<std::wstring> m_directoryPaths;
    std::wstring m_currentDir;
    std::wstring m_search;
    int m_selected = -1;
    int m_hover = -1;
    bool m_hasError = false;
    bool m_passwordProtected = false;
    bool m_encryptedEntries = false;
    std::wstring m_errorText;

    // Layout of the last frame, used for hit testing.
    struct RowRect {
        float top = 0.0f;
        float bottom = 0.0f;
    };
    struct CrumbRect {
        float left = 0.0f;
        float right = 0.0f;
        size_t index = 0;
    };
    std::vector<RowRect> m_rowRects;
    std::vector<CrumbRect> m_crumbRects;

    float m_scrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    float m_contentHeight = 0.0f;
};

} // namespace WinPreview
