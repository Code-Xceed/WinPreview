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
        D2D1_RECT_F* outContentBounds = nullptr,
        UINT frameIndex = 0
    );

    // Frames in a container: pages for TIFF, animation frames for GIF.
    static UINT GetFrameCount(const std::wstring& filePath);

    // True when Windows has no decoder registered for this file's format
    // (WINCODEC_ERR_COMPONENTNOTFOUND), as opposed to a damaged file.
    static bool MissingCodec(IWICImagingFactory* wic, const std::wstring& filePath);

    // HRESULT of the last decoder probe (diagnostics: 0x88982F50 = no codec).
    static long LastDecoderHr();

    // GIF frame delay in milliseconds (0 when the frame has no delay).
    static UINT GetFrameDelayMs(const std::wstring& filePath, UINT frameIndex);

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

    // thumbnailOnly refuses to fall back to the file-type icon: an icon is not a
    // preview, and showing one made undecodable images look like they opened.
    static bool LoadShellThumbnail(
        IWICImagingFactory* factory,
        ID2D1RenderTarget* renderTarget,
        const std::wstring& filePath,
        UINT targetDimension,
        ID2D1Bitmap** outBitmap,
        bool thumbnailOnly = false
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
