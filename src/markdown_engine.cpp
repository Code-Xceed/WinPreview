#include "markdown_engine.h"
#include "d2d_engine.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace WinPreview {

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &out[0], size);
    return out;
}

MarkdownEngine::MarkdownEngine() {}

MarkdownEngine::~MarkdownEngine() {
    Close();
}

void MarkdownEngine::Close() {
    m_isLoaded = false;
    m_isNotebook = false;
    m_filePath.clear();
    m_blocks.clear();
    m_scrollY = 0.0f;
    m_contentHeight = 0.0f;
    m_cellCount = 0;
    m_kernelName.clear();
}

bool MarkdownEngine::LoadMarkdownFile(const std::wstring& filePath) {
    Close();
    std::ifstream file(filePath.c_str(), std::ios::binary);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    m_filePath = filePath;
    m_isNotebook = false;
    ParseMarkdown(content);
    m_isLoaded = true;
    return true;
}

bool MarkdownEngine::LoadNotebookFile(const std::wstring& filePath) {
    Close();
    std::ifstream file(filePath.c_str(), std::ios::binary);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    m_filePath = filePath;
    m_isNotebook = true;
    ParseNotebook(content);
    m_isLoaded = true;
    return true;
}

void MarkdownEngine::ParseMarkdown(const std::string& rawText) {
    std::stringstream ss(rawText);
    std::string line;
    bool inCodeBlock = false;
    std::wstring codeAccum;
    std::wstring codeLang;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Code block toggle
        if (line.rfind("```", 0) == 0) {
            if (inCodeBlock) {
                MdBlock b;
                b.type = MdBlockType::CodeBlock;
                b.text = codeAccum;
                b.extra = codeLang;
                m_blocks.push_back(b);
                codeAccum.clear();
                codeLang.clear();
                inCodeBlock = false;
            } else {
                inCodeBlock = true;
                std::string lang = line.substr(3);
                while (!lang.empty() && (lang.front() == ' ' || lang.front() == '`')) lang.erase(0, 1);
                codeLang = Utf8ToWide(lang);
            }
            continue;
        }

        if (inCodeBlock) {
            codeAccum += Utf8ToWide(line) + L"\n";
            continue;
        }

        // Blank lines
        if (line.empty()) {
            continue;
        }

        // Headers
        if (line.rfind("### ", 0) == 0) {
            MdBlock b;
            b.type = MdBlockType::Header3;
            b.text = Utf8ToWide(line.substr(4));
            m_blocks.push_back(b);
        } else if (line.rfind("## ", 0) == 0) {
            MdBlock b;
            b.type = MdBlockType::Header2;
            b.text = Utf8ToWide(line.substr(3));
            m_blocks.push_back(b);
        } else if (line.rfind("# ", 0) == 0) {
            MdBlock b;
            b.type = MdBlockType::Header1;
            b.text = Utf8ToWide(line.substr(2));
            m_blocks.push_back(b);
        } else if (line.rfind("> ", 0) == 0) {
            MdBlock b;
            b.type = MdBlockType::Blockquote;
            b.text = Utf8ToWide(line.substr(2));
            m_blocks.push_back(b);
        } else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
            MdBlock b;
            b.type = MdBlockType::BulletItem;
            b.text = Utf8ToWide(line.substr(2));
            m_blocks.push_back(b);
        } else if (line == "---" || line == "***" || line == "___") {
            MdBlock b;
            b.type = MdBlockType::Divider;
            m_blocks.push_back(b);
        } else {
            MdBlock b;
            b.type = MdBlockType::Paragraph;
            b.text = Utf8ToWide(line);
            m_blocks.push_back(b);
        }
    }

    if (inCodeBlock && !codeAccum.empty()) {
        MdBlock b;
        b.type = MdBlockType::CodeBlock;
        b.text = codeAccum;
        b.extra = codeLang;
        m_blocks.push_back(b);
    }
}

