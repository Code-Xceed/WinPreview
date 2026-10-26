#include "wic_decoder.h"
#include "heif_decoder.h"
#include <propvarutil.h>
#include <shobjidl.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace WinPreview {

static bool LoadRawPreview(
    IWICImagingFactory* factory,
    ID2D1RenderTarget* renderTarget,
    const std::wstring& filePath,
    ID2D1Bitmap** outBitmap,
    UINT* outWidth,
    UINT* outHeight,
    D2D1_RECT_F* outContentBounds
) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart < 65536) {
        CloseHandle(hFile);
        return false;
    }

    DWORD readSize = (DWORD)(std::min)(liSize.QuadPart, (LONGLONG)(32 * 1024 * 1024));
    std::vector<BYTE> buf(readSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buf.data(), readSize, &bytesRead, NULL) || bytesRead < 65536) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    size_t bestOffset = 0;
    size_t bestLen = 0;

    for (size_t i = 0; i + 3 < bytesRead; ++i) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD8 && buf[i + 2] == 0xFF) {
            for (size_t j = i + 1024; j + 1 < bytesRead; ++j) {
                if (buf[j] == 0xFF && buf[j + 1] == 0xD9) {
                    size_t len = (j + 2) - i;
                    if (len > bestLen && len >= 65536) {
                        bestOffset = i;
                        bestLen = len;
                    }
                    i = j + 1;
                    break;
                }
            }
        }
    }

    if (bestLen >= 65536) {
        if (WicDecoder::LoadImageFromMemory(factory, renderTarget, buf.data() + bestOffset, (DWORD)bestLen, outBitmap)) {
            if (*outBitmap) {
                D2D1_SIZE_F sz = (*outBitmap)->GetSize();
                if (outWidth) *outWidth = (UINT)sz.width;
                if (outHeight) *outHeight = (UINT)sz.height;
                if (outContentBounds) *outContentBounds = D2D1::RectF(0.0f, 0.0f, sz.width, sz.height);
                return true;
            }
        }
    }

    return false;
}

