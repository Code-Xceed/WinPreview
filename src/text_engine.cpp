#include "text_engine.h"
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace WinPreview {

TextEngine::TextEngine() {
}

TextEngine::~TextEngine() {
    Close();
}

void TextEngine::Close() {
    SafeRelease(m_fontCustom);
    ReleaseBrushes();
    m_lines.clear();
    m_rows.clear();
    m_columnWidths.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_isLoaded = false;
    m_isModified = false;
    m_scrollY = 0.0f;
    m_targetScrollY = 0.0f;
    m_scrollX = 0.0f;
    m_targetScrollX = 0.0f;
    m_cursor = { 0, 0 };
    m_anchor = { 0, 0 };
    m_caretVisible = true;
    m_caretBlinkTimer = 0.0f;
    m_isSelectingText = false;
}

void TextEngine::ReleaseBrushes() {
    SafeRelease(m_brComment);
    SafeRelease(m_brString);
    SafeRelease(m_brKeyword);
    SafeRelease(m_brActiveLine);
    SafeRelease(m_brSelection);
    SafeRelease(m_brGutterBg);
    SafeRelease(m_brStatusBg);
    m_lastRenderTarget = nullptr;
}

void TextEngine::EnsureBrushes(ID2D1RenderTarget* rt) {
    if (m_lastRenderTarget == rt && m_brComment) return;
    ReleaseBrushes();
    m_lastRenderTarget = rt;
    if (!rt) return;

    rt->CreateSolidColorBrush(D2D1::ColorF(0.42f, 0.46f, 0.58f), &m_brComment);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.82f, 0.56f), &m_brString);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.48f, 0.78f, 0.98f), &m_brKeyword);
    rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.038f), &m_brActiveLine);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.35f, 0.60f, 0.65f), &m_brSelection);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.072f, 0.082f, 0.98f), &m_brGutterBg);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.066f, 0.066f, 0.078f, 1.0f), &m_brStatusBg);
}

void TextEngine::DetectLanguage(const std::wstring& ext, const std::wstring& filename) {
    std::wstring lowerFn = filename;
    std::transform(lowerFn.begin(), lowerFn.end(), lowerFn.begin(), ::towlower);

    if (lowerFn == L"makefile" || ext == L".mk") m_languageName = L"Makefile";
    else if (lowerFn == L"dockerfile" || ext == L".dockerfile") m_languageName = L"Dockerfile";
    else if (lowerFn == L"cmakelists.txt" || ext == L".cmake") m_languageName = L"CMake";
    else if (lowerFn == L"gemfile" || lowerFn == L"rakefile") m_languageName = L"Ruby";
    else if (lowerFn == L"license" || lowerFn == L"licence" || lowerFn == L"copying") m_languageName = L"License";
    else if (lowerFn == L".gitignore" || lowerFn == L".gitattributes" || lowerFn == L".gitmodules" || lowerFn == L".gitconfig") m_languageName = L"Git Config";
    else if (lowerFn == L".editorconfig") m_languageName = L"EditorConfig";
    else if (lowerFn.rfind(L".env", 0) == 0 || ext == L".env") m_languageName = L"Environment Config";
    else if (lowerFn == L".npmrc" || lowerFn == L".yarnrc" || lowerFn == L".bowerrc") m_languageName = L"Package Config";
    else if (lowerFn == L".eslintrc" || lowerFn == L".prettierrc" || lowerFn == L".babelrc") m_languageName = L"Tool Config";
    else if (ext == L".txt") m_languageName = L"Plain Text";
    else if (ext == L".md" || ext == L".markdown" || ext == L".mb" || ext == L".mdown" || ext == L".mkd" || ext == L".mdx") m_languageName = L"Markdown";
    else if (ext == L".py" || ext == L".pyw") m_languageName = L"Python";
    else if (ext == L".js" || ext == L".mjs" || ext == L".cjs") m_languageName = L"JavaScript";
    else if (ext == L".ts" || ext == L".mts") m_languageName = L"TypeScript";
    else if (ext == L".jsx" || ext == L".tsx") m_languageName = L"React / TSX";
    else if (ext == L".json") m_languageName = L"JSON";
    else if (ext == L".jsonc") m_languageName = L"JSON with Comments";
    else if (ext == L".json5") m_languageName = L"JSON5";
    else if (ext == L".jsonl" || ext == L".ndjson") m_languageName = L"JSON Lines";
    else if (ext == L".xml" || ext == L".xaml") m_languageName = L"XML";
    else if (ext == L".html" || ext == L".htm" || ext == L".xhtml") m_languageName = L"HTML";
    else if (ext == L".css" || ext == L".scss" || ext == L".sass" || ext == L".less") m_languageName = L"CSS";
    else if (ext == L".cpp" || ext == L".cxx" || ext == L".cc") m_languageName = L"C++";
    else if (ext == L".c") m_languageName = L"C";
    else if (ext == L".h" || ext == L".hpp" || ext == L".hxx") m_languageName = L"C/C++ Header";
    else if (ext == L".cs") m_languageName = L"C#";
    else if (ext == L".rs") m_languageName = L"Rust";
    else if (ext == L".go") m_languageName = L"Go";
    else if (ext == L".java") m_languageName = L"Java";
    else if (ext == L".kt" || ext == L".kts") m_languageName = L"Kotlin";
    else if (ext == L".scala" || ext == L".sc") m_languageName = L"Scala";
    else if (ext == L".clj" || ext == L".cljs" || ext == L".edn") m_languageName = L"Clojure";
    else if (ext == L".ex" || ext == L".exs") m_languageName = L"Elixir";
    else if (ext == L".erl" || ext == L".hrl") m_languageName = L"Erlang";
    else if (ext == L".fs" || ext == L".fsi" || ext == L".fsx") m_languageName = L"F#";
    else if (ext == L".hs" || ext == L".lhs") m_languageName = L"Haskell";
    else if (ext == L".jl") m_languageName = L"Julia";
    else if (ext == L".nim") m_languageName = L"Nim";
    else if (ext == L".v") m_languageName = L"V";
    else if (ext == L".sol") m_languageName = L"Solidity";
    else if (ext == L".tf" || ext == L".tfvars") m_languageName = L"Terraform";
    else if (ext == L".prisma") m_languageName = L"Prisma";
    else if (ext == L".tex" || ext == L".latex" || ext == L".bib") m_languageName = L"LaTeX";
    else if (ext == L".rst") m_languageName = L"reStructuredText";
    else if (ext == L".adoc" || ext == L".asciidoc") m_languageName = L"AsciiDoc";
    else if (ext == L".srt" || ext == L".vtt" || ext == L".sub" || ext == L".ass" || ext == L".ssa") m_languageName = L"Subtitles";
    else if (ext == L".cue") m_languageName = L"Cue Sheet";
    else if (ext == L".m3u" || ext == L".m3u8" || ext == L".pls") m_languageName = L"Playlist";
    else if (ext == L".swift") m_languageName = L"Swift";
    else if (ext == L".dart") m_languageName = L"Dart";
    else if (ext == L".zig") m_languageName = L"Zig";
    else if (ext == L".lua") m_languageName = L"Lua";
    else if (ext == L".r") m_languageName = L"R";
    else if (ext == L".proto") m_languageName = L"Protocol Buffers";
    else if (ext == L".graphql" || ext == L".gql") m_languageName = L"GraphQL";
    else if (ext == L".svelte" || ext == L".vue" || ext == L".astro") m_languageName = L"Web Component";
    else if (ext == L".php") m_languageName = L"PHP";
    else if (ext == L".rb") m_languageName = L"Ruby";
    else if (ext == L".sh" || ext == L".bash" || ext == L".zsh" || ext == L".fish") m_languageName = L"Shell";
    else if (ext == L".bat" || ext == L".cmd") m_languageName = L"Batch";
    else if (ext == L".ps1" || ext == L".psm1" || ext == L".psd1" || ext == L".ps1xml") m_languageName = L"PowerShell";
    else if (ext == L".reg") m_languageName = L"Windows Registry";
    else if (ext == L".inf") m_languageName = L"Setup Information";
    else if (ext == L".vbs" || ext == L".wsf" || ext == L".vba" || ext == L".bas") m_languageName = L"VBScript";
    else if (ext == L".yaml" || ext == L".yml") m_languageName = L"YAML";
    else if (ext == L".ini" || ext == L".cfg" || ext == L".conf" || ext == L".config" || ext == L".toml") m_languageName = L"Config";
    else if (ext == L".properties") m_languageName = L"Properties";
    else if (ext == L".gradle" || ext == L".groovy") m_languageName = L"Gradle";
    else if (ext == L".lock" || ext == L".sum") m_languageName = L"Lockfile";
    else if (ext == L".nfo") m_languageName = L"System Info / NFO";
    else if (ext == L".sql") m_languageName = L"SQL";
    else if (ext == L".diff" || ext == L".patch") m_languageName = L"Diff";
    else if (ext == L".log") m_languageName = L"Log";
    else if (ext == L".csv") m_languageName = L"CSV";
    else if (ext == L".tsv" || ext == L".tab") m_languageName = L"TSV";
    else if (ext == L".psv") m_languageName = L"PSV";
    else if (ext == L".webmanifest") m_languageName = L"Web Manifest";
    else if (ext == L".plist") m_languageName = L"Apple Property List";
    else if (ext == L".csproj" || ext == L".vcxproj" || ext == L".fsproj" || ext == L".vbproj" || ext == L".props" || ext == L".targets" || ext == L".nuspec") m_languageName = L"MSBuild / XML";
    else m_languageName = L"Plain Text";
}

