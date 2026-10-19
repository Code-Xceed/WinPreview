#pragma once
#include "d2d_engine.h"
#include <string>

namespace WinPreview {

struct ImageMetadata {
    UINT width = 0;
    UINT height = 0;
    double dpiX = 96.0;
    double dpiY = 96.0;
    UINT bitDepth = 32;
    std::wstring formatName;
    std::wstring colorSpace;
    std::wstring cameraMake;
    std::wstring cameraModel;
    std::wstring dateTaken;
    std::wstring exposureTime;
    std::wstring fNumber;
    std::wstring isoSpeed;
    std::wstring focalLength;
};

class WicDecoder {
public:
    static bool LoadImageFromFile(
        IWICImagingFactory* factory,
        ID2D1RenderTarget* renderTarget,
        const std::wstring& filePath,
        ID2D1Bitmap** outBitmap,
        UINT* outWidth,
        UINT* outHeight,
        D2D1_RECT_F* outContentBounds = nullptr
    );

    static bool CreateThumbnail(
        IWICImagingFactory* factory,
        ID2D1RenderTarget* renderTarget,
        const std::wstring& filePath,
        UINT maxDimension,
        ID2D1Bitmap** outBitmap
    );

    static bool GetImageMetadata(
        IWICImagingFactory* factory,
        const std::wstring& filePath,
        ImageMetadata* outMeta
    );

    static bool SaveImage(
        IWICImagingFactory* factory,
        const std::wstring& inPath,
        const std::wstring& outPath,
        const D2D1_RECT_F* cropRect = nullptr,
        int rotation = 0,
        bool flipH = false,
        bool flipV = false,
        float qualityJpeg = 0.92f
    );

    static bool LoadShellThumbnail(
        IWICImagingFactory* factory,
        ID2D1RenderTarget* renderTarget,
        const std::wstring& filePath,
        UINT targetDimension,
        ID2D1Bitmap** outBitmap
    );

    static bool LoadImageFromMemory(
        IWICImagingFactory* factory,
        ID2D1RenderTarget* renderTarget,
        const void* data,
        DWORD size,
        ID2D1Bitmap** outBitmap
    );
};

} // namespace WinPreview
