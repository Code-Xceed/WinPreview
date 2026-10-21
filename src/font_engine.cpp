#include "font_engine.h"
#include "d2d_engine.h"
#include <algorithm>
#include <cmath>

namespace WinPreview {

static inline UINT16 ReadBE16(const uint8_t* p) {
    return (UINT16)((p[0] << 8) | p[1]);
}

FontEngine::FontEngine() {}

FontEngine::~FontEngine() {
    Close();
}

void FontEngine::Close() {
    SafeRelease(m_fmtHero);
    SafeRelease(m_fmtLarge);
    SafeRelease(m_fmtMedium);
    SafeRelease(m_fmtSmall);
    SafeRelease(m_fmtMono);
    SafeRelease(m_fontFace);
    SafeRelease(m_fontFile);

    m_isLoaded = false;
    m_info = {};
    m_filePath.clear();
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
    m_contentHeight = 0.0f;
    m_dwrite = nullptr;
}

void FontEngine::ParseNameTable(const void* tableData, UINT32 tableSize) {
    if (!tableData || tableSize < 6) return;
    const uint8_t* data = (const uint8_t*)tableData;

    UINT16 format = ReadBE16(data);
    UINT16 count = ReadBE16(data + 2);
    UINT16 stringOffset = ReadBE16(data + 4);

    if (tableSize < (UINT32)(6 + count * 12)) return;
    const uint8_t* strings = data + stringOffset;

    for (UINT16 i = 0; i < count; ++i) {
        const uint8_t* rec = data + 6 + i * 12;
        UINT16 platformID = ReadBE16(rec);
        UINT16 encodingID = ReadBE16(rec + 2);
        UINT16 nameID     = ReadBE16(rec + 6);
        UINT16 length     = ReadBE16(rec + 8);
        UINT16 offset     = ReadBE16(rec + 10);

        if ((size_t)(stringOffset + offset + length) > tableSize) continue;
        const uint8_t* strPtr = strings + offset;

        std::wstring str;
        // Windows Unicode (UTF-16BE)
        if (platformID == 3 && (encodingID == 1 || encodingID == 10)) {
            size_t numChars = length / 2;
            str.resize(numChars);
            for (size_t c = 0; c < numChars; ++c) {
                str[c] = (wchar_t)ReadBE16(strPtr + c * 2);
            }
        }
        // Macintosh / ASCII
        else if (platformID == 1 && encodingID == 0) {
            str.resize(length);
            for (size_t c = 0; c < length; ++c) {
                str[c] = (wchar_t)strPtr[c];
            }
        }

        if (!str.empty()) {
            if (nameID == 1 && m_info.familyName.empty()) {
                m_info.familyName = str;
            } else if (nameID == 2 && m_info.styleName.empty()) {
                m_info.styleName = str;
            } else if (nameID == 5 && m_info.version.empty()) {
                m_info.version = str;
            } else if (nameID == 0 && m_info.copyright.empty()) {
                m_info.copyright = str;
            }
        }
    }
}

bool FontEngine::LoadFromFile(IDWriteFactory* dwrite, const std::wstring& filePath) {
    Close();
    if (!dwrite) return false;
    m_dwrite = dwrite;
    m_filePath = filePath;

    HRESULT hr = dwrite->CreateFontFileReference(filePath.c_str(), NULL, &m_fontFile);
    if (FAILED(hr) || !m_fontFile) return false;

    BOOL isSupported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType;
    DWRITE_FONT_FACE_TYPE faceType;
    UINT32 numFaces = 0;
    hr = m_fontFile->Analyze(&isSupported, &fileType, &faceType, &numFaces);
    if (FAILED(hr) || !isSupported) return false;

    hr = dwrite->CreateFontFace(faceType, 1, &m_fontFile, 0, DWRITE_FONT_SIMULATIONS_NONE, &m_fontFace);
    if (FAILED(hr) || !m_fontFace) return false;

    // File size
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        m_info.fileSizeBytes = ((UINT64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    }

    // Font Face Metrics
    DWRITE_FONT_METRICS metrics = {};
    m_fontFace->GetMetrics(&metrics);
    m_info.glyphCount = m_fontFace->GetGlyphCount();
    m_info.designUnitsPerEm = metrics.designUnitsPerEm;
    m_info.ascent = metrics.ascent;
    m_info.descent = metrics.descent;
    m_info.lineGap = metrics.lineGap;
    m_info.capHeight = metrics.capHeight;
    m_info.xHeight = metrics.xHeight;

    if (faceType == DWRITE_FONT_FACE_TYPE_TRUETYPE) {
        m_info.formatType = L"TRUETYPE";
    } else if (faceType == DWRITE_FONT_FACE_TYPE_CFF || faceType == DWRITE_FONT_FACE_TYPE_RAW_CFF) {
        m_info.formatType = L"OPENTYPE";
    } else {
        m_info.formatType = L"VECTOR FONT";
    }

    // Parse 'name' table (table tag: 0x656d616e)
    const void* tableData = nullptr;
    UINT32 tableSize = 0;
    void* tableContext = nullptr;
    BOOL exists = FALSE;
    hr = m_fontFace->TryGetFontTable(0x656d616e, &tableData, &tableSize, &tableContext, &exists);
    if (SUCCEEDED(hr) && exists && tableData) {
        ParseNameTable(tableData, tableSize);
        m_fontFace->ReleaseFontTable(tableContext);
    }

    if (m_info.familyName.empty()) {
        size_t dot = filePath.find_last_of(L'.');
        size_t slash = filePath.find_last_of(L"\\/");
        size_t start = (slash != std::wstring::npos) ? slash + 1 : 0;
        size_t len = (dot != std::wstring::npos && dot > start) ? dot - start : std::wstring::npos;
        m_info.familyName = filePath.substr(start, len);
    }
    if (m_info.styleName.empty()) {
        m_info.styleName = L"Regular";
    }

    // Create DirectWrite formats for specimen presentation
    const wchar_t* family = m_info.familyName.c_str();
    dwrite->CreateTextFormat(family, NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 42.0f, L"en-us", &m_fmtHero);
    dwrite->CreateTextFormat(family, NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 26.0f, L"en-us", &m_fmtLarge);
    dwrite->CreateTextFormat(family, NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"en-us", &m_fmtMedium);
    dwrite->CreateTextFormat(family, NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &m_fmtSmall);
    dwrite->CreateTextFormat(L"Cascadia Code", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &m_fmtMono);

    m_isLoaded = true;
    return true;
}

void FontEngine::Scroll(float dy) {
    float maxScroll = (std::max)(0.0f, m_contentHeight - 500.0f);
    m_targetScrollY += dy;
    if (m_targetScrollY > 0.0f) m_targetScrollY = 0.0f;
    if (m_targetScrollY < -maxScroll) m_targetScrollY = -maxScroll;
}

bool FontEngine::UpdateAnimation(float dt) {
    float dy = m_targetScrollY - m_scrollY;
    if (std::abs(dy) > 0.25f) {
        float safeDt = (std::max)(0.001f, (std::min)(0.1f, dt));
        float decay = 1.0f - std::exp(-22.0f * safeDt);
        m_scrollY += dy * decay;
        return true;
    } else if (m_scrollY != m_targetScrollY) {
        m_scrollY = m_targetScrollY;
        return true;
    }
    return false;
}

void FontEngine::Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale) {
    if (!rt || !m_isLoaded) return;

    float clientW = targetRect.right - targetRect.left;
    float clientH = targetRect.bottom - targetRect.top;
    float marginX = (std::max)(48.0f, (clientW - 860.0f) / 2.0f);
    float textW = clientW - marginX * 2.0f;

    float curY = targetRect.top + 36.0f + m_scrollY;

    // --- 1. Header Card ---
    {
        // Family Title
        IDWriteTextLayout* layoutTitle = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(m_info.familyName.c_str(), (UINT32)m_info.familyName.length(),
            d2d.FontHero ? d2d.FontHero : d2d.FontTitle, textW, 46.0f, &layoutTitle);
        if (layoutTitle) {
            rt->DrawTextLayout(D2D1::Point2F(marginX, curY), layoutTitle, d2d.BrushText);
            layoutTitle->Release();
        }

        // Badges: Style, Format, Glyphs
        float badgeY = curY + 48.0f;
        auto drawPill = [&](const std::wstring& txt, float x, ID2D1SolidColorBrush* bg, ID2D1SolidColorBrush* fg) -> float {
            float pw = 18.0f + (float)txt.length() * 7.5f;
            D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(x, badgeY, x + pw, badgeY + 22.0f), 11.0f, 11.0f);
            rt->FillRoundedRectangle(r, bg);
            rt->DrawRoundedRectangle(r, d2d.BrushBorder, 1.0f);

            IDWriteTextLayout* tl = nullptr;
            d2d.GetDWriteFactory()->CreateTextLayout(txt.c_str(), (UINT32)txt.length(), d2d.FontBadge, pw, 22.0f, &tl);
            if (tl) {
                tl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                rt->DrawTextLayout(D2D1::Point2F(x, badgeY), tl, fg);
                tl->Release();
            }
            return pw + 8.0f;
        };

        float bx = marginX;
        bx += drawPill(m_info.styleName, bx, d2d.BrushCard, d2d.BrushAccent);
        bx += drawPill(m_info.formatType, bx, d2d.BrushCard, d2d.BrushText);
        WCHAR glyphStr[64];
        swprintf_s(glyphStr, L"%u GLYPHS", m_info.glyphCount);
        bx += drawPill(glyphStr, bx, d2d.BrushCard, d2d.BrushTextMuted);

        WCHAR unitsStr[64];
        swprintf_s(unitsStr, L"%u UPM", m_info.designUnitsPerEm);
        bx += drawPill(unitsStr, bx, d2d.BrushCard, d2d.BrushTextMuted);

        curY += 86.0f;
    }

