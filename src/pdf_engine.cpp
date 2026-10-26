#include "pdf_engine.h"
#include <vector>
#include <string>
#include <algorithm>

namespace WinPreview {

// Render flags used by both rasterizer backends:
// 0x01 = FPDF_ANNOT (render annotations, forms, signatures)
// 0x02 = FPDF_LCD_TEXT (subpixel LCD text anti-aliasing for maximum sharpness)
// NOTE: 0x10 was FPDF_REVERSE_BYTE_ORDER, which erroneously swapped R and B
// bytes. Omitting it preserves native BGRA byte order for B8G8R8A8_UNORM.
static constexpr int kPdfiumRenderFlags = 0x01 | 0x02;


struct NativePdfConfig {
    int version = 2;
    const char** m_pUserFontPaths = nullptr;
    void* m_pIsolate = nullptr;
    unsigned int m_v8EmbedderSlot = 0;
};

PdfEngine::PdfEngine() {}

PdfEngine::~PdfEngine() {
    CloseTextPage();
    CloseDocument();
    if (fn_FPDF_DestroyLibrary) {
        fn_FPDF_DestroyLibrary();
    }
    if (m_hPdfium) {
        FreeLibrary(m_hPdfium);
        m_hPdfium = nullptr;
    }
}

bool PdfEngine::Initialize() {
    if (m_hPdfium) return true;

    // Search paths for pdfium.dll
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }

    std::vector<std::wstring> candidates = {
        exeDir + L"\\pdfium.dll",
        L"pdfium.dll",
        exeDir + L"\\src\\pdfium.dll",
        exeDir + L"\\..\\src\\pdfium.dll"
    };

    for (const auto& path : candidates) {
        m_hPdfium = LoadLibraryW(path.c_str());
        if (m_hPdfium) break;
    }

    if (!m_hPdfium) return false;

    // Resolve Core Functions
    fn_FPDF_InitLibraryWithConfig = (void(*)(const void*))GetProcAddress(m_hPdfium, "FPDF_InitLibraryWithConfig");
    fn_FPDF_DestroyLibrary = (void(*)())GetProcAddress(m_hPdfium, "FPDF_DestroyLibrary");
    fn_FPDF_LoadDocument = (FPDF_DOCUMENT(*)(const char*, const char*))GetProcAddress(m_hPdfium, "FPDF_LoadDocument");
    fn_FPDF_CloseDocument = (void(*)(FPDF_DOCUMENT))GetProcAddress(m_hPdfium, "FPDF_CloseDocument");
    fn_FPDF_GetPageCount = (int(*)(FPDF_DOCUMENT))GetProcAddress(m_hPdfium, "FPDF_GetPageCount");
    fn_FPDF_LoadPage = (FPDF_PAGE(*)(FPDF_DOCUMENT, int))GetProcAddress(m_hPdfium, "FPDF_LoadPage");
    fn_FPDF_ClosePage = (void(*)(FPDF_PAGE))GetProcAddress(m_hPdfium, "FPDF_ClosePage");
    fn_FPDF_GetPageWidthF = (float(*)(FPDF_PAGE))GetProcAddress(m_hPdfium, "FPDF_GetPageWidthF");
    fn_FPDF_GetPageHeightF = (float(*)(FPDF_PAGE))GetProcAddress(m_hPdfium, "FPDF_GetPageHeightF");
    fn_FPDFBitmap_CreateEx = (FPDF_BITMAP(*)(int, int, int, void*, int))GetProcAddress(m_hPdfium, "FPDFBitmap_CreateEx");
    fn_FPDFBitmap_FillRect = (void(*)(FPDF_BITMAP, int, int, int, int, unsigned long))GetProcAddress(m_hPdfium, "FPDFBitmap_FillRect");
    fn_FPDF_RenderPageBitmap = (void(*)(FPDF_BITMAP, FPDF_PAGE, int, int, int, int, int, int))GetProcAddress(m_hPdfium, "FPDF_RenderPageBitmap");
    fn_FPDFBitmap_Destroy = (void(*)(FPDF_BITMAP))GetProcAddress(m_hPdfium, "FPDFBitmap_Destroy");
    fn_FPDFBitmap_GetBuffer = (void*(*)(FPDF_BITMAP))GetProcAddress(m_hPdfium, "FPDFBitmap_GetBuffer");
    fn_FPDFBitmap_GetStride = (int(*)(FPDF_BITMAP))GetProcAddress(m_hPdfium, "FPDFBitmap_GetStride");

