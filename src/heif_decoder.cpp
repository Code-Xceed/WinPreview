#include "heif_decoder.h"
#include <windows.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace WinPreview {

struct heif_error {
    int code;
    int subcode;
    const char* message;
};

struct heif_context;
struct heif_image_handle;
struct heif_image;

enum heif_colorspace {
    heif_colorspace_undefined = 99,
    heif_colorspace_YCbCr = 0,
    heif_colorspace_RGB = 1,
    heif_colorspace_monochrome = 2
};

enum heif_chroma {
    heif_chroma_undefined = 99,
    heif_chroma_monochrome = 0,
    heif_chroma_420 = 1,
    heif_chroma_422 = 2,
    heif_chroma_444 = 3,
    heif_chroma_interleaved_RGB = 10,
    heif_chroma_interleaved_RGBA = 11,
    heif_chroma_interleaved_RRGGBB_BE = 12,
    heif_chroma_interleaved_RRGGBBAA_BE = 14
};

enum heif_channel {
    heif_channel_Y = 0,
    heif_channel_Cb = 1,
    heif_channel_Cr = 2,
    heif_channel_R = 3,
    heif_channel_G = 4,
    heif_channel_B = 5,
    heif_channel_Alpha = 6,
    heif_channel_interleaved = 10
};

typedef heif_context* (*pfn_heif_context_alloc)();
typedef void (*pfn_heif_context_free)(heif_context*);
typedef heif_error (*pfn_heif_context_read_from_file)(heif_context*, const char*, void*);
typedef heif_error (*pfn_heif_context_get_primary_image_handle)(heif_context*, heif_image_handle**);
typedef int (*pfn_heif_image_handle_get_width)(heif_image_handle*);
typedef int (*pfn_heif_image_handle_get_height)(heif_image_handle*);
typedef void (*pfn_heif_image_handle_release)(heif_image_handle*);
typedef heif_error (*pfn_heif_decode_image)(heif_image_handle*, heif_image**, heif_colorspace, heif_chroma, void*);
typedef const uint8_t* (*pfn_heif_image_get_plane_readonly)(heif_image*, heif_channel, int* out_stride);
typedef void (*pfn_heif_image_release)(heif_image*);
typedef int (*pfn_heif_image_handle_get_number_of_metadata_blocks)(heif_image_handle*, const char* type);
typedef int (*pfn_heif_image_handle_get_list_of_metadata_block_IDs)(heif_image_handle*, const char* type, uint32_t* token_array, int count);
typedef size_t (*pfn_heif_image_handle_get_metadata_size)(heif_image_handle*, uint32_t token);
typedef heif_error (*pfn_heif_image_handle_get_metadata)(heif_image_handle*, uint32_t token, void* out_data);

static HMODULE s_hHeif = nullptr;
static pfn_heif_context_alloc fn_heif_context_alloc = nullptr;
static pfn_heif_context_free fn_heif_context_free = nullptr;
static pfn_heif_context_read_from_file fn_heif_context_read_from_file = nullptr;
static pfn_heif_context_get_primary_image_handle fn_heif_context_get_primary_image_handle = nullptr;
static pfn_heif_image_handle_get_width fn_heif_image_handle_get_width = nullptr;
static pfn_heif_image_handle_get_height fn_heif_image_handle_get_height = nullptr;
static pfn_heif_image_handle_release fn_heif_image_handle_release = nullptr;
static pfn_heif_decode_image fn_heif_decode_image = nullptr;
static pfn_heif_image_get_plane_readonly fn_heif_image_get_plane_readonly = nullptr;
static pfn_heif_image_release fn_heif_image_release = nullptr;
static pfn_heif_image_handle_get_number_of_metadata_blocks fn_heif_image_handle_get_number_of_metadata_blocks = nullptr;
static pfn_heif_image_handle_get_list_of_metadata_block_IDs fn_heif_image_handle_get_list_of_metadata_block_IDs = nullptr;
static pfn_heif_image_handle_get_metadata_size fn_heif_image_handle_get_metadata_size = nullptr;
static pfn_heif_image_handle_get_metadata fn_heif_image_handle_get_metadata = nullptr;

