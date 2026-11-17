#include "office_engine.h"
#include "archive_engine.h"
#include "d2d_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwchar>
#include <functional>
#include <shlobj.h>
#include <map>
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
    if (!m_packageDir.empty()) {
        std::wstring from = m_packageDir + L'\0';
        SHFILEOPSTRUCTW op = {};
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        SHFileOperationW(&op);
        m_packageDir.clear();
    }
    m_isLoaded = false;
    m_kind = OfficeKind::None;
    m_format = OfficeFormat::None;
    m_filePath.clear();
    m_title.clear();
    m_errorText.clear();
    m_blocks.clear();
    m_sheets.clear();
    m_activeSheet = 0;
    m_slides.clear();
    m_activeSlide = 0;
    m_slideRects.clear();
    m_chapters.clear();
    m_activeChapter = 0;
    m_blockOffsets.clear();
    m_bookScrollTarget = -1.0f;
    m_sheetTabRects.clear();
    m_scrollX = 0.0f;
    m_targetScrollX = 0.0f;
    m_sheetContentWidth = 0.0f;
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

static bool ReadWholeFile(const std::wstring& path, size_t maxBytes, std::string& out) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || (UINT64)size.QuadPart > maxBytes) {
        CloseHandle(hFile);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    bool ok = ReadFile(hFile, &out[0], (DWORD)out.size(), &read, NULL) && read == out.size();
    CloseHandle(hFile);
    if (!ok) out.clear();
    return ok;
}

bool OfficeEngine::PreparePackage(const std::wstring& filePath) {
    if (!m_packageDir.empty()) return true;
    WCHAR temp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, temp)) return false;

    std::wstring root = std::wstring(temp) + L"WinPreview-office";
    std::wstring dir = root + L"\\" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(root.c_str(), NULL);

    ArchiveEngine archive;
    std::wstring error;
    if (!archive.ExtractAll(filePath, dir, error)) {
        return false;
    }
    m_packageDir = dir;
    return true;
}

bool OfficeEngine::ReadPart(const std::wstring& filePath, const std::wstring& entry,
                            size_t maxBytes, std::string& out, std::wstring& error) {
    if (m_packageDir.empty()) {
        ArchiveEngine archive;
        return ReadPart(filePath, entry, maxBytes, out, error);
    }
    std::wstring path = m_packageDir + L"\\" + entry;
    for (auto& ch : path) {
        if (ch == L'/') ch = L'\\';
    }
    if (ReadWholeFile(path, maxBytes, out)) return true;
    error = L"Part missing from the package: " + entry;
    return false;
}

