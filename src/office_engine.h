#pragma once
#ifndef WINPREVIEW_OFFICE_ENGINE_H
#define WINPREVIEW_OFFICE_ENGINE_H

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

enum class OfficeKind {
    None,
    Word,       // .docx / .odt
    Sheet,      // .xlsx / .ods  (next increment)
    Slide,      // .pptx / .odp  (next increment)
    Legacy      // .doc / .xls / .ppt: no parser, caller keeps the thumbnail
};

enum class OfficeFormat {
    None,
    Docx,
    Odt,
    Xlsx,
    Ods,
    Pptx,
    Odp
};

enum class OfficeBlockType {
    Heading1,
    Heading2,
    Heading3,
    Paragraph,
    Bullet,
    Numbered,
    Table,
    Quote
};

struct OfficeTableRow {
    std::vector<std::wstring> cells;
    bool header = false;
};

struct OfficeBlock {
    OfficeBlockType type = OfficeBlockType::Paragraph;
    std::wstring text;
    int level = 0;                              // list indent level
    std::vector<OfficeTableRow> rows;           // Table only
    float height = 0.0f;                        // measured while rendering
};

struct OfficeCell {
    std::wstring text;
    bool numeric = false;
};

struct OfficeSlide {
    std::wstring title;
    std::vector<OfficeBlock> blocks;
};

struct OfficeSheet {
    std::wstring name;
    std::vector<std::vector<OfficeCell>> rows;
    size_t columns = 0;
};

class OfficeEngine {
public:
    OfficeEngine();
    ~OfficeEngine();

    // Loads a Word/OpenDocument text document. Returns false for formats this
    // increment does not parse (sheets, slides, legacy binary formats).
    bool LoadDocument(const std::wstring& filePath);

    void Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale);
    void Scroll(float dy);
    bool UpdateAnimation(float dt);
    void Close();

    bool IsLoaded() const { return m_isLoaded; }
    OfficeKind GetKind() const { return m_kind; }

    // Sheets
    int SheetCount() const { return (int)m_sheets.size(); }
    int ActiveSheet() const { return m_activeSheet; }
    void SetActiveSheet(int index);
    std::wstring SheetName(int index) const;

    // Slides
    int SlideCount() const { return (int)m_slides.size(); }
    int ActiveSlide() const { return m_activeSlide; }
    void SetActiveSlide(int index);
    std::wstring SlideTitle(int index) const;
    bool GetSlideRect(int index, D2D1_RECT_F& out) const;
    int HitTestSlide(float x, float y) const;
    const OfficeSheet* GetSheet(int index) const {
        if (index < 0 || index >= (int)m_sheets.size()) return nullptr;
        return &m_sheets[(size_t)index];
    }
    void ScrollHorizontal(float dx);
    int HitTestSheetTab(float x, float y) const;
    bool GetSheetTabRect(int index, D2D1_RECT_F& out) const;
    OfficeFormat GetFormat() const { return m_format; }
    const std::wstring& GetTitle() const { return m_title; }
    const std::wstring& GetErrorText() const { return m_errorText; }
    std::wstring GetFormatBadge() const;
    std::wstring GetPlainText() const;

    UINT32 ParagraphCount() const { return m_paragraphCount; }
    UINT32 TableCount() const { return m_tableCount; }
    UINT32 HeadingCount() const { return m_headingCount; }

    float GetContentHeight() const { return m_contentHeight; }

private:
    void AddParagraphText(const std::wstring& text, const std::wstring& style, bool listItem, int level);
    void FinishDocument();
    bool PreparePackage(const std::wstring& filePath);
    bool ReadPart(const std::wstring& filePath, const std::wstring& entry, size_t maxBytes,
                  std::string& out, std::wstring& error);
    bool LoadXlsx(const std::wstring& filePath);
    bool LoadOds(const std::wstring& filePath);
    bool LoadPptx(const std::wstring& filePath);
    bool LoadOdp(const std::wstring& filePath);
    void RenderSlides(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& rect,
                      float marginX, float contentW, float curY);
    void RenderWord(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& rect, float marginX, float contentW, float curY);
    void RenderSheet(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& rect, float marginX, float contentW, float curY);

    bool m_isLoaded = false;
    OfficeKind m_kind = OfficeKind::None;
    OfficeFormat m_format = OfficeFormat::None;
    std::wstring m_filePath;
    std::wstring m_title;
    std::wstring m_errorText;
    std::wstring m_packageDir;
    std::vector<OfficeBlock> m_blocks;
    std::vector<OfficeSheet> m_sheets;
    int m_activeSheet = 0;
    std::vector<OfficeSlide> m_slides;
    int m_activeSlide = 0;
    std::vector<std::pair<D2D1_RECT_F, int>> m_slideRects;
    std::vector<std::pair<D2D1_RECT_F, int>> m_sheetTabRects;
    float m_scrollX = 0.0f;
    float m_targetScrollX = 0.0f;
    float m_sheetContentWidth = 0.0f;
    UINT32 m_paragraphCount = 0;
    UINT32 m_tableCount = 0;
    UINT32 m_headingCount = 0;

    void EnsureFormats(D2DEngine& d2d);
    void ReleaseFormats();

    IDWriteTextFormat* m_formatBody = nullptr;
    IDWriteTextFormat* m_formatH1 = nullptr;
    IDWriteTextFormat* m_formatH2 = nullptr;
    IDWriteTextFormat* m_formatH3 = nullptr;

    float m_scrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    float m_contentHeight = 0.0f;
};

// Diagnostic: prints the parsed block list for a document (dev/CI tool).
int RunOfficeDump(const std::wstring& filePath);

} // namespace WinPreview

#endif // WINPREVIEW_OFFICE_ENGINE_H