static bool InitializeHeif() {
    if (s_hHeif) return true;

    // 1. Discover executable directory
    WCHAR exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) exeDir = exeDir.substr(0, lastSlash);

    std::vector<std::wstring> candidateDirs = {
        exeDir,
        exeDir + L"\\lib",
        exeDir + L"\\dist\\WinPreview"
    };

    // 2. Discover python site-packages (for local developer machine fallback)
    WCHAR appDataBuf[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appDataBuf, MAX_PATH) > 0) {
        candidateDirs.push_back(std::wstring(appDataBuf) + L"\\Python\\Python314\\site-packages");
        candidateDirs.push_back(std::wstring(appDataBuf) + L"\\Python\\Python313\\site-packages");
        candidateDirs.push_back(std::wstring(appDataBuf) + L"\\Python\\Python312\\site-packages");
    }

    std::wstring foundDll;
    for (const auto& dir : candidateDirs) {
        WIN32_FIND_DATAW fd;
        std::wstring searchPattern = dir + L"\\*heif*.dll";
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring name = fd.cFileName;
                    if (name.rfind(L"libheif", 0) == 0) {
                        foundDll = dir + L"\\" + name;
                        SetDllDirectoryW(dir.c_str());
                        break;
                    }
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
            if (!foundDll.empty()) break;
        }
    }

    if (foundDll.empty()) {
        // Try system search
        foundDll = L"libheif.dll";
    }

    s_hHeif = LoadLibraryW(foundDll.c_str());
    SetDllDirectoryW(NULL);

    if (!s_hHeif) return false;

    fn_heif_context_alloc = (pfn_heif_context_alloc)GetProcAddress(s_hHeif, "heif_context_alloc");
    fn_heif_context_free = (pfn_heif_context_free)GetProcAddress(s_hHeif, "heif_context_free");
    fn_heif_context_read_from_file = (pfn_heif_context_read_from_file)GetProcAddress(s_hHeif, "heif_context_read_from_file");
    fn_heif_context_get_primary_image_handle = (pfn_heif_context_get_primary_image_handle)GetProcAddress(s_hHeif, "heif_context_get_primary_image_handle");
    fn_heif_image_handle_get_width = (pfn_heif_image_handle_get_width)GetProcAddress(s_hHeif, "heif_image_handle_get_width");
    fn_heif_image_handle_get_height = (pfn_heif_image_handle_get_height)GetProcAddress(s_hHeif, "heif_image_handle_get_height");
    fn_heif_image_handle_release = (pfn_heif_image_handle_release)GetProcAddress(s_hHeif, "heif_image_handle_release");
    fn_heif_decode_image = (pfn_heif_decode_image)GetProcAddress(s_hHeif, "heif_decode_image");
    fn_heif_image_get_plane_readonly = (pfn_heif_image_get_plane_readonly)GetProcAddress(s_hHeif, "heif_image_get_plane_readonly");
    fn_heif_image_release = (pfn_heif_image_release)GetProcAddress(s_hHeif, "heif_image_release");
    fn_heif_image_handle_get_number_of_metadata_blocks = (pfn_heif_image_handle_get_number_of_metadata_blocks)GetProcAddress(s_hHeif, "heif_image_handle_get_number_of_metadata_blocks");
    fn_heif_image_handle_get_list_of_metadata_block_IDs = (pfn_heif_image_handle_get_list_of_metadata_block_IDs)GetProcAddress(s_hHeif, "heif_image_handle_get_list_of_metadata_block_IDs");
    fn_heif_image_handle_get_metadata_size = (pfn_heif_image_handle_get_metadata_size)GetProcAddress(s_hHeif, "heif_image_handle_get_metadata_size");
    fn_heif_image_handle_get_metadata = (pfn_heif_image_handle_get_metadata)GetProcAddress(s_hHeif, "heif_image_handle_get_metadata");

    if (!fn_heif_context_alloc || !fn_heif_context_read_from_file || !fn_heif_decode_image) {
        FreeLibrary(s_hHeif);
        s_hHeif = nullptr;
        return false;
    }

    return true;
}

bool HeifDecoder::IsAvailable() {
    return InitializeHeif();
}

