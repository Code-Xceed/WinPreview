#pragma once
#include "d2d_engine.h"
#include <string>
#include <vector>
#include <algorithm>

namespace WinPreview {

enum class TextFileType {
    PlainText,
    Code,
    Markdown,
    Csv,
    Tsv
};

struct SpreadsheetCell {
    std::wstring text;
    float width = 80.0f;
};

struct TextPos {
    int line = 0;
    int col = 0;

    bool operator==(const TextPos& o) const { return line == o.line && col == o.col; }
    bool operator!=(const TextPos& o) const { return !(*this == o); }
    bool operator<(const TextPos& o) const {
        if (line != o.line) return line < o.line;
        return col < o.col;
    }
    bool operator<=(const TextPos& o) const {
        return *this < o || *this == o;
    }
    bool operator>(const TextPos& o) const {
        return o < *this;
    }
    bool operator>=(const TextPos& o) const {
        return !(*this < o);
    }
};

struct UndoSnapshot {
    std::vector<std::wstring> lines;
    TextPos cursor;
    TextPos anchor;
    // Approximate heap cost of this snapshot, used to keep the stack's total
    // memory bounded instead of just its entry count.
    size_t approxBytes = 0;
};

class TextEngine {
public:
    TextEngine();
    ~TextEngine();

    bool LoadFile(const std::wstring& filePath, IDWriteFactory* dwriteFactory);
    void Close();
    // Marks the max-line-width cache dirty after any line-affecting edit.
    void InvalidateLayoutCache() { m_maxLineWidthDirty = true; }

    bool IsLoaded() const { return m_isLoaded; }
    TextFileType GetFileType() const { return m_fileType; }
    const std::wstring& GetLanguageName() const { return m_languageName; }
    const std::wstring& GetEncodingName() const { return m_encodingName; }
    const std::wstring& GetLineEndingName() const { return m_lineEndingName; }
    int GetLineCount() const { return (int)m_lines.size(); }
    size_t GetCharCount() const { return m_charCount; }
    int64_t GetFileSize() const { return m_fileSizeBytes; }
    int GetRowCount() const { return (int)m_rows.size(); }
    int GetColumnCount() const { return m_columnCount; }
    std::wstring GetAllText() const;

    // Scrolling & Zoom
    void Scroll(float deltaY, float deltaX = 0.0f);
    void PageUp(float viewportHeight);
    void PageDown(float viewportHeight);
    void ScrollToTop();
    void ScrollToBottom(float viewportHeight);
    void SetScrollPosition(float scrollY, float scrollX = 0.0f);
    float GetScrollY() const { return m_scrollY; }
    float GetScrollX() const { return m_scrollX; }
    bool UpdateAnimation(float dt);

    void ZoomIn();
    void ZoomOut();
    void ResetZoom();
    float GetFontSize() const { return m_fontSize; }

    bool IsWordWrap() const { return m_wordWrap; }
    void ToggleWordWrap() { m_wordWrap = !m_wordWrap; }

    bool IsTableView() const { return m_isTableView; }
    void ToggleTableView();

    // Cursor & Selection
    TextPos GetCursor() const { return m_cursor; }
    TextPos GetAnchor() const { return m_anchor; }
    bool HasSelection() const { return m_cursor != m_anchor; }
    TextPos GetSelectionStart() const { return (m_cursor < m_anchor) ? m_cursor : m_anchor; }
    TextPos GetSelectionEnd() const { return (m_cursor < m_anchor) ? m_anchor : m_cursor; }
    std::wstring GetSelectedText() const;
    void SelectAll();
    void ClearSelection();

    // Layout & Dimensions
    float GetGutterWidth(float dpiScale) const;
    float GetHeaderTopPadding(float dpiScale) const { return 52.0f * dpiScale; }
    float GetCharWidth() const { return m_charWidth; }
    float GetLineHeight() const { return m_lineHeight; }
    float GetColumnX(int lineIdx, int col) const;

    // Mouse Selection & Caret Placement
    TextPos PointToPosition(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale) const;
    void HandleClick(float x, float y, const D2D1_RECT_F& viewportRect, bool shiftHeld, float dpiScale);
    void HandleDoubleClick(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale);
    void HandleTripleClick(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale);
    void HandleDrag(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale);
    bool HandleMouseDown(float x, float y, const D2D1_RECT_F& viewportRect);
    bool HandleMouseMove(float x, float y, const D2D1_RECT_F& viewportRect);
    void HandleMouseUp();
    bool IsDraggingScrollbar() const { return m_isDraggingVScroll || m_isDraggingHScroll; }
    bool IsSelectingText() const { return m_isSelectingText; }

    // Keyboard Navigation
    void MoveCursor(int dLine, int dCol, bool select = false);
    void MoveWord(int direction, bool select = false);
    void MoveHome(bool select = false);
    void MoveEnd(bool select = false);
    void ResetCaretBlink();

    // Editing Operations (Mini VS Code)
    bool InsertChar(wchar_t ch);
    bool InsertNewline();
    bool DeleteChar(bool backspace);
    bool DeleteSelection();
    bool Indent(bool unindent = false);
    bool Cut(HWND hwnd);
    bool Copy(HWND hwnd);
    bool Paste(HWND hwnd);