bool OfficeEngine::LoadDocument(const std::wstring& filePath) {
    Close();
    m_filePath = filePath;
    // One extraction per document: reading 60 slide parts through separate
    // process launches cost seconds.
    PreparePackage(filePath);

    size_t dot = filePath.find_last_of(L'.');
    std::wstring ext = (dot == std::wstring::npos) ? L"" : filePath.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    std::string xml;
    std::wstring error;

    if (ext == L".docx") {
        m_format = OfficeFormat::Docx;
        m_kind = OfficeKind::Word;
        if (!ReadPart(filePath, L"word/document.xml", 64u * 1024 * 1024, xml, error)) {
            m_errorText = error.empty() ? L"This document could not be read." : error;
            return false;
        }
    } else if (ext == L".odt") {
        m_format = OfficeFormat::Odt;
        m_kind = OfficeKind::Word;
        if (!ReadPart(filePath, L"content.xml", 64u * 1024 * 1024, xml, error)) {
            m_errorText = error.empty() ? L"This document could not be read." : error;
            return false;
        }
    } else if (ext == L".xlsx") {
        m_format = OfficeFormat::Xlsx;
        m_kind = OfficeKind::Sheet;
        if (!LoadXlsx(filePath)) return false;
        FinishDocument();
        return true;
    } else if (ext == L".ods") {
        m_format = OfficeFormat::Ods;
        m_kind = OfficeKind::Sheet;
        if (!LoadOds(filePath)) return false;
        FinishDocument();
        return true;
    } else if (ext == L".pptx") {
        m_format = OfficeFormat::Pptx;
        m_kind = OfficeKind::Slide;
        if (!LoadPptx(filePath)) return false;
        FinishDocument();
        return true;
    } else if (ext == L".odp") {
        m_format = OfficeFormat::Odp;
        m_kind = OfficeKind::Slide;
        if (!LoadOdp(filePath)) return false;
        FinishDocument();
        return true;
    } else if (ext == L".epub") {
        m_format = OfficeFormat::Epub;
        m_kind = OfficeKind::Book;
        if (!LoadEpub(filePath)) return false;
        FinishDocument();
        return true;
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
    if (ReadPart(filePath, coreEntry, 1024 * 1024, core, coreError)) {
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

    if (m_kind == OfficeKind::Word && m_blocks.empty()) {
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
    if (m_kind == OfficeKind::Book) {
        for (const auto& block : m_blocks) {
            if (block.type == OfficeBlockType::ChapterTitle) {
                out += L"\n## " + block.text + L"\n";
                continue;
            }
            if (block.type == OfficeBlockType::Table) {
                for (const auto& row : block.rows) {
                    for (size_t c = 0; c < row.cells.size(); ++c) {
                        if (c) out.push_back(L'\t');
                        out += row.cells[c];
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
    if (m_kind == OfficeKind::Slide) {
        for (size_t i = 0; i < m_slides.size(); ++i) {
            out += L"--- Slide " + std::to_wstring(i + 1) + L" ---\n";
            if (!m_slides[i].title.empty()) out += m_slides[i].title + L"\n";
            for (const auto& block : m_slides[i].blocks) out += std::wstring(L"• ") + block.text + L"\n";
        }
        return out;
    }
    if (m_kind == OfficeKind::Sheet) {
        if (m_activeSheet < 0 || m_activeSheet >= (int)m_sheets.size()) return out;
        for (const auto& row : m_sheets[(size_t)m_activeSheet].rows) {
            for (size_t c = 0; c < row.size(); ++c) {
                if (c) out.push_back(L'\t');
                out += row[c].text;
            }
            out.push_back(L'\n');
        }
        return out;
    }
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
        case OfficeFormat::Epub: name = L"EPUB BOOK"; break;
        default: break;
    }

    if (!m_isLoaded) {
        return std::wstring(name) + (m_errorText.empty() ? L"" : L" • " + m_errorText);
    }

    WCHAR buffer[192];
    if (m_kind == OfficeKind::Sheet) {
        size_t rows = 0, columns = 0;
        if (m_activeSheet >= 0 && m_activeSheet < (int)m_sheets.size()) {
            rows = m_sheets[(size_t)m_activeSheet].rows.size();
            columns = m_sheets[(size_t)m_activeSheet].columns;
        }
        swprintf_s(buffer, L"%s • %u ROWS × %u COLUMNS • %d SHEETS",
                   name, (unsigned)rows, (unsigned)columns, (int)m_sheets.size());
        return buffer;
    }
    if (m_kind == OfficeKind::Slide) {
        swprintf_s(buffer, L"%s • %d SLIDES", name, (int)m_slides.size());
        return buffer;
    }
    if (m_kind == OfficeKind::Book) {
        swprintf_s(buffer, L"%s • %d CHAPTERS", name, (int)m_chapters.size());
        return buffer;
    }
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

    bool fixedHeader = (m_kind == OfficeKind::Sheet || m_kind == OfficeKind::Slide);
    float curY = targetRect.top + 32.0f + (fixedHeader ? 0.0f : m_scrollY);

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
        if (m_kind == OfficeKind::Sheet) {
            size_t rows = 0, columns = 0;
            if (m_activeSheet >= 0 && m_activeSheet < (int)m_sheets.size()) {
                rows = m_sheets[(size_t)m_activeSheet].rows.size();
                columns = m_sheets[(size_t)m_activeSheet].columns;
            }
            swprintf_s(stats, L"%u ROWS × %u COLUMNS", (unsigned)rows, (unsigned)columns);
            bx += drawPill(stats, bx, d2d.BrushText, false);
            if (m_sheets.size() > 1) {
                WCHAR sheetPill[48];
                swprintf_s(sheetPill, L"%d SHEETS", (int)m_sheets.size());
                drawPill(sheetPill, bx, d2d.BrushGreen, false);
            }
        } else if (m_kind == OfficeKind::Book) {
            WCHAR chapterPill[64];
            swprintf_s(chapterPill, L"%d CHAPTERS", (int)m_chapters.size());
            bx += drawPill(chapterPill, bx, d2d.BrushText, false);
            if (!m_chapters.empty()) {
                swprintf_s(chapterPill, L"CHAPTER %d / %d", m_activeChapter + 1, (int)m_chapters.size());
                drawPill(chapterPill, bx, d2d.BrushGreen, false);
            }
        } else if (m_kind == OfficeKind::Slide) {
            WCHAR slidePill[48];
            swprintf_s(slidePill, L"%d SLIDES", (int)m_slides.size());
            bx += drawPill(slidePill, bx, d2d.BrushText, false);
            swprintf_s(slidePill, L"SLIDE %d / %d", m_activeSlide + 1, (int)m_slides.size());
            drawPill(slidePill, bx, d2d.BrushGreen, false);
        } else {
            swprintf_s(stats, L"%u PARAGRAPHS • %u TABLES", m_paragraphCount, m_tableCount);
            bx += drawPill(stats, bx, d2d.BrushText, false);

            if (m_headingCount > 0) {
                WCHAR headings[64];
                swprintf_s(headings, L"%u HEADINGS", m_headingCount);
                drawPill(headings, bx, d2d.BrushGreen, false);
            }
        }

        curY += 40.0f;
        rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY),
                     d2d.BrushBorderSubtle, 1.0f);
        curY += 18.0f;
    }

    if (m_kind == OfficeKind::Sheet) {
        RenderSheet(rt, d2d, targetRect, marginX, contentW, curY);
        return;
    }
    if (m_kind == OfficeKind::Slide) {
        RenderSlides(rt, d2d, targetRect, marginX, contentW, curY);
        return;
    }

    RenderWord(rt, d2d, targetRect, marginX, contentW, curY);
}

void OfficeEngine::RenderWord(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect,
                              float marginX, float contentW, float curY) {
    // Body blocks.
    const float rowPad = 8.0f;
    m_blockOffsets.assign(m_blocks.size(), 0.0f);
    for (size_t blockIndex = 0; blockIndex < m_blocks.size(); ++blockIndex) {
        auto& block = m_blocks[blockIndex];
        m_blockOffsets[blockIndex] = curY - (targetRect.top + 32.0f) - m_scrollY;
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
            case OfficeBlockType::ChapterTitle:
                if (m_formatH1) font = m_formatH1;
                if (blockIndex > 0) curY += 26.0f;
                break;
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
        float spacing = (block.type == OfficeBlockType::ChapterTitle) ? 16.0f
                      : (block.type == OfficeBlockType::Heading1) ? 20.0f
                      : (block.type == OfficeBlockType::Heading2) ? 16.0f
                      : (block.type == OfficeBlockType::Heading3) ? 12.0f : 8.0f;
        curY += textH + spacing;
    }

    m_contentHeight = curY - (targetRect.top + 32.0f + m_scrollY);
}


static std::wstring Utf8ToWideLocal(const std::string& text) {
    if (text.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0);
    if (len <= 0) return L"";
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], len);
    return out;
}

// ---------------------------------------------------------------------------
// Spreadsheets
// ---------------------------------------------------------------------------

namespace {

// "AB" -> 27 (0-based column index).
int ColumnIndexFromRef(const std::wstring& ref) {
    int value = 0;
    for (wchar_t ch : ref) {
        if (ch >= L'A' && ch <= L'Z') value = value * 26 + (ch - L'A' + 1);
        else if (ch >= L'a' && ch <= L'z') value = value * 26 + (ch - L'a' + 1);
        else break;
    }
    return (std::max)(0, value - 1);
}

std::wstring ColumnRefFromIndex(int index) {
    std::wstring out;
    int value = index + 1;
    while (value > 0) {
        int rem = (value - 1) % 26;
        out.insert(out.begin(), (wchar_t)(L'A' + rem));
        value = (value - 1) / 26;
    }
    return out;
}

std::wstring TrimNumber(const std::wstring& raw) {
    std::wstring text = raw;
    size_t dot = text.find(L'.');
    if (dot != std::wstring::npos) {
        while (!text.empty() && text.back() == L'0') text.pop_back();
        if (!text.empty() && text.back() == L'.') text.pop_back();
    }
    return text;
}

// Excel serial date -> "YYYY-MM-DD" (and " HH:MM" when there is a time part).
std::wstring ExcelSerialToDate(double serial) {
    if (serial <= 0.0 || serial > 2958465.0) return L"";
    int days = (int)floor(serial);
    double fraction = serial - days;

    SYSTEMTIME base = {};
    base.wYear = 1899;
    base.wMonth = 12;
    base.wDay = 30;
    FILETIME baseFt = {};
    if (!SystemTimeToFileTime(&base, &baseFt)) return L"";
    ULARGE_INTEGER value;
    value.LowPart = baseFt.dwLowDateTime;
    value.HighPart = baseFt.dwHighDateTime;
    value.QuadPart += (ULONGLONG)days * 86400ULL * 10000000ULL;

    FILETIME ft;
    ft.dwLowDateTime = value.LowPart;
    ft.dwHighDateTime = value.HighPart;
    SYSTEMTIME st = {};
    if (!FileTimeToSystemTime(&ft, &st)) return L"";

    WCHAR buffer[64];
    int minutes = (int)(fraction * 24.0 * 60.0 + 0.5);
    if (minutes > 0) {
        swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay,
                   minutes / 60, minutes % 60);
    } else {
        swprintf_s(buffer, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    }
    return buffer;
}

bool LooksLikeDateCode(const std::wstring& code) {
    if (code.empty()) return false;
    std::wstring lower = code;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    bool hasDate = lower.find(L'y') != std::wstring::npos || lower.find(L'd') != std::wstring::npos;
    return hasDate;
}

} // namespace

bool OfficeEngine::LoadXlsx(const std::wstring& filePath) {
    std::string xml;
    std::wstring error;

    // Shared strings (optional).
    std::vector<std::wstring> sharedStrings;
    if (ReadPart(filePath, L"xl/sharedStrings.xml", 32u * 1024 * 1024, xml, error)) {
        XmlNode root;
        if (ParseXml(xml, root, 300000, 60)) {
            const XmlNode* sst = FindChild(&root, L"sst");
            if (sst) {
                for (const auto& item : sst->children) {
                    if (item->name != L"si") continue;
                    sharedStrings.push_back(CollapseWhitespace(TextContent(item.get())));
                }
            }
        }
    }

    // Cell style -> numFmtId -> date formatting.
    std::vector<bool> dateStyles;
    {
        std::string stylesXml;
        if (ReadPart(filePath, L"xl/styles.xml", 8u * 1024 * 1024, stylesXml, error)) {
            XmlNode styles;
            if (ParseXml(stylesXml, styles, 60000, 40)) {
                std::map<int, std::wstring> customFormats;
                const XmlNode* styleSheet = FindChild(&styles, L"styleSheet");
                const XmlNode* numFmts = styleSheet ? FindChild(styleSheet, L"numFmts") : nullptr;
                if (numFmts) {
                    for (const auto& fmt : numFmts->children) {
                        if (fmt->name != L"numFmt") continue;
                        const std::wstring* id = fmt->Attr(L"numFmtId");
                        const std::wstring* code = fmt->Attr(L"formatCode");
                        if (id && code) customFormats[_wtoi(id->c_str())] = *code;
                    }
                }
                const XmlNode* cellXfs = styleSheet ? FindChild(styleSheet, L"cellXfs") : nullptr;
                if (cellXfs) {
                    for (const auto& xf : cellXfs->children) {
                        if (xf->name != L"xf") continue;
                        int id = 0;
                        if (const std::wstring* value = xf->Attr(L"numFmtId")) id = _wtoi(value->c_str());
                        bool isDate = (id >= 14 && id <= 22) || (id >= 45 && id <= 47);
                        auto custom = customFormats.find(id);
                        if (!isDate && custom != customFormats.end()) isDate = LooksLikeDateCode(custom->second);
                        dateStyles.push_back(isDate);
                    }
                }
            }
        }
    }

    // Workbook sheet list + relationships.
    if (!ReadPart(filePath, L"xl/workbook.xml", 8u * 1024 * 1024, xml, error)) {
        m_errorText = error.empty() ? L"This workbook has no sheet list." : error;
        return false;
    }
    XmlNode workbook;
    if (!ParseXml(xml, workbook, 100000, 60)) {
        m_errorText = L"This workbook's XML could not be parsed.";
        return false;
    }

    std::vector<std::pair<std::wstring, std::wstring>> sheetRefs;  // name, rId
    const XmlNode* sheetsNode = FindDescendant(&workbook, L"sheets", 6);
    if (sheetsNode) {
        for (const auto& sheet : sheetsNode->children) {
            if (sheet->name != L"sheet") continue;
            std::wstring name = L"Sheet";
            if (const std::wstring* value = sheet->Attr(L"name")) name = *value;
            std::wstring rid;
            if (const std::wstring* value = sheet->Attr(L"r:id")) rid = *value;
            if (rid.empty()) {
                if (const std::wstring* value = sheet->Attr(L"id")) rid = *value;
            }
            sheetRefs.emplace_back(name, rid);
        }
    }
    if (sheetRefs.empty()) {
        m_errorText = L"This workbook has no sheets.";
        return false;
    }

    std::map<std::wstring, std::wstring> relTargets;
    {
        std::string relsXml;
        bool relsRead = ReadPart(filePath, L"xl/_rels/workbook.xml.rels", 4u * 1024 * 1024, relsXml, error);
        if (relsRead) {
            XmlNode rels;
            if (ParseXml(relsXml, rels, 20000, 30)) {
                const XmlNode* relationships = FindChild(&rels, L"Relationships");
                if (relationships) {
                    for (const auto& rel : relationships->children) {
                        if (rel->name != L"Relationship") continue;
                        const std::wstring* id = rel->Attr(L"Id");
                        const std::wstring* target = rel->Attr(L"Target");
                        if (id && target) relTargets[*id] = *target;
                    }
                }
            }
        }
    }

    const size_t kMaxSheets = 40;
    const size_t kMaxRows = 500;
    const size_t kMaxColumns = 64;

    for (const auto& ref : sheetRefs) {
        if (m_sheets.size() >= kMaxSheets) break;
        std::wstring target;
        auto it = relTargets.find(ref.second);
        if (it != relTargets.end()) target = it->second;
        if (target.empty()) continue;

        std::wstring entry = target;
        if (entry.rfind(L"/", 0) == 0) entry = entry.substr(1);
        if (entry.rfind(L"xl/", 0) != 0) entry = L"xl/" + entry;

        std::string sheetXml;
        if (!ReadPart(filePath, entry, 64u * 1024 * 1024, sheetXml, error)) {
            continue;
        }

        XmlNode sheetRoot;
        if (!ParseXml(sheetXml, sheetRoot, 400000, 80)) {
            continue;
        }

        OfficeSheet sheet;
        sheet.name = ref.first;

        const XmlNode* sheetData = FindDescendant(&sheetRoot, L"sheetData", 8);
        if (!sheetData) {
            m_sheets.push_back(std::move(sheet));
            continue;
        }

        for (const auto& rowNode : sheetData->children) {
            if (rowNode->name != L"row") continue;
            if (sheet.rows.size() >= kMaxRows) break;

            int rowNumber = (int)sheet.rows.size();
            if (const std::wstring* value = rowNode->Attr(L"r")) rowNumber = (std::max)(0, _wtoi(value->c_str()) - 1);
            while ((int)sheet.rows.size() < rowNumber && sheet.rows.size() < kMaxRows) {
                sheet.rows.emplace_back();
            }

            std::vector<OfficeCell> cells;
            int nextColumn = 0;
            for (const auto& cellNode : rowNode->children) {
                if (cellNode->name != L"c") continue;
                int column = nextColumn;
                if (const std::wstring* refAttr = cellNode->Attr(L"r")) column = ColumnIndexFromRef(*refAttr);
                if (column >= (int)kMaxColumns) break;
                nextColumn = column + 1;
                while ((int)cells.size() < column) cells.emplace_back();

                std::wstring type;
                if (const std::wstring* t = cellNode->Attr(L"t")) type = *t;

                std::wstring raw;
                if (const XmlNode* v = FindChild(cellNode.get(), L"v")) raw = TextContent(v);
                else if (const XmlNode* is = FindChild(cellNode.get(), L"is")) raw = TextContent(is);

                OfficeCell cell;
                if (type == L"s") {
                    int index = _wtoi(raw.c_str());
                    if (index >= 0 && index < (int)sharedStrings.size()) cell.text = sharedStrings[index];
                } else if (type == L"b") {
                    cell.text = (raw == L"1") ? L"TRUE" : L"FALSE";
                } else if (type == L"str" || type == L"inlineStr" || type == L"e") {
                    cell.text = CollapseWhitespace(raw);
                } else if (!raw.empty()) {
                    double value = _wtof(raw.c_str());
                    int styleIndex = -1;
                    if (const std::wstring* sAttr = cellNode->Attr(L"s")) styleIndex = _wtoi(sAttr->c_str());
                    bool isDate = styleIndex >= 0 && styleIndex < (int)dateStyles.size() && dateStyles[styleIndex];
                    if (isDate) {
                        cell.text = ExcelSerialToDate(value);
                    } else {
                        cell.text = TrimNumber(raw);
                    }
                    cell.numeric = !isDate;
                }
                cells.push_back(std::move(cell));
            }

            sheet.columns = (std::max)(sheet.columns, cells.size());
            sheet.rows.push_back(std::move(cells));
        }

        // Trim trailing empty rows/columns so the grid is as small as possible.
        while (!sheet.rows.empty()) {
            bool empty = true;
            for (const auto& cell : sheet.rows.back()) {
                if (!cell.text.empty()) { empty = false; break; }
            }
            if (!empty) break;
            sheet.rows.pop_back();
        }
        for (auto& row : sheet.rows) {
            while (!row.empty() && row.back().text.empty()) row.pop_back();
        }
        sheet.columns = 0;
        for (const auto& row : sheet.rows) sheet.columns = (std::max)(sheet.columns, row.size());

        m_sheets.push_back(std::move(sheet));
    }

    if (m_sheets.empty()) {
        m_errorText = L"No sheets in this workbook could be read.";
        return false;
    }
    return true;
}

bool OfficeEngine::LoadOds(const std::wstring& filePath) {
    std::string xml;
    std::wstring error;
    if (!ReadPart(filePath, L"content.xml", 64u * 1024 * 1024, xml, error)) {
        m_errorText = error.empty() ? L"This spreadsheet could not be read." : error;
        return false;
    }

    XmlNode root;
    if (!ParseXml(xml, root, 400000, 120)) {
        m_errorText = L"This spreadsheet's XML could not be parsed.";
        return false;
    }

    const XmlNode* doc = FindChild(&root, L"office:document-content");
    const XmlNode* body = doc ? FindChild(doc, L"office:body") : nullptr;
    const XmlNode* spreadsheet = body ? FindChild(body, L"office:spreadsheet") : nullptr;
    if (!spreadsheet) {
        m_errorText = L"This spreadsheet has no sheets.";
        return false;
    }

    const size_t kMaxSheets = 40;
    const size_t kMaxRows = 500;
    const size_t kMaxColumns = 64;

    for (const auto& tableNode : spreadsheet->children) {
        if (tableNode->name != L"table:table") continue;
        if (m_sheets.size() >= kMaxSheets) break;

        OfficeSheet sheet;
        if (const std::wstring* name = tableNode->Attr(L"table:name")) sheet.name = *name;
        if (sheet.name.empty()) sheet.name = L"Sheet" + std::to_wstring(m_sheets.size() + 1);

        for (const auto& rowNode : tableNode->children) {
            if (rowNode->name != L"table:table-row") continue;
            if (sheet.rows.size() >= kMaxRows) break;

            int repeatRows = 1;
            if (const std::wstring* repeat = rowNode->Attr(L"table:number-rows-repeated")) {
                repeatRows = (std::max)(1, (std::min)(16, _wtoi(repeat->c_str())));
            }

            std::vector<OfficeCell> cells;
            for (const auto& cellNode : rowNode->children) {
                if (cellNode->name != L"table:table-cell" && cellNode->name != L"table:covered-table-cell") continue;

                int repeatCols = 1;
                if (const std::wstring* repeat = cellNode->Attr(L"table:number-columns-repeated")) {
                    repeatCols = (std::max)(1, (std::min)(32, _wtoi(repeat->c_str())));
                }

                std::wstring type;
                if (const std::wstring* value = cellNode->Attr(L"office:value-type")) type = *value;

                OfficeCell cell;
                if (type == L"date") {
                    if (const std::wstring* value = cellNode->Attr(L"office:date-value")) {
                        cell.text = *value;
                        if (cell.text.size() > 10) cell.text.resize(10);
                    }
                } else if (type == L"float" || type == L"percentage" || type == L"currency") {
                    if (const std::wstring* value = cellNode->Attr(L"office:value")) {
                        cell.text = TrimNumber(*value);
                        cell.numeric = true;
                    }
                } else if (type == L"boolean") {
                    if (const std::wstring* value = cellNode->Attr(L"office:boolean-value")) {
                        cell.text = (*value == L"true") ? L"TRUE" : L"FALSE";
                    }
                } else if (type == L"time") {
                    if (const std::wstring* value = cellNode->Attr(L"office:time-value")) cell.text = *value;
                }
                if (cell.text.empty()) {
                    cell.text = CollapseWhitespace(TextContent(cellNode.get()));
                }

                for (int i = 0; i < repeatCols && cells.size() < kMaxColumns; ++i) {
                    cells.push_back(cell);
                }
            }

            while (!cells.empty() && cells.back().text.empty()) cells.pop_back();
            for (int i = 0; i < repeatRows && sheet.rows.size() < kMaxRows; ++i) {
                sheet.rows.push_back(cells);
            }
        }

        while (!sheet.rows.empty()) {
            bool empty = true;
            for (const auto& cell : sheet.rows.back()) {
                if (!cell.text.empty()) { empty = false; break; }
            }
            if (!empty) break;
            sheet.rows.pop_back();
        }
        for (auto& row : sheet.rows) {
            while (!row.empty() && row.back().text.empty()) row.pop_back();
        }
        sheet.columns = 0;
        for (const auto& row : sheet.rows) sheet.columns = (std::max)(sheet.columns, row.size());

        m_sheets.push_back(std::move(sheet));
    }

    if (m_sheets.empty()) {
        m_errorText = L"No sheets in this spreadsheet could be read.";
        return false;
    }
    return true;
}

void OfficeEngine::SetActiveSheet(int index) {
    if (index < 0 || index >= (int)m_sheets.size()) return;
    m_activeSheet = index;
    m_scrollX = 0.0f;
    m_targetScrollX = 0.0f;
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
}

std::wstring OfficeEngine::SheetName(int index) const {
    if (index < 0 || index >= (int)m_sheets.size()) return L"";
    return m_sheets[(size_t)index].name;
}

bool OfficeEngine::GetSheetTabRect(int index, D2D1_RECT_F& out) const {
    for (const auto& entry : m_sheetTabRects) {
        if (entry.second == index) {
            out = entry.first;
            return true;
        }
    }
    return false;
}

int OfficeEngine::HitTestSheetTab(float x, float y) const {
    for (const auto& entry : m_sheetTabRects) {
        if (x >= entry.first.left && x <= entry.first.right &&
            y >= entry.first.top && y <= entry.first.bottom) {
            return entry.second;
        }
    }
    return -1;
}

void OfficeEngine::ScrollHorizontal(float dx) {
    float maxScroll = (std::max)(0.0f, m_sheetContentWidth - 600.0f);
    m_scrollX += dx;
    if (m_scrollX > 0.0f) m_scrollX = 0.0f;
    if (m_scrollX < -maxScroll) m_scrollX = -maxScroll;
    m_targetScrollX = m_scrollX;
}

void OfficeEngine::RenderSheet(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& rect,
                                float marginX, float contentW, float curY) {
    (void)rect;
    if (m_sheets.empty()) return;
    if (m_activeSheet < 0 || m_activeSheet >= (int)m_sheets.size()) m_activeSheet = 0;

    m_sheetTabRects.clear();

    // Sheet tabs.
    {
        float tabX = marginX;
        float tabY = curY;
        for (int i = 0; i < (int)m_sheets.size(); ++i) {
            const std::wstring& name = m_sheets[(size_t)i].name;
            std::wstring label = name;
            if (label.size() > 22) label = label.substr(0, 20) + L"…";
            float tabW = 26.0f + (float)label.length() * 7.2f;
            if (tabX + tabW > marginX + contentW - 8.0f) break;

            bool active = (i == m_activeSheet);
            D2D1_ROUNDED_RECT tab = D2D1::RoundedRect(
                D2D1::RectF(tabX, tabY, tabX + tabW, tabY + 28.0f), 6.0f, 6.0f);
            rt->FillRoundedRectangle(tab, active ? d2d.BrushCard : (d2d.BrushChipBg ? d2d.BrushChipBg : d2d.BrushCard));
            rt->DrawRoundedRectangle(tab, active ? (ID2D1Brush*)d2d.BrushAccent : (ID2D1Brush*)d2d.BrushBorder, 1.0f);

            IDWriteTextLayout* layout = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(label.c_str(), (UINT32)label.length(),
                m_formatBody ? m_formatBody : d2d.FontBody, tabW - 16.0f, 28.0f, &layout);
            if (layout) {
                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(tabX + 8.0f, tabY), layout,
                                   active ? d2d.BrushText : d2d.BrushTextMuted);
                layout->Release();
            }
            m_sheetTabRects.emplace_back(
                D2D1::RectF(tabX, tabY, tabX + tabW, tabY + 28.0f), i);
            tabX += tabW + 8.0f;
        }
        curY += 44.0f;
    }

    const OfficeSheet& sheet = m_sheets[(size_t)m_activeSheet];
    if (sheet.rows.empty()) {
        rt->DrawText(L"This sheet is empty.", 20, d2d.FontBody,
                     D2D1::RectF(marginX, curY + 20.0f, marginX + contentW, curY + 60.0f), d2d.BrushTextMuted);
        m_contentHeight = 200.0f;
        return;
    }

    const float gutterW = 52.0f;
    const float rowH = 24.0f;
    const float colW = 118.0f;

    size_t columns = (std::max)(sheet.columns, (size_t)1);
    m_sheetContentWidth = gutterW + (float)columns * colW;

    float gridTop = curY + 8.0f;
    float gridBottom = rect.bottom - 12.0f;
    if (gridBottom <= gridTop + rowH) gridBottom = gridTop + rowH;

    rt->PushAxisAlignedClip(D2D1::RectF(marginX, gridTop, marginX + contentW, gridBottom),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float originX = marginX + m_scrollX;
    float originY = gridTop + m_scrollY;

    // Column header band (letters).
    for (size_t c = 0; c < columns; ++c) {
        float x = originX + gutterW + (float)c * colW;
        if (x + colW < marginX || x > marginX + contentW) continue;
        D2D1_RECT_F head = D2D1::RectF(x, originY, x + colW, originY + rowH);
        rt->FillRectangle(head, d2d.BrushCard);
        std::wstring letter = ColumnRefFromIndex((int)c);
        IDWriteTextLayout* layout = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(letter.c_str(), (UINT32)letter.length(),
            d2d.FontBadge ? d2d.FontBadge : d2d.FontCaption, colW - 12.0f, rowH, &layout);
        if (layout) {
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(x, originY), layout, d2d.BrushTextMuted);
            layout->Release();
        }
    }

    // Cells.
    IDWriteTextFormat* cellFont = m_formatBody ? m_formatBody : d2d.FontBody;
    for (size_t r = 0; r < sheet.rows.size(); ++r) {
        float y = originY + rowH + (float)r * rowH;
        if (y > gridBottom) break;

        // Row number gutter.
        D2D1_RECT_F gutter = D2D1::RectF(originX, y, originX + gutterW, y + rowH);
        rt->FillRectangle(gutter, d2d.BrushCard);
        WCHAR number[16];
        swprintf_s(number, L"%u", (unsigned)(r + 1));
        IDWriteTextLayout* numLayout = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(number, (UINT32)wcslen(number),
            d2d.FontBadge ? d2d.FontBadge : d2d.FontCaption, gutterW - 10.0f, rowH, &numLayout);
        if (numLayout) {
            numLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            numLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(originX, y), numLayout, d2d.BrushTextMuted);
            numLayout->Release();
        }

        const auto& row = sheet.rows[r];
        for (size_t c = 0; c < row.size(); ++c) {
            const OfficeCell& cell = row[c];
            if (cell.text.empty()) continue;
            float x = originX + gutterW + (float)c * colW;
            if (x + colW < marginX || x > marginX + contentW) continue;

            if (c % 2 == 1) {
                rt->FillRectangle(D2D1::RectF(x, y, x + colW, y + rowH), d2d.BrushCard);
            }
            rt->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x, y + rowH), d2d.BrushBorderSubtle, 1.0f);

            IDWriteTextLayout* layout = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(cell.text.c_str(), (UINT32)cell.text.length(),
                cellFont, colW - 14.0f, rowH, &layout);
            if (layout) {
                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                layout->SetTextAlignment(cell.numeric ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
                layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                layout->SetTrimming(&trimming, nullptr);
                rt->DrawTextLayout(D2D1::Point2F(x + 7.0f, y), layout, d2d.BrushText);
                layout->Release();
            }
        }
        rt->DrawLine(D2D1::Point2F(originX, y + rowH), D2D1::Point2F(originX + gutterW + (float)columns * colW, y + rowH),
                     d2d.BrushBorderSubtle, 1.0f);
    }

    rt->PopAxisAlignedClip();

    m_contentHeight = (float)sheet.rows.size() * rowH + 120.0f;
}