static void ParseExifBuffer(const uint8_t* data, size_t size, ImageMetadata* outMeta) {
    if (!data || size < 16 || !outMeta) return;

    // HEIF Exif metadata usually has a 4-byte offset or starts directly with "Exif\0\0"
    size_t offset = 0;
    if (size >= 4) {
        uint32_t skip = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | (uint32_t)data[3];
        if (skip < size - 8) {
            offset = 4 + skip;
        }
    }

    if (offset + 6 <= size && memcmp(data + offset, "Exif\0\0", 6) == 0) {
        offset += 6;
    } else if (size >= 6 && memcmp(data, "Exif\0\0", 6) == 0) {
        offset = 6;
    }

    if (offset + 8 > size) return;

    const uint8_t* tiff = data + offset;
    size_t tiffSize = size - offset;

    bool littleEndian = false;
    if (tiff[0] == 'I' && tiff[1] == 'I') littleEndian = true;
    else if (tiff[0] == 'M' && tiff[1] == 'M') littleEndian = false;
    else return;

    auto read16 = [littleEndian](const uint8_t* p) -> uint16_t {
        if (littleEndian) return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
    };
    auto read32 = [littleEndian](const uint8_t* p) -> uint32_t {
        if (littleEndian) return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    };

    uint16_t magic = read16(tiff + 2);
    if (magic != 42) return;

    uint32_t ifd0Offset = read32(tiff + 4);
    if (ifd0Offset >= tiffSize - 2) return;

    auto parseIfd = [&](uint32_t dirOffset, auto& self) -> void {
        if (dirOffset >= tiffSize - 2) return;
        uint16_t numEntries = read16(tiff + dirOffset);
        uint32_t cur = dirOffset + 2;

        for (uint16_t i = 0; i < numEntries; ++i) {
            if (cur + 12 > tiffSize) break;
            uint16_t tag = read16(tiff + cur);
            uint16_t type = read16(tiff + cur + 2);
            uint32_t count = read32(tiff + cur + 4);
            const uint8_t* valPtr = tiff + cur + 8;
            if (count > 4 || (type == 5 && count > 0)) {
                uint32_t valOffset = read32(tiff + cur + 8);
                if (valOffset < tiffSize) valPtr = tiff + valOffset;
            }

            auto readString = [&]() -> std::wstring {
                if (!valPtr || valPtr >= tiff + tiffSize) return L"";
                size_t maxLen = (tiff + tiffSize) - valPtr;
                size_t len = (count < maxLen) ? count : maxLen;
                std::string s((const char*)valPtr, len);
                while (!s.empty() && (s.back() == '\0' || s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
                if (s.empty()) return L"";
                int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
                if (wlen > 0) {
                    std::wstring ws(wlen, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], wlen);
                    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
                    return ws;
                }
                return L"";
            };

            auto readRational = [&]() -> double {
                if (!valPtr || valPtr + 8 > tiff + tiffSize) return 0.0;
                uint32_t num = read32(valPtr);
                uint32_t den = read32(valPtr + 4);
                if (den == 0) return 0.0;
                return (double)num / (double)den;
            };

            switch (tag) {
                case 0x010F: outMeta->cameraMake = readString(); break;
                case 0x0110: outMeta->cameraModel = readString(); break;
                case 0x0132: if (outMeta->dateTaken.empty()) outMeta->dateTaken = readString(); break;
                case 0x9003: outMeta->dateTaken = readString(); break;
                case 0x829A: {
                    double t = readRational();
                    if (t > 0.0) {
                        if (t < 1.0) {
                            wchar_t buf[32];
                            swprintf_s(buf, L"1/%d", (int)(1.0 / t + 0.5));
                            outMeta->exposureTime = buf;
                        } else {
                            wchar_t buf[32];
                            swprintf_s(buf, L"%.1f s", t);
                            outMeta->exposureTime = buf;
                        }
                    }
                    break;
                }
                case 0x829D: {
                    double f = readRational();
                    if (f > 0.0) {
                        wchar_t buf[32];
                        swprintf_s(buf, L"f/%.1f", f);
                        outMeta->fNumber = buf;
                    }
                    break;
                }
                case 0x8827: {
                    uint16_t iso = read16(valPtr);
                    if (iso > 0) outMeta->isoSpeed = std::to_wstring(iso);
                    break;
                }
                case 0x920A: {
                    double fl = readRational();
                    if (fl > 0.0) {
                        wchar_t buf[32];
                        swprintf_s(buf, L"%.1f mm", fl);
                        outMeta->focalLength = buf;
                    }
                    break;
                }
                case 0x8769: { // SubIFD (Exif Offset)
                    uint32_t subOffset = read32(tiff + cur + 8);
                    if (subOffset < tiffSize) self(subOffset, self);
                    break;
                }
                default: break;
            }
            cur += 12;
        }
    };

    parseIfd(ifd0Offset, parseIfd);
}

bool HeifDecoder::LoadImageFromFile(
    ID2D1RenderTarget* renderTarget,
    const std::wstring& filePath,
    ID2D1Bitmap** outBitmap,
    UINT* outWidth,
    UINT* outHeight,
    ImageMetadata* outMeta
) {
    if (!renderTarget || !outBitmap) return false;
    *outBitmap = nullptr;
    if (outWidth) *outWidth = 0;
    if (outHeight) *outHeight = 0;
    if (outMeta) *outMeta = ImageMetadata();

    if (!InitializeHeif()) return false;

    // Convert path to UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return false;
    std::string utf8Path(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, &utf8Path[0], utf8Len, NULL, NULL);

    heif_context* ctx = fn_heif_context_alloc();
    if (!ctx) return false;

    heif_error err = fn_heif_context_read_from_file(ctx, utf8Path.c_str(), nullptr);
    if (err.code != 0) {
        fn_heif_context_free(ctx);
        return false;
    }

    heif_image_handle* handle = nullptr;
    err = fn_heif_context_get_primary_image_handle(ctx, &handle);
    if (err.code != 0 || !handle) {
        fn_heif_context_free(ctx);
        return false;
    }

    int width = fn_heif_image_handle_get_width(handle);
    int height = fn_heif_image_handle_get_height(handle);

    // Extract EXIF Metadata if requested
    if (outMeta && fn_heif_image_handle_get_number_of_metadata_blocks) {
        int nBlocks = fn_heif_image_handle_get_number_of_metadata_blocks(handle, "Exif");
        if (nBlocks > 0) {
            std::vector<uint32_t> blockIds(nBlocks);
            fn_heif_image_handle_get_list_of_metadata_block_IDs(handle, "Exif", blockIds.data(), nBlocks);
            size_t metaSize = fn_heif_image_handle_get_metadata_size(handle, blockIds[0]);
            if (metaSize > 0 && metaSize < 50 * 1024 * 1024) {
                std::vector<uint8_t> metaBuf(metaSize);
                if (fn_heif_image_handle_get_metadata(handle, blockIds[0], metaBuf.data()).code == 0) {
                    ParseExifBuffer(metaBuf.data(), metaSize, outMeta);
                }
            }
        }
    }

    heif_image* img = nullptr;
    // Decode directly into 32-bit interleaved RGBA
    err = fn_heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
    fn_heif_image_handle_release(handle);
    fn_heif_context_free(ctx);

    if (err.code != 0 || !img) return false;

    int stride = 0;
    const uint8_t* plane = fn_heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
    if (!plane || stride <= 0) {
        fn_heif_image_release(img);
        return false;
    }

    // Convert RGBA to PBGRA for native Direct2D hardware surface
    const UINT origW = (UINT)width;
    const UINT origH = (UINT)height;
    
    // Check if large downscale is beneficial (e.g. > 16 MP)
    constexpr float kMaxDecodePixels = 16.0f * 1024.0f * 1024.0f;
    UINT targetW = origW;
    UINT targetH = origH;
    int step = 1;
    if ((float)origW * (float)origH > kMaxDecodePixels) {
        step = 2;
        targetW = (origW + 1) / 2;
        targetH = (origH + 1) / 2;
    }

    const UINT dstStride = targetW * 4;
    std::vector<BYTE> bgraBuffer((size_t)dstStride * targetH);
    BYTE* dst = bgraBuffer.data();

    if (step == 1) {
        for (UINT y = 0; y < targetH; ++y) {
            const uint8_t* sRow = plane + y * stride;
            BYTE* dRow = dst + y * dstStride;
            for (UINT x = 0; x < targetW; ++x) {
                BYTE r = sRow[x * 4 + 0];
                BYTE g = sRow[x * 4 + 1];
                BYTE b = sRow[x * 4 + 2];
                BYTE a = sRow[x * 4 + 3];
                // Premultiply alpha if needed
                if (a < 255) {
                    dRow[x * 4 + 0] = (BYTE)((b * a + 127) / 255);
                    dRow[x * 4 + 1] = (BYTE)((g * a + 127) / 255);
                    dRow[x * 4 + 2] = (BYTE)((r * a + 127) / 255);
                    dRow[x * 4 + 3] = a;
                } else {
                    dRow[x * 4 + 0] = b;
                    dRow[x * 4 + 1] = g;
                    dRow[x * 4 + 2] = r;
                    dRow[x * 4 + 3] = 255;
                }
            }
        }
    } else {
        // Simple 2x2 box downsampling
        for (UINT y = 0; y < targetH; ++y) {
            UINT srcY = y * 2;
            const uint8_t* sRow0 = plane + srcY * stride;
            const uint8_t* sRow1 = (srcY + 1 < origH) ? (plane + (srcY + 1) * stride) : sRow0;
            BYTE* dRow = dst + y * dstStride;

            for (UINT x = 0; x < targetW; ++x) {
                UINT srcX = x * 2;
                UINT r = sRow0[srcX * 4 + 0] + sRow1[srcX * 4 + 0];
                UINT g = sRow0[srcX * 4 + 1] + sRow1[srcX * 4 + 1];
                UINT b = sRow0[srcX * 4 + 2] + sRow1[srcX * 4 + 2];
                UINT a = sRow0[srcX * 4 + 3] + sRow1[srcX * 4 + 3];

                if (srcX + 1 < origW) {
                    r += sRow0[(srcX + 1) * 4 + 0] + sRow1[(srcX + 1) * 4 + 0];
                    g += sRow0[(srcX + 1) * 4 + 1] + sRow1[(srcX + 1) * 4 + 1];
                    b += sRow0[(srcX + 1) * 4 + 2] + sRow1[(srcX + 1) * 4 + 2];
                    a += sRow0[(srcX + 1) * 4 + 3] + sRow1[(srcX + 1) * 4 + 3];
                    r >>= 2; g >>= 2; b >>= 2; a >>= 2;
                } else {
                    r >>= 1; g >>= 1; b >>= 1; a >>= 1;
                }

                dRow[x * 4 + 0] = (BYTE)b;
                dRow[x * 4 + 1] = (BYTE)g;
                dRow[x * 4 + 2] = (BYTE)r;
                dRow[x * 4 + 3] = (BYTE)a;
            }
        }
    }

    fn_heif_image_release(img);

    D2D1_BITMAP_PROPERTIES props;
    props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;

    HRESULT hr = renderTarget->CreateBitmap(
        D2D1::SizeU(targetW, targetH),
        dst,
        dstStride,
        &props,
        outBitmap
    );

    if (SUCCEEDED(hr) && *outBitmap) {
        if (outWidth) *outWidth = origW;
        if (outHeight) *outHeight = origH;
        return true;
    }

    return false;
}

bool HeifDecoder::GetMetadata(const std::wstring& filePath, ImageMetadata* outMeta) {
    if (!outMeta || !InitializeHeif()) return false;

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return false;
    std::string utf8Path(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, &utf8Path[0], utf8Len, NULL, NULL);

    heif_context* ctx = fn_heif_context_alloc();
    if (!ctx) return false;

    if (fn_heif_context_read_from_file(ctx, utf8Path.c_str(), nullptr).code != 0) {
        fn_heif_context_free(ctx);
        return false;
    }

    heif_image_handle* handle = nullptr;
    if (fn_heif_context_get_primary_image_handle(ctx, &handle).code != 0 || !handle) {
        fn_heif_context_free(ctx);
        return false;
    }

    if (fn_heif_image_handle_get_number_of_metadata_blocks) {
        int nBlocks = fn_heif_image_handle_get_number_of_metadata_blocks(handle, "Exif");
        if (nBlocks > 0) {
            std::vector<uint32_t> blockIds(nBlocks);
            fn_heif_image_handle_get_list_of_metadata_block_IDs(handle, "Exif", blockIds.data(), nBlocks);
            size_t metaSize = fn_heif_image_handle_get_metadata_size(handle, blockIds[0]);
            if (metaSize > 0 && metaSize < 50 * 1024 * 1024) {
                std::vector<uint8_t> metaBuf(metaSize);
                if (fn_heif_image_handle_get_metadata(handle, blockIds[0], metaBuf.data()).code == 0) {
                    ParseExifBuffer(metaBuf.data(), metaSize, outMeta);
                }
            }
        }
    }

    fn_heif_image_handle_release(handle);
    fn_heif_context_free(ctx);
    return true;
}

} // namespace WinPreview