void MarkdownEngine::ParseNotebook(const std::string& rawJson) {
    // Extract kernel display name
    size_t kpos = rawJson.find("\"display_name\"");
    if (kpos != std::string::npos) {
        size_t cpos = rawJson.find(':', kpos);
        size_t q1 = rawJson.find('\"', cpos);
        size_t q2 = (q1 != std::string::npos) ? rawJson.find('\"', q1 + 1) : std::string::npos;
        if (q1 != std::string::npos && q2 != std::string::npos) {
            m_kernelName = Utf8ToWide(rawJson.substr(q1 + 1, q2 - q1 - 1));
        }
    }
    if (m_kernelName.empty()) {
        m_kernelName = L"Python 3 (IPython Kernel)";
    }

    // Search cells
    size_t cellsPos = rawJson.find("\"cells\"");
    if (cellsPos == std::string::npos) return;

    size_t cur = cellsPos;
    int execIdx = 1;

    while (true) {
        size_t typePos = rawJson.find("\"cell_type\"", cur);
        if (typePos == std::string::npos) break;

        size_t q1 = rawJson.find('\"', typePos + 11);
        size_t q2 = (q1 != std::string::npos) ? rawJson.find('\"', q1 + 1) : std::string::npos;
        if (q1 == std::string::npos || q2 == std::string::npos) break;

        std::string cellType = rawJson.substr(q1 + 1, q2 - q1 - 1);
        m_cellCount++;

        // Find "source": [ ... ]
        size_t srcPos = rawJson.find("\"source\"", q2);
        if (srcPos == std::string::npos) break;

        size_t arrStart = rawJson.find('[', srcPos);
        size_t arrEnd = (arrStart != std::string::npos) ? rawJson.find(']', arrStart) : std::string::npos;
        if (arrStart == std::string::npos || arrEnd == std::string::npos) break;

        std::string srcSlice = rawJson.substr(arrStart + 1, arrEnd - arrStart - 1);

        // Parse string lines in source array
        std::wstring cellText;
        size_t sidx = 0;
        while (true) {
            size_t sq1 = srcSlice.find('\"', sidx);
            if (sq1 == std::string::npos) break;
            size_t sq2 = sq1 + 1;
            while (sq2 < srcSlice.length()) {
                if (srcSlice[sq2] == '\"' && srcSlice[sq2 - 1] != '\\') break;
                sq2++;
            }
            if (sq2 >= srcSlice.length()) break;

            std::string lineStr = srcSlice.substr(sq1 + 1, sq2 - sq1 - 1);
            // Handle unescape characters
            std::string unescaped;
            for (size_t c = 0; c < lineStr.length(); ++c) {
                if (lineStr[c] == '\\' && c + 1 < lineStr.length()) {
                    if (lineStr[c + 1] == 'n') { unescaped += '\n'; c++; continue; }
                    if (lineStr[c + 1] == '\"') { unescaped += '\"'; c++; continue; }
                    if (lineStr[c + 1] == '\\') { unescaped += '\\'; c++; continue; }
                    if (lineStr[c + 1] == 't') { unescaped += '\t'; c++; continue; }
                }
                unescaped += lineStr[c];
            }
            cellText += Utf8ToWide(unescaped);
            sidx = sq2 + 1;
        }

        if (cellType == "markdown") {
            ParseMarkdown(std::string(cellText.begin(), cellText.end()));
        } else if (cellType == "code") {
            WCHAR promptBuf[32];
            swprintf_s(promptBuf, L"In [%d]:", execIdx);

            MdBlock hb;
            hb.type = MdBlockType::NotebookCellHeader;
            hb.text = promptBuf;
            m_blocks.push_back(hb);

            MdBlock cb;
            cb.type = MdBlockType::NotebookCode;
            cb.text = cellText;
            cb.extra = L"python";
            m_blocks.push_back(cb);

            // Look for text outputs in the same cell block before next cell
            size_t nextCell = rawJson.find("\"cell_type\"", arrEnd);
            size_t outPos = rawJson.find("\"text\"", arrEnd);
            if (outPos != std::string::npos && (nextCell == std::string::npos || outPos < nextCell)) {
                size_t oarrStart = rawJson.find('[', outPos);
                size_t oarrEnd = (oarrStart != std::string::npos) ? rawJson.find(']', oarrStart) : std::string::npos;
                if (oarrStart != std::string::npos && oarrEnd != std::string::npos) {
                    std::string outSlice = rawJson.substr(oarrStart + 1, oarrEnd - oarrStart - 1);
                    std::wstring outText;
                    size_t osidx = 0;
                    while (true) {
                        size_t osq1 = outSlice.find('\"', osidx);
                        if (osq1 == std::string::npos) break;
                        size_t osq2 = osq1 + 1;
                        while (osq2 < outSlice.length()) {
                            if (outSlice[osq2] == '\"' && outSlice[osq2 - 1] != '\\') break;
                            osq2++;
                        }
                        if (osq2 >= outSlice.length()) break;
                        std::string oline = outSlice.substr(osq1 + 1, osq2 - osq1 - 1);
                        outText += Utf8ToWide(oline);
                        osidx = osq2 + 1;
                    }
                    if (!outText.empty()) {
                        MdBlock ob;
                        ob.type = MdBlockType::NotebookOutput;
                        WCHAR outPrompt[32];
                        swprintf_s(outPrompt, L"Out [%d]:", execIdx);
                        ob.extra = outPrompt;
                        ob.text = outText;
                        m_blocks.push_back(ob);
                    }
                }
            }

            execIdx++;
        }

        cur = arrEnd;
    }
}