// ---------------------------------------------------------------------------
// Presentations
// ---------------------------------------------------------------------------

namespace {

// Slide paragraphs live in <a:p> runs; keep one block per paragraph.
void AppendSlideParagraphs(const XmlNode* container, std::vector<OfficeBlock>& blocks,
                           std::wstring* title, bool treatAsBullets) {
    for (const auto& p : container->children) {
        if (p->name != L"a:p" && p->name != L"text:p" && p->name != L"text:h") continue;
        std::wstring text = CollapseWhitespace(p->name == L"a:p" ? TextContent(p.get())
                                                                 : TextContent(p.get()));
        if (text.empty()) continue;

        if (title && title->empty()) {
            *title = text;
            continue;
        }

        OfficeBlock block;
        block.type = treatAsBullets ? OfficeBlockType::Bullet : OfficeBlockType::Paragraph;
        int level = 0;
        if (p->name == L"a:p") {
            const XmlNode* pPr = FindChild(p.get(), L"a:pPr");
            if (pPr) {
                if (const std::wstring* lvl = pPr->Attr(L"lvl")) level = _wtoi(lvl->c_str());
            }
        }
        block.level = (std::max)(0, (std::min)(level, 4));
        block.text = text;
        blocks.push_back(std::move(block));
    }
}

} // namespace

