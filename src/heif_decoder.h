#pragma once
#include <d2d1.h>
#include <string>
#include "wic_decoder.h"

namespace WinPreview {

class HeifDecoder {
public:
    static bool IsAvailable();
    
    static bool LoadImageFromFile(
        ID2D1RenderTarget* renderTarget,
        const std::wstring& filePath,
        ID2D1Bitmap** outBitmap,
        UINT* outWidth,
        UINT* outHeight,
        ImageMetadata* outMeta
    );

    static bool GetMetadata(
        const std::wstring& filePath,
        ImageMetadata* outMeta
    );
};

} // namespace WinPreview