bool WicDecoder::LoadImageFromFile(
    IWICImagingFactory* factory,
    ID2D1RenderTarget* renderTarget,
    const std::wstring& filePath,
    ID2D1Bitmap** outBitmap,
    UINT* outWidth,
    UINT* outHeight,
    D2D1_RECT_F* outContentBounds
) {
    if (!factory || !renderTarget || !outBitmap) return false;
    *outBitmap = nullptr;
    if (outWidth) *outWidth = 0;
    if (outHeight) *outHeight = 0;
    if (outContentBounds) *outContentBounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(
        filePath.c_str(),
        NULL,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder
    );
    if (FAILED(hr) || !decoder) {
        std::wstring ext;
        size_t dotPos = filePath.find_last_of(L'.');
        if (dotPos != std::wstring::npos) {
            ext = filePath.substr(dotPos);
            for (auto& c : ext) c = towlower(c);
        }

        // Tier 2: HEIC / HEIF / AVIF via HeifDecoder
        if (ext == L".heic" || ext == L".heif" || ext == L".avif") {
            ImageMetadata meta;
            if (HeifDecoder::LoadImageFromFile(renderTarget, filePath, outBitmap, outWidth, outHeight, &meta)) {
                if (outContentBounds && outWidth && outHeight) {
                    *outContentBounds = D2D1::RectF(0.0f, 0.0f, (float)*outWidth, (float)*outHeight);
                }
                return true;
            }
        }

        // Tier 3: Camera RAW embedded high-res preview
        if (ext == L".cr2" || ext == L".nef" || ext == L".arw" || ext == L".dng" ||
            ext == L".raw" || ext == L".orf" || ext == L".rw2" || ext == L".pef") {
            if (LoadRawPreview(factory, renderTarget, filePath, outBitmap, outWidth, outHeight, outContentBounds)) {
                return true;
            }
        }

        // Tier 4: Shell Image Factory (large preview)
        if (LoadShellThumbnail(factory, renderTarget, filePath, 3840, outBitmap)) {
            if (outBitmap && *outBitmap) {
                D2D1_SIZE_F sz = (*outBitmap)->GetSize();
                if (outWidth) *outWidth = (UINT)sz.width;
                if (outHeight) *outHeight = (UINT)sz.height;
                if (outContentBounds) *outContentBounds = D2D1::RectF(0.0f, 0.0f, sz.width, sz.height);
                return true;
            }
        }

        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        SafeRelease(decoder);
        return false;
    }

    UINT width = 0, height = 0;
    frame->GetSize(&width, &height);

    // Check EXIF orientation
    WICBitmapTransformOptions transform = WICBitmapTransformRotate0;
    IWICMetadataQueryReader* queryReader = nullptr;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&queryReader))) {
        PROPVARIANT propValue;
        PropVariantInit(&propValue);
        if (SUCCEEDED(queryReader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &propValue))) {
            if (propValue.vt == VT_UI2) {
                switch (propValue.uiVal) {
                    case 3: transform = WICBitmapTransformRotate180; break;
                    case 6: transform = WICBitmapTransformRotate90; break;
                    case 8: transform = WICBitmapTransformRotate270; break;
                    default: transform = WICBitmapTransformRotate0; break;
                }
            }
            PropVariantClear(&propValue);
        }
        SafeRelease(queryReader);
    }

    IWICBitmapSource* source = frame;
    source->AddRef();

    // If rotation needed, apply rotator
    if (transform != WICBitmapTransformRotate0) {
        IWICBitmapFlipRotator* rotator = nullptr;
        if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rotator))) {
            if (SUCCEEDED(rotator->Initialize(source, transform))) {
                SafeRelease(source);
                source = rotator;
                source->AddRef();
                source->GetSize(&width, &height);
            }
            SafeRelease(rotator);
        }
    }

    // Convert to 32bpp PBGRA. For very large images (RAW exports, panoramas,
    // 100 MP photos) decoding at full resolution costs hundreds of MB of RAM
    // and a long UI-thread stall for pixels the previewer can never show at
    // native size. Cap the decode at ~8 MP (4K-class with zoom headroom) via
    // WIC's on-the-fly scaler; reported dimensions stay original so zoom
    // math and metadata are unaffected, and content bounds are mapped back.
    constexpr float kMaxDecodePixels = 8.0f * 1024.0f * 1024.0f;
    const UINT origWidth = width, origHeight = height;
    float boundsScale = 1.0f;

    IWICBitmapScaler* scaler = nullptr;
    if ((float)width * (float)height > kMaxDecodePixels) {
        const float s = sqrtf(kMaxDecodePixels / ((float)width * (float)height));
        const UINT scaledW = (std::max)(1u, (UINT)(width * s + 0.5f));
        const UINT scaledH = (std::max)(1u, (UINT)(height * s + 0.5f));
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(source, scaledW, scaledH, WICBitmapInterpolationModeFant))) {
            width = scaledW;
            height = scaledH;
            boundsScale = (float)origWidth / (float)width;
        } else {
            SafeRelease(scaler); // fall back to full-res decode
        }
    }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(
            scaler ? (IWICBitmapSource*)scaler : source,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            NULL,
            0.0f,
            WICBitmapPaletteTypeCustom
        );
    }

    D2D1_RECT_F contentBounds = D2D1::RectF(0.0f, 0.0f, (float)width, (float)height);

    if (SUCCEEDED(hr)) {
        if (outContentBounds && width > 32 && height > 32 && (width * height <= 2073600)) {
            IWICBitmap* wicBitmap = nullptr;
            if (SUCCEEDED(factory->CreateBitmapFromSource(converter, WICBitmapCacheOnDemand, &wicBitmap))) {
                WICRect lockRc = { 0, 0, (INT)width, (INT)height };
                IWICBitmapLock* lock = nullptr;
                if (SUCCEEDED(wicBitmap->Lock(&lockRc, WICBitmapLockRead, &lock))) {
                    UINT bufferSize = 0;
                    BYTE* pData = nullptr;
                    UINT stride = 0;
                    if (SUCCEEDED(lock->GetDataPointer(&bufferSize, &pData)) && SUCCEEDED(lock->GetStride(&stride))) {
                        const int threshold = 18;
                        auto isPixelBlack = [&](int px, int py) {
                            const BYTE* pRow = pData + py * stride;
                            const UINT32* pPix = (const UINT32*)pRow;
                            UINT32 c = pPix[px];
                            BYTE b = (BYTE)(c & 0xFF);
                            BYTE g = (BYTE)((c >> 8) & 0xFF);
                            BYTE r = (BYTE)((c >> 16) & 0xFF);
                            return (r <= threshold && g <= threshold && b <= threshold);
                        };

                        int cropLeft = 0;
                        int maxScanW = (int)width / 3;
                        for (int x = 0; x < maxScanW; x++) {
                            bool colBlack = true;
                            int stepY = (std::max)(1, (int)height / 64);
                            for (int y = 0; y < (int)height; y += stepY) {
                                if (!isPixelBlack(x, y)) {
                                    colBlack = false;
                                    break;
                                }
                            }
                            if (!colBlack) {
                                cropLeft = x;
                                break;
                            }
                        }

                        int cropRight = 0;
                        for (int x = (int)width - 1; x >= (int)width - maxScanW; x--) {
                            bool colBlack = true;
                            int stepY = (std::max)(1, (int)height / 64);
                            for (int y = 0; y < (int)height; y += stepY) {
                                if (!isPixelBlack(x, y)) {
                                    colBlack = false;
                                    break;
                                }
                            }
                            if (!colBlack) {
                                cropRight = (int)width - 1 - x;
                                break;
                            }
                        }

                        int cropTop = 0;
                        int maxScanH = (int)height / 3;
                        for (int y = 0; y < maxScanH; y++) {
                            bool rowBlack = true;
                            int stepX = (std::max)(1, (int)width / 64);
                            for (int x = 0; x < (int)width; x += stepX) {
                                if (!isPixelBlack(x, y)) {
                                    rowBlack = false;
                                    break;
                                }
                            }
                            if (!rowBlack) {
                                cropTop = y;
                                break;
                            }
                        }

                        int cropBottom = 0;
                        for (int y = (int)height - 1; y >= (int)height - maxScanH; y--) {
                            bool rowBlack = true;
                            int stepX = (std::max)(1, (int)width / 64);
                            for (int x = 0; x < (int)width; x += stepX) {
                                if (!isPixelBlack(x, y)) {
                                    rowBlack = false;
                                    break;
                                }
                            }
                            if (!rowBlack) {
                                cropBottom = (int)height - 1 - y;
                                break;
                            }
                        }

                        if (cropLeft >= 8 || cropRight >= 8 || cropTop >= 8 || cropBottom >= 8) {
                            contentBounds = D2D1::RectF(
                                (float)cropLeft,
                                (float)cropTop,
                                (float)(width - cropRight),
                                (float)(height - cropBottom)
                            );
                        }
                    }
                    SafeRelease(lock);
                }
                hr = renderTarget->CreateBitmapFromWicBitmap(wicBitmap, NULL, outBitmap);
                SafeRelease(wicBitmap);
            }
        } else {
            hr = renderTarget->CreateBitmapFromWicBitmap(converter, NULL, outBitmap);
        }
    }

    if (SUCCEEDED(hr) && *outBitmap) {
        if (outWidth) *outWidth = origWidth;
        if (outHeight) *outHeight = origHeight;
        if (outContentBounds) {
            *outContentBounds = D2D1::RectF(
                contentBounds.left * boundsScale,
                contentBounds.top * boundsScale,
                contentBounds.right * boundsScale,
                contentBounds.bottom * boundsScale
            );
        }
    }

    SafeRelease(converter);
    SafeRelease(scaler);
    SafeRelease(source);
    SafeRelease(frame);
    SafeRelease(decoder);

    return SUCCEEDED(hr);
}