bool TextEngine::LoadFile(const std::wstring& filePath, IDWriteFactory* dwriteFactory) {
    Close();
    m_filePath = filePath;
    m_dwriteFactory = dwriteFactory;

    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(hFile, &liSize)) {
        CloseHandle(hFile);
        return false;
    }

    m_fileSizeBytes = liSize.QuadPart;
    DWORD bytesToRead = (DWORD)std::min<int64_t>(m_fileSizeBytes, 16 * 1024 * 1024);

    std::vector<uint8_t> rawBuffer(bytesToRead + 4, 0);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, rawBuffer.data(), bytesToRead, &bytesRead, NULL) || bytesRead == 0) {
        CloseHandle(hFile);
        if (m_fileSizeBytes == 0) {
            m_lines.push_back(L"");
            m_isLoaded = true;
            return true;
        }
        return false;
    }
    CloseHandle(hFile);

    std::wstring fullContent;
    uint8_t* data = rawBuffer.data();

    // Check UTF-8 BOM
    if (bytesRead >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        m_encodingName = L"UTF-8 (BOM)";
        int wlen = MultiByteToWideChar(CP_UTF8, 0, (char*)(data + 3), bytesRead - 3, NULL, 0);
        if (wlen > 0) {
            fullContent.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, (char*)(data + 3), bytesRead - 3, fullContent.data(), wlen);
        }
    }
    // Check UTF-16 LE BOM
    else if (bytesRead >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        m_encodingName = L"UTF-16 LE";
        int wlen = (bytesRead - 2) / 2;
        fullContent.assign((wchar_t*)(data + 2), wlen);
    }
    // Check UTF-16 BE BOM
    else if (bytesRead >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        m_encodingName = L"UTF-16 BE";
        int wlen = (bytesRead - 2) / 2;
        fullContent.resize(wlen);
        const uint8_t* src = data + 2;
        for (int i = 0; i < wlen; ++i) {
            fullContent[i] = (wchar_t)((src[i * 2] << 8) | src[i * 2 + 1]);
        }
    }
    // Fallback: Test UTF-8, else ANSI
    else {
        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (char*)data, bytesRead, NULL, 0);
        if (wlen > 0) {
            m_encodingName = L"UTF-8";
            fullContent.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, (char*)data, bytesRead, fullContent.data(), wlen);
        } else {
            m_encodingName = L"ANSI";
            wlen = MultiByteToWideChar(CP_ACP, 0, (char*)data, bytesRead, NULL, 0);
            if (wlen > 0) {
                fullContent.resize(wlen);
                MultiByteToWideChar(CP_ACP, 0, (char*)data, bytesRead, fullContent.data(), wlen);
            }
        }
    }

    m_charCount = fullContent.length();

    // Detect Line Ending
    if (fullContent.find(L"\r\n") != std::wstring::npos) {
        m_lineEndingName = L"CRLF";
    } else {
        m_lineEndingName = L"LF";
    }

    // Split into lines (normalize tabs to spaces to eliminate character drift)
    m_lines.clear();
    std::wstring line;
    for (size_t i = 0; i < fullContent.length(); ++i) {
        wchar_t ch = fullContent[i];
        if (ch == L'\r') {
            if (i + 1 < fullContent.length() && fullContent[i + 1] == L'\n') i++;
            m_lines.push_back(line);
            line.clear();
        } else if (ch == L'\n') {
            m_lines.push_back(line);
            line.clear();
        } else if (ch == L'\t') {
            int spacesToAdd = m_tabSpaces - ((int)line.length() % m_tabSpaces);
            if (spacesToAdd <= 0) spacesToAdd = m_tabSpaces;
            line.append((size_t)spacesToAdd, L' ');
        } else {
            line.push_back(ch);
        }
    }
    m_lines.push_back(line);
    if (m_lines.empty()) m_lines.push_back(L"");

    // Extension & File Type
    std::wstring ext;
    std::wstring fn;
    size_t lastSlash = filePath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) fn = filePath.substr(lastSlash + 1);
    else fn = filePath;

    size_t dot = filePath.find_last_of(L'.');
    if (dot != std::wstring::npos && (lastSlash == std::wstring::npos || dot > lastSlash)) {
        ext = filePath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }
    DetectLanguage(ext, fn);

    if (ext == L".csv") {
        m_fileType = TextFileType::Csv;
        ParseCsv(fullContent, L',');
        m_isTableView = true;
    } else if (ext == L".tsv" || ext == L".tab") {
        m_fileType = TextFileType::Tsv;
        ParseCsv(fullContent, L'\t');
        m_isTableView = true;
    } else if (ext == L".psv") {
        m_fileType = TextFileType::Csv;
        ParseCsv(fullContent, L'|');
        m_isTableView = true;
    } else if (ext == L".md" || ext == L".markdown" || ext == L".mb" || ext == L".mdown" || ext == L".mkd" || ext == L".mdx") {
        m_fileType = TextFileType::Markdown;
        m_isTableView = false;
    } else {
        m_fileType = TextFileType::Code;
        m_isTableView = false;
    }

    m_isLoaded = true;
    m_isModified = false;
    m_cursor = { 0, 0 };
    m_anchor = { 0, 0 };
    return true;
}

void TextEngine::ToggleTableView() {
    m_isTableView = !m_isTableView;
    if (m_isTableView && (m_fileType == TextFileType::Csv || m_fileType == TextFileType::Tsv)) {
        // Re-parse current lines into table rows
        wchar_t delim = (m_fileType == TextFileType::Tsv) ? L'\t' : L',';
        ParseCsv(GetAllText(), delim);
    }
}