    // Undo / Redo
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }
    void PushUndo();

    // File Persistence (Save on Ctrl+S)
    bool SaveFile();
    bool IsModified() const { return m_isModified; }
    void SetModified(bool mod) { m_isModified = mod; }

    // Status Bar & Indentation
    int GetTabSpaces() const { return m_tabSpaces; }
    void ToggleTabSpaces() { m_tabSpaces = (m_tabSpaces == 4) ? 2 : 4; }
    bool HandleStatusBarClick(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale);

    // Direct2D Rendering
    void RenderText(
        ID2D1RenderTarget* rt,
        D2DEngine& d2d,
        const D2D1_RECT_F& viewportRect,
        float dpiScale
    );

    void RenderSpreadsheet(
        ID2D1RenderTarget* rt,
        D2DEngine& d2d,
        const D2D1_RECT_F& viewportRect,
        float dpiScale
    );

    // Ensure Caret is visible in viewport
    void EnsureCursorVisible(const D2D1_RECT_F& viewportRect, float dpiScale);

private:
    bool m_isLoaded = false;
    std::wstring m_filePath;
    TextFileType m_fileType = TextFileType::PlainText;
    std::wstring m_languageName = L"Plain Text";
    std::wstring m_encodingName = L"UTF-8";
    std::wstring m_lineEndingName = L"CRLF";
    size_t m_charCount = 0;
    int64_t m_fileSizeBytes = 0;

    // Text Lines
    std::vector<std::wstring> m_lines;
    float m_maxLineWidth = 0.0f;
    // Cache of the widest line (by approximate char count) - recomputed only when the
    // document changes, never per rendered frame.
    bool m_maxLineWidthDirty = true;
    void RecomputeMaxLineWidth();

    // Spreadsheet Rows & Columns
    std::vector<std::vector<std::wstring>> m_rows;
    std::vector<float> m_columnWidths;
    int m_columnCount = 0;
    float m_totalTableWidth = 0.0f;

    // View Options
    float m_fontSize = 13.0f;
    bool m_wordWrap = false;
    bool m_isTableView = true; // For CSV/TSV
    int m_tabSpaces = 4;

    // Smooth Scrolling
    float m_scrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    float m_scrollX = 0.0f;
    float m_targetScrollX = 0.0f;
    float m_lineHeight = 22.0f;
    float m_charWidth = 8.0f;
    float m_gutterWidth = 48.0f;

    // Editor Caret & Selection State
    TextPos m_cursor = { 0, 0 };
    TextPos m_anchor = { 0, 0 };
    float m_caretBlinkTimer = 0.0f;
    bool m_caretVisible = true;
    bool m_isSelectingText = false;
    bool m_isModified = false;

    // Undo / Redo Stacks
    std::vector<UndoSnapshot> m_undoStack;
    std::vector<UndoSnapshot> m_redoStack;

    // Contiguous typing/backspace runs coalesce into a single snapshot so a
    // burst of 100 keystrokes costs one undo step, not one hundred.
    unsigned long long m_lastEditTick = 0; // GetTickCount64() of last coalescable edit
    int m_lastEditKind = 0;                // 0 none, 1 typed char, 2 backspace, 3 delete
    TextPos m_lastEditCursorAfter;         // cursor right after that edit
    bool ShouldCoalesceUndo(int kind) const;
    void NoteUndoEdit(int kind);

    // Scrollbar Dragging
    bool m_isDraggingVScroll = false;
    bool m_isDraggingHScroll = false;
    float m_dragStartY = 0.0f;
    float m_dragStartX = 0.0f;
    float m_initialScrollY = 0.0f;
    float m_initialScrollX = 0.0f;

    // Status bar clickable regions (cached for hit test)
    D2D1_RECT_F m_statusSpacesRect = {};
    D2D1_RECT_F m_statusWrapRect = {};
    D2D1_RECT_F m_statusSaveRect = {};

    // DirectWrite Resources
    IDWriteTextFormat* m_fontCustom = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    float m_lastCachedFontSize = 0.0f;

    // Cached Direct2D Brushes (reused across frames to eliminate per-frame allocations)
    ID2D1RenderTarget* m_lastRenderTarget = nullptr;
    ID2D1SolidColorBrush* m_brComment = nullptr;
    ID2D1SolidColorBrush* m_brString = nullptr;
    ID2D1SolidColorBrush* m_brKeyword = nullptr;
    ID2D1SolidColorBrush* m_brActiveLine = nullptr;
    ID2D1SolidColorBrush* m_brSelection = nullptr;
    ID2D1SolidColorBrush* m_brGutterBg = nullptr;
    ID2D1SolidColorBrush* m_brStatusBg = nullptr;

    void EnsureBrushes(ID2D1RenderTarget* rt);
    void ReleaseBrushes();
    void EnsureFont(float dpiScale);
    void ParseCsv(const std::wstring& content, wchar_t delimiter);
    void DetectLanguage(const std::wstring& ext, const std::wstring& filename = L"");
    void MeasureTableColumns(IDWriteFactory* dwriteFactory, float dpiScale);
};

} // namespace WinPreview