    // Resolve Text Functions
    fn_FPDFText_LoadPage = (FPDF_TEXTPAGE(*)(FPDF_PAGE))GetProcAddress(m_hPdfium, "FPDFText_LoadPage");
    fn_FPDFText_ClosePage = (void(*)(FPDF_TEXTPAGE))GetProcAddress(m_hPdfium, "FPDFText_ClosePage");
    fn_FPDFText_CountChars = (int(*)(FPDF_TEXTPAGE))GetProcAddress(m_hPdfium, "FPDFText_CountChars");
    fn_FPDFText_GetCharBox = (int(*)(FPDF_TEXTPAGE, int, double*, double*, double*, double*))GetProcAddress(m_hPdfium, "FPDFText_GetCharBox");
    fn_FPDFText_GetCharIndexAtPos = (int(*)(FPDF_TEXTPAGE, double, double, double, double))GetProcAddress(m_hPdfium, "FPDFText_GetCharIndexAtPos");
    fn_FPDFText_GetText = (int(*)(FPDF_TEXTPAGE, int, int, unsigned short*))GetProcAddress(m_hPdfium, "FPDFText_GetText");
    fn_FPDFText_CountRects = (int(*)(FPDF_TEXTPAGE, int, int))GetProcAddress(m_hPdfium, "FPDFText_CountRects");
    fn_FPDFText_GetRect = (int(*)(FPDF_TEXTPAGE, int, double*, double*, double*, double*))GetProcAddress(m_hPdfium, "FPDFText_GetRect");
    fn_FPDFText_FindStart = (FPDF_SCHHANDLE(*)(FPDF_TEXTPAGE, const unsigned short*, unsigned long, int))GetProcAddress(m_hPdfium, "FPDFText_FindStart");
    fn_FPDFText_FindNext = (int(*)(FPDF_SCHHANDLE))GetProcAddress(m_hPdfium, "FPDFText_FindNext");
    fn_FPDFText_FindPrev = (int(*)(FPDF_SCHHANDLE))GetProcAddress(m_hPdfium, "FPDFText_FindPrev");
    fn_FPDFText_GetSchResultIndex = (int(*)(FPDF_SCHHANDLE))GetProcAddress(m_hPdfium, "FPDFText_GetSchResultIndex");
    fn_FPDFText_GetSchCount = (int(*)(FPDF_SCHHANDLE))GetProcAddress(m_hPdfium, "FPDFText_GetSchCount");
    fn_FPDFText_FindClose = (void(*)(FPDF_SCHHANDLE))GetProcAddress(m_hPdfium, "FPDFText_FindClose");

    // Resolve Outline / Bookmark Functions
    fn_FPDFBookmark_GetFirstChild = (FPDF_BOOKMARK(*)(FPDF_DOCUMENT, FPDF_BOOKMARK))GetProcAddress(m_hPdfium, "FPDFBookmark_GetFirstChild");
    fn_FPDFBookmark_GetNextSibling = (FPDF_BOOKMARK(*)(FPDF_DOCUMENT, FPDF_BOOKMARK))GetProcAddress(m_hPdfium, "FPDFBookmark_GetNextSibling");
    fn_FPDFBookmark_GetTitle = (unsigned long(*)(FPDF_BOOKMARK, void*, unsigned long))GetProcAddress(m_hPdfium, "FPDFBookmark_GetTitle");
    fn_FPDFBookmark_GetDest = (FPDF_DEST(*)(FPDF_DOCUMENT, FPDF_BOOKMARK))GetProcAddress(m_hPdfium, "FPDFBookmark_GetDest");
    fn_FPDFDest_GetDestPageIndex = (int(*)(FPDF_DOCUMENT, FPDF_DEST))GetProcAddress(m_hPdfium, "FPDFDest_GetDestPageIndex");