void TextEngine::ParseCsv(const std::wstring& content, wchar_t delimiter) {
    m_rows.clear();
    m_columnWidths.clear();
    m_columnCount = 0;

    std::vector<std::wstring> curRow;
    std::wstring curCell;
    bool inQuotes = false;

    for (size_t i = 0; i < content.length(); ++i) {
        wchar_t c = content[i];

        if (c == L'"') {
            if (inQuotes && i + 1 < content.length() && content[i + 1] == L'"') {
                curCell.push_back(L'"');
                i++;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == delimiter && !inQuotes) {
            curRow.push_back(curCell);
            curCell.clear();
        } else if ((c == L'\r' || c == L'\n') && !inQuotes) {
            if (c == L'\r' && i + 1 < content.length() && content[i + 1] == L'\n') i++;
            curRow.push_back(curCell);
            curCell.clear();
            if (!curRow.empty()) {
                m_columnCount = std::max(m_columnCount, (int)curRow.size());
                m_rows.push_back(curRow);
            }
            curRow.clear();
        } else {
            curCell.push_back(c);
        }
    }

    if (!curCell.empty() || !curRow.empty()) {
        curRow.push_back(curCell);
        m_columnCount = std::max(m_columnCount, (int)curRow.size());
        m_rows.push_back(curRow);
    }

    m_columnWidths.assign(m_columnCount, 85.0f);
    m_totalTableWidth = m_columnCount * 85.0f;
}

std::wstring TextEngine::GetAllText() const {
    std::wstring result;
    std::wstring eol = (m_lineEndingName == L"LF") ? L"\n" : L"\r\n";
    for (size_t i = 0; i < m_lines.size(); ++i) {
        result += m_lines[i];
        if (i + 1 < m_lines.size()) result += eol;
    }
    return result;
}

std::wstring TextEngine::GetSelectedText() const {
    if (!HasSelection()) return L"";
    TextPos start = GetSelectionStart();
    TextPos end = GetSelectionEnd();

    if (start.line == end.line) {
        if (start.line < 0 || start.line >= (int)m_lines.size()) return L"";
        const std::wstring& line = m_lines[start.line];
        int s = std::clamp(start.col, 0, (int)line.length());
        int e = std::clamp(end.col, 0, (int)line.length());
        return line.substr(s, e - s);
    }

    std::wstring res;
    std::wstring eol = (m_lineEndingName == L"LF") ? L"\n" : L"\r\n";

    // First line
    if (start.line >= 0 && start.line < (int)m_lines.size()) {
        const std::wstring& line = m_lines[start.line];
        int s = std::clamp(start.col, 0, (int)line.length());
        res += line.substr(s) + eol;
    }

    // Middle lines
    for (int i = start.line + 1; i < end.line; ++i) {
        if (i >= 0 && i < (int)m_lines.size()) {
            res += m_lines[i] + eol;
        }
    }

    // Last line
    if (end.line >= 0 && end.line < (int)m_lines.size()) {
        const std::wstring& line = m_lines[end.line];
        int e = std::clamp(end.col, 0, (int)line.length());
        res += line.substr(0, e);
    }

    return res;
}

void TextEngine::SelectAll() {
    if (m_lines.empty()) return;
    m_anchor = { 0, 0 };
    int lastLine = (int)m_lines.size() - 1;
    m_cursor = { lastLine, (int)m_lines[lastLine].length() };
    ResetCaretBlink();
}

void TextEngine::ClearSelection() {
    m_anchor = m_cursor;
    ResetCaretBlink();
}

void TextEngine::ResetCaretBlink() {
    m_caretVisible = true;
    m_caretBlinkTimer = 0.0f;
}

void TextEngine::Scroll(float deltaY, float deltaX) {
    m_targetScrollY = std::max(0.0f, m_targetScrollY + deltaY);
    m_targetScrollX = std::max(0.0f, m_targetScrollX + deltaX);
}

void TextEngine::PageUp(float viewportHeight) {
    Scroll(-viewportHeight * 0.85f);
}

void TextEngine::PageDown(float viewportHeight) {
    Scroll(viewportHeight * 0.85f);
}

void TextEngine::ScrollToTop() {
    m_targetScrollY = 0.0f;
    m_targetScrollX = 0.0f;
}

void TextEngine::ScrollToBottom(float viewportHeight) {
    float totalH = (float)m_lines.size() * m_lineHeight;
    m_targetScrollY = std::max(0.0f, totalH - viewportHeight + 100.0f);
}

void TextEngine::SetScrollPosition(float scrollY, float scrollX) {
    m_scrollY = scrollY;
    m_targetScrollY = scrollY;
    m_scrollX = scrollX;
    m_targetScrollX = scrollX;
}

void TextEngine::ZoomIn() {
    m_fontSize = std::min(32.0f, m_fontSize + 1.5f);
    SafeRelease(m_fontCustom);
}

void TextEngine::ZoomOut() {
    m_fontSize = std::max(8.0f, m_fontSize - 1.5f);
    SafeRelease(m_fontCustom);
}

void TextEngine::ResetZoom() {
    m_fontSize = 13.0f;
    SafeRelease(m_fontCustom);
}

bool TextEngine::UpdateAnimation(float dt) {
    bool dirty = false;

    // Smooth Kinetic Lerp Scroll (Frame-rate independent exponential decay)
    float dy = m_targetScrollY - m_scrollY;
    float dx = m_targetScrollX - m_scrollX;

    float safeDt = (std::max)(0.001f, (std::min)(0.1f, dt));
    float decay = 1.0f - std::exp(-22.0f * safeDt);

    if (std::abs(dy) > 0.25f) {
        m_scrollY += dy * decay;
        dirty = true;
    } else if (m_scrollY != m_targetScrollY) {
        m_scrollY = m_targetScrollY;
        dirty = true;
    }

    if (std::abs(dx) > 0.25f) {
        m_scrollX += dx * decay;
        dirty = true;
    } else if (m_scrollX != m_targetScrollX) {
        m_scrollX = m_targetScrollX;
        dirty = true;
    }

    // Caret blink timer (500ms cycle)
    m_caretBlinkTimer += dt;
    if (m_caretBlinkTimer >= 0.5f) {
        m_caretBlinkTimer = 0.0f;
        m_caretVisible = !m_caretVisible;
        dirty = true;
    }

    return dirty;
}

float TextEngine::GetGutterWidth(float dpiScale) const {
    int totalLines = (int)m_lines.size();
    int digits = 2;
    if (totalLines >= 10000) digits = 5;
    else if (totalLines >= 1000) digits = 4;
    else if (totalLines >= 100) digits = 3;
    return digits * 8.5f * dpiScale + 22.0f * dpiScale;
}

void TextEngine::EnsureFont(float dpiScale) {
    float targetSize = m_fontSize * dpiScale;
    if (!m_fontCustom || std::abs(m_lastCachedFontSize - targetSize) > 0.1f) {
        SafeRelease(m_fontCustom);
        m_lastCachedFontSize = targetSize;
        if (!m_dwriteFactory) return;

        // Check if Cascadia Code is installed on system; otherwise use Consolas (guaranteed on Windows)
        const wchar_t* primaryFont = L"Consolas";
        IDWriteFontCollection* coll = nullptr;
        if (SUCCEEDED(m_dwriteFactory->GetSystemFontCollection(&coll)) && coll) {
            UINT32 fIdx = 0;
            BOOL fExists = FALSE;
            if (SUCCEEDED(coll->FindFamilyName(L"Cascadia Code", &fIdx, &fExists)) && fExists) {
                primaryFont = L"Cascadia Code";
            } else if (SUCCEEDED(coll->FindFamilyName(L"Cascadia Mono", &fIdx, &fExists)) && fExists) {
                primaryFont = L"Cascadia Mono";
            }
            coll->Release();
        }

        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            primaryFont,
            NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            m_lastCachedFontSize,
            L"en-us",
            &m_fontCustom
        );

        if (FAILED(hr) || !m_fontCustom) {
            m_dwriteFactory->CreateTextFormat(
                L"Consolas",
                NULL,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                m_lastCachedFontSize,
                L"en-us",
                &m_fontCustom
            );
        }

        if (m_fontCustom) {
            m_fontCustom->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            m_fontCustom->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

            // Measure monospace character advance width exactly via DirectWrite layout metrics
            // Using 10 characters "0123456789" eliminates subpixel rounding error
            IDWriteTextLayout* layout = nullptr;
            const wchar_t testStr[] = L"0123456789";
            m_dwriteFactory->CreateTextLayout(testStr, 10, m_fontCustom, 4000.0f, 100.0f, &layout);
            if (layout) {
                DWRITE_TEXT_METRICS tm = {};
                layout->GetMetrics(&tm);
                if (tm.widthIncludingTrailingWhitespace > 1.0f) {
                    m_charWidth = tm.widthIncludingTrailingWhitespace / 10.0f;
                }
                layout->Release();
            }
            if (m_charWidth < 1.0f) m_charWidth = 7.15f * dpiScale;
        }
    }
}

float TextEngine::GetColumnX(int lineIdx, int col) const {
    if (lineIdx < 0 || lineIdx >= (int)m_lines.size() || col <= 0) return 0.0f;
    const std::wstring& line = m_lines[lineIdx];
    int clampedCol = std::clamp(col, 0, (int)line.length());
    if (clampedCol == 0) return 0.0f;

    // Check if prefix contains only standard ASCII (0..127)
    bool isAscii = true;
    for (int c = 0; c < clampedCol; ++c) {
        if (line[c] > 127) { isAscii = false; break; }
    }

    if (isAscii && m_charWidth > 0.0f) {
        return (float)clampedCol * m_charWidth;
    }

    // DirectWrite precise hit-testing for lines with Unicode/CJK/emojis
    if (m_dwriteFactory && m_fontCustom) {
        IDWriteTextLayout* layout = nullptr;
        m_dwriteFactory->CreateTextLayout(line.c_str(), (UINT32)line.length(), m_fontCustom, 10000.0f, 100.0f, &layout);
        if (layout) {
            float ptX = 0, ptY = 0;
            DWRITE_HIT_TEST_METRICS htm = {};
            layout->HitTestTextPosition((UINT32)clampedCol, FALSE, &ptX, &ptY, &htm);
            layout->Release();
            return ptX;
        }
    }

    return (float)clampedCol * m_charWidth;
}

TextPos TextEngine::PointToPosition(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale) const {
    if (m_lines.empty()) return { 0, 0 };

    float topPadding = GetHeaderTopPadding(dpiScale);
    float gutterW = GetGutterWidth(dpiScale);
    float textLeft = viewportRect.left + gutterW + 12.0f * dpiScale - m_scrollX;
    float relY = y - (viewportRect.top + topPadding) + m_scrollY;

    int line = 0;
    if (m_lineHeight > 0.0f && relY > 0.0f) {
        line = (int)(relY / m_lineHeight);
    }
    line = std::clamp(line, 0, (int)m_lines.size() - 1);

    const std::wstring& lineText = m_lines[line];
    if (lineText.empty()) return { line, 0 };

    float relX = x - textLeft;
    if (relX <= 0.0f) return { line, 0 };

    // Check if line contains only ASCII
    bool isAscii = true;
    for (wchar_t ch : lineText) {
        if (ch > 127) { isAscii = false; break; }
    }

    int col = 0;
    if (isAscii && m_charWidth > 0.0f) {
        col = (int)std::floor((relX + m_charWidth * 0.5f) / m_charWidth);
    } else if (m_dwriteFactory && m_fontCustom) {
        IDWriteTextLayout* layout = nullptr;
        m_dwriteFactory->CreateTextLayout(lineText.c_str(), (UINT32)lineText.length(), m_fontCustom, 10000.0f, 100.0f, &layout);
        if (layout) {
            BOOL isTrailingHit = FALSE;
            BOOL isInside = FALSE;
            DWRITE_HIT_TEST_METRICS htm = {};
            layout->HitTestPoint(relX, 0.0f, &isTrailingHit, &isInside, &htm);
            col = (int)htm.textPosition + (isTrailingHit ? 1 : 0);
            layout->Release();
        } else if (m_charWidth > 0.0f) {
            col = (int)std::floor((relX + m_charWidth * 0.5f) / m_charWidth);
        }
    } else if (m_charWidth > 0.0f) {
        col = (int)std::floor((relX + m_charWidth * 0.5f) / m_charWidth);
    }
    col = std::clamp(col, 0, (int)lineText.length());

    return { line, col };
}

void TextEngine::EnsureCursorVisible(const D2D1_RECT_F& viewportRect, float dpiScale) {
    float statusH = 26.0f * dpiScale;
    float topPadding = GetHeaderTopPadding(dpiScale);
    float viewH = (viewportRect.bottom - viewportRect.top) - statusH - topPadding;
    float gutterW = GetGutterWidth(dpiScale);
    float viewW = (viewportRect.right - viewportRect.left) - (gutterW + 24.0f * dpiScale);

    float cursorY = (float)m_cursor.line * m_lineHeight;
    if (cursorY < m_scrollY) {
        m_targetScrollY = cursorY;
    } else if (cursorY + m_lineHeight > m_scrollY + viewH) {
        m_targetScrollY = cursorY + m_lineHeight - viewH + 20.0f * dpiScale;
    }

    float cursorX = GetColumnX(m_cursor.line, m_cursor.col);
    if (cursorX < m_scrollX) {
        m_targetScrollX = std::max(0.0f, cursorX - 40.0f * dpiScale);
    } else if (cursorX + 24.0f * dpiScale > m_scrollX + viewW) {
        m_targetScrollX = cursorX + 48.0f * dpiScale - viewW;
    }
}