void MarkdownEngine::Scroll(float dy) {
    m_scrollY += dy;
    if (m_scrollY > 0.0f) m_scrollY = 0.0f;
    float maxScroll = (std::max)(0.0f, m_contentHeight - 400.0f);
    if (m_scrollY < -maxScroll) m_scrollY = -maxScroll;
}

void MarkdownEngine::Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale) {
    if (!rt || !m_isLoaded) return;

    float clientW = targetRect.right - targetRect.left;
    float clientH = targetRect.bottom - targetRect.top;
    float marginX = (std::max)(48.0f, (clientW - 860.0f) / 2.0f);
    float textW = clientW - marginX * 2.0f;

    float curY = targetRect.top + 36.0f + m_scrollY;

    float viewTop = targetRect.top - 60.0f;
    float viewBottom = targetRect.bottom + 60.0f;

    for (size_t i = 0; i < m_blocks.size(); ++i) {
        auto& b = m_blocks[i];
        if (b.height > 0.0f && (curY + b.height < viewTop || curY > viewBottom)) {
            curY += b.height;
            continue;
        }

        float blockStartY = curY;

        if (b.type == MdBlockType::Header1) {
            curY += 12.0f;
            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.text.c_str(), (UINT32)b.text.length(),
                d2d.FontHero ? d2d.FontHero : d2d.FontTitle, textW, 42.0f, &tl);
            if (tl) {
                rt->DrawTextLayout(D2D1::Point2F(marginX, curY), tl, d2d.BrushText);
                tl->Release();
            }
            curY += 46.0f;
            rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + textW, curY), d2d.BrushBorderSubtle, 1.0f);
            curY += 18.0f;
        }
        else if (b.type == MdBlockType::Header2) {
            curY += 10.0f;
            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.text.c_str(), (UINT32)b.text.length(),
                d2d.FontTitle ? d2d.FontTitle : d2d.FontBody, textW, 32.0f, &tl);
            if (tl) {
                rt->DrawTextLayout(D2D1::Point2F(marginX, curY), tl, d2d.BrushText);
                tl->Release();
            }
            curY += 36.0f;
        }
        else if (b.type == MdBlockType::Header3) {
            curY += 8.0f;
            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.text.c_str(), (UINT32)b.text.length(),
                d2d.FontBody ? d2d.FontBody : d2d.FontCaption, textW, 26.0f, &tl);
            if (tl) {
                rt->DrawTextLayout(D2D1::Point2F(marginX, curY), tl, d2d.BrushText);
                tl->Release();
            }
            curY += 30.0f;
        }
        else if (b.type == MdBlockType::Paragraph) {
            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.text.c_str(), (UINT32)b.text.length(),
                d2d.FontBody, textW, 600.0f, &tl);
            if (tl) {
                DWRITE_TEXT_METRICS tm;
                tl->GetMetrics(&tm);
                rt->DrawTextLayout(D2D1::Point2F(marginX, curY), tl, d2d.BrushText);
                curY += tm.height + 12.0f;
                tl->Release();
            } else {
                curY += 24.0f;
            }
        }
        else if (b.type == MdBlockType::BulletItem) {
            // Bullet dot
            D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(marginX + 8.0f, curY + 11.0f), 2.5f, 2.5f);
            rt->FillEllipse(dot, d2d.BrushAccent);

            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.text.c_str(), (UINT32)b.text.length(),
                d2d.FontBody, textW - 24.0f, 400.0f, &tl);
            if (tl) {
                DWRITE_TEXT_METRICS tm;
                tl->GetMetrics(&tm);
                rt->DrawTextLayout(D2D1::Point2F(marginX + 20.0f, curY), tl, d2d.BrushText);
                curY += tm.height + 8.0f;
                tl->Release();
            } else {
                curY += 20.0f;
            }
        }
        else if (b.type == MdBlockType::Blockquote) {
            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.text.c_str(), (UINT32)b.text.length(),
                d2d.FontBody, textW - 28.0f, 400.0f, &tl);
            if (tl) {
                DWRITE_TEXT_METRICS tm;
                tl->GetMetrics(&tm);
                // Vertical accent bar on left
                rt->DrawLine(D2D1::Point2F(marginX + 4.0f, curY), D2D1::Point2F(marginX + 4.0f, curY + tm.height + 6.0f), d2d.BrushAccent, 3.0f);
                rt->DrawTextLayout(D2D1::Point2F(marginX + 18.0f, curY + 2.0f), tl, d2d.BrushTextMuted);
                curY += tm.height + 14.0f;
                tl->Release();
            } else {
                curY += 20.0f;
            }
        }
        else if (b.type == MdBlockType::Divider) {
            curY += 8.0f;
            rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + textW, curY), d2d.BrushBorderSubtle, 1.0f);
            curY += 16.0f;
        }
        else if (b.type == MdBlockType::CodeBlock || b.type == MdBlockType::NotebookCode) {
            curY += 6.0f;
            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.text.c_str(), (UINT32)b.text.length(),
                d2d.FontMono, textW - 32.0f, 3000.0f, &tl);
            if (tl) {
                DWRITE_TEXT_METRICS tm;
                tl->GetMetrics(&tm);
                float cardH = tm.height + 28.0f;

                D2D1_ROUNDED_RECT cardR = D2D1::RoundedRect(D2D1::RectF(marginX, curY, marginX + textW, curY + cardH), 6.0f, 6.0f);
                rt->FillRoundedRectangle(cardR, d2d.BrushCard);
                rt->DrawRoundedRectangle(cardR, d2d.BrushBorder, 1.0f);

                // Language badge on top right
                if (!b.extra.empty()) {
                    IDWriteTextLayout* langL = nullptr;
                    d2d.GetDWriteFactory()->CreateTextLayout(b.extra.c_str(), (UINT32)b.extra.length(), d2d.FontBadge, 120.0f, 18.0f, &langL);
                    if (langL) {
                        langL->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                        rt->DrawTextLayout(D2D1::Point2F(marginX + textW - 136.0f, curY + 8.0f), langL, d2d.BrushTextMuted);
                        langL->Release();
                    }
                }

                rt->DrawTextLayout(D2D1::Point2F(marginX + 16.0f, curY + 14.0f), tl, d2d.BrushText);
                curY += cardH + 14.0f;
                tl->Release();
            } else {
                curY += 30.0f;
            }
        }
        else if (b.type == MdBlockType::NotebookCellHeader) {
            curY += 10.0f;
            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.text.c_str(), (UINT32)b.text.length(),
                d2d.FontMono, 120.0f, 20.0f, &tl);
            if (tl) {
                rt->DrawTextLayout(D2D1::Point2F(marginX, curY), tl, d2d.BrushAccent);
                curY += 22.0f;
                tl->Release();
            } else {
                curY += 22.0f;
            }
        }
        else if (b.type == MdBlockType::NotebookOutput) {
            curY += 4.0f;
            IDWriteTextLayout* promptL = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.extra.c_str(), (UINT32)b.extra.length(),
                d2d.FontMono, 120.0f, 20.0f, &promptL);
            if (promptL) {
                rt->DrawTextLayout(D2D1::Point2F(marginX, curY), promptL, d2d.BrushTextMuted);
                promptL->Release();
            }
            curY += 22.0f;

            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(b.text.c_str(), (UINT32)b.text.length(),
                d2d.FontMono, textW - 24.0f, 1500.0f, &tl);
            if (tl) {
                DWRITE_TEXT_METRICS tm;
                tl->GetMetrics(&tm);
                rt->DrawTextLayout(D2D1::Point2F(marginX + 16.0f, curY), tl, d2d.BrushTextMuted);
                curY += tm.height + 14.0f;
                tl->Release();
            } else {
                curY += 20.0f;
            }
        }

        b.height = curY - blockStartY;
    }

    m_contentHeight = curY - (targetRect.top + 36.0f + m_scrollY);
}

std::wstring MarkdownEngine::GetFormatBadge() const {
    if (m_isNotebook) {
        WCHAR buf[128];
        swprintf_s(buf, L"JUPYTER NOTEBOOK • %u CELLS • %s", m_cellCount, m_kernelName.c_str());
        return buf;
    }
    return L"MARKDOWN • RICH READER";
}

} // namespace WinPreview