bool OfficeEngine::LoadPptx(const std::wstring& filePath) {
    std::string xml;
    std::wstring error;

    if (!ReadPart(filePath, L"ppt/presentation.xml", 16u * 1024 * 1024, xml, error)) {
        m_errorText = error.empty() ? L"This presentation could not be read." : error;
        return false;
    }
    XmlNode presentation;
    if (!ParseXml(xml, presentation, 100000, 60)) {
        m_errorText = L"This presentation's XML could not be parsed.";
        return false;
    }

    std::vector<std::wstring> slideIds;
    const XmlNode* idList = FindDescendant(&presentation, L"p:sldIdLst", 8);
    if (idList) {
        for (const auto& id : idList->children) {
            if (id->name != L"p:sldId") continue;
            std::wstring rid;
            if (const std::wstring* value = id->Attr(L"r:id")) rid = *value;
            if (!rid.empty()) slideIds.push_back(rid);
        }
    }
    if (slideIds.empty()) {
        m_errorText = L"This presentation has no slides.";
        return false;
    }

    std::map<std::wstring, std::wstring> relTargets;
    {
        std::string relsXml;
        if (ReadPart(filePath, L"ppt/_rels/presentation.xml.rels", 4u * 1024 * 1024, relsXml, error)) {
            XmlNode rels;
            if (ParseXml(relsXml, rels, 20000, 30)) {
                const XmlNode* relationships = FindChild(&rels, L"Relationships");
                if (relationships) {
                    for (const auto& rel : relationships->children) {
                        if (rel->name != L"Relationship") continue;
                        const std::wstring* id = rel->Attr(L"Id");
                        const std::wstring* target = rel->Attr(L"Target");
                        if (id && target) relTargets[*id] = *target;
                    }
                }
            }
        }
    }

    const size_t kMaxSlides = 120;
    for (const auto& rid : slideIds) {
        if (m_slides.size() >= kMaxSlides) break;
        auto it = relTargets.find(rid);
        if (it == relTargets.end()) continue;

        std::wstring entry = it->second;
        if (entry.rfind(L"/", 0) == 0) entry = entry.substr(1);
        if (entry.rfind(L"ppt/", 0) != 0) entry = L"ppt/" + entry;

        std::string slideXml;
        if (!ReadPart(filePath, entry, 32u * 1024 * 1024, slideXml, error)) continue;

        XmlNode slideRoot;
        if (!ParseXml(slideXml, slideRoot, 200000, 80)) continue;

        OfficeSlide slide;
        const XmlNode* cSld = FindDescendant(&slideRoot, L"p:cSld", 8);
        const XmlNode* spTree = cSld ? FindChild(cSld, L"p:spTree") : nullptr;
        if (spTree) {
            for (const auto& node : spTree->children) {
                if (node->name == L"p:sp") {
                    std::wstring placeholder;
                    const XmlNode* ph = FindDescendant(node.get(), L"p:ph", 6);
                    if (ph) {
                        if (const std::wstring* type = ph->Attr(L"type")) placeholder = *type;
                    }
                    std::vector<OfficeBlock> shapeBlocks;
                    std::wstring shapeTitle;
                    const XmlNode* txBody = FindChild(node.get(), L"p:txBody");
                    if (!txBody) txBody = FindDescendant(node.get(), L"p:txBody", 6);
                    if (txBody) {
                        bool isTitle = (placeholder == L"title" || placeholder == L"ctrTitle");
                        AppendSlideParagraphs(txBody, shapeBlocks, isTitle ? &shapeTitle : nullptr, !isTitle);
                    }
                    if (!shapeTitle.empty() && slide.title.empty()) slide.title = shapeTitle;
                    for (auto& block : shapeBlocks) slide.blocks.push_back(std::move(block));
                } else if (node->name == L"p:graphicFrame") {
                    std::vector<OfficeBlock> frameBlocks;
                    AppendSlideParagraphs(node.get(), frameBlocks, nullptr, false);
                    for (auto& block : frameBlocks) slide.blocks.push_back(std::move(block));
                }
            }
        }

        if (slide.title.empty()) {
            for (const auto& block : slide.blocks) {
                if (!block.text.empty()) { slide.title = block.text; break; }
            }
        }
        if (slide.title.size() > 90) slide.title = slide.title.substr(0, 88) + L"…";
        m_slides.push_back(std::move(slide));
    }

    if (m_slides.empty()) {
        m_errorText = L"No slides in this presentation could be read.";
        return false;
    }
    return true;
}

