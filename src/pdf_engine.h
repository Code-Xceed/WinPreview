#pragma once
#include "d2d_engine.h"
#include <string>
#include <vector>

namespace WinPreview {

typedef void* FPDF_DOCUMENT;
typedef void* FPDF_PAGE;
typedef void* FPDF_BITMAP;
typedef void* FPDF_TEXTPAGE;
typedef void* FPDF_SCHHANDLE;
typedef void* FPDF_BOOKMARK;
typedef void* FPDF_DEST;

struct PdfSearchMatch {
    int pageIndex = 0;
    int charIndex = 0;
    int charCount = 0;
    std::vector<D2D1_RECT_F> rects; // In page coordinate space (PDF points)
};

struct PdfOutlineItem {
    std::wstring title;
    int pageIndex = 0;
    int level = 0;
};

class PdfEngine {
public:
    PdfEngine();
    ~PdfEngine();

    bool Initialize();
    bool IsAvailable() const { return m_hPdfium != nullptr; }

    bool OpenDocument(const std::wstring& filePath);
    void CloseDocument();

    int GetPageCount() const { return m_pageCount; }
    bool GetPageSize(int pageIndex, float* outWidth, float* outHeight);

    bool RenderPageToBitmap(
        int pageIndex,
        float targetWidth,
        float targetHeight,
        ID2D1RenderTarget* renderTarget,
        ID2D1Bitmap** outBitmap
    );

    bool RenderThumbnail(
        int pageIndex,
        float maxDimension,
        ID2D1RenderTarget* renderTarget,
        ID2D1Bitmap** outBitmap
    );

    // Text Extraction & Selection
    bool LoadTextPage(int pageIndex);
    void CloseTextPage();
    int GetCurrentTextPageIndex() const { return m_textPageIndex; }
    int GetCharCount();
    int GetCharIndexAtPos(double pageX, double pageY, double tolX = 4.0, double tolY = 4.0);
    bool GetCharBox(int charIndex, double* left, double* right, double* bottom, double* top);
    std::vector<D2D1_RECT_F> GetSelectionRects(int startIndex, int endIndex);
    std::wstring GetSelectedText(int startIndex, int endIndex);

    // In-Document Search
    std::vector<PdfSearchMatch> Search(const std::wstring& query, bool matchCase = false, bool wholeWord = false);

    // Table of Contents / Outline Bookmarks
    std::vector<PdfOutlineItem> GetOutline();

private:
    void TraverseBookmarks(FPDF_BOOKMARK parent, int level, std::vector<PdfOutlineItem>& outList);

    HMODULE m_hPdfium = nullptr;
    FPDF_DOCUMENT m_document = nullptr;
    int m_pageCount = 0;
    std::wstring m_currentFilePath;

    // Cached Text Page for the active page
    FPDF_PAGE m_activePage = nullptr;
    FPDF_TEXTPAGE m_activeTextPage = nullptr;
    int m_textPageIndex = -1;

    // Core Function Pointers
    void (*fn_FPDF_InitLibraryWithConfig)(const void* config) = nullptr;
    void (*fn_FPDF_DestroyLibrary)() = nullptr;
    FPDF_DOCUMENT (*fn_FPDF_LoadDocument)(const char* file_path, const char* password) = nullptr;
    void (*fn_FPDF_CloseDocument)(FPDF_DOCUMENT document) = nullptr;
    int (*fn_FPDF_GetPageCount)(FPDF_DOCUMENT document) = nullptr;
    FPDF_PAGE (*fn_FPDF_LoadPage)(FPDF_DOCUMENT document, int page_index) = nullptr;
    void (*fn_FPDF_ClosePage)(FPDF_PAGE page) = nullptr;
    float (*fn_FPDF_GetPageWidthF)(FPDF_PAGE page) = nullptr;
    float (*fn_FPDF_GetPageHeightF)(FPDF_PAGE page) = nullptr;
    FPDF_BITMAP (*fn_FPDFBitmap_CreateEx)(int width, int height, int format, void* first_scan, int stride) = nullptr;
    void (*fn_FPDFBitmap_FillRect)(FPDF_BITMAP bitmap, int left, int top, int width, int height, unsigned long color) = nullptr;
    void (*fn_FPDF_RenderPageBitmap)(FPDF_BITMAP bitmap, FPDF_PAGE page, int start_x, int start_y, int size_x, int size_y, int rotate, int flags) = nullptr;
    void (*fn_FPDFBitmap_Destroy)(FPDF_BITMAP bitmap) = nullptr;
    void* (*fn_FPDFBitmap_GetBuffer)(FPDF_BITMAP bitmap) = nullptr;
    int (*fn_FPDFBitmap_GetStride)(FPDF_BITMAP bitmap) = nullptr;

    // Text Engine Function Pointers
    FPDF_TEXTPAGE (*fn_FPDFText_LoadPage)(FPDF_PAGE page) = nullptr;
    void (*fn_FPDFText_ClosePage)(FPDF_TEXTPAGE text_page) = nullptr;
    int (*fn_FPDFText_CountChars)(FPDF_TEXTPAGE text_page) = nullptr;
    int (*fn_FPDFText_GetCharBox)(FPDF_TEXTPAGE text_page, int index, double* left, double* right, double* bottom, double* top) = nullptr;
    int (*fn_FPDFText_GetCharIndexAtPos)(FPDF_TEXTPAGE text_page, double x, double y, double xTolerance, double yTolerance) = nullptr;
    int (*fn_FPDFText_GetText)(FPDF_TEXTPAGE text_page, int start_index, int count, unsigned short* result) = nullptr;
    int (*fn_FPDFText_CountRects)(FPDF_TEXTPAGE text_page, int start_index, int count) = nullptr;
    int (*fn_FPDFText_GetRect)(FPDF_TEXTPAGE text_page, int rect_index, double* left, double* top, double* right, double* bottom) = nullptr;
    FPDF_SCHHANDLE (*fn_FPDFText_FindStart)(FPDF_TEXTPAGE text_page, const unsigned short* findwhat, unsigned long flags, int start_index) = nullptr;
    int (*fn_FPDFText_FindNext)(FPDF_SCHHANDLE handle) = nullptr;
    int (*fn_FPDFText_FindPrev)(FPDF_SCHHANDLE handle) = nullptr;
    int (*fn_FPDFText_GetSchResultIndex)(FPDF_SCHHANDLE handle) = nullptr;
    int (*fn_FPDFText_GetSchCount)(FPDF_SCHHANDLE handle) = nullptr;
    void (*fn_FPDFText_FindClose)(FPDF_SCHHANDLE handle) = nullptr;

    // Outline / Bookmarks Function Pointers
    FPDF_BOOKMARK (*fn_FPDFBookmark_GetFirstChild)(FPDF_DOCUMENT document, FPDF_BOOKMARK bookmark) = nullptr;
    FPDF_BOOKMARK (*fn_FPDFBookmark_GetNextSibling)(FPDF_DOCUMENT document, FPDF_BOOKMARK bookmark) = nullptr;
    unsigned long (*fn_FPDFBookmark_GetTitle)(FPDF_BOOKMARK bookmark, void* buffer, unsigned long buflen) = nullptr;
    FPDF_DEST (*fn_FPDFBookmark_GetDest)(FPDF_DOCUMENT document, FPDF_BOOKMARK bookmark) = nullptr;
    int (*fn_FPDFDest_GetDestPageIndex)(FPDF_DOCUMENT document, FPDF_DEST dest) = nullptr;
};

} // namespace WinPreview