void TextEngine::HandleClick(float x, float y, const D2D1_RECT_F& viewportRect, bool shiftHeld, float dpiScale) {
    if (HandleStatusBarClick(x, y, viewportRect, dpiScale)) return;

    float gutterW = GetGutterWidth(dpiScale);
    float topPadding = GetHeaderTopPadding(dpiScale);
    float textOriginX = viewportRect.left + gutterW;

    // Clicking inside line numbers gutter selects the entire line
    if (x >= viewportRect.left && x < textOriginX && y >= viewportRect.top + topPadding) {
        float relY = y - (viewportRect.top + topPadding) + m_scrollY;
        int line = 0;
        if (m_lineHeight > 0.0f && relY > 0.0f) {
            line = (int)(relY / m_lineHeight);
        }
        line = std::clamp(line, 0, (int)m_lines.size() - 1);
        m_cursor = { line, (int)m_lines[line].length() };
        m_anchor = { line, 0 };
        m_isSelectingText = true;
        ResetCaretBlink();
        EnsureCursorVisible(viewportRect, dpiScale);
        return;
    }

    TextPos pos = PointToPosition(x, y, viewportRect, dpiScale);
    m_cursor = pos;
    if (!shiftHeld) {
        m_anchor = pos;
    }
    m_isSelectingText = true;
    ResetCaretBlink();
    EnsureCursorVisible(viewportRect, dpiScale);
}

void TextEngine::HandleDoubleClick(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale) {
    TextPos pos = PointToPosition(x, y, viewportRect, dpiScale);
    if (pos.line < 0 || pos.line >= (int)m_lines.size()) return;
    const std::wstring& line = m_lines[pos.line];
    if (line.empty()) {
        m_cursor = pos;
        m_anchor = pos;
        return;
    }

    int col = std::clamp(pos.col, 0, (int)line.length());
    if (col == (int)line.length() && col > 0) col--;

    auto isWordChar = [](wchar_t c) { return iswalnum(c) || c == L'_'; };

    int startCol = col;
    int endCol = col;

    if (isWordChar(line[col])) {
        while (startCol > 0 && isWordChar(line[startCol - 1])) startCol--;
        while (endCol < (int)line.length() && isWordChar(line[endCol])) endCol++;
    } else if (iswspace(line[col])) {
        while (startCol > 0 && iswspace(line[startCol - 1])) startCol--;
        while (endCol < (int)line.length() && iswspace(line[endCol])) endCol++;
    } else {
        while (startCol > 0 && !isWordChar(line[startCol - 1]) && !iswspace(line[startCol - 1])) startCol--;
        while (endCol < (int)line.length() && !isWordChar(line[endCol]) && !iswspace(line[endCol])) endCol++;
    }

    m_anchor = { pos.line, startCol };
    m_cursor = { pos.line, endCol };
    ResetCaretBlink();
    EnsureCursorVisible(viewportRect, dpiScale);
}

void TextEngine::HandleTripleClick(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale) {
    TextPos pos = PointToPosition(x, y, viewportRect, dpiScale);
    if (pos.line < 0 || pos.line >= (int)m_lines.size()) return;
    m_anchor = { pos.line, 0 };
    m_cursor = { pos.line, (int)m_lines[pos.line].length() };
    ResetCaretBlink();
    EnsureCursorVisible(viewportRect, dpiScale);
}

void TextEngine::HandleDrag(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale) {
    if (!m_isSelectingText) return;
    m_cursor = PointToPosition(x, y, viewportRect, dpiScale);
    ResetCaretBlink();
    EnsureCursorVisible(viewportRect, dpiScale);
}

bool TextEngine::HandleMouseDown(float x, float y, const D2D1_RECT_F& viewportRect) {
    float topPadding = 52.0f;
    float statusH = 24.0f;
    float viewH = (viewportRect.bottom - viewportRect.top) - topPadding - statusH;
    float totalH = (float)m_lines.size() * m_lineHeight;
    float maxScrollY = std::max(0.0f, totalH - viewH + 60.0f);

    if (totalH > viewH && maxScrollY > 0.0f) {
        float scrollbarW = 12.0f;

        if (x >= viewportRect.right - scrollbarW - 6.0f && x <= viewportRect.right &&
            y >= viewportRect.top + topPadding && y <= viewportRect.bottom - statusH) {
            m_isDraggingVScroll = true;
            m_dragStartY = y;
            m_initialScrollY = m_scrollY;
            return true;
        }
    }
    return false;
}

bool TextEngine::HandleMouseMove(float x, float y, const D2D1_RECT_F& viewportRect) {
    if (m_isDraggingVScroll) {
        float topPadding = 52.0f;
        float statusH = 24.0f;
        float viewH = (viewportRect.bottom - viewportRect.top) - topPadding - statusH;
        float totalH = (float)m_lines.size() * m_lineHeight;
        float maxScrollY = std::max(0.0f, totalH - viewH + 60.0f);
        float thumbH = std::max(28.0f, (viewH / totalH) * viewH);
        float trackTravel = viewH - thumbH;

        if (trackTravel > 0.0f) {
            float deltaPix = y - m_dragStartY;
            float scrollDelta = (deltaPix / trackTravel) * maxScrollY;
            m_targetScrollY = std::clamp(m_initialScrollY + scrollDelta, 0.0f, maxScrollY);
            m_scrollY = m_targetScrollY;
        }
        return true;
    }
    return false;
}

void TextEngine::HandleMouseUp() {
    m_isDraggingVScroll = false;
    m_isDraggingHScroll = false;
    m_isSelectingText = false;
}

void TextEngine::MoveCursor(int dLine, int dCol, bool select) {
    if (m_lines.empty()) return;

    if (dLine != 0) {
        m_cursor.line = std::clamp(m_cursor.line + dLine, 0, (int)m_lines.size() - 1);
        m_cursor.col = std::clamp(m_cursor.col, 0, (int)m_lines[m_cursor.line].length());
    }

    if (dCol != 0) {
        int newCol = m_cursor.col + dCol;
        if (newCol < 0) {
            if (m_cursor.line > 0) {
                m_cursor.line--;
                m_cursor.col = (int)m_lines[m_cursor.line].length();
            } else {
                m_cursor.col = 0;
            }
        } else if (newCol > (int)m_lines[m_cursor.line].length()) {
            if (m_cursor.line + 1 < (int)m_lines.size()) {
                m_cursor.line++;
                m_cursor.col = 0;
            } else {
                m_cursor.col = (int)m_lines[m_cursor.line].length();
            }
        } else {
            m_cursor.col = newCol;
        }
    }

    if (!select) m_anchor = m_cursor;
    ResetCaretBlink();
}

void TextEngine::MoveWord(int direction, bool select) {
    if (m_lines.empty()) return;
    int line = m_cursor.line;
    int col = m_cursor.col;

    auto isWord = [](wchar_t c) { return iswalnum(c) || c == L'_'; };

    if (direction < 0) {
        if (col > 0) {
            const std::wstring& l = m_lines[line];
            col--;
            while (col > 0 && iswspace(l[col])) col--;
            while (col > 0 && isWord(l[col - 1])) col--;
        } else if (line > 0) {
            line--;
            col = (int)m_lines[line].length();
        }
    } else {
        const std::wstring& l = m_lines[line];
        if (col < (int)l.length()) {
            while (col < (int)l.length() && isWord(l[col])) col++;
            while (col < (int)l.length() && iswspace(l[col])) col++;
        } else if (line + 1 < (int)m_lines.size()) {
            line++;
            col = 0;
        }
    }

    m_cursor = { line, col };
    if (!select) m_anchor = m_cursor;
    ResetCaretBlink();
}

void TextEngine::MoveHome(bool select) {
    if (m_cursor.line < 0 || m_cursor.line >= (int)m_lines.size()) return;
    const std::wstring& line = m_lines[m_cursor.line];
    size_t firstNonWs = line.find_first_not_of(L" \t");
    int targetCol = 0;
    if (firstNonWs != std::wstring::npos) {
        if (m_cursor.col == (int)firstNonWs) targetCol = 0;
        else targetCol = (int)firstNonWs;
    }
    m_cursor.col = targetCol;
    if (!select) m_anchor = m_cursor;
    ResetCaretBlink();
}

void TextEngine::MoveEnd(bool select) {
    if (m_cursor.line < 0 || m_cursor.line >= (int)m_lines.size()) return;
    m_cursor.col = (int)m_lines[m_cursor.line].length();
    if (!select) m_anchor = m_cursor;
    ResetCaretBlink();
}