bool OfficeEngine::LoadOdp(const std::wstring& filePath) {
    std::string xml;
    std::wstring error;
    if (!ReadPart(filePath, L"content.xml", 64u * 1024 * 1024, xml, error)) {
        m_errorText = error.empty() ? L"This presentation could not be read." : error;
        return false;
    }
    XmlNode root;
    if (!ParseXml(xml, root, 400000, 120)) {
        m_errorText = L"This presentation's XML could not be parsed.";
        return false;
    }

    const XmlNode* doc = FindChild(&root, L"office:document-content");
    const XmlNode* body = doc ? FindChild(doc, L"office:body") : nullptr;
    const XmlNode* presentation = body ? FindChild(body, L"office:presentation") : nullptr;
    if (!presentation) {
        m_errorText = L"This presentation has no slides.";
        return false;
    }

    const size_t kMaxSlides = 120;
    for (const auto& page : presentation->children) {
        if (page->name != L"draw:page") continue;
        if (m_slides.size() >= kMaxSlides) break;

        OfficeSlide slide;
        for (const auto& frame : page->children) {
            if (frame->name != L"draw:frame") continue;
            std::wstring frameClass;
            if (const std::wstring* cls = frame->Attr(L"presentation:class")) frameClass = *cls;
            bool isTitle = (frameClass == L"title" || frameClass == L"subtitle");

            std::vector<OfficeBlock> frameBlocks;
            std::wstring frameTitle;
            AppendSlideParagraphs(frame.get(), frameBlocks, isTitle ? &frameTitle : nullptr, !isTitle);
            if (isTitle && !frameTitle.empty() && slide.title.empty()) slide.title = frameTitle;
            for (auto& block : frameBlocks) slide.blocks.push_back(std::move(block));
        }

        if (slide.title.empty()) {
            for (const auto& block : slide.blocks) {
                if (!block.text.empty()) { slide.title = block.text; break; }
            }
        }
        if (slide.title.size() > 90) slide.title = slide.title.substr(0, 88) + L"…";
        m_slides.push_back(std::move(slide));
    }

    if (m_slides.empty()) {
        m_errorText = L"No slides in this presentation could be read.";
        return false;
    }
    return true;
}

