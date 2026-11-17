#include "office_engine.h"
#include "archive_engine.h"
#include "d2d_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwchar>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace WinPreview {
namespace {

// ---------------------------------------------------------------------------
// Minimal tolerant XML reader. OOXML/ODF parts are plain XML with no DTDs, so
// a small DOM (elements, attributes, text) is enough and keeps the binary free
// of a third-party parser.
// ---------------------------------------------------------------------------

struct XmlNode {
    std::wstring name;
    std::wstring text;
    std::vector<std::pair<std::wstring, std::wstring>> attrs;
    std::vector<std::unique_ptr<XmlNode>> children;

    const std::wstring* Attr(const wchar_t* key) const {
        for (const auto& attr : attrs) {
            if (attr.first == key) return &attr.second;
        }
        return nullptr;
    }
};

std::wstring DecodeEntities(const std::wstring& in) {
    if (in.find(L'&') == std::wstring::npos) return in;
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != L'&') {
            out.push_back(in[i]);
            continue;
        }
        size_t semi = in.find(L';', i + 1);
        if (semi == std::wstring::npos || semi - i > 10) {
            out.push_back(in[i]);
            continue;
        }
        std::wstring entity = in.substr(i + 1, semi - i - 1);
        if (entity == L"amp") out.push_back(L'&');
        else if (entity == L"lt") out.push_back(L'<');
        else if (entity == L"gt") out.push_back(L'>');
        else if (entity == L"quot") out.push_back(L'"');
        else if (entity == L"apos") out.push_back(L'\'');
        else if (entity == L"nbsp") out.push_back(L' ');
        else if (!entity.empty() && entity[0] == L'#') {
            long value = 0;
            bool hex = entity.size() > 1 && (entity[1] == L'x' || entity[1] == L'X');
            for (size_t k = hex ? 2 : 1; k < entity.size(); ++k) {
                wchar_t ch = entity[k];
                int digit = (ch >= L'0' && ch <= L'9') ? ch - L'0'
                          : (ch >= L'a' && ch <= L'f') ? ch - L'a' + 10
                          : (ch >= L'A' && ch <= L'F') ? ch - L'A' + 10 : -1;
                if (digit < 0) { value = -1; break; }
                value = value * (hex ? 16 : 10) + digit;
                if (value > 0x10FFFF) { value = -1; break; }
            }
            if (value > 0) out.push_back((wchar_t)value);
        } else {
            out.append(in, i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

bool ParseXml(const std::string& utf8, XmlNode& root, size_t maxNodes, size_t maxDepth) {
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (wideLen <= 0) return false;
    std::wstring text((size_t)wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &text[0], wideLen);

    root.name = L"#document";
    std::vector<XmlNode*> stack;
    stack.push_back(&root);
    size_t nodes = 0;
    size_t i = 0;

    auto skipPast = [&](const wchar_t* terminator) {
        size_t pos = text.find(terminator, i);
        i = (pos == std::wstring::npos) ? text.size() : pos + wcslen(terminator);
    };

    while (i < text.size()) {
        if (text[i] != L'<') {
            size_t next = text.find(L'<', i);
            if (next == std::wstring::npos) next = text.size();
            if (next > i && !stack.empty()) {
                auto textNode = std::make_unique<XmlNode>();
                textNode->name = L"#text";
                textNode->text = text.substr(i, next - i);
                stack.back()->children.push_back(std::move(textNode));
            }
            i = next;
            continue;
        }

        if (text.compare(i, 4, L"<!--") == 0) { i += 4; skipPast(L"-->"); continue; }
        if (text.compare(i, 2, L"<?") == 0) { i += 2; skipPast(L"?>"); continue; }
        if (text.compare(i, 9, L"<![CDATA[") == 0) {
            i += 9;
            size_t end = text.find(L"]]>", i);
            if (end == std::wstring::npos) end = text.size();
            if (!stack.empty()) {
                auto textNode = std::make_unique<XmlNode>();
                textNode->name = L"#text";
                textNode->text = text.substr(i, end - i);
                stack.back()->children.push_back(std::move(textNode));
            }
            i = (end + 3 <= text.size()) ? end + 3 : text.size();
            continue;
        }
        if (text.compare(i, 2, L"<!") == 0) { i += 2; skipPast(L">"); continue; }
        if (text.compare(i, 2, L"</") == 0) {
            i += 2;
            size_t end = text.find(L'>', i);
            if (end == std::wstring::npos) break;
            if (stack.size() > 1) stack.pop_back();
            i = end + 1;
            continue;
        }

        // Opening tag.
        ++i;
        size_t nameStart = i;
        while (i < text.size() && !iswspace(text[i]) && text[i] != L'>' && text[i] != L'/') ++i;
        if (i == nameStart) { i = text.find(L'>', i); if (i == std::wstring::npos) break; ++i; continue; }

        if (stack.size() > maxDepth || nodes >= maxNodes) {
            // Too big to be a useful preview; stop early with what we have.
            break;
        }

        auto node = std::make_unique<XmlNode>();
        node->name = text.substr(nameStart, i - nameStart);
        bool selfClosing = false;

        // Attributes.
        while (i < text.size()) {
            while (i < text.size() && iswspace(text[i])) ++i;
            if (i >= text.size()) break;
            if (text[i] == L'/') { selfClosing = true; ++i; continue; }
            if (text[i] == L'>') { ++i; break; }
            size_t attrStart = i;
            while (i < text.size() && !iswspace(text[i]) && text[i] != L'=' && text[i] != L'>' && text[i] != L'/') ++i;
            if (i == attrStart) { ++i; continue; }
            std::wstring attrName = text.substr(attrStart, i - attrStart);
            while (i < text.size() && iswspace(text[i])) ++i;
            if (i < text.size() && text[i] == L'=') {
                ++i;
                while (i < text.size() && iswspace(text[i])) ++i;
                if (i < text.size() && (text[i] == L'"' || text[i] == L'\'')) {
                    wchar_t quote = text[i++];
                    size_t valueStart = i;
                    while (i < text.size() && text[i] != quote) ++i;
                    if (node->attrs.size() < 32) {
                        node->attrs.emplace_back(attrName, DecodeEntities(text.substr(valueStart, i - valueStart)));
                    }
                    if (i < text.size()) ++i;
                }
            }
        }

        XmlNode* raw = node.get();
        ++nodes;
        stack.back()->children.push_back(std::move(node));
        if (!selfClosing) stack.push_back(raw);
    }

    // Decode text once per node (text lives in "#text" children).
    std::vector<XmlNode*> pending;
    pending.push_back(&root);
    while (!pending.empty()) {
        XmlNode* node = pending.back();
        pending.pop_back();
        node->text = DecodeEntities(node->text);
        for (auto& child : node->children) pending.push_back(child.get());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Tree helpers
// ---------------------------------------------------------------------------

const XmlNode* FindChild(const XmlNode* node, const wchar_t* name) {
    if (!node) return nullptr;
    for (const auto& child : node->children) {
        if (child->name == name) return child.get();
    }
    return nullptr;
}

const XmlNode* FindDescendant(const XmlNode* node, const wchar_t* name, int depth = 6) {
    if (!node || depth <= 0) return nullptr;
    for (const auto& child : node->children) {
        if (child->name == name) return child.get();
        if (const XmlNode* nested = FindDescendant(child.get(), name, depth - 1)) return nested;
    }
    return nullptr;
}

// Concatenated text of direct "#text" children.
std::wstring DirectText(const XmlNode* node) {
    std::wstring out;
    if (!node) return out;
    for (const auto& child : node->children) {
        if (child->name == L"#text") out += child->text;
    }
    return out;
}

// All text below a node, in document order.
std::wstring TextContent(const XmlNode* node) {
    std::wstring out;
    if (!node) return out;
    for (const auto& child : node->children) {
        if (child->name == L"#text") out += child->text;
        else out += TextContent(child.get());
    }
    return out;
}

bool HasChild(const XmlNode* node, const wchar_t* name) {
    return FindChild(node, name) != nullptr;
}

std::wstring CollapseWhitespace(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    bool pendingSpace = false;
    for (wchar_t ch : in) {
        if (ch == L'\r') continue;
        if (ch == L'\n') { out.push_back(L'\n'); pendingSpace = false; continue; }
        if (ch == L' ' || ch == L'\t') { pendingSpace = true; continue; }
        if (pendingSpace) {
            if (!out.empty() && out.back() != L'\n') out.push_back(L' ');
            pendingSpace = false;
        }
        out.push_back(ch);
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

bool StartsWithNoCase(const std::wstring& text, const wchar_t* prefix) {
    size_t n = wcslen(prefix);
    if (text.size() < n) return false;
    return _wcsnicmp(text.c_str(), prefix, n) == 0;
}

int HeadingLevelFromStyle(const std::wstring& style) {
    if (style.empty()) return 0;
    if (_wcsicmp(style.c_str(), L"Title") == 0) return 1;
    if (StartsWithNoCase(style, L"Heading")) {
        int level = _wtoi(style.c_str() + 7);
        if (level >= 1 && level <= 6) return level;
    }
    if (StartsWithNoCase(style, L"berschrift")) {  // German Word style names
        int level = _wtoi(style.c_str() + 10);
        if (level >= 1 && level <= 6) return level;
    }
    return 0;
}

int HeadingLevelFromOutline(const XmlNode* pPr) {
    if (!pPr) return 0;
    const XmlNode* outline = FindChild(pPr, L"w:outlineLvl");
    if (outline) {
        const std::wstring* value = outline->Attr(L"w:val");
        if (value) {
            int level = _wtoi(value->c_str()) + 1;
            if (level >= 1 && level <= 6) return level;
        }
    }
    return 0;
}

// Word: collect visible text from runs (w:t, w:tab, w:br), ignoring field codes.
void CollectWordText(const XmlNode* node, std::wstring& out) {
    for (const auto& child : node->children) {
        const std::wstring& name = child->name;
        if (name == L"#text") {
            out += child->text;
        } else if (name == L"w:t") {
            out += DirectText(child.get());
        } else if (name == L"w:tab") {
            out.push_back(L'\t');
        } else if (name == L"w:br" || name == L"w:cr") {
            out.push_back(L'\n');
        } else if (name == L"w:noBreakHyphen") {
            out.push_back(L'-');
        } else if (name == L"w:instrText" || name == L"w:delText") {
            continue;  // field code, deleted text
        } else if (name == L"w:del") {
            continue;
        } else {
            CollectWordText(child.get(), out);
        }
    }
}

// ODF: text:s carries a repeat count, text:tab / text:line-break are elements.
void CollectOdfText(const XmlNode* node, std::wstring& out) {
    for (const auto& child : node->children) {
        const std::wstring& name = child->name;
        if (name == L"#text") {
            out += child->text;
        } else if (name == L"text:s") {
            int count = 1;
            if (const std::wstring* c = child->Attr(L"text:c")) count = (std::max)(1, _wtoi(c->c_str()));
            out.append((size_t)(std::min)(count, 64), L' ');
        } else if (name == L"text:tab") {
            out.push_back(L'\t');
        } else if (name == L"text:line-break") {
            out.push_back(L'\n');
        } else if (name == L"text:note") {
            out += L" [note]";
        } else if (name == L"office:annotation") {
            continue;  // comments are not body text
        } else {
            CollectOdfText(child.get(), out);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// OfficeEngine
// ---------------------------------------------------------------------------

OfficeEngine::OfficeEngine() {}

OfficeEngine::~OfficeEngine() {
    Close();
}

void OfficeEngine::Close() {
    ReleaseFormats();
    m_isLoaded = false;
    m_kind = OfficeKind::None;
    m_format = OfficeFormat::None;
    m_filePath.clear();
    m_title.clear();
    m_errorText.clear();
    m_blocks.clear();
    m_paragraphCount = 0;
    m_tableCount = 0;
    m_headingCount = 0;
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
    m_contentHeight = 0.0f;
}

void OfficeEngine::AddParagraphText(const std::wstring& text, const std::wstring& style,
                                    bool listItem, int level) {
    std::wstring clean = CollapseWhitespace(text);
    if (clean.empty()) return;

    OfficeBlock block;
    int heading = HeadingLevelFromStyle(style);
    if (heading == 1) block.type = OfficeBlockType::Heading1;
    else if (heading == 2) block.type = OfficeBlockType::Heading2;
    else if (heading >= 3) block.type = OfficeBlockType::Heading3;
    else if (listItem) block.type = (style == L"Quote") ? OfficeBlockType::Quote : OfficeBlockType::Bullet;
    else block.type = OfficeBlockType::Paragraph;
    block.text = clean;
    block.level = (std::max)(0, (std::min)(level, 4));

    if (block.type == OfficeBlockType::Heading1 || block.type == OfficeBlockType::Heading2 ||
        block.type == OfficeBlockType::Heading3) {
        ++m_headingCount;
    } else {
        ++m_paragraphCount;
    }
    m_blocks.push_back(std::move(block));
}

void OfficeEngine::FinishDocument() {
    if (m_title.empty()) {
        size_t slash = m_filePath.find_last_of(L"\\/");
        m_title = (slash == std::wstring::npos) ? m_filePath : m_filePath.substr(slash + 1);
    }
    m_isLoaded = true;
}

bool OfficeEngine::LoadDocument(const std::wstring& filePath) {
    Close();
    m_filePath = filePath;

    size_t dot = filePath.find_last_of(L'.');
    std::wstring ext = (dot == std::wstring::npos) ? L"" : filePath.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    ArchiveEngine archive;
    std::string xml;
    std::wstring error;

    if (ext == L".docx") {
        m_format = OfficeFormat::Docx;
        m_kind = OfficeKind::Word;
        if (!archive.ReadEntry(filePath, L"word/document.xml", 64u * 1024 * 1024, xml, error)) {
            m_errorText = error.empty() ? L"This document could not be read." : error;
            return false;
        }
    } else if (ext == L".odt") {
        m_format = OfficeFormat::Odt;
        m_kind = OfficeKind::Word;
        if (!archive.ReadEntry(filePath, L"content.xml", 64u * 1024 * 1024, xml, error)) {
            m_errorText = error.empty() ? L"This document could not be read." : error;
            return false;
        }
    } else if (ext == L".doc" || ext == L".xls" || ext == L".ppt") {
        m_kind = OfficeKind::Legacy;
        m_errorText = L"Legacy binary Office files have no text preview yet.";
        return false;
    } else {
        return false;  // sheets/slides arrive in the next increment
    }

    XmlNode root;
    if (!ParseXml(xml, root, 400000, 200)) {
        m_errorText = L"This document's XML could not be parsed.";
        return false;
    }

    if (m_kind == OfficeKind::Word && m_format == OfficeFormat::Docx) {
        const XmlNode* document = FindChild(&root, L"w:document");
        const XmlNode* body = document ? FindChild(document, L"w:body") : nullptr;
        if (!body) {
            m_errorText = L"This document has no body.";
            return false;
        }

        for (const auto& child : body->children) {
            if (child->name == L"w:p") {
                const XmlNode* pPr = FindChild(child.get(), L"w:pPr");
                std::wstring style;
                if (pPr) {
                    if (const XmlNode* pStyle = FindChild(pPr, L"w:pStyle")) {
                        if (const std::wstring* value = pStyle->Attr(L"w:val")) style = *value;
                    }
                }
                bool listItem = pPr && HasChild(pPr, L"w:numPr");
                int level = 0;
                if (pPr) {
                    if (const XmlNode* ind = FindChild(pPr, L"w:ind")) {
                        if (const std::wstring* left = ind->Attr(L"w:left")) {
                            level = (std::min)(4, (std::max)(0, _wtoi(left->c_str()) / 720));
                        }
                    }
                }
                if (style.empty()) {
                    int outline = HeadingLevelFromOutline(pPr);
                    if (outline == 1) style = L"Heading1";
                    else if (outline == 2) style = L"Heading2";
                    else if (outline >= 3) style = L"Heading3";
                }
                std::wstring text;
                CollectWordText(child.get(), text);
                AddParagraphText(text, style, listItem, level);
            } else if (child->name == L"w:tbl") {
                OfficeBlock table;
                table.type = OfficeBlockType::Table;
                for (const auto& rowNode : child->children) {
                    if (rowNode->name != L"w:tr") continue;
                    OfficeTableRow row;
                    for (const auto& cellNode : rowNode->children) {
                        if (cellNode->name != L"w:tc") continue;
                        std::wstring cellText;
                        for (const auto& cellChild : cellNode->children) {
                            if (cellChild->name == L"w:p") {
                                std::wstring paragraphText;
                                CollectWordText(cellChild.get(), paragraphText);
                                paragraphText = CollapseWhitespace(paragraphText);
                                if (!paragraphText.empty()) {
                                    if (!cellText.empty()) cellText += L" ";
                                    cellText += paragraphText;
                                }
                            }
                        }
                        row.cells.push_back(cellText);
                    }
                    if (!row.cells.empty()) {
                        if (table.rows.empty()) row.header = true;
                        table.rows.push_back(std::move(row));
                    }
                }
                if (!table.rows.empty()) {
                    ++m_tableCount;
                    m_blocks.push_back(std::move(table));
                }
            }
        }
    } else if (m_kind == OfficeKind::Word && m_format == OfficeFormat::Odt) {
        const XmlNode* doc = FindChild(&root, L"office:document-content");
        const XmlNode* body = doc ? FindChild(doc, L"office:body") : nullptr;
        const XmlNode* textNode = body ? FindChild(body, L"office:text") : nullptr;
        if (!textNode) {
            m_errorText = L"This document has no text body.";
            return false;
        }

        // Walk ODF blocks: headings, paragraphs, lists (with nesting) and tables.
        std::function<void(const XmlNode*, int)> walk = [&](const XmlNode* parent, int listLevel) {
            for (const auto& child : parent->children) {
                if (child->name == L"text:h") {
                    int level = 1;
                    if (const std::wstring* value = child->Attr(L"text:outline-level")) {
                        level = (std::max)(1, (std::min)(6, _wtoi(value->c_str())));
                    }
                    std::wstring text;
                    CollectOdfText(child.get(), text);
                    std::wstring style = (level == 1) ? L"Heading1" : (level == 2 ? L"Heading2" : L"Heading3");
                    AddParagraphText(text, style, false, 0);
                } else if (child->name == L"text:p") {
                    std::wstring text;
                    CollectOdfText(child.get(), text);
                    AddParagraphText(text, listLevel > 0 ? L"ListParagraph" : L"", listLevel > 0, listLevel);
                } else if (child->name == L"text:list") {
                    for (const auto& item : child->children) {
                        if (item->name == L"text:list-item") walk(item.get(), listLevel + 1);
                    }
                } else if (child->name == L"table:table") {
                    OfficeBlock table;
                    table.type = OfficeBlockType::Table;
                    for (const auto& rowNode : child->children) {
                        if (rowNode->name != L"table:table-row") continue;
                        OfficeTableRow row;
                        for (const auto& cellNode : rowNode->children) {
                            if (cellNode->name != L"table:table-cell") continue;
                            std::wstring cellText;
                            for (const auto& cellChild : cellNode->children) {
                                if (cellChild->name == L"text:p") {
                                    std::wstring paragraphText;
                                    CollectOdfText(cellChild.get(), paragraphText);
                                    paragraphText = CollapseWhitespace(paragraphText);
                                    if (!paragraphText.empty()) {
                                        if (!cellText.empty()) cellText += L" ";
                                        cellText += paragraphText;
                                    }
                                }
                            }
                            row.cells.push_back(cellText);
                        }
                        if (!row.cells.empty()) {
                            if (table.rows.empty()) row.header = true;
                            table.rows.push_back(std::move(row));
                        }
                    }
                    if (!table.rows.empty()) {
                        ++m_tableCount;
                        m_blocks.push_back(std::move(table));
                    }
                } else if (child->name == L"text:section" || child->name == L"text:list-header") {
                    walk(child.get(), listLevel);
                }
            }
        };
        walk(textNode, 0);
    }

    // Optional document title from the package metadata.
    std::string core;
    std::wstring coreError;
    const wchar_t* coreEntry = (m_format == OfficeFormat::Docx) ? L"docProps/core.xml" : L"meta.xml";
    if (archive.ReadEntry(filePath, coreEntry, 1024 * 1024, core, coreError)) {
        XmlNode meta;
        if (ParseXml(core, meta, 5000, 30)) {
            const XmlNode* title = FindDescendant(&meta, L"dc:title", 6);
            if (!title) title = FindDescendant(&meta, L"meta:title", 6);
            if (title) {

                std::wstring value = CollapseWhitespace(TextContent(title));
                if (!value.empty()) m_title = value;
            }
        }
    }

    if (m_blocks.empty()) {
        m_errorText = L"This document has no readable text.";
        return false;
    }

    FinishDocument();
    return true;
}

void OfficeEngine::Scroll(float dy) {
    float maxScroll = (std::max)(0.0f, m_contentHeight - 400.0f);
    m_scrollY += dy;
    if (m_scrollY > 0.0f) m_scrollY = 0.0f;
    if (m_scrollY < -maxScroll) m_scrollY = -maxScroll;
    m_targetScrollY = m_scrollY;
}

bool OfficeEngine::UpdateAnimation(float dt) {
    if (m_scrollY != m_targetScrollY) {
        m_scrollY = m_targetScrollY;
        return true;
    }
    return false;
}

std::wstring OfficeEngine::GetPlainText() const {
    std::wstring out;
    for (const auto& block : m_blocks) {
        if (block.type == OfficeBlockType::Table) {
            for (const auto& row : block.rows) {
                for (size_t i = 0; i < row.cells.size(); ++i) {
                    if (i) out.push_back(L'\t');
                    out += row.cells[i];
                }
                out.push_back(L'\n');
            }
            continue;
        }
        if (block.type == OfficeBlockType::Bullet) out += L"• ";
        out += block.text;
        out.push_back(L'\n');
    }
    return out;
}

std::wstring OfficeEngine::GetFormatBadge() const {
    const wchar_t* name = L"DOCUMENT";
    switch (m_format) {
        case OfficeFormat::Docx: name = L"WORD DOCUMENT"; break;
        case OfficeFormat::Odt: name = L"OPENDOCUMENT TEXT"; break;
        case OfficeFormat::Xlsx: name = L"EXCEL WORKBOOK"; break;
        case OfficeFormat::Ods: name = L"OPENDOCUMENT SHEET"; break;
        case OfficeFormat::Pptx: name = L"PRESENTATION"; break;
        case OfficeFormat::Odp: name = L"OPENDOCUMENT SLIDES"; break;
        default: break;
    }

    if (!m_isLoaded) {
        return std::wstring(name) + (m_errorText.empty() ? L"" : L" • " + m_errorText);
    }

    WCHAR buffer[192];
    swprintf_s(buffer, L"%s • %u PARAGRAPHS • %u HEADINGS • %u TABLES",
               name, m_paragraphCount, m_headingCount, m_tableCount);
    return buffer;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

namespace {

float MeasureLayout(IDWriteTextLayout* layout) {
    if (!layout) return 0.0f;
    DWRITE_TEXT_METRICS metrics = {};
    layout->GetMetrics(&metrics);
    return metrics.height;
}

} // namespace

void OfficeEngine::ReleaseFormats() {
    if (m_formatBody) { m_formatBody->Release(); m_formatBody = nullptr; }
    if (m_formatH1) { m_formatH1->Release(); m_formatH1 = nullptr; }
    if (m_formatH2) { m_formatH2->Release(); m_formatH2 = nullptr; }
    if (m_formatH3) { m_formatH3->Release(); m_formatH3 = nullptr; }
}

void OfficeEngine::EnsureFormats(D2DEngine& d2d) {
    if (m_formatBody) return;
    IDWriteFactory* factory = d2d.GetDWriteFactory();
    if (!factory) return;

    // Match the app's body family so a document looks like the rest of the UI,
    // but with a real hierarchy: the shared title/hero formats are built for
    // centred hero text and carry a far/centre paragraph alignment.
    WCHAR family[128] = L"Segoe UI";
    if (d2d.FontBody) {
        UINT32 length = d2d.FontBody->GetFontFamilyNameLength();
        if (length > 0 && length < 127) {
            d2d.FontBody->GetFontFamilyName(family, length + 1);
        }
    }

    factory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"", &m_formatBody);
    factory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, 30.0f, L"", &m_formatH1);
    factory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, 22.0f, L"", &m_formatH2);
    factory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"", &m_formatH3);

    for (IDWriteTextFormat* format : { m_formatBody, m_formatH1, m_formatH2, m_formatH3 }) {
        if (!format) continue;
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
}

void OfficeEngine::Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale) {
    if (!rt || !m_isLoaded) return;
    EnsureFormats(d2d);

    float clientW = targetRect.right - targetRect.left;
    float marginX = (std::max)(40.0f, (clientW - 780.0f) / 2.0f);
    float contentW = clientW - marginX * 2.0f;

    float curY = targetRect.top + 32.0f + m_scrollY;

    // Header: icon + title + file path, then a chip row.
    {
        float iconSize = 44.0f;
        float iconX = marginX;
        D2D1_ROUNDED_RECT iconBox = D2D1::RoundedRect(
            D2D1::RectF(iconX, curY, iconX + iconSize, curY + iconSize), 10.0f, 10.0f);
        rt->FillRoundedRectangle(iconBox, d2d.BrushCard);
        rt->DrawRoundedRectangle(iconBox, d2d.BrushBorder, 1.0f);
        d2d.DrawIconFileDocument(iconX + iconSize / 2, curY + iconSize / 2, 22.0f, d2d.BrushAccent, rt);

        float textX = iconX + iconSize + 18.0f;
        float titleW = contentW - iconSize - 20.0f;

        IDWriteTextLayout* title = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(m_title.c_str(), (UINT32)m_title.length(),
            d2d.FontHero ? d2d.FontHero : d2d.FontTitle, titleW, 36.0f, &title);
        if (title) {
            title->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            title->SetTrimming(&trimming, nullptr);
            rt->DrawTextLayout(D2D1::Point2F(textX, curY), title, d2d.BrushText);
            title->Release();
        }

        IDWriteTextLayout* path = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(m_filePath.c_str(), (UINT32)m_filePath.length(),
            d2d.FontCaption ? d2d.FontCaption : d2d.FontBody, titleW, 20.0f, &path);
        if (path) {
            path->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            path->SetTrimming(&trimming, nullptr);
            rt->DrawTextLayout(D2D1::Point2F(textX, curY + 36.0f), path, d2d.BrushTextMuted);
            path->Release();
        }
        curY += 72.0f;

        auto drawPill = [&](const std::wstring& text, float x, ID2D1SolidColorBrush* fg, bool accent) -> float {
            float pillW = 18.0f + (float)text.length() * 7.2f;
            D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(
                D2D1::RectF(x, curY, x + pillW, curY + 24.0f), 6.0f, 6.0f);
            rt->FillRoundedRectangle(pill, d2d.BrushChipBg ? d2d.BrushChipBg : d2d.BrushCard);
            rt->DrawRoundedRectangle(pill, accent ? (ID2D1Brush*)d2d.BrushAccent : (ID2D1Brush*)d2d.BrushBorder, 1.0f);
            IDWriteTextLayout* layout = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(text.c_str(), (UINT32)text.length(),
                d2d.FontUtilityMono ? d2d.FontUtilityMono : d2d.FontBadge, pillW, 24.0f, &layout);
            if (layout) {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(x, curY), layout, fg ? fg : d2d.BrushText);
                layout->Release();
            }
            return pillW + 8.0f;
        };

        float bx = marginX;
        std::wstring formatName = GetFormatBadge();
        size_t sep = formatName.find(L" • ");
        if (sep != std::wstring::npos) formatName = formatName.substr(0, sep);
        bx += drawPill(formatName, bx, d2d.BrushAccent, true);

        WCHAR stats[96];
        swprintf_s(stats, L"%u PARAGRAPHS • %u TABLES", m_paragraphCount, m_tableCount);
        bx += drawPill(stats, bx, d2d.BrushText, false);

        if (m_headingCount > 0) {
            WCHAR headings[64];
            swprintf_s(headings, L"%u HEADINGS", m_headingCount);
            drawPill(headings, bx, d2d.BrushGreen, false);
        }

        curY += 40.0f;
        rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY),
                     d2d.BrushBorderSubtle, 1.0f);
        curY += 18.0f;
    }

    // Body blocks.
    const float rowPad = 8.0f;
    for (auto& block : m_blocks) {
        if (block.type == OfficeBlockType::Table) {
            if (block.rows.empty()) continue;
            size_t columns = 1;
            for (const auto& row : block.rows) columns = (std::max)(columns, row.cells.size());
            columns = (std::min)(columns, (size_t)8);
            float colW = contentW / (float)columns;

            float tableTop = curY;
            for (size_t rowIndex = 0; rowIndex < block.rows.size(); ++rowIndex) {
                const auto& row = block.rows[rowIndex];
                float rowH = 26.0f;

                std::vector<IDWriteTextLayout*> layouts;
                for (size_t c = 0; c < columns; ++c) {
                    const std::wstring& cell = (c < row.cells.size()) ? row.cells[c] : std::wstring();
                    IDWriteTextLayout* layout = nullptr;
                    IDWriteTextFormat* cellFont = row.header
                        ? (d2d.FontBadge ? d2d.FontBadge : (m_formatBody ? m_formatBody : d2d.FontBody))
                        : (m_formatBody ? m_formatBody : d2d.FontBody);
                    d2d.GetDWriteFactory()->CreateTextLayout(cell.c_str(), (UINT32)cell.length(),
                        cellFont, colW - 16.0f, 200.0f, &layout);
                    if (layout) {
                        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                        float h = MeasureLayout(layout);
                        if (h > rowH - 10.0f) {
                            DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                            layout->SetTrimming(&trimming, nullptr);
                            layout->SetMaxHeight(rowH - 10.0f);
                        }
                    }
                    layouts.push_back(layout);
                }

                if (row.header) {
                    D2D1_ROUNDED_RECT head = D2D1::RoundedRect(
                        D2D1::RectF(marginX, curY, marginX + contentW, curY + rowH), 5.0f, 5.0f);
                    rt->FillRoundedRectangle(head, d2d.BrushCard);
                } else if (rowIndex % 2 == 0) {
                    D2D1_ROUNDED_RECT stripe = D2D1::RoundedRect(
                        D2D1::RectF(marginX, curY, marginX + contentW, curY + rowH), 5.0f, 5.0f);
                    rt->FillRoundedRectangle(stripe, d2d.BrushCard);
                }

                for (size_t c = 0; c < columns; ++c) {
                    float cellX = marginX + colW * (float)c;
                    if (c > 0) {
                        rt->DrawLine(D2D1::Point2F(cellX, curY + 4.0f), D2D1::Point2F(cellX, curY + rowH - 4.0f),
                                     d2d.BrushBorderSubtle, 1.0f);
                    }
                    if (layouts[c]) {
                        float cellH = MeasureLayout(layouts[c]);
                        float cellY = curY + (std::max)(0.0f, (rowH - cellH) / 2.0f);
                        rt->DrawTextLayout(D2D1::Point2F(cellX + rowPad, cellY),
                                           layouts[c], row.header ? d2d.BrushTextMuted : d2d.BrushText);
                        layouts[c]->Release();
                    }
                }
                curY += rowH;
            }
            rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY),
                         d2d.BrushBorderSubtle, 1.0f);
            (void)tableTop;
            curY += 18.0f;
            block.height = curY - tableTop;
            continue;
        }

        IDWriteTextFormat* font = m_formatBody ? m_formatBody : d2d.FontBody;
        ID2D1Brush* brush = d2d.BrushText;
        float indent = 0.0f;
        bool prefixed = false;

        switch (block.type) {
            case OfficeBlockType::Heading1:
                if (m_formatH1) font = m_formatH1;
                break;
            case OfficeBlockType::Heading2:
                if (m_formatH2) font = m_formatH2;
                break;
            case OfficeBlockType::Heading3:
                if (m_formatH3) font = m_formatH3;
                break;
            case OfficeBlockType::Quote:
                brush = d2d.BrushTextMuted;
                indent = 14.0f;
                break;
            case OfficeBlockType::Bullet:
                indent = 18.0f + (float)block.level * 18.0f;
                prefixed = true;
                break;
            default:
                break;
        }

        std::wstring text = prefixed ? (L"•  " + block.text) : block.text;
        float wrapW = contentW - indent - (block.type == OfficeBlockType::Quote ? 18.0f : 0.0f);

        IDWriteTextLayout* layout = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(text.c_str(), (UINT32)text.length(), font, wrapW, 4000.0f, &layout);
        if (!layout) continue;
        // Never inherit a centred/far alignment: it would draw the block far
        // below its y (the shared hero/title formats are built for centred text).
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        float textH = MeasureLayout(layout);

        if (block.type == OfficeBlockType::Quote) {
            rt->FillRectangle(D2D1::RectF(marginX + 2.0f, curY, marginX + 4.0f, curY + textH),
                              d2d.BrushAccent);
        }

        rt->DrawTextLayout(D2D1::Point2F(marginX + indent, curY), layout, brush);
        layout->Release();

        block.height = textH;
        float spacing = (block.type == OfficeBlockType::Heading1) ? 20.0f
                      : (block.type == OfficeBlockType::Heading2) ? 16.0f
                      : (block.type == OfficeBlockType::Heading3) ? 12.0f : 8.0f;
        curY += textH + spacing;
    }

    m_contentHeight = curY - (targetRect.top + 32.0f + m_scrollY);
}

int RunOfficeDump(const std::wstring& filePath) {
    OfficeEngine engine;
    bool loaded = engine.LoadDocument(filePath);
    wprintf(L"loaded=%d kind=%d format=%d title='%ls' error='%ls'\n",
            loaded ? 1 : 0, (int)engine.GetKind(), (int)engine.GetFormat(),
            engine.GetTitle().c_str(), engine.GetErrorText().c_str());
    if (!loaded) return 1;

    wprintf(L"paragraphs=%u headings=%u tables=%u\n",
            engine.ParagraphCount(), engine.HeadingCount(), engine.TableCount());

    const std::wstring text = engine.GetPlainText();
    wprintf(L"--- plain text ---\n%ls\n", text.c_str());
    return 0;
}

} // namespace WinPreview