bool WicDecoder::CreateThumbnail(
    IWICImagingFactory* factory,
    ID2D1RenderTarget* renderTarget,
    const std::wstring& filePath,
    UINT maxDimension,
    ID2D1Bitmap** outBitmap
) {
    if (!factory || !renderTarget || !outBitmap) return false;
    *outBitmap = nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(
        filePath.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        return LoadImageFromFile(factory, renderTarget, filePath, outBitmap, nullptr, nullptr, nullptr);
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        SafeRelease(decoder);
        return false;
    }

    UINT width = 0, height = 0;
    frame->GetSize(&width, &height);

    UINT targetW = maxDimension;
    UINT targetH = maxDimension;
    if (width > height) {
        targetH = (UINT)((float)height * maxDimension / width);
    } else {
        targetW = (UINT)((float)width * maxDimension / height);
    }
    if (targetW < 1) targetW = 1;
    if (targetH < 1) targetH = 1;

    IWICBitmapScaler* scaler = nullptr;
    hr = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr)) {
        hr = scaler->Initialize(frame, targetW, targetH, WICBitmapInterpolationModeFant);
    }

    IWICFormatConverter* converter = nullptr;
    if (SUCCEEDED(hr)) {
        hr = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, NULL, 0.0f, WICBitmapPaletteTypeCustom);
    }

    if (SUCCEEDED(hr)) {
        hr = renderTarget->CreateBitmapFromWicBitmap(converter, NULL, outBitmap);
    }

    SafeRelease(converter);
    SafeRelease(scaler);
    SafeRelease(frame);
    SafeRelease(decoder);

    return SUCCEEDED(hr);
}

