#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace WinPreview {

// HTML is previewed as a reader view, not as a browser: tags are mapped to the
// Markdown block model the app already renders, scripts/styles/frames are
// discarded and nothing is executed. This is deliberate — a previewer should
// not run page script from a downloaded file.
bool ConvertHtmlFile(const std::wstring& filePath, std::string& markdownOut,
                     std::wstring& titleOut, int& linkCount);

// Hidden test flag: --html-dump <file> prints the reader text for inspection.
int RunHtmlDump(const std::wstring& filePath);

} // namespace WinPreview