void TextEngine::PushUndo() {
    UndoSnapshot snap;
    snap.lines = m_lines;
    snap.cursor = m_cursor;
    snap.anchor = m_anchor;
    m_undoStack.push_back(std::move(snap));
    if (m_undoStack.size() > 100) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

bool TextEngine::Undo() {
    if (m_undoStack.empty()) return false;
    UndoSnapshot cur;
    cur.lines = m_lines;
    cur.cursor = m_cursor;
    cur.anchor = m_anchor;
    m_redoStack.push_back(std::move(cur));

    UndoSnapshot prev = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    m_lines = std::move(prev.lines);
    m_cursor = prev.cursor;
    m_anchor = prev.anchor;
    m_isModified = true;
    ResetCaretBlink();
    return true;
}

bool TextEngine::Redo() {
    if (m_redoStack.empty()) return false;
    UndoSnapshot cur;
    cur.lines = m_lines;
    cur.cursor = m_cursor;
    cur.anchor = m_anchor;
    m_undoStack.push_back(std::move(cur));

    UndoSnapshot next = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    m_lines = std::move(next.lines);
    m_cursor = next.cursor;
    m_anchor = next.anchor;
    m_isModified = true;
    ResetCaretBlink();
    return true;
}

bool TextEngine::DeleteSelection() {
    if (!HasSelection()) return false;
    PushUndo();

    TextPos start = GetSelectionStart();
    TextPos end = GetSelectionEnd();

    start.line = std::clamp(start.line, 0, (int)m_lines.size() - 1);
    end.line = std::clamp(end.line, 0, (int)m_lines.size() - 1);
    start.col = std::clamp(start.col, 0, (int)m_lines[start.line].length());
    end.col = std::clamp(end.col, 0, (int)m_lines[end.line].length());

    std::wstring head = m_lines[start.line].substr(0, start.col);
    std::wstring tail = m_lines[end.line].substr(end.col);

    m_lines[start.line] = head + tail;
    if (end.line > start.line) {
        m_lines.erase(m_lines.begin() + start.line + 1, m_lines.begin() + end.line + 1);
    }

    m_cursor = start;
    m_anchor = start;
    m_isModified = true;
    ResetCaretBlink();
    return true;
}

bool TextEngine::InsertChar(wchar_t ch) {
    if (HasSelection()) {
        DeleteSelection();
    }
    PushUndo();

    if (m_lines.empty()) m_lines.push_back(L"");
    m_cursor.line = std::clamp(m_cursor.line, 0, (int)m_lines.size() - 1);
    m_cursor.col = std::clamp(m_cursor.col, 0, (int)m_lines[m_cursor.line].length());

    m_lines[m_cursor.line].insert(m_cursor.col, 1, ch);
    m_cursor.col++;
    m_anchor = m_cursor;
    m_isModified = true;
    ResetCaretBlink();
    return true;
}

bool TextEngine::InsertNewline() {
    if (HasSelection()) {
        DeleteSelection();
    }
    PushUndo();

    if (m_lines.empty()) m_lines.push_back(L"");
    m_cursor.line = std::clamp(m_cursor.line, 0, (int)m_lines.size() - 1);
    m_cursor.col = std::clamp(m_cursor.col, 0, (int)m_lines[m_cursor.line].length());

    const std::wstring& curLine = m_lines[m_cursor.line];

    // Smart auto-indent: inherit leading spaces/tabs from previous line
    std::wstring indent;
    for (wchar_t c : curLine) {
        if (c == L' ' || c == L'\t') indent.push_back(c);
        else break;
    }

    std::wstring remainder = curLine.substr(m_cursor.col);
    m_lines[m_cursor.line] = curLine.substr(0, m_cursor.col);

    m_lines.insert(m_lines.begin() + m_cursor.line + 1, indent + remainder);
    m_cursor.line++;
    m_cursor.col = (int)indent.length();
    m_anchor = m_cursor;
    m_isModified = true;
    ResetCaretBlink();
    return true;
}

bool TextEngine::DeleteChar(bool backspace) {
    if (HasSelection()) {
        return DeleteSelection();
    }

    if (m_lines.empty()) return false;
    m_cursor.line = std::clamp(m_cursor.line, 0, (int)m_lines.size() - 1);
    m_cursor.col = std::clamp(m_cursor.col, 0, (int)m_lines[m_cursor.line].length());

    if (backspace) {
        if (m_cursor.col > 0) {
            PushUndo();
            m_lines[m_cursor.line].erase(m_cursor.col - 1, 1);
            m_cursor.col--;
            m_anchor = m_cursor;
            m_isModified = true;
            ResetCaretBlink();
            return true;
        } else if (m_cursor.line > 0) {
            PushUndo();
            int prevLen = (int)m_lines[m_cursor.line - 1].length();
            m_lines[m_cursor.line - 1] += m_lines[m_cursor.line];
            m_lines.erase(m_lines.begin() + m_cursor.line);
            m_cursor.line--;
            m_cursor.col = prevLen;
            m_anchor = m_cursor;
            m_isModified = true;
            ResetCaretBlink();
            return true;
        }
    } else { // Delete
        if (m_cursor.col < (int)m_lines[m_cursor.line].length()) {
            PushUndo();
            m_lines[m_cursor.line].erase(m_cursor.col, 1);
            m_anchor = m_cursor;
            m_isModified = true;
            ResetCaretBlink();
            return true;
        } else if (m_cursor.line + 1 < (int)m_lines.size()) {
            PushUndo();
            m_lines[m_cursor.line] += m_lines[m_cursor.line + 1];
            m_lines.erase(m_lines.begin() + m_cursor.line + 1);
            m_anchor = m_cursor;
            m_isModified = true;
            ResetCaretBlink();
            return true;
        }
    }
    return false;
}

bool TextEngine::Indent(bool unindent) {
    PushUndo();
    std::wstring spaces(m_tabSpaces, L' ');

    if (HasSelection()) {
        TextPos start = GetSelectionStart();
        TextPos end = GetSelectionEnd();

        for (int i = start.line; i <= end.line && i < (int)m_lines.size(); ++i) {
            std::wstring& line = m_lines[i];
            if (unindent) {
                int toRemove = 0;
                while (toRemove < m_tabSpaces && toRemove < (int)line.length() && line[toRemove] == L' ') {
                    toRemove++;
                }
                if (toRemove > 0) line.erase(0, toRemove);
            } else {
                line.insert(0, spaces);
            }
        }
        m_isModified = true;
        ResetCaretBlink();
        return true;
    }

    if (unindent) {
        std::wstring& line = m_lines[m_cursor.line];
        int toRemove = 0;
        while (toRemove < m_tabSpaces && m_cursor.col > 0 && line[m_cursor.col - 1] == L' ') {
            line.erase(m_cursor.col - 1, 1);
            m_cursor.col--;
            toRemove++;
        }
        m_anchor = m_cursor;
    } else {
        m_lines[m_cursor.line].insert(m_cursor.col, spaces);
        m_cursor.col += m_tabSpaces;
        m_anchor = m_cursor;
    }

    m_isModified = true;
    ResetCaretBlink();
    return true;
}

bool TextEngine::Copy(HWND hwnd) {
    std::wstring textToCopy;
    if (HasSelection()) {
        textToCopy = GetSelectedText();
    } else if (m_cursor.line >= 0 && m_cursor.line < (int)m_lines.size()) {
        // VS Code style: copy entire current line
        textToCopy = m_lines[m_cursor.line] + L"\r\n";
    }
    if (textToCopy.empty()) return false;

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (textToCopy.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            memcpy(GlobalLock(hMem), textToCopy.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
        return true;
    }
    return false;
}

bool TextEngine::Cut(HWND hwnd) {
    if (!Copy(hwnd)) return false;

    if (HasSelection()) {
        DeleteSelection();
    } else if (m_cursor.line >= 0 && m_cursor.line < (int)m_lines.size()) {
        PushUndo();
        if (m_lines.size() > 1) {
            m_lines.erase(m_lines.begin() + m_cursor.line);
            m_cursor.line = std::clamp(m_cursor.line, 0, (int)m_lines.size() - 1);
            m_cursor.col = 0;
            m_anchor = m_cursor;
        } else {
            m_lines[0].clear();
            m_cursor = { 0, 0 };
            m_anchor = m_cursor;
        }
        m_isModified = true;
    }
    ResetCaretBlink();
    return true;
}

bool TextEngine::Paste(HWND hwnd) {
    if (!OpenClipboard(hwnd)) return false;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return false;
    }
    wchar_t* pText = (wchar_t*)GlobalLock(hData);
    if (!pText) {
        CloseClipboard();
        return false;
    }
    std::wstring pasted(pText);
    GlobalUnlock(hData);
    CloseClipboard();

    if (pasted.empty()) return false;

    if (HasSelection()) {
        DeleteSelection();
    }
    PushUndo();

    std::vector<std::wstring> pasteLines;
    std::wstring cur;
    for (size_t i = 0; i < pasted.length(); ++i) {
        if (pasted[i] == L'\r') {
            if (i + 1 < pasted.length() && pasted[i + 1] == L'\n') i++;
            pasteLines.push_back(cur);
            cur.clear();
        } else if (pasted[i] == L'\n') {
            pasteLines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(pasted[i]);
        }
    }
    pasteLines.push_back(cur);

    if (m_lines.empty()) m_lines.push_back(L"");
    m_cursor.line = std::clamp(m_cursor.line, 0, (int)m_lines.size() - 1);
    m_cursor.col = std::clamp(m_cursor.col, 0, (int)m_lines[m_cursor.line].length());

    if (pasteLines.size() == 1) {
        m_lines[m_cursor.line].insert(m_cursor.col, pasteLines[0]);
        m_cursor.col += (int)pasteLines[0].length();
    } else {
        std::wstring lineRemainder = m_lines[m_cursor.line].substr(m_cursor.col);
        m_lines[m_cursor.line] = m_lines[m_cursor.line].substr(0, m_cursor.col) + pasteLines[0];

        for (size_t i = 1; i < pasteLines.size() - 1; ++i) {
            m_lines.insert(m_lines.begin() + m_cursor.line + i, pasteLines[i]);
        }
        int lastIdx = (int)pasteLines.size() - 1;
        std::wstring lastLine = pasteLines[lastIdx] + lineRemainder;
        m_lines.insert(m_lines.begin() + m_cursor.line + lastIdx, lastLine);

        m_cursor.line += lastIdx;
        m_cursor.col = (int)pasteLines[lastIdx].length();
    }

    m_anchor = m_cursor;
    m_isModified = true;
    ResetCaretBlink();
    return true;
}

bool TextEngine::SaveFile() {
    if (m_filePath.empty()) return false;

    std::wstring eol = (m_lineEndingName == L"LF") ? L"\n" : L"\r\n";
    std::wstring fullText;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        fullText += m_lines[i];
        if (i + 1 < m_lines.size()) fullText += eol;
    }

    HANDLE hFile = CreateFileW(
        m_filePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;

    if (m_encodingName == L"UTF-8 (BOM)") {
        uint8_t bom[3] = { 0xEF, 0xBB, 0xBF };
        WriteFile(hFile, bom, 3, &written, NULL);
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, fullText.c_str(), (int)fullText.length(), NULL, 0, NULL, NULL);
        if (utf8Len > 0) {
            std::string utf8Buf(utf8Len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, fullText.c_str(), (int)fullText.length(), utf8Buf.data(), utf8Len, NULL, NULL);
            WriteFile(hFile, utf8Buf.data(), utf8Len, &written, NULL);
        }
    } else if (m_encodingName == L"UTF-16 LE") {
        uint8_t bom[2] = { 0xFF, 0xFE };
        WriteFile(hFile, bom, 2, &written, NULL);
        WriteFile(hFile, fullText.data(), (DWORD)(fullText.length() * sizeof(wchar_t)), &written, NULL);
    } else if (m_encodingName == L"ANSI") {
        int ansiLen = WideCharToMultiByte(CP_ACP, 0, fullText.c_str(), (int)fullText.length(), NULL, 0, NULL, NULL);
        if (ansiLen > 0) {
            std::string ansiBuf(ansiLen, '\0');
            WideCharToMultiByte(CP_ACP, 0, fullText.c_str(), (int)fullText.length(), ansiBuf.data(), ansiLen, NULL, NULL);
            WriteFile(hFile, ansiBuf.data(), ansiLen, &written, NULL);
        }
    } else { // UTF-8 standard
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, fullText.c_str(), (int)fullText.length(), NULL, 0, NULL, NULL);
        if (utf8Len > 0) {
            std::string utf8Buf(utf8Len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, fullText.c_str(), (int)fullText.length(), utf8Buf.data(), utf8Len, NULL, NULL);
            WriteFile(hFile, utf8Buf.data(), utf8Len, &written, NULL);
        }
    }

    CloseHandle(hFile);
    m_isModified = false;
    return true;
}

