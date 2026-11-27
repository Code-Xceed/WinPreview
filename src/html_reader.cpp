#include "html_reader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace WinPreview {

namespace {

const size_t kMaxHtmlBytes = 16u * 1024u * 1024u;

size_t FindIgnoreCase(const std::string& haystack, const std::string& needle, size_t from);

bool IsSpaceChar(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string LowerAscii(const std::string& value) {
    std::string out = value;
    for (char& c : out) c = (char)tolower((unsigned char)c);
    return out;
}

bool EqualsIgnoreCase(const std::string& a, const char* b) {
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    }
    return true;
}

void AppendUtf8(std::string& out, unsigned int code) {
    if (code == 0) return;
    if (code < 0x80) {
        out += (char)code;
    } else if (code < 0x800) {
        out += (char)(0xC0 | (code >> 6));
        out += (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += (char)(0xE0 | (code >> 12));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    } else {
        out += (char)(0xF0 | (code >> 18));
        out += (char)(0x80 | ((code >> 12) & 0x3F));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    }
}

unsigned int NamedEntity(const std::string& name) {
    struct Entry { const char* name; unsigned int code; };
    static const Entry kEntities[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
        { "nbsp", 0xA0 }, { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "thinsp", 0x2009 },
        { "ndash", 0x2013 }, { "mdash", 0x2014 }, { "hellip", 0x2026 },
        { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
        { "bull", 0x2022 }, { "middot", 0x00B7 }, { "copy", 0x00A9 }, { "reg", 0x00AE },
        { "trade", 0x2122 }, { "deg", 0x00B0 }, { "plusmn", 0x00B1 }, { "times", 0x00D7 },
        { "divide", 0x00F7 }, { "frac12", 0x00BD }, { "frac14", 0x00BC }, { "frac34", 0x00BE },
        { "euro", 0x20AC }, { "pound", 0x00A3 }, { "yen", 0x00A5 }, { "cent", 0x00A2 },
        { "sect", 0x00A7 }, { "para", 0x00B6 }, { "laquo", 0x00AB }, { "raquo", 0x00BB },
        { "aacute", 0x00E1 }, { "eacute", 0x00E9 }, { "iacute", 0x00ED }, { "oacute", 0x00F3 },
        { "uacute", 0x00FA }, { "auml", 0x00E4 }, { "ouml", 0x00F6 }, { "uuml", 0x00FC },
        { "szlig", 0x00DF }, { "ccedil", 0x00E7 }, { "ntilde", 0x00F1 },
    };
    for (const auto& entry : kEntities) {
        if (name == entry.name) return entry.code;
    }
    return 0;
}

// Decodes &amp; / &#8212; / &#x2014; at `pos` (which points at '&').
bool DecodeEntity(const std::string& text, size_t pos, unsigned int& code, size_t& consumed) {
    size_t end = text.find(';', pos + 1);
    if (end == std::string::npos || end - pos > 12) return false;
    std::string body = text.substr(pos + 1, end - pos - 1);
    if (body.empty()) return false;
    if (body[0] == '#') {
        unsigned int value = 0;
        size_t digit = 1;
        int base = 10;
        if (body.size() > 2 && (body[1] == 'x' || body[1] == 'X')) {
            base = 16;
            digit = 2;
        }
        if (digit >= body.size()) return false;
        for (size_t i = digit; i < body.size(); ++i) {
            char c = body[i];
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (base == 16 && c >= 'a' && c <= 'f') d = 10 + c - 'a';
            else if (base == 16 && c >= 'A' && c <= 'F') d = 10 + c - 'A';
            else return false;
            value = value * (unsigned)base + (unsigned)d;
            if (value > 0x10FFFF) return false;
        }
        code = value;
        consumed = end - pos + 1;
        return true;
    }
    unsigned int named = NamedEntity(body);
    if (!named) return false;
    code = named;
    consumed = end - pos + 1;
    return true;
}

std::string DecodeEntities(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '&') {
            unsigned int code = 0;
            size_t consumed = 0;
            if (DecodeEntity(text, i, code, consumed)) {
                AppendUtf8(out, code);
                i += consumed;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

std::string StripTags(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    for (size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            size_t end = html.find('>', i);
            if (end == std::string::npos) break;
            i = end + 1;
        } else {
            out += html[i++];
        }
    }
    return out;
}

std::string ExtractTitle(const std::string& html) {
    size_t open = FindIgnoreCase(html, "<title", 0);
    if (open == std::string::npos) return std::string();
    size_t start = html.find('>', open);
    if (start == std::string::npos) return std::string();
    size_t close = FindIgnoreCase(html, "</title", start + 1);
    if (close == std::string::npos) return std::string();
    std::string raw = html.substr(start + 1, close - start - 1);
    std::string text = DecodeEntities(StripTags(raw));
    // collapse whitespace
    std::string cleaned;
    bool space = false;
    for (char c : text) {
        if (IsSpaceChar(c)) { space = true; continue; }
        if (space && !cleaned.empty()) cleaned += ' ';
        space = false;
        cleaned += c;
    }
    return cleaned;
}

size_t FindIgnoreCase(const std::string& haystack, const std::string& needle, size_t from) {
    if (needle.empty() || from >= haystack.size()) return std::string::npos;
    for (size_t i = from; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t k = 0; k < needle.size(); ++k) {
            if (tolower((unsigned char)haystack[i + k]) !=
                tolower((unsigned char)needle[k])) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return std::string::npos;
}

std::string GetAttribute(const std::string& attrs, const char* name) {
    std::string lower = LowerAscii(attrs);
    size_t pos = 0;
    while ((pos = lower.find(name, pos)) != std::string::npos) {
        // must be preceded by whitespace and followed by '='
        bool boundary = pos == 0 || IsSpaceChar(lower[pos - 1]);
        size_t after = pos + strlen(name);
        if (boundary && after < lower.size() && lower[after] == '=') {
            size_t quote = attrs.find_first_not_of(" \t\r\n", after + 1);
            if (quote == std::string::npos) return std::string();
            char q = attrs[quote];
            if (q == '"' || q == '\'') {
                size_t end = attrs.find(q, quote + 1);
                if (end == std::string::npos) return std::string();
                return attrs.substr(quote + 1, end - quote - 1);
            }
            size_t end = attrs.find_first_of(" \t\r\n>", quote);
            return attrs.substr(quote, end == std::string::npos ? std::string::npos : end - quote);
        }
        pos += strlen(name);
    }
    return std::string();
}

bool IsSafeLink(const std::string& href) {
    std::string lower = LowerAscii(href);
    return lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0 ||
           lower.rfind("mailto:", 0) == 0;
}

bool IsBlockTag(const std::string& name) {
    static const char* kBlocks[] = {
        "p", "div", "section", "article", "main", "header", "footer", "nav", "aside",
        "ul", "ol", "dl", "dt", "dd", "table", "thead", "tbody", "tfoot", "tr",
        "figure", "figcaption", "address", "form", "fieldset", "details", "summary",
    };
    for (const char* block : kBlocks) {
        if (name == block) return true;
    }
    return false;
}

bool IsSkippedTag(const std::string& name) {
    static const char* kSkipped[] = {
        "script", "style", "noscript", "template", "svg", "canvas", "iframe",
        "object", "embed", "head", "video", "audio",
    };
    for (const char* skipped : kSkipped) {
        if (name == skipped) return true;
    }
    return false;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring out((size_t)length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), length);
    return out;
}

bool ReadFileCapped(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || (unsigned long long)size.QuadPart > kMaxHtmlBytes) {
        CloseHandle(file);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    bool ok = ReadFile(file, out.data(), (DWORD)out.size(), &read, nullptr) != FALSE && read == out.size();
    CloseHandle(file);
    if (!ok) out.clear();
    return ok;
}

} // namespace

static bool Convert(const std::string& html, std::string& out, std::wstring& title, int& linkCount) {
    out.clear();
    linkCount = 0;
    std::string pageTitle = ExtractTitle(html);
    title = Utf8ToWide(pageTitle);

    std::vector<std::string> links;
    bool pre = false;

    auto trailingNewlines = [&]() -> size_t {
        size_t count = 0;
        for (size_t i = out.size(); i > 0 && out[i - 1] == '\n'; --i) ++count;
        return count;
    };
    auto ensureNewlines = [&](size_t wanted) {
        size_t have = trailingNewlines();
        for (; have < wanted; ++have) out += '\n';
    };
    auto atLineStart = [&]() -> bool {
        return out.empty() || out.back() == '\n';
    };
    auto ensureSpace = [&]() {
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
    };

    size_t i = 0;
    while (i < html.size()) {
        if (html[i] != '<') {
            size_t next = html.find('<', i);
            if (next == std::string::npos) next = html.size();
            std::string chunk = DecodeEntities(html.substr(i, next - i));
            if (pre) {
                out += chunk;
            } else {
                bool pendingSpace = false;
                for (char c : chunk) {
                    if (IsSpaceChar(c)) {
                        pendingSpace = true;
                        continue;
                    }
                    if (pendingSpace) {
                        if (!atLineStart()) out += ' ';
                        pendingSpace = false;
                    }
                    out += c;
                }
                if (pendingSpace && !atLineStart()) out += ' ';
            }
            i = next;
            continue;
        }

        if (html.compare(i, 4, "<!--") == 0) {
            size_t end = html.find("-->", i + 4);
            i = end == std::string::npos ? html.size() : end + 3;
            continue;
        }
        if (html.compare(i, 2, "<!") == 0 || html.compare(i, 2, "<?") == 0) {
            size_t end = html.find('>', i);
            i = end == std::string::npos ? html.size() : end + 1;
            continue;
        }

        bool closing = i + 1 < html.size() && html[i + 1] == '/';
        size_t nameStart = i + (closing ? 2 : 1);
        size_t cursor = nameStart;
        while (cursor < html.size() &&
               (isalnum((unsigned char)html[cursor]) || html[cursor] == '-' || html[cursor] == ':')) {
            ++cursor;
        }
        std::string name = LowerAscii(html.substr(nameStart, cursor - nameStart));

        size_t tagEnd = cursor;
        char quote = 0;
        while (tagEnd < html.size()) {
            char c = html[tagEnd];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
            ++tagEnd;
        }
        std::string attrs = html.substr(cursor, (tagEnd < html.size() ? tagEnd : html.size()) - cursor);
        i = tagEnd < html.size() ? tagEnd + 1 : html.size();

        if (!closing && IsSkippedTag(name)) {
            size_t end = FindIgnoreCase(html, "</" + name, i);
            if (end == std::string::npos) {
                i = html.size();
            } else {
                size_t gt = html.find('>', end);
                i = gt == std::string::npos ? html.size() : gt + 1;
            }
            continue;
        }

        if (name == "br") {
            out += '\n';
            continue;
        }
        if (name == "hr") {
            ensureNewlines(2);
            out += "---";
            ensureNewlines(2);
            continue;
        }
        if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
            ensureNewlines(2);
            if (!closing) {
                out.append((size_t)(name[1] - '0'), '#');
                out += ' ';
            } else {
                ensureNewlines(2);
            }
            continue;
        }
        if (name == "li") {
            ensureNewlines(1);
            if (!closing) out += "- ";
            continue;
        }
        if (name == "td" || name == "th") {
            // The Markdown reader has no table block, so a table reads as one
            // aligned row per line.
            if (!closing && !atLineStart()) out += " | ";
            continue;
        }
        if (name == "tr") {
            ensureNewlines(1);
            continue;
        }
        if (name == "blockquote") {
            ensureNewlines(1);
            if (!closing && atLineStart()) out += "> ";
            continue;
        }
        if (name == "pre") {
            ensureNewlines(2);
            if (!closing) {
                out += "```\n";
                pre = true;
            } else {
                pre = false;
                while (!out.empty() && out.back() == '\n') out.pop_back();
                out += "\n```";
                ensureNewlines(2);
            }
            continue;
        }
        if (IsBlockTag(name)) {
            ensureNewlines(2);
            continue;
        }
        // The reader styles headings, lists, quotes and code blocks; inline
        // emphasis markers are dropped rather than printed as punctuation.
        if (name == "strong" || name == "b" || name == "em" || name == "i" ||
            (name == "code" && !pre)) {
            continue;
        }
        if (name == "a" && !closing) {
            std::string href = DecodeEntities(GetAttribute(attrs, "href"));
            if (IsSafeLink(href) &&
                std::find(links.begin(), links.end(), href) == links.end() &&
                links.size() < 60) {
                links.push_back(href);
            }
            continue;
        }
        if (name == "img" && !closing) {
            std::string alt = DecodeEntities(GetAttribute(attrs, "alt"));
            if (!alt.empty()) {
                ensureSpace();
                out += "[image: " + alt + "]";
                ensureSpace();
            }
            continue;
        }
        // Every other tag is decoration for the reader view.
    }

    // Tidy: at most one blank line, no trailing spaces, no leading/trailing gaps.
    std::string cleaned;
    cleaned.reserve(out.size());
    size_t newlineRun = 0;
    for (char c : out) {
        if (c == '\n') {
            ++newlineRun;
            if (newlineRun <= 2) cleaned += c;
        } else {
            newlineRun = 0;
            cleaned += c;
        }
    }
    while (!cleaned.empty() && (cleaned.back() == '\n' || cleaned.back() == ' ')) cleaned.pop_back();
    size_t first = cleaned.find_first_not_of("\n ");
    if (first == std::string::npos) cleaned.clear();
    else cleaned.erase(0, first);

    if (!links.empty()) {
        cleaned += "\n\n## Links\n\n";
        for (const auto& href : links) {
            cleaned += "- " + href + "\n";
        }
        linkCount = (int)links.size();
    }

    out = cleaned;
    return true;
}

bool ConvertHtmlFile(const std::wstring& filePath, std::string& markdownOut,
                     std::wstring& titleOut, int& linkCount) {
    std::string html;
    if (!ReadFileCapped(filePath, html)) return false;
    // Reject binary masquerading as HTML.
    if (html.find('\0') != std::string::npos) return false;
    return Convert(html, markdownOut, titleOut, linkCount);
}

int RunHtmlDump(const std::wstring& filePath) {
    std::string markdown;
    std::wstring title;
    int links = 0;
    if (!ConvertHtmlFile(filePath, markdown, title, links)) {
        wprintf(L"loaded=0\n");
        return 1;
    }
    std::wstring text = Utf8ToWide(markdown);
    wprintf(L"loaded=1 title='%ls' links=%d chars=%zu\n", title.c_str(), links, text.size());
    wprintf(L"--- reader ---\n%ls\n", text.c_str());
    return 0;
}

} // namespace WinPreview