void OfficeEngine::SetActiveSlide(int index) {
    if (index < 0 || index >= (int)m_slides.size()) return;
    m_activeSlide = index;
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
}

std::wstring OfficeEngine::SlideTitle(int index) const {
    if (index < 0 || index >= (int)m_slides.size()) return L"";
    return m_slides[(size_t)index].title;
}

bool OfficeEngine::GetSlideRect(int index, D2D1_RECT_F& out) const {
    for (const auto& entry : m_slideRects) {
        if (entry.second == index) {
            out = entry.first;
            return true;
        }
    }
    return false;
}

int OfficeEngine::HitTestSlide(float x, float y) const {
    for (const auto& entry : m_slideRects) {
        if (x >= entry.first.left && x <= entry.first.right &&
            y >= entry.first.top && y <= entry.first.bottom) {
            return entry.second;
        }
    }
    return -1;
}

void OfficeEngine::RenderSlides(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& rect,
                                 float marginX, float contentW, float curY) {
    if (m_slides.empty()) return;
    if (m_activeSlide < 0 || m_activeSlide >= (int)m_slides.size()) m_activeSlide = 0;

    m_slideRects.clear();

    const float stripW = 212.0f;
    const float cardH = 54.0f;
    const float stripTop = curY;
    const float stripBottom = rect.bottom - 16.0f;

    rt->PushAxisAlignedClip(D2D1::RectF(marginX, stripTop, marginX + stripW, stripBottom),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float cardY = stripTop + m_scrollY;
    for (int i = 0; i < (int)m_slides.size(); ++i) {
        D2D1_RECT_F card = D2D1::RectF(marginX, cardY, marginX + stripW - 12.0f, cardY + cardH - 6.0f);
        m_slideRects.emplace_back(card, i);
        if (card.bottom >= stripTop && card.top <= stripBottom) {
            bool active = (i == m_activeSlide);
            D2D1_ROUNDED_RECT r = D2D1::RoundedRect(card, 7.0f, 7.0f);
            rt->FillRoundedRectangle(r, active ? d2d.BrushCardSelected : d2d.BrushCard);
            rt->DrawRoundedRectangle(r, active ? (ID2D1Brush*)d2d.BrushAccent : (ID2D1Brush*)d2d.BrushBorder, 1.0f);

            WCHAR number[16];
            swprintf_s(number, L"%d", i + 1);
            rt->DrawText(number, (UINT32)wcslen(number),
                         d2d.FontBadge ? d2d.FontBadge : d2d.FontCaption,
                         D2D1::RectF(card.left + 10.0f, card.top + 6.0f, card.left + 34.0f, card.top + 26.0f),
                         d2d.BrushTextMuted);

            const std::wstring& title = m_slides[(size_t)i].title;
            IDWriteTextLayout* layout = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(title.c_str(), (UINT32)title.length(),
                m_formatBody ? m_formatBody : d2d.FontBody, card.right - card.left - 44.0f, 38.0f, &layout);
            if (layout) {
                layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                layout->SetTrimming(&trimming, nullptr);
                rt->DrawTextLayout(D2D1::Point2F(card.left + 38.0f, card.top + 6.0f), layout,
                                   active ? d2d.BrushText : d2d.BrushTextMuted);
                layout->Release();
            }
        }
        cardY += cardH;
    }

    rt->PopAxisAlignedClip();
    m_contentHeight = (float)m_slides.size() * cardH + 100.0f;

    // Current slide.
    float slideX = marginX + stripW + 24.0f;
    float slideW = (marginX + contentW) - slideX;
    if (slideW < 160.0f) return;

    const OfficeSlide& slide = m_slides[(size_t)m_activeSlide];
    float y = stripTop;

    D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(
        D2D1::RectF(slideX, y, slideX + slideW, (std::min)(stripBottom, y + 420.0f)), 10.0f, 10.0f);
    rt->FillRoundedRectangle(panel, d2d.BrushCard);
    rt->DrawRoundedRectangle(panel, d2d.BrushBorderSubtle, 1.0f);

    float innerX = slideX + 26.0f;
    float innerW = slideW - 52.0f;
    y += 24.0f;

    if (!slide.title.empty()) {
        IDWriteTextLayout* title = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(slide.title.c_str(), (UINT32)slide.title.length(),
            m_formatH2 ? m_formatH2 : d2d.FontTitle, innerW, 120.0f, &title);
        if (title) {
            title->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            title->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            float h = 0.0f;
            DWRITE_TEXT_METRICS metrics = {};
            title->GetMetrics(&metrics);
            h = metrics.height;
            rt->DrawTextLayout(D2D1::Point2F(innerX, y), title, d2d.BrushText);
            title->Release();
            y += h + 14.0f;
        }
    }

    for (const auto& block : slide.blocks) {
        if (y > stripBottom - 20.0f) break;
        float indent = 16.0f + (float)block.level * 18.0f;
        std::wstring text = L"•  " + block.text;
        IDWriteTextLayout* layout = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(text.c_str(), (UINT32)text.length(),
            m_formatBody ? m_formatBody : d2d.FontBody, innerW - indent, 200.0f, &layout);
        if (!layout) continue;
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);
        rt->DrawTextLayout(D2D1::Point2F(innerX + indent, y), layout, d2d.BrushText);
        layout->Release();
        y += metrics.height + 8.0f;
    }

    WCHAR counter[48];
    swprintf_s(counter, L"SLIDE %d / %d", m_activeSlide + 1, (int)m_slides.size());
    rt->DrawText(counter, (UINT32)wcslen(counter), d2d.FontBadge ? d2d.FontBadge : d2d.FontCaption,
                 D2D1::RectF(slideX + 4.0f, stripBottom - 22.0f, slideX + slideW, stripBottom),
                 d2d.BrushTextMuted);
}