bool TextEngine::HandleStatusBarClick(float x, float y, const D2D1_RECT_F& viewportRect, float dpiScale) {
    auto hit = [&](const D2D1_RECT_F& r) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    };

    if (hit(m_statusSpacesRect)) {
        ToggleTabSpaces();
        return true;
    }
    if (hit(m_statusWrapRect)) {
        ToggleWordWrap();
        return true;
    }
    if (hit(m_statusSaveRect)) {
        SaveFile();
        return true;
    }
    return false;
}

void TextEngine::RenderText(
    ID2D1RenderTarget* rt,
    D2DEngine& d2d,
    const D2D1_RECT_F& viewportRect,
    float dpiScale
) {
    if (!rt || m_lines.empty()) return;
    EnsureFont(dpiScale);
    IDWriteTextFormat* font = m_fontCustom ? m_fontCustom : d2d.FontMono;
    if (!font) font = d2d.FontBody;

    m_gutterWidth = GetGutterWidth(dpiScale);
    m_lineHeight = std::max(18.0f * dpiScale, m_fontSize * dpiScale * 1.55f);
    float padY = std::floor((m_lineHeight - m_fontSize * dpiScale) * 0.5f);

    float statusH = 24.0f * dpiScale;
    float topPadding = GetHeaderTopPadding(dpiScale);
    D2D1_RECT_F codeViewport = D2D1::RectF(
        viewportRect.left,
        viewportRect.top + topPadding,
        viewportRect.right,
        viewportRect.bottom - statusH
    );

    // 1. Dark sleek VS Code zinc background (#141417)
    rt->FillRectangle(viewportRect, d2d.BrushCard);

    // 2. Viewport and coordinate calculations
    EnsureBrushes(rt);
    float textOriginX = codeViewport.left + m_gutterWidth;
    D2D1_RECT_F textAreaClip = D2D1::RectF(textOriginX, codeViewport.top, codeViewport.right, codeViewport.bottom);
    D2D1_RECT_F gutterRect = D2D1::RectF(codeViewport.left, codeViewport.top, textOriginX, codeViewport.bottom);

    // 3. Scroll clamping (vertical & horizontal)
    int totalLines = (int)m_lines.size();
    float totalH = (float)totalLines * m_lineHeight;
    float viewH = codeViewport.bottom - codeViewport.top;
    float maxScrollY = std::max(0.0f, totalH - viewH + 60.0f * dpiScale);
    m_targetScrollY = std::clamp(m_targetScrollY, 0.0f, maxScrollY);
    m_scrollY = std::clamp(m_scrollY, 0.0f, maxScrollY);

    float textVisibleW = (codeViewport.right - textOriginX) - 32.0f * dpiScale;
    float maxLineWidth = 0.0f;
    for (const auto& line : m_lines) {
        float lw = (float)line.length() * m_charWidth;
        if (lw > maxLineWidth) maxLineWidth = lw;
    }
    float maxScrollX = std::max(0.0f, maxLineWidth - textVisibleW + 60.0f * dpiScale);
    m_targetScrollX = std::clamp(m_targetScrollX, 0.0f, maxScrollX);
    m_scrollX = std::clamp(m_scrollX, 0.0f, maxScrollX);

    int startLine = std::max(0, (int)(m_scrollY / m_lineHeight));
    int endLine = std::min(totalLines, startLine + (int)(viewH / m_lineHeight) + 3);

    // VS Code Syntax & Selection Brushes (cached in m_brComment, m_brString, etc. via EnsureBrushes)
    float textLeft = textOriginX + 12.0f * dpiScale - m_scrollX;
    float textRight = codeViewport.right - 16.0f * dpiScale;

    TextPos selStart = GetSelectionStart();
    TextPos selEnd = GetSelectionEnd();
    bool hasSel = HasSelection();

    // =========================================================================
    // LAYER 1: CODE TEXT VIEWPORT (Strictly clipped to text area - cannot bleed into gutter)
    // =========================================================================
    rt->PushAxisAlignedClip(textAreaClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    for (int i = startLine; i < endLine; ++i) {
        float lineY = codeViewport.top + (float)i * m_lineHeight - m_scrollY;
        if (lineY + m_lineHeight < codeViewport.top || lineY > codeViewport.bottom) continue;

        // Active Line Highlight (inside text area)
        if (i == m_cursor.line && m_brActiveLine) {
            D2D1_RECT_F activeRect = D2D1::RectF(textOriginX, lineY, codeViewport.right, lineY + m_lineHeight);
            rt->FillRectangle(activeRect, m_brActiveLine);
        }

        // Selection Highlight on this line
        if (hasSel && i >= selStart.line && i <= selEnd.line && m_brSelection) {
            const std::wstring& line = m_lines[i];
            int colS = 0;
            int colE = (int)line.length();

            if (i == selStart.line && i == selEnd.line) {
                colS = selStart.col;
                colE = selEnd.col;
            } else if (i == selStart.line) {
                colS = selStart.col;
                colE = (int)line.length();
            } else if (i == selEnd.line) {
                colS = 0;
                colE = selEnd.col;
            }

            float selLeftX = textLeft + GetColumnX(i, colS);
            float selRightX = textLeft + GetColumnX(i, colE);
            if (i < selEnd.line) selRightX += 6.0f * dpiScale; // newline block highlight

            if (selRightX > selLeftX) {
                D2D1_RECT_F selBox = D2D1::RectF(selLeftX, lineY + 1.0f, selRightX, lineY + m_lineHeight - 1.0f);
                rt->FillRectangle(selBox, m_brSelection);
            }
        }

        // Code Line Text
        const std::wstring& line = m_lines[i];
        if (!line.empty()) {
            float textTop = lineY + padY;
            D2D1_RECT_F lineRect = D2D1::RectF(
                textLeft,
                textTop,
                std::max(textRight, textLeft + (float)line.length() * m_charWidth + 20.0f * dpiScale),
                textTop + m_fontSize * dpiScale + 4.0f
            );

            std::wstring trimmed = line;
            size_t firstNonSpace = trimmed.find_first_not_of(L" \t");
            ID2D1Brush* lineBrush = d2d.BrushText;

            if (firstNonSpace != std::wstring::npos) {
                std::wstring sub = trimmed.substr(firstNonSpace);
                if (sub.rfind(L"//", 0) == 0 || sub.rfind(L"#", 0) == 0 || 
                    sub.rfind(L"--", 0) == 0 || sub.rfind(L"/*", 0) == 0 || 
                    sub.rfind(L";", 0) == 0 || sub.rfind(L"rem ", 0) == 0) {
                    lineBrush = m_brComment ? (ID2D1Brush*)m_brComment : (ID2D1Brush*)d2d.BrushTextMuted;
                } else if ((sub.front() == L'[' && sub.back() == L']') ||
                           sub.rfind(L"Windows Registry Editor", 0) == 0) {
                    lineBrush = m_brKeyword ? (ID2D1Brush*)m_brKeyword : (ID2D1Brush*)d2d.BrushAccent;
                } else if (sub.rfind(L"import ", 0) == 0 || sub.rfind(L"export ", 0) == 0 ||
                           sub.rfind(L"function ", 0) == 0 || sub.rfind(L"class ", 0) == 0 ||
                           sub.rfind(L"const ", 0) == 0 || sub.rfind(L"return ", 0) == 0 ||
                           sub.rfind(L"def ", 0) == 0 || sub.rfind(L"struct ", 0) == 0 ||
                           sub.rfind(L"namespace ", 0) == 0 || sub.rfind(L"public:", 0) == 0 ||
                           sub.rfind(L"if ", 0) == 0 || sub.rfind(L"else", 0) == 0 ||
                           sub.rfind(L"for ", 0) == 0 || sub.rfind(L"while ", 0) == 0) {
                    lineBrush = m_brKeyword ? (ID2D1Brush*)m_brKeyword : (ID2D1Brush*)d2d.BrushAccent;
                } else if ((sub.front() == L'"' && sub.back() == L'"') || (sub.front() == L'\'' && sub.back() == L'\'')) {
                    lineBrush = m_brString ? (ID2D1Brush*)m_brString : (ID2D1Brush*)d2d.BrushText;
                }
            }

            rt->DrawText(line.c_str(), (UINT32)line.length(), font, lineRect, lineBrush);
        }

        // Caret (Blinking Cursor Bar - perfectly matching font cap/baseline height)
        if (i == m_cursor.line && m_caretVisible) {
            float caretX = textLeft + GetColumnX(i, m_cursor.col);
            float textTop = lineY + padY;
            D2D1_RECT_F caretRect = D2D1::RectF(
                caretX,
                textTop,
                caretX + 2.0f * dpiScale,
                textTop + m_fontSize * dpiScale
            );
            rt->FillRectangle(caretRect, d2d.BrushAccent);
        }
    }

    rt->PopAxisAlignedClip();

    // =========================================================================
    // LAYER 2: PINNED GUTTER COLUMN (Solid background, line numbers, border)
    // =========================================================================
    rt->PushAxisAlignedClip(gutterRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Solid Gutter background (#121215)
    rt->FillRectangle(gutterRect, m_brGutterBg ? m_brGutterBg : d2d.BrushCard);

    for (int i = startLine; i < endLine; ++i) {
        float lineY = codeViewport.top + (float)i * m_lineHeight - m_scrollY;
        if (lineY + m_lineHeight < codeViewport.top || lineY > codeViewport.bottom) continue;

        // Active line highlight in gutter
        if (i == m_cursor.line && m_brActiveLine) {
            D2D1_RECT_F gutterActiveRect = D2D1::RectF(gutterRect.left, lineY, gutterRect.right, lineY + m_lineHeight);
            rt->FillRectangle(gutterActiveRect, m_brActiveLine);
        }

        // Gutter Line Number (Active line is pure white, others muted)
        WCHAR numBuf[32];
        swprintf_s(numBuf, L"%d", i + 1);
        D2D1_RECT_F numRect = D2D1::RectF(
            gutterRect.left + 4.0f * dpiScale,
            lineY,
            gutterRect.right - 8.0f * dpiScale,
            lineY + m_lineHeight
        );
        ID2D1Brush* numBrush = (i == m_cursor.line) ? (ID2D1Brush*)d2d.BrushText : (ID2D1Brush*)d2d.BrushTextMuted;
        rt->DrawText(numBuf, (UINT32)wcslen(numBuf), d2d.FontBadge, numRect, numBrush);
    }

    rt->PopAxisAlignedClip();

    // Gutter vertical separator line (pinned at textOriginX)
    rt->DrawLine(
        D2D1::Point2F(textOriginX, codeViewport.top),
        D2D1::Point2F(textOriginX, codeViewport.bottom),
        d2d.BrushBorderSubtle,
        1.0f
    );

    // 5. Scrollbars (Vertical & Horizontal)
    if (totalH > viewH) {
        float scrollbarW = 6.0f * dpiScale;
        float trackH = viewH;
        float thumbH = std::max(28.0f * dpiScale, (viewH / totalH) * trackH);
        float thumbY = codeViewport.top + (m_scrollY / maxScrollY) * (trackH - thumbH);

        D2D1_RECT_F thumbRect = D2D1::RectF(
            codeViewport.right - scrollbarW - 2.0f,
            thumbY,
            codeViewport.right - 2.0f,
            thumbY + thumbH
        );
        D2D1_ROUNDED_RECT rThumb = D2D1::RoundedRect(thumbRect, scrollbarW / 2.0f, scrollbarW / 2.0f);
        rt->FillRoundedRectangle(rThumb, d2d.BrushBorder);
    }

    if (maxScrollX > 0.0f) {
        float scrollbarH = 4.0f * dpiScale;
        float trackW = textVisibleW;
        float thumbW = std::max(28.0f * dpiScale, (trackW / (trackW + maxScrollX)) * trackW);
        float thumbX = textOriginX + (m_scrollX / maxScrollX) * (trackW - thumbW);

        D2D1_RECT_F thumbRect = D2D1::RectF(
            thumbX,
            codeViewport.bottom - scrollbarH - 2.0f,
            thumbX + thumbW,
            codeViewport.bottom - 2.0f
        );
        D2D1_ROUNDED_RECT rThumb = D2D1::RoundedRect(thumbRect, scrollbarH / 2.0f, scrollbarH / 2.0f);
        rt->FillRoundedRectangle(rThumb, d2d.BrushBorder);
    }

    // =========================================================================
    // 6. MINI VS CODE STATUS BAR (Bottom 24px) - Collision-Free & Responsive
    // =========================================================================
    D2D1_RECT_F statusRect = D2D1::RectF(
        viewportRect.left,
        viewportRect.bottom - statusH,
        viewportRect.right,
        viewportRect.bottom
    );

    // Background (#111114) and top subtle boundary line
    if (m_brStatusBg) {
        rt->FillRectangle(statusRect, m_brStatusBg);
    }
    rt->DrawLine(
        D2D1::Point2F(statusRect.left, statusRect.top),
        D2D1::Point2F(statusRect.right, statusRect.top),
        d2d.BrushBorderSubtle,
        1.0f
    );

    float statY = statusRect.top;
    float statH = statusH;
    float rightX = statusRect.right - 14.0f * dpiScale;
    IDWriteTextFormat* fontRight = d2d.FontCaptionRight ? d2d.FontCaptionRight : d2d.FontCaption;

    // 1. Right-aligned items (Language, EOL, Encoding, Modified)
    // Modified Badge (if dirty): "● Modified (Ctrl+S)"
    if (m_isModified) {
        const WCHAR* modText = L"● Modified (Ctrl+S)";
        float modW = 125.0f * dpiScale;
        m_statusSaveRect = D2D1::RectF(rightX - modW, statY, rightX, statY + statH);
        rt->DrawText(modText, (UINT32)wcslen(modText), fontRight, m_statusSaveRect, d2d.BrushAccent);
        rightX -= modW + 14.0f * dpiScale;
    } else {
        m_statusSaveRect = D2D1::RectF(0, 0, 0, 0);
    }

    // Language Name: e.g. "Python", "TypeScript", "Plain Text"
    float langW = std::max(60.0f * dpiScale, (float)m_languageName.length() * 7.5f * dpiScale + 12.0f * dpiScale);
    D2D1_RECT_F langRect = D2D1::RectF(rightX - langW, statY, rightX, statY + statH);
    rt->DrawText(m_languageName.c_str(), (UINT32)m_languageName.length(), fontRight, langRect, d2d.BrushText);
    rightX -= langW + 14.0f * dpiScale;

    // Line Ending: "CRLF" / "LF" (only if enough space)
    if (rightX > 480.0f * dpiScale) {
        float eolW = 38.0f * dpiScale;
        D2D1_RECT_F eolRect = D2D1::RectF(rightX - eolW, statY, rightX, statY + statH);
        rt->DrawText(m_lineEndingName.c_str(), (UINT32)m_lineEndingName.length(), fontRight, eolRect, d2d.BrushTextMuted);
        rightX -= eolW + 14.0f * dpiScale;
    }

    // Encoding: "UTF-8" (only if enough space)
    if (rightX > 420.0f * dpiScale) {
        float encW = 54.0f * dpiScale;
        D2D1_RECT_F encRect = D2D1::RectF(rightX - encW, statY, rightX, statY + statH);
        rt->DrawText(m_encodingName.c_str(), (UINT32)m_encodingName.length(), fontRight, encRect, d2d.BrushTextMuted);
        rightX -= encW + 14.0f * dpiScale;
    }

    // 2. Left-aligned items (Line/Col, Spaces, Wrap)
    float statX = statusRect.left + 12.0f * dpiScale;

    // A. Line & Column readout: "Ln 14, Col 21"
    WCHAR posBuf[64];
    if (hasSel) {
        std::wstring sel = GetSelectedText();
        swprintf_s(posBuf, L"Ln %d, Col %d (%zu sel)", m_cursor.line + 1, m_cursor.col + 1, sel.length());
    } else {
        swprintf_s(posBuf, L"Ln %d, Col %d", m_cursor.line + 1, m_cursor.col + 1);
    }
    float posW = (float)wcslen(posBuf) * 7.5f * dpiScale + 12.0f * dpiScale;
    if (statX + posW < rightX - 10.0f * dpiScale) {
        D2D1_RECT_F posRect = D2D1::RectF(statX, statY, statX + posW, statY + statH);
        rt->DrawText(posBuf, (UINT32)wcslen(posBuf), d2d.FontCaption, posRect, d2d.BrushText);
        statX += posW + 16.0f * dpiScale;
    }

    // B. Indentation Pill: "Spaces: 4" (Clickable!)
    WCHAR tabBuf[32];
    swprintf_s(tabBuf, L"Spaces: %d", m_tabSpaces);
    float tabW = 68.0f * dpiScale;
    if (statX + tabW < rightX - 10.0f * dpiScale) {
        m_statusSpacesRect = D2D1::RectF(statX, statY, statX + tabW, statY + statH);
        rt->DrawText(tabBuf, (UINT32)wcslen(tabBuf), d2d.FontCaption, m_statusSpacesRect, d2d.BrushTextMuted);
        statX += tabW + 14.0f * dpiScale;
    } else {
        m_statusSpacesRect = D2D1::RectF(0, 0, 0, 0);
    }

    // C. Word Wrap Pill: "Wrap: ON/OFF" (Clickable!)
    const WCHAR* wrapStr = m_wordWrap ? L"Wrap: ON" : L"Wrap: OFF";
    float wrapW = 68.0f * dpiScale;
    if (statX + wrapW < rightX - 10.0f * dpiScale) {
        m_statusWrapRect = D2D1::RectF(statX, statY, statX + wrapW, statY + statH);
        rt->DrawText(wrapStr, (UINT32)wcslen(wrapStr), d2d.FontCaption, m_statusWrapRect, m_wordWrap ? d2d.BrushAccent : d2d.BrushTextMuted);
        statX += wrapW + 14.0f * dpiScale;
    } else {
        m_statusWrapRect = D2D1::RectF(0, 0, 0, 0);
    }
}

void TextEngine::RenderSpreadsheet(
    ID2D1RenderTarget* rt,
    D2DEngine& d2d,
    const D2D1_RECT_F& viewportRect,
    float dpiScale
) {
    // If user toggled to Raw Text View, render using standard code/text editor!
    if (!m_isTableView) {
        RenderText(rt, d2d, viewportRect, dpiScale);
        return;
    }

    if (!rt || m_rows.empty()) return;
    EnsureFont(dpiScale);
    IDWriteTextFormat* font = m_fontCustom ? m_fontCustom : d2d.FontMono;
    if (!font) font = d2d.FontBody;

    float topPadding = GetHeaderTopPadding(dpiScale);
    float gridTop = viewportRect.top + topPadding;
    float rowH = 26.0f * dpiScale;
    float headerH = 28.0f * dpiScale;
    float indexColW = 44.0f * dpiScale;

    // Background
    rt->FillRectangle(viewportRect, d2d.BrushCard);

    int totalRows = (int)m_rows.size();
    float totalH = headerH + (totalRows > 1 ? (totalRows - 1) : 0) * rowH;
    float viewH = viewportRect.bottom - gridTop;
    float maxScrollY = std::max(0.0f, totalH - viewH + 40.0f * dpiScale);
    m_targetScrollY = std::min(m_targetScrollY, maxScrollY);
    m_scrollY = std::min(m_scrollY, maxScrollY);

    float viewW = viewportRect.right - viewportRect.left - indexColW;
    float totalW = 0.0f;
    for (float w : m_columnWidths) totalW += w * dpiScale;
    float maxScrollX = std::max(0.0f, totalW - viewW + 40.0f * dpiScale);
    m_targetScrollX = std::min(m_targetScrollX, maxScrollX);
    m_scrollX = std::min(m_scrollX, maxScrollX);

    // Grid viewport clipping
    D2D1_RECT_F dataClip = D2D1::RectF(
        viewportRect.left + indexColW,
        gridTop + headerH,
        viewportRect.right,
        viewportRect.bottom
    );
    rt->PushAxisAlignedClip(dataClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    int startRow = std::max(1, 1 + (int)(m_scrollY / rowH));
    int endRow = std::min(totalRows, startRow + (int)(viewH / rowH) + 3);

    for (int r = startRow; r < endRow; ++r) {
        float rowY = gridTop + headerH + (float)(r - 1) * rowH - m_scrollY;
        if (rowY + rowH < gridTop + headerH || rowY > viewportRect.bottom) continue;

        // Alternating row background stripes
        if (r % 2 == 1) {
            D2D1_RECT_F stripeRect = D2D1::RectF(viewportRect.left + indexColW, rowY, viewportRect.right, rowY + rowH);
            rt->FillRectangle(stripeRect, d2d.BrushHover);
        }

        // Row cells
        const auto& rowData = m_rows[r];
        float curX = viewportRect.left + indexColW - m_scrollX;

        for (int c = 0; c < m_columnCount; ++c) {
            float colW = (c < (int)m_columnWidths.size() ? m_columnWidths[c] : 85.0f) * dpiScale;
            if (curX + colW >= viewportRect.left + indexColW && curX <= viewportRect.right) {
                if (c < (int)rowData.size() && !rowData[c].empty()) {
                    D2D1_RECT_F cellTextRect = D2D1::RectF(curX + 8.0f * dpiScale, rowY, curX + colW - 8.0f * dpiScale, rowY + rowH);
                    rt->DrawText(rowData[c].c_str(), (UINT32)rowData[c].length(), font, cellTextRect, d2d.BrushText);
                }

                // Vertical cell divider line
                rt->DrawLine(
                    D2D1::Point2F(curX + colW, rowY),
                    D2D1::Point2F(curX + colW, rowY + rowH),
                    d2d.BrushBorderSubtle,
                    0.5f
                );
            }
            curX += colW;
        }

        // Horizontal row divider line
        rt->DrawLine(
            D2D1::Point2F(viewportRect.left + indexColW, rowY + rowH),
            D2D1::Point2F(viewportRect.right, rowY + rowH),
            d2d.BrushBorderSubtle,
            0.5f
        );
    }
    rt->PopAxisAlignedClip();

    // Index Column Background & Gutter numbers
    D2D1_RECT_F indexColRect = D2D1::RectF(viewportRect.left, gridTop + headerH, viewportRect.left + indexColW, viewportRect.bottom);
    rt->FillRectangle(indexColRect, d2d.BrushSidebar);
    rt->DrawLine(
        D2D1::Point2F(viewportRect.left + indexColW, gridTop + headerH),
        D2D1::Point2F(viewportRect.left + indexColW, viewportRect.bottom),
        d2d.BrushBorder,
        1.0f
    );

    for (int r = startRow; r < endRow; ++r) {
        float rowY = gridTop + headerH + (float)(r - 1) * rowH - m_scrollY;
        if (rowY + rowH < gridTop + headerH || rowY > viewportRect.bottom) continue;

        WCHAR numBuf[16];
        swprintf_s(numBuf, L"%d", r);
        D2D1_RECT_F numRect = D2D1::RectF(viewportRect.left + 4.0f, rowY, viewportRect.left + indexColW - 6.0f, rowY + rowH);
        rt->DrawText(numBuf, (UINT32)wcslen(numBuf), d2d.FontBadge, numRect, d2d.BrushTextMuted);
    }

    // Sticky Header Row (Row 0)
    D2D1_RECT_F headerRect = D2D1::RectF(viewportRect.left, gridTop, viewportRect.right, gridTop + headerH);
    rt->FillRectangle(headerRect, d2d.BrushToolbar);
    rt->DrawLine(
        D2D1::Point2F(viewportRect.left, gridTop + headerH),
        D2D1::Point2F(viewportRect.right, gridTop + headerH),
        d2d.BrushBorder,
        1.0f
    );

    // Corner cell
    D2D1_RECT_F cornerCell = D2D1::RectF(viewportRect.left, gridTop, viewportRect.left + indexColW, gridTop + headerH);
    rt->FillRectangle(cornerCell, d2d.BrushSidebar);
    rt->DrawLine(
        D2D1::Point2F(viewportRect.left + indexColW, gridTop),
        D2D1::Point2F(viewportRect.left + indexColW, gridTop + headerH),
        d2d.BrushBorder,
        1.0f
    );

    // Header labels
    D2D1_RECT_F headerClip = D2D1::RectF(viewportRect.left + indexColW, gridTop, viewportRect.right, gridTop + headerH);
    rt->PushAxisAlignedClip(headerClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (!m_rows.empty()) {
        const auto& headers = m_rows[0];
        float curX = viewportRect.left + indexColW - m_scrollX;

        for (int c = 0; c < m_columnCount; ++c) {
            float colW = (c < (int)m_columnWidths.size() ? m_columnWidths[c] : 85.0f) * dpiScale;
            if (curX + colW >= viewportRect.left + indexColW && curX <= viewportRect.right) {
                std::wstring title = (c < (int)headers.size()) ? headers[c] : L"";
                D2D1_RECT_F hTextRect = D2D1::RectF(curX + 8.0f * dpiScale, gridTop, curX + colW - 8.0f * dpiScale, gridTop + headerH);
                rt->DrawText(title.c_str(), (UINT32)title.length(), d2d.FontButton, hTextRect, d2d.BrushText);

                rt->DrawLine(
                    D2D1::Point2F(curX + colW, viewportRect.top + 3.0f),
                    D2D1::Point2F(curX + colW, viewportRect.top + headerH - 3.0f),
                    d2d.BrushBorder,
                    1.0f
                );
            }
            curX += colW;
        }
    }
    rt->PopAxisAlignedClip();
}

} // namespace WinPreview