// =========================================================================
// METADATA EXTRACTION
// =========================================================================

static std::wstring PropVariantToWString(const PROPVARIANT& prop) {
    if (prop.vt == VT_LPWSTR && prop.pwszVal) {
        return prop.pwszVal;
    }
    if (prop.vt == VT_LPSTR && prop.pszVal) {
        int len = MultiByteToWideChar(CP_ACP, 0, prop.pszVal, -1, NULL, 0);
        if (len > 0) {
            std::wstring res(len, L'\0');
            MultiByteToWideChar(CP_ACP, 0, prop.pszVal, -1, &res[0], len);
            if (!res.empty() && res.back() == L'\0') res.pop_back();
            return res;
        }
    }
    if (prop.vt == VT_UI2) {
        return std::to_wstring(prop.uiVal);
    }
    if (prop.vt == VT_UI4) {
        return std::to_wstring(prop.ulVal);
    }
    if (prop.vt == VT_R8) {
        wchar_t buf[32];
        swprintf_s(buf, L"%.2f", prop.dblVal);
        return buf;
    }
    return L"";
}

bool WicDecoder::GetImageMetadata(
    IWICImagingFactory* factory,
    const std::wstring& filePath,
    ImageMetadata* outMeta
) {
    if (!factory || !outMeta) return false;
    *outMeta = ImageMetadata();

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(
        filePath.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        std::wstring ext;
        size_t dotPos = filePath.find_last_of(L'.');
        if (dotPos != std::wstring::npos) {
            ext = filePath.substr(dotPos);
            for (auto& c : ext) c = towlower(c);
        }
        if (ext == L".heic" || ext == L".heif" || ext == L".avif") {
            outMeta->formatName = (ext == L".avif") ? L"AVIF Image" : L"HEIF / HEIC Image";
            return HeifDecoder::GetMetadata(filePath, outMeta);
        }
        return false;
    }

    // Format Name
    GUID containerFormat = {};
    if (SUCCEEDED(decoder->GetContainerFormat(&containerFormat))) {
        if (IsEqualGUID(containerFormat, GUID_ContainerFormatPng)) outMeta->formatName = L"PNG Image";
        else if (IsEqualGUID(containerFormat, GUID_ContainerFormatJpeg)) outMeta->formatName = L"JPEG Image";
        else if (IsEqualGUID(containerFormat, GUID_ContainerFormatBmp)) outMeta->formatName = L"BMP Image";
        else if (IsEqualGUID(containerFormat, GUID_ContainerFormatTiff)) outMeta->formatName = L"TIFF Image";
        else if (IsEqualGUID(containerFormat, GUID_ContainerFormatGif)) outMeta->formatName = L"GIF Image";
        else if (IsEqualGUID(containerFormat, GUID_ContainerFormatWmp)) outMeta->formatName = L"HD Photo / WebP";
        else outMeta->formatName = L"Raster Image";
    }

    IWICBitmapFrameDecode* frame = nullptr;
    if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
        frame->GetSize(&outMeta->width, &outMeta->height);
        frame->GetResolution(&outMeta->dpiX, &outMeta->dpiY);

        WICPixelFormatGUID pixelFormat = {};
        if (SUCCEEDED(frame->GetPixelFormat(&pixelFormat))) {
            IWICComponentInfo* compInfo = nullptr;
            if (SUCCEEDED(factory->CreateComponentInfo(pixelFormat, &compInfo))) {
                IWICPixelFormatInfo* pxInfo = nullptr;
                if (SUCCEEDED(compInfo->QueryInterface(IID_PPV_ARGS(&pxInfo)))) {
                    pxInfo->GetBitsPerPixel(&outMeta->bitDepth);
                    SafeRelease(pxInfo);
                }
                SafeRelease(compInfo);
            }
        }

        IWICMetadataQueryReader* qReader = nullptr;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&qReader))) {
            PROPVARIANT pv;
            auto ReadTag = [&](const wchar_t* query) -> std::wstring {
                PropVariantInit(&pv);
                std::wstring val;
                if (SUCCEEDED(qReader->GetMetadataByName(query, &pv))) {
                    val = PropVariantToWString(pv);
                    PropVariantClear(&pv);
                }
                return val;
            };

            outMeta->cameraMake = ReadTag(L"/app1/ifd/{ushort=271}");
            outMeta->cameraModel = ReadTag(L"/app1/ifd/{ushort=272}");
            outMeta->dateTaken = ReadTag(L"/app1/ifd/exif/{ushort=36867}");
            if (outMeta->dateTaken.empty()) outMeta->dateTaken = ReadTag(L"/app1/ifd/{ushort=306}");
            outMeta->exposureTime = ReadTag(L"/app1/ifd/exif/{ushort=33434}");
            outMeta->fNumber = ReadTag(L"/app1/ifd/exif/{ushort=33437}");
            outMeta->isoSpeed = ReadTag(L"/app1/ifd/exif/{ushort=34855}");
            outMeta->focalLength = ReadTag(L"/app1/ifd/exif/{ushort=37386}");

            SafeRelease(qReader);
        }
        SafeRelease(frame);
    }
    SafeRelease(decoder);
    return true;
}

