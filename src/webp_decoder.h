#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <string>

namespace WinPreview {

// WebP decoding through the vendored libwebp, independent of the Windows WebP
// codec package (which many machines do not have).
class WebpDecoder {
public:
    static bool IsWebpFile(const std::wstring& path);
    static bool Decode(const std::wstring& path, ID2D1RenderTarget* renderTarget,
                       ID2D1Bitmap** outBitmap, UINT* outWidth, UINT* outHeight);
};

} // namespace WinPreview