    if (!fn_FPDF_InitLibraryWithConfig || !fn_FPDF_LoadDocument || !fn_FPDF_RenderPageBitmap) {
        FreeLibrary(m_hPdfium);
        m_hPdfium = nullptr;
        return false;
    }

    NativePdfConfig cfg;
    fn_FPDF_InitLibraryWithConfig(&cfg);
    return true;
}

bool PdfEngine::OpenDocument(const std::wstring& filePath) {
    if (!m_hPdfium) return false;

    std::lock_guard<std::mutex> lock(m_apiMutex);
    CloseDocumentLocked();

    // Convert to UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return false;

    std::string utf8Path(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, &utf8Path[0], utf8Len, NULL, NULL);

    m_document = fn_FPDF_LoadDocument(utf8Path.c_str(), NULL);
    if (!m_document) return false;

    m_pageCount = fn_FPDF_GetPageCount(m_document);
    m_currentFilePath = filePath;
    return true;
}

void PdfEngine::CloseDocument() {
    std::lock_guard<std::mutex> lock(m_apiMutex);
    CloseTextPage();
    if (m_document && fn_FPDF_CloseDocument) {
        fn_FPDF_CloseDocument(m_document);
        m_document = nullptr;
    }
    m_pageCount = 0;
    m_currentFilePath.clear();
}

// Internal variant for callers that already hold m_apiMutex.
void PdfEngine::CloseDocumentLocked() {
    CloseTextPage();
    if (m_document && fn_FPDF_CloseDocument) {
        fn_FPDF_CloseDocument(m_document);
        m_document = nullptr;
    }
    m_pageCount = 0;
    m_currentFilePath.clear();
}

bool PdfEngine::GetPageSize(int pageIndex, float* outWidth, float* outHeight) {
    if (!m_document || pageIndex < 0 || pageIndex >= m_pageCount) return false;

    std::lock_guard<std::mutex> lock(m_apiMutex);
    if (!m_document || pageIndex < 0 || pageIndex >= m_pageCount) return false;

    FPDF_PAGE page = fn_FPDF_LoadPage(m_document, pageIndex);
    if (!page) return false;

    if (outWidth) *outWidth = fn_FPDF_GetPageWidthF(page);
    if (outHeight) *outHeight = fn_FPDF_GetPageHeightF(page);

    fn_FPDF_ClosePage(page);
    return true;
}