    // Divider Line
    rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + textW, curY), d2d.BrushBorderSubtle, 1.0f);
    curY += 28.0f;

    // --- 2. Specimen Waterfall Sections ---
    struct SpecimenRow {
        const wchar_t* label;
        const wchar_t* text;
        IDWriteTextFormat* format;
        float height;
    };

    SpecimenRow rows[] = {
        { L"42 PT", L"The quick brown fox jumps over the lazy dog.", m_fmtHero ? m_fmtHero : d2d.FontHero, 70.0f },
        { L"26 PT", L"Pack my box with five dozen liquor jugs. Heavy waves wash youth.", m_fmtLarge ? m_fmtLarge : d2d.FontTitle, 46.0f },
        { L"17 PT", L"Sphinx of black quartz, judge my vow. How vexingly quick daft zebras jump!", m_fmtMedium ? m_fmtMedium : d2d.FontBody, 36.0f },
        { L"UPPERCASE", L"A B C D E F G H I J K L M N O P Q R S T U V W X Y Z", m_fmtMedium ? m_fmtMedium : d2d.FontBody, 36.0f },
        { L"LOWERCASE", L"a b c d e f g h i j k l m n o p q r s t u v w x y z", m_fmtMedium ? m_fmtMedium : d2d.FontBody, 36.0f },
        { L"NUMERALS & SYMBOLS", L"0 1 2 3 4 5 6 7 8 9   ! @ # $ % ^ & * ( ) _ + - = [ ] { } : ; ' \" < > , . ? /", m_fmtMedium ? m_fmtMedium : d2d.FontBody, 38.0f }
    };

    for (const auto& row : rows) {
        if (!row.format) continue;

        // Label Tag
        IDWriteTextLayout* lblLayout = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(row.label, (UINT32)wcslen(row.label), d2d.FontMono, 140.0f, 18.0f, &lblLayout);
        if (lblLayout) {
            rt->DrawTextLayout(D2D1::Point2F(marginX, curY), lblLayout, d2d.BrushTextMuted);
            lblLayout->Release();
        }
        curY += 22.0f;

        // Specimen text
        IDWriteTextLayout* textLayout = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(row.text, (UINT32)wcslen(row.text), row.format, textW, row.height, &textLayout);
        if (textLayout) {
            rt->DrawTextLayout(D2D1::Point2F(marginX, curY), textLayout, d2d.BrushText);
            textLayout->Release();
        }
        curY += row.height + 18.0f;

        rt->DrawLine(D2D1::Point2F(marginX, curY - 8.0f), D2D1::Point2F(marginX + textW, curY - 8.0f), d2d.BrushBorderSubtle, 0.6f);
    }

    // --- 3. Typography Metrics Card ---
    {
        curY += 12.0f;
        D2D1_ROUNDED_RECT cardR = D2D1::RoundedRect(D2D1::RectF(marginX, curY, marginX + textW, curY + 84.0f), 8.0f, 8.0f);
        rt->FillRoundedRectangle(cardR, d2d.BrushCard);
        rt->DrawRoundedRectangle(cardR, d2d.BrushBorder, 1.0f);

        WCHAR meta1[256], meta2[256];
        swprintf_s(meta1, L"Ascent: %d  •  Descent: %d  •  Line Gap: %d  •  Cap Height: %d  •  x-Height: %d",
            m_info.ascent, m_info.descent, m_info.lineGap, m_info.capHeight, m_info.xHeight);
        swprintf_s(meta2, L"%s  •  %s", m_info.version.empty() ? L"Version 1.00" : m_info.version.c_str(),
            m_info.copyright.empty() ? L"Commercial & Personal Typography" : m_info.copyright.c_str());

        IDWriteTextLayout* l1 = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(meta1, (UINT32)wcslen(meta1), d2d.FontMono, textW - 32.0f, 22.0f, &l1);
        if (l1) {
            rt->DrawTextLayout(D2D1::Point2F(marginX + 16.0f, curY + 16.0f), l1, d2d.BrushText);
            l1->Release();
        }

        IDWriteTextLayout* l2 = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(meta2, (UINT32)wcslen(meta2), d2d.FontCaption, textW - 32.0f, 20.0f, &l2);
        if (l2) {
            rt->DrawTextLayout(D2D1::Point2F(marginX + 16.0f, curY + 44.0f), l2, d2d.BrushTextMuted);
            l2->Release();
        }

        curY += 110.0f;
    }

    m_contentHeight = curY - (targetRect.top + 36.0f + m_scrollY);
}

std::wstring FontEngine::GetFormatBadge() const {
    WCHAR buf[128];
    swprintf_s(buf, L"FONT • %s • %u GLYPHS", m_info.formatType.c_str(), m_info.glyphCount);
    return buf;
}

} // namespace WinPreview