// =========================================================================
// IMAGE CROPPING & LOSSLESS SAVING
// =========================================================================

bool WicDecoder::SaveImage(
    IWICImagingFactory* factory,
    const std::wstring& inPath,
    const std::wstring& outPath,
    const D2D1_RECT_F* cropRect,
    int rotation,
    bool flipH,
    bool flipV,
    float qualityJpeg
) {
    if (!factory) return false;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(
        inPath.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return false;

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        SafeRelease(decoder);
        return false;
    }

    UINT origW = 0, origH = 0;
    frame->GetSize(&origW, &origH);

    IWICBitmapSource* currentSource = frame;
    currentSource->AddRef();

    // 1. Crop
    if (cropRect) {
        int x = std::clamp((int)cropRect->left, 0, (int)origW - 1);
        int y = std::clamp((int)cropRect->top, 0, (int)origH - 1);
        int w = std::clamp((int)(cropRect->right - cropRect->left), 1, (int)origW - x);
        int h = std::clamp((int)(cropRect->bottom - cropRect->top), 1, (int)origH - y);

        WICRect rc = { x, y, w, h };
        IWICBitmapClipper* clipper = nullptr;
        if (SUCCEEDED(factory->CreateBitmapClipper(&clipper))) {
            if (SUCCEEDED(clipper->Initialize(currentSource, &rc))) {
                SafeRelease(currentSource);
                currentSource = clipper;
                currentSource->AddRef();
            }
            SafeRelease(clipper);
        }
    }

    // 2. Rotate & Flip
    WICBitmapTransformOptions transform = WICBitmapTransformRotate0;
    int normRot = (rotation % 360 + 360) % 360;
    if (normRot == 90) transform = WICBitmapTransformRotate90;
    else if (normRot == 180) transform = WICBitmapTransformRotate180;
    else if (normRot == 270) transform = WICBitmapTransformRotate270;

    if (flipH) {
        transform = (WICBitmapTransformOptions)(transform | WICBitmapTransformFlipHorizontal);
    }
    if (flipV) {
        transform = (WICBitmapTransformOptions)(transform | WICBitmapTransformFlipVertical);
    }

    if (transform != WICBitmapTransformRotate0) {
        IWICBitmapFlipRotator* rotator = nullptr;
        if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rotator))) {
            if (SUCCEEDED(rotator->Initialize(currentSource, transform))) {
                SafeRelease(currentSource);
                currentSource = rotator;
                currentSource->AddRef();
            }
            SafeRelease(rotator);
        }
    }

    // Determine target container format by extension
    GUID containerGUID = GUID_ContainerFormatPng;
    std::wstring ext = outPath.substr(outPath.find_last_of(L".") + 1);
    for (auto& c : ext) c = towlower(c);

    bool isJpeg = false;
    if (ext == L"jpg" || ext == L"jpeg") {
        containerGUID = GUID_ContainerFormatJpeg;
        isJpeg = true;
    } else if (ext == L"bmp") {
        containerGUID = GUID_ContainerFormatBmp;
    } else if (ext == L"tif" || ext == L"tiff") {
        containerGUID = GUID_ContainerFormatTiff;
    }

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(outPath.c_str(), GENERIC_WRITE);
    }

    IWICBitmapEncoder* encoder = nullptr;
    if (SUCCEEDED(hr)) {
        hr = factory->CreateEncoder(containerGUID, NULL, &encoder);
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    }

    IWICBitmapFrameEncode* frameEncode = nullptr;
    IPropertyBag2* propBag = nullptr;
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frameEncode, &propBag);
    }

    if (SUCCEEDED(hr) && isJpeg && propBag) {
        PROPBAG2 opt = {};
        opt.pstrName = (LPOLESTR)L"ImageQuality";
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_R4;
        var.fltVal = qualityJpeg;
        propBag->Write(1, &opt, &var);
    }

    if (SUCCEEDED(hr)) {
        hr = frameEncode->Initialize(propBag);
    }
    if (SUCCEEDED(hr)) {
        hr = frameEncode->WriteSource(currentSource, NULL);
    }
    if (SUCCEEDED(hr)) {
        hr = frameEncode->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }

    SafeRelease(propBag);
    SafeRelease(frameEncode);
    SafeRelease(encoder);
    SafeRelease(stream);
    SafeRelease(currentSource);
    SafeRelease(frame);
    SafeRelease(decoder);

    return SUCCEEDED(hr);
}