bool PdfEngine::RasterizePageLocked(int pageIndex, int w, int h,
                                   std::vector<unsigned char>& pixels, int& stride) {
    pixels.clear();
    stride = 0;
    if (!m_document || w <= 0 || h <= 0) return false;

    FPDF_PAGE page = fn_FPDF_LoadPage(m_document, pageIndex);
    if (!page) return false;

    // 4 = FPDFBitmap_BGRA
    FPDF_BITMAP bitmap = fn_FPDFBitmap_CreateEx(w, h, 4, NULL, 0);
    if (!bitmap) {
        fn_FPDF_ClosePage(page);
        return false;
    }

    // Fill white (ARGB: 0xFFFFFFFF)
    fn_FPDFBitmap_FillRect(bitmap, 0, 0, w, h, 0xFFFFFFFF);
    fn_FPDF_RenderPageBitmap(bitmap, page, 0, 0, w, h, 0, kPdfiumRenderFlags);

    void* buffer = fn_FPDFBitmap_GetBuffer(bitmap);
    stride = fn_FPDFBitmap_GetStride(bitmap);

    bool ok = false;
    if (buffer && stride > 0) {
        // Copy: pdfium may over-allocate/render into memory we don't own, and
        // the caller needs a buffer that outlives the FPDF_BITMAP.
        pixels.resize((size_t)stride * (size_t)h);
        if (stride == w * 4) {
            memcpy(pixels.data(), buffer, (size_t)stride * (size_t)h);
            ok = true;
        } else {
            // Tightly pack row padding out so D2D can consume stride == w*4.
            for (int row = 0; row < h; ++row) {
                memcpy(pixels.data() + (size_t)row * (size_t)w * 4,
                       (const unsigned char*)buffer + (size_t)row * (size_t)stride,
                       (size_t)w * 4);
            }
            stride = w * 4;
            ok = true;
        }
    }

    fn_FPDFBitmap_Destroy(bitmap);
    fn_FPDF_ClosePage(page);
    return ok;
}

bool PdfEngine::RenderPageToMemory(
    int pageIndex,
    int width,
    int height,
    std::vector<unsigned char>& pixels,
    int& stride
) {
    pixels.clear();
    stride = 0;
    if (pageIndex < 0 || pageIndex >= m_pageCount) return false;

    std::lock_guard<std::mutex> lock(m_apiMutex);
    return RasterizePageLocked(pageIndex, width, height, pixels, stride);
}

bool PdfEngine::RenderPageToBitmap(
    int pageIndex,
    float targetWidth,
    float targetHeight,
    ID2D1RenderTarget* renderTarget,
    ID2D1Bitmap** outBitmap
) {
    if (!m_document || !renderTarget || !outBitmap || pageIndex < 0 || pageIndex >= m_pageCount) return false;
    *outBitmap = nullptr;

    int w = (int)targetWidth;
    int h = (int)targetHeight;
    if (w <= 0 || h <= 0) return false;

    std::lock_guard<std::mutex> lock(m_apiMutex);

    std::vector<unsigned char> pixels;
    int stride = 0;
    if (!RasterizePageLocked(pageIndex, w, h, pixels, stride)) return false;

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f, 96.0f
    );

    HRESULT hr = renderTarget->CreateBitmap(D2D1::SizeU(w, h), pixels.data(), stride, props, outBitmap);
    return SUCCEEDED(hr);
}

bool PdfEngine::RenderThumbnail(
    int pageIndex,
    float maxDimension,
    ID2D1RenderTarget* renderTarget,
    ID2D1Bitmap** outBitmap
) {
    float pageW = 0.0f, pageH = 0.0f;
    if (!GetPageSize(pageIndex, &pageW, &pageH)) return false;

    float aspect = pageW / pageH;
    float targetW = maxDimension;
    float targetH = maxDimension;

    if (aspect >= 1.0f) {
        targetH = maxDimension / aspect;
    } else {
        targetW = maxDimension * aspect;
    }

    return RenderPageToBitmap(pageIndex, targetW, targetH, renderTarget, outBitmap);
}

// =========================================================================
// TEXT SELECTION & EXTRACTION
// =========================================================================

bool PdfEngine::LoadTextPage(int pageIndex) {
    std::lock_guard<std::mutex> lock(m_apiMutex);
    if (!m_document || pageIndex < 0 || pageIndex >= m_pageCount) return false;
    if (m_textPageIndex == pageIndex && m_activeTextPage) return true;

    CloseTextPage();

    if (!fn_FPDFText_LoadPage) return false;

    m_activePage = fn_FPDF_LoadPage(m_document, pageIndex);
    if (!m_activePage) return false;

    m_activeTextPage = fn_FPDFText_LoadPage(m_activePage);
    if (!m_activeTextPage) {
        fn_FPDF_ClosePage(m_activePage);
        m_activePage = nullptr;
        return false;
    }

    m_textPageIndex = pageIndex;
    return true;
}