// ---------------------------------------------------------------------------
// EPUB
// ---------------------------------------------------------------------------

namespace {

std::wstring ResolveHref(const std::wstring& baseDir, const std::wstring& href) {
    std::wstring path = href;
    size_t hash = path.find(L'#');
    if (hash != std::wstring::npos) path.resize(hash);
    for (auto& ch : path) {
        if (ch == L'\\') ch = L'/';
    }
    if (path.empty()) return path;
    if (path[0] == L'/') return path.substr(1);
    if (!baseDir.empty()) path = baseDir + L"/" + path;

    // Collapse "." and ".." segments.
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find(L'/', start);
        if (slash == std::wstring::npos) slash = path.size();
        std::wstring part = path.substr(start, slash - start);
        if (part == L"..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != L".") {
            parts.push_back(part);
        }
        start = slash + 1;
    }
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out.push_back(L'/');
        out += parts[i];
    }
    return out;
}

int HeadingLevelForTag(const std::wstring& name) {
    if (name.size() != 2) return 0;
    if (name[0] != L'h') return 0;
    wchar_t digit = name[1];
    if (digit < L'1' || digit > L'6') return 0;
    int level = digit - L'0';
    return level <= 2 ? level : 3;
}

} // namespace

bool OfficeEngine::LoadEpub(const std::wstring& filePath) {
    std::string xml;
    std::wstring error;

    if (!ReadPart(filePath, L"META-INF/container.xml", 1024 * 1024, xml, error)) {
        m_errorText = error.empty() ? L"This EPUB has no container." : error;
        return false;
    }
    XmlNode container;
    if (!ParseXml(xml, container, 5000, 40)) {
        m_errorText = L"This EPUB's container could not be parsed.";
        return false;
    }
    const XmlNode* rootfile = FindDescendant(&container, L"rootfile", 8);
    std::wstring opfPath;
    if (rootfile) {
        if (const std::wstring* full = rootfile->Attr(L"full-path")) opfPath = *full;
    }
    if (opfPath.empty()) {
        m_errorText = L"This EPUB does not name its package document.";
        return false;
    }

    std::string opfXml;
    if (!ReadPart(filePath, opfPath, 16u * 1024 * 1024, opfXml, error)) {
        m_errorText = error.empty() ? L"This EPUB's package document is missing." : error;
        return false;
    }
    XmlNode opf;
    if (!ParseXml(opfXml, opf, 100000, 60)) {
        m_errorText = L"This EPUB's package document could not be parsed.";
        return false;
    }

    std::wstring opfDir = opfPath;
    size_t slash = opfDir.find_last_of(L'/');
    opfDir = (slash == std::wstring::npos) ? L"" : opfDir.substr(0, slash);

    const XmlNode* package = FindChild(&opf, L"package");
    if (!package) package = FindDescendant(&opf, L"package", 4);

    // Metadata title.
    if (package) {
        const XmlNode* metadata = FindChild(package, L"metadata");
        if (metadata) {
            for (const auto& child : metadata->children) {
                std::wstring name = child->name;
                if (name == L"dc:title" || name == L"title") {
                    std::wstring value = CollapseWhitespace(TextContent(child.get()));
                    if (!value.empty()) { m_title = value; break; }
                }
            }
        }
    }

    // Manifest: id -> href.
    std::map<std::wstring, std::wstring> manifest;
    std::map<std::wstring, std::wstring> manifestType;
    if (package) {
        const XmlNode* items = FindChild(package, L"manifest");
        if (items) {
            for (const auto& item : items->children) {
                if (item->name != L"item") continue;
                const std::wstring* id = item->Attr(L"id");
                const std::wstring* href = item->Attr(L"href");
                if (!id || !href) continue;
                manifest[*id] = *href;
                if (const std::wstring* type = item->Attr(L"media-type")) manifestType[*id] = *type;
            }
        }
    }

    // Spine order.
    std::vector<std::wstring> spine;
    if (package) {
        const XmlNode* itemrefs = FindChild(package, L"spine");
        if (itemrefs) {
            for (const auto& ref : itemrefs->children) {
                if (ref->name != L"itemref") continue;
                if (const std::wstring* idref = ref->Attr(L"idref")) {
                    if (manifest.find(*idref) != manifest.end()) spine.push_back(*idref);
                }
            }
        }
    }
    if (spine.empty()) {
        m_errorText = L"This EPUB has no reading order.";
        return false;
    }

    const size_t kMaxChapters = 300;
    int chapterNumber = 0;
    for (const auto& id : spine) {
        if (chapterNumber >= (int)kMaxChapters) break;
        std::wstring href = ResolveHref(opfDir, manifest[id]);
        if (href.empty()) continue;
        auto type = manifestType.find(id);
        if (type != manifestType.end() && type->second.find(L"html") == std::wstring::npos) continue;

        std::string chapterXml;
        if (!ReadPart(filePath, href, 32u * 1024 * 1024, chapterXml, error)) continue;
        ++chapterNumber;
        ParseXhtmlChapter(Utf8ToWideLocal(chapterXml), L"Chapter " + std::to_wstring(chapterNumber), chapterNumber);
    }

    if (m_blocks.empty()) {
        m_errorText = L"This EPUB has no readable text.";
        return false;
    }
    return true;
}