bool WicDecoder::LoadShellThumbnail(
    IWICImagingFactory* factory,
    ID2D1RenderTarget* renderTarget,
    const std::wstring& filePath,
    UINT targetDimension,
    ID2D1Bitmap** outBitmap
) {
    if (!factory || !renderTarget || !outBitmap) return false;
    *outBitmap = nullptr;

    IShellItem* pItem = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(filePath.c_str(), NULL, IID_PPV_ARGS(&pItem));
    if (FAILED(hr) || !pItem) return false;

    IShellItemImageFactory* pFactory = nullptr;
    hr = pItem->QueryInterface(IID_PPV_ARGS(&pFactory));
    if (FAILED(hr) || !pFactory) {
        pItem->Release();
        return false;
    }

    SIZE sz = { (LONG)targetDimension, (LONG)targetDimension };
    HBITMAP hBmp = nullptr;
    hr = pFactory->GetImage(sz, SIIGBF_BIGGERSIZEOK | SIIGBF_SCALEUP, &hBmp);
    if (FAILED(hr) || !hBmp) {
        hr = pFactory->GetImage(sz, SIIGBF_RESIZETOFIT, &hBmp);
    }
    if (FAILED(hr) || !hBmp) {
        hr = pFactory->GetImage(sz, SIIGBF_THUMBNAILONLY, &hBmp);
    }

    pFactory->Release();
    pItem->Release();

    if (FAILED(hr) || !hBmp) return false;

    IWICBitmap* wicBmp = nullptr;
    hr = factory->CreateBitmapFromHBITMAP(hBmp, NULL, WICBitmapUseAlpha, &wicBmp);
    DeleteObject(hBmp);

    if (FAILED(hr) || !wicBmp) return false;

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(
            wicBmp,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            NULL,
            0.0f,
            WICBitmapPaletteTypeCustom
        );
        if (SUCCEEDED(hr)) {
            hr = renderTarget->CreateBitmapFromWicBitmap(converter, NULL, outBitmap);
        }
        converter->Release();
    }
    wicBmp->Release();

    return SUCCEEDED(hr) && (*outBitmap != nullptr);
}

bool WicDecoder::LoadImageFromMemory(
    IWICImagingFactory* factory,
    ID2D1RenderTarget* renderTarget,
    const void* data,
    DWORD size,
    ID2D1Bitmap** outBitmap
) {
    if (!factory || !renderTarget || !data || size == 0 || !outBitmap) return false;
    *outBitmap = nullptr;

    IWICStream* stream = nullptr;
    if (FAILED(factory->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromMemory((BYTE*)data, size))) {
        stream->Release();
        return false;
    }

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(factory->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder))) {
        stream->Release();
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        decoder->Release();
        stream->Release();
        return false;
    }

    IWICFormatConverter* converter = nullptr;
    if (FAILED(factory->CreateFormatConverter(&converter))) {
        frame->Release();
        decoder->Release();
        stream->Release();
        return false;
    }

    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
        converter->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        return false;
    }

    HRESULT hr = renderTarget->CreateBitmapFromWicBitmap(converter, NULL, outBitmap);
    converter->Release();
    frame->Release();
    decoder->Release();
    stream->Release();

    return SUCCEEDED(hr) && (*outBitmap != nullptr);
}

} // namespace WinPreview
