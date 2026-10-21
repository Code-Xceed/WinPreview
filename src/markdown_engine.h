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

enum class MdBlockType {
    Header1,
    Header2,
    Header3,
    Paragraph,
    CodeBlock,
    Blockquote,
    BulletItem,
    Divider,
    NotebookCellHeader,
    NotebookCode,
    NotebookOutput
};

struct MdBlock {
    MdBlockType type = MdBlockType::Paragraph;
    std::wstring text;
    std::wstring extra; // e.g. language or cell number
    float height = 0.0f;
};

class MarkdownEngine {
public:
    MarkdownEngine();
    ~MarkdownEngine();

    bool LoadMarkdownFile(const std::wstring& filePath);
    bool LoadNotebookFile(const std::wstring& filePath);
    void Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale);
    void Scroll(float dy);
    bool UpdateAnimation(float dt);
    void Close();

    bool IsLoaded() const { return m_isLoaded; }
    bool IsNotebook() const { return m_isNotebook; }
    std::wstring GetFormatBadge() const;

private:
    void ParseMarkdown(const std::string& rawText);
    void ParseNotebook(const std::string& rawJson);

    bool m_isLoaded = false;
    bool m_isNotebook = false;
    std::wstring m_filePath;
    std::vector<MdBlock> m_blocks;
    float m_scrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    float m_contentHeight = 0.0f;
    UINT32 m_cellCount = 0;
    std::wstring m_kernelName;
};

} // namespace WinPreview