void OfficeEngine::ParseXhtmlChapter(const std::wstring& xhtml, const std::wstring& fallbackTitle,
                                      int chapterNumber) {
    (void)chapterNumber;
    // Parse the chapter as XML (EPUB3 chapters are XHTML; tolerate the common
    // named entities by reusing the tolerant reader).
    std::string utf8;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, xhtml.c_str(), (int)xhtml.size(), NULL, 0, NULL, NULL);
    if (bytes > 0) {
        utf8.resize((size_t)bytes);
        WideCharToMultiByte(CP_UTF8, 0, xhtml.c_str(), (int)xhtml.size(), &utf8[0], bytes, NULL, NULL);
    }
    XmlNode root;
    if (!ParseXml(utf8, root, 300000, 120)) return;

    const XmlNode* body = FindDescendant(&root, L"body", 10);

    size_t chapterStart = m_blocks.size();
    std::wstring chapterTitle;

    std::function<void(const XmlNode*, int)> walk = [&](const XmlNode* parent, int listLevel) {
        for (const auto& node : parent->children) {
            const std::wstring& name = node->name;
            int heading = HeadingLevelForTag(name);
            if (heading > 0) {
                std::wstring text = CollapseWhitespace(TextContent(node.get()));
                if (text.empty()) continue;
                // A chapter's leading heading is the chapter title; the marker
                // block already renders it, so don't add it twice.
                if (chapterTitle.empty() && m_blocks.size() == chapterStart) {
                    chapterTitle = text;
                    continue;
                }
                OfficeBlock block;
                block.type = heading == 1 ? OfficeBlockType::Heading1
                           : heading == 2 ? OfficeBlockType::Heading2 : OfficeBlockType::Heading3;
                block.text = text;
                ++m_headingCount;
                m_blocks.push_back(std::move(block));
            } else if (name == L"p" || name == L"pre" || name == L"code" || name == L"dt" || name == L"dd") {
                std::wstring text = CollapseWhitespace(TextContent(node.get()));
                if (!text.empty()) {
                    OfficeBlock block;
                    block.type = listLevel > 0 ? OfficeBlockType::Bullet : OfficeBlockType::Paragraph;
                    block.level = (std::min)(listLevel, 4);
                    block.text = text;
                    ++m_paragraphCount;
                    m_blocks.push_back(std::move(block));
                }
            } else if (name == L"li") {
                std::wstring text = CollapseWhitespace(TextContent(node.get()));
                if (!text.empty()) {
                    OfficeBlock block;
                    block.type = OfficeBlockType::Bullet;
                    block.level = (std::min)(listLevel, 4);
                    block.text = text;
                    ++m_paragraphCount;
                    m_blocks.push_back(std::move(block));
                }
            } else if (name == L"blockquote") {
                std::wstring text = CollapseWhitespace(TextContent(node.get()));
                if (!text.empty()) {
                    OfficeBlock block;
                    block.type = OfficeBlockType::Quote;
                    block.text = text;
                    ++m_paragraphCount;
                    m_blocks.push_back(std::move(block));
                }
            } else if (name == L"img") {
                const std::wstring* alt = node->Attr(L"alt");
                OfficeBlock block;
                block.type = OfficeBlockType::Paragraph;
                block.text = (alt && !alt->empty()) ? (L"[image: " + *alt + L"]") : L"[image]";
                m_blocks.push_back(std::move(block));
            } else if (name == L"ul" || name == L"ol") {
                walk(node.get(), listLevel + 1);
            } else if (name == L"table") {
                OfficeBlock table;
                table.type = OfficeBlockType::Table;
                std::function<void(const XmlNode*)> collectRows = [&](const XmlNode* holder) {
                    for (const auto& row : holder->children) {
                        if (row->name != L"tr") continue;
                        OfficeTableRow outRow;
                        for (const auto& cell : row->children) {
                            if (cell->name != L"td" && cell->name != L"th") continue;
                            outRow.cells.push_back(CollapseWhitespace(TextContent(cell.get())));
                            if (cell->name == L"th") outRow.header = true;
                        }
                        if (!outRow.cells.empty()) table.rows.push_back(std::move(outRow));
                    }
                };
                collectRows(node.get());
                if (!table.rows.empty()) {
                    ++m_tableCount;
                    m_blocks.push_back(std::move(table));
                }
            } else if (name == L"br" || name == L"hr") {
                continue;
            } else if (name == L"style" || name == L"script" || name == L"head") {
                continue;
            } else {
                walk(node.get(), listLevel);
            }
        }
    };

    if (body) walk(body, 0);
    if (m_blocks.size() == chapterStart) return;

    OfficeChapter chapter;
    chapter.title = chapterTitle.empty() ? fallbackTitle : chapterTitle;
    chapter.blockIndex = chapterStart;

    OfficeBlock marker;
    marker.type = OfficeBlockType::ChapterTitle;
    marker.text = chapter.title;
    marker.level = (int)m_chapters.size() + 1;
    m_blocks.insert(m_blocks.begin() + (ptrdiff_t)chapterStart, std::move(marker));
    // The inserted marker shifts the chapter's first real block by one.
    chapter.blockIndex = chapterStart;
    m_chapters.push_back(std::move(chapter));
}

bool OfficeEngine::GoToChapter(int index) {
    if (index < 0 || index >= (int)m_chapters.size()) return false;
    m_activeChapter = index;
    float offset = 0.0f;
    size_t blockIndex = m_chapters[(size_t)index].blockIndex;
    if (blockIndex < m_blockOffsets.size()) {
        offset = m_blockOffsets[blockIndex] - 24.0f;
        if (offset < 0.0f) offset = 0.0f;
    }
    m_scrollY = -offset;
    m_targetScrollY = m_scrollY;
    return true;
}

std::wstring OfficeEngine::ChapterTitle(int index) const {
    if (index < 0 || index >= (int)m_chapters.size()) return L"";
    return m_chapters[(size_t)index].title;
}

float OfficeEngine::ChapterOffset(int index) const {
    if (index < 0 || index >= (int)m_chapters.size()) return 0.0f;
    size_t blockIndex = m_chapters[(size_t)index].blockIndex;
    if (blockIndex < m_blockOffsets.size()) return m_blockOffsets[blockIndex];
    return 0.0f;
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

    if (engine.GetKind() == OfficeKind::Book) {
        wprintf(L"chapters=%d active=%d\n", engine.ChapterCount(), engine.ActiveChapter());
        for (int i = 0; i < engine.ChapterCount(); ++i) {
            wprintf(L"chapter[%d] title='%ls'\n", i, engine.ChapterTitle(i).c_str());
        }
        const std::wstring text = engine.GetPlainText();
        wprintf(L"--- text ---\n%ls\n", text.c_str());
        return 0;
    }
    if (engine.GetKind() == OfficeKind::Slide) {
        wprintf(L"slides=%d active=%d\n", engine.SlideCount(), engine.ActiveSlide());
        for (int i = 0; i < engine.SlideCount(); ++i) {
            wprintf(L"slide[%d] title='%ls'\n", i, engine.SlideTitle(i).c_str());
        }
        // Body text of the first slide, one bullet per line.
        {
            std::wstring text = engine.GetPlainText();
            size_t start = text.find(L"--- Slide 2 ---");
            if (start == std::wstring::npos) start = text.size();
            wprintf(L"first slide body:\n%ls\n", text.substr(0, start).c_str());
        }
        return 0;
    }
    if (engine.GetKind() == OfficeKind::Sheet) {
        wprintf(L"sheets=%d active=%d\n", engine.SheetCount(), engine.ActiveSheet());
        for (int i = 0; i < engine.SheetCount(); ++i) {
            wprintf(L"sheet[%d] name='%ls'\n", i, engine.SheetName(i).c_str());
        }
        const OfficeSheet* sheet = engine.GetSheet(engine.ActiveSheet());
        if (sheet) {
            wprintf(L"rows=%u columns=%u\n", (unsigned)sheet->rows.size(), (unsigned)sheet->columns);
            auto printRow = [](const std::vector<OfficeCell>& row) {
                std::wstring line;
                for (size_t c = 0; c < row.size(); ++c) {
                    if (c) line += L"\t";
                    line += row[c].text;
                }
                wprintf(L"%ls\n", line.c_str());
            };
            size_t maxRows = (std::min)(sheet->rows.size(), (size_t)8);
            for (size_t r = 0; r < maxRows; ++r) printRow(sheet->rows[r]);
            if (sheet->rows.size() > maxRows) {
                wprintf(L"... (%u more rows)\n", (unsigned)(sheet->rows.size() - maxRows));
                printRow(sheet->rows.back());
            }
        }
        return 0;
    }

    const std::wstring text = engine.GetPlainText();
    wprintf(L"--- plain text ---\n%ls\n", text.c_str());
    return 0;
}

} // namespace WinPreview