// Note: caller must hold m_apiMutex (CloseDocument/LoadTextPage lock it).
void PdfEngine::CloseTextPage() {
    if (m_activeTextPage && fn_FPDFText_ClosePage) {
        fn_FPDFText_ClosePage(m_activeTextPage);
        m_activeTextPage = nullptr;
    }
    if (m_activePage && fn_FPDF_ClosePage) {
        fn_FPDF_ClosePage(m_activePage);
        m_activePage = nullptr;
    }
    m_textPageIndex = -1;
}

int PdfEngine::GetCharCount() {
    if (!m_activeTextPage || !fn_FPDFText_CountChars) return 0;
    return fn_FPDFText_CountChars(m_activeTextPage);
}

int PdfEngine::GetCharIndexAtPos(double pageX, double pageY, double tolX, double tolY) {
    if (!m_activeTextPage || !fn_FPDFText_GetCharIndexAtPos) return -1;
    return fn_FPDFText_GetCharIndexAtPos(m_activeTextPage, pageX, pageY, tolX, tolY);
}

bool PdfEngine::GetCharBox(int charIndex, double* left, double* right, double* bottom, double* top) {
    if (!m_activeTextPage || !fn_FPDFText_GetCharBox) return false;
    return fn_FPDFText_GetCharBox(m_activeTextPage, charIndex, left, right, bottom, top) != 0;
}

std::vector<D2D1_RECT_F> PdfEngine::GetSelectionRects(int startIndex, int endIndex) {
    std::vector<D2D1_RECT_F> rects;
    if (!m_activeTextPage || !fn_FPDFText_CountRects || !fn_FPDFText_GetRect) return rects;

    int totalChars = GetCharCount();
    if (totalChars <= 0) return rects;

    int start = std::clamp(std::min(startIndex, endIndex), 0, totalChars - 1);
    int end = std::clamp(std::max(startIndex, endIndex), 0, totalChars - 1);
    int count = end - start + 1;
    if (count <= 0) return rects;

    int rectCount = fn_FPDFText_CountRects(m_activeTextPage, start, count);
    rects.reserve(rectCount);

    for (int i = 0; i < rectCount; ++i) {
        double left = 0, top = 0, right = 0, bottom = 0;
        if (fn_FPDFText_GetRect(m_activeTextPage, i, &left, &top, &right, &bottom)) {
            // Note: In PDF page coords, top > bottom.
            rects.push_back(D2D1::RectF((float)left, (float)top, (float)right, (float)bottom));
        }
    }
    return rects;
}

std::wstring PdfEngine::GetSelectedText(int startIndex, int endIndex) {
    if (!m_activeTextPage || !fn_FPDFText_GetText) return L"";

    int totalChars = GetCharCount();
    if (totalChars <= 0) return L"";

    int start = std::clamp(std::min(startIndex, endIndex), 0, totalChars - 1);
    int end = std::clamp(std::max(startIndex, endIndex), 0, totalChars - 1);
    int count = end - start + 1;
    if (count <= 0) return L"";

    std::vector<unsigned short> buffer(count + 2, 0);
    int written = fn_FPDFText_GetText(m_activeTextPage, start, count, buffer.data());
    if (written <= 0) return L"";

    return std::wstring(reinterpret_cast<wchar_t*>(buffer.data()));
}

// =========================================================================
// IN-DOCUMENT SEARCH
// =========================================================================

std::vector<PdfSearchMatch> PdfEngine::Search(const std::wstring& query, bool matchCase, bool wholeWord) {
    std::vector<PdfSearchMatch> matches;
    if (!m_document || query.empty() || !fn_FPDFText_FindStart) return matches;

    std::lock_guard<std::mutex> lock(m_apiMutex);
    if (!m_document) return matches;

    unsigned long flags = 0;
    if (matchCase) flags |= 0x01; // FPDF_MATCHCASE
    if (wholeWord) flags |= 0x02; // FPDF_MATCHWHOLEWORD

    for (int pageIdx = 0; pageIdx < m_pageCount; ++pageIdx) {
        FPDF_PAGE page = fn_FPDF_LoadPage(m_document, pageIdx);
        if (!page) continue;

        FPDF_TEXTPAGE textPage = fn_FPDFText_LoadPage(page);
        if (!textPage) {
            fn_FPDF_ClosePage(page);
            continue;
        }

        FPDF_SCHHANDLE findHandle = fn_FPDFText_FindStart(
            textPage,
            reinterpret_cast<const unsigned short*>(query.c_str()),
            flags,
            0
        );

        if (findHandle) {
            while (fn_FPDFText_FindNext(findHandle)) {
                PdfSearchMatch match;
                match.pageIndex = pageIdx;
                match.charIndex = fn_FPDFText_GetSchResultIndex(findHandle);
                match.charCount = fn_FPDFText_GetSchCount(findHandle);

                int rectCount = fn_FPDFText_CountRects(textPage, match.charIndex, match.charCount);
                for (int r = 0; r < rectCount; ++r) {
                    double left = 0, top = 0, right = 0, bottom = 0;
                    if (fn_FPDFText_GetRect(textPage, r, &left, &top, &right, &bottom)) {
                        match.rects.push_back(D2D1::RectF((float)left, (float)top, (float)right, (float)bottom));
                    }
                }
                matches.push_back(match);
            }
            fn_FPDFText_FindClose(findHandle);
        }

        fn_FPDFText_ClosePage(textPage);
        fn_FPDF_ClosePage(page);
    }

    return matches;
}

// =========================================================================
// TABLE OF CONTENTS / BOOKMARKS OUTLINE
// =========================================================================

void PdfEngine::TraverseBookmarks(FPDF_BOOKMARK parent, int level, std::vector<PdfOutlineItem>& outList) {
    if (!m_document || !fn_FPDFBookmark_GetTitle) return;

    FPDF_BOOKMARK cur = parent ? fn_FPDFBookmark_GetFirstChild(m_document, parent)
                               : fn_FPDFBookmark_GetFirstChild(m_document, NULL);

    while (cur) {
        // Query title size
        unsigned long bytes = fn_FPDFBookmark_GetTitle(cur, NULL, 0);
        std::wstring title;
        if (bytes > 0) {
            std::vector<unsigned char> titleBuf(bytes + 2, 0);
            fn_FPDFBookmark_GetTitle(cur, titleBuf.data(), bytes);
            title = reinterpret_cast<const wchar_t*>(titleBuf.data());
        }

        // Query destination
        int pageIndex = 0;
        if (fn_FPDFBookmark_GetDest && fn_FPDFDest_GetDestPageIndex) {
            FPDF_DEST dest = fn_FPDFBookmark_GetDest(m_document, cur);
            if (dest) {
                pageIndex = fn_FPDFDest_GetDestPageIndex(m_document, dest);
            }
        }

        if (pageIndex < 0) pageIndex = 0;

        PdfOutlineItem item;
        item.title = title.empty() ? L"Untitled" : title;
        item.pageIndex = pageIndex;
        item.level = level;
        outList.push_back(item);

        // Recurse children
        TraverseBookmarks(cur, level + 1, outList);

        // Advance to next sibling
        cur = fn_FPDFBookmark_GetNextSibling ? fn_FPDFBookmark_GetNextSibling(m_document, cur) : nullptr;
    }
}

std::vector<PdfOutlineItem> PdfEngine::GetOutline() {
    std::vector<PdfOutlineItem> outline;
    if (!m_document || !fn_FPDFBookmark_GetFirstChild) return outline;
    TraverseBookmarks(nullptr, 0, outline);
    return outline;
}
} // namespace WinPreview
