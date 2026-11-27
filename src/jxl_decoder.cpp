#include "jxl_decoder.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "jxl/decode.h"

namespace WinPreview {

namespace {

const size_t kMaxJxlBytes = 512u * 1024u * 1024u;
const uint64_t kMaxPixels = 268435456ull;  // 256 MP

struct JxlApi {
    HMODULE module = nullptr;
    JxlDecoder* (__cdecl* create)(const JxlMemoryManager*) = nullptr;
    void (__cdecl* destroy)(JxlDecoder*) = nullptr;
    JxlDecoderStatus (__cdecl* subscribe)(JxlDecoder*, int) = nullptr;
    JxlDecoderStatus (__cdecl* setInput)(JxlDecoder*, const uint8_t*, size_t) = nullptr;
    JxlDecoderStatus (__cdecl* closeInput)(JxlDecoder*) = nullptr;
    JxlDecoderStatus (__cdecl* process)(JxlDecoder*) = nullptr;
    JxlDecoderStatus (__cdecl* basicInfo)(const JxlDecoder*, JxlBasicInfo*) = nullptr;
    JxlDecoderStatus (__cdecl* bufferSize)(JxlDecoder*, const JxlPixelFormat*, size_t*) = nullptr;
    JxlDecoderStatus (__cdecl* setOut)(JxlDecoder*, const JxlPixelFormat*, void*, size_t) = nullptr;
    JxlDecoderStatus (__cdecl* releaseInput)(JxlDecoder*, size_t*) = nullptr;
    void (__cdecl* srgb)(JxlColorEncoding*, JXL_BOOL) = nullptr;
    JxlDecoderStatus (__cdecl* preferred)(JxlDecoder*, const JxlColorEncoding*) = nullptr;
    uint32_t (__cdecl* version)() = nullptr;

    bool ready() const {
        return module && create && destroy && subscribe && setInput && process &&
               basicInfo && bufferSize && setOut;
    }
};

JxlApi& Api() {
    static JxlApi api;
    static bool attempted = false;
    if (!attempted) {
        attempted = true;
        // Load from the application directory first (LoadLibraryW does), so the
        // vendored DLL wins over any system copy.
        api.module = LoadLibraryW(L"jxl.dll");
        if (api.module) {
            api.create = (decltype(api.create))GetProcAddress(api.module, "JxlDecoderCreate");
            api.destroy = (decltype(api.destroy))GetProcAddress(api.module, "JxlDecoderDestroy");
            api.subscribe = (decltype(api.subscribe))GetProcAddress(api.module, "JxlDecoderSubscribeEvents");
            api.setInput = (decltype(api.setInput))GetProcAddress(api.module, "JxlDecoderSetInput");
            api.closeInput = (decltype(api.closeInput))GetProcAddress(api.module, "JxlDecoderCloseInput");
            api.process = (decltype(api.process))GetProcAddress(api.module, "JxlDecoderProcessInput");
            api.basicInfo = (decltype(api.basicInfo))GetProcAddress(api.module, "JxlDecoderGetBasicInfo");
            api.bufferSize = (decltype(api.bufferSize))GetProcAddress(api.module, "JxlDecoderImageOutBufferSize");
            api.setOut = (decltype(api.setOut))GetProcAddress(api.module, "JxlDecoderSetImageOutBuffer");
            api.releaseInput = (decltype(api.releaseInput))GetProcAddress(api.module, "JxlDecoderReleaseInput");
            api.srgb = (decltype(api.srgb))GetProcAddress(api.module, "JxlColorEncodingSetToSRGB");
            api.preferred = (decltype(api.preferred))GetProcAddress(api.module, "JxlDecoderSetPreferredColorProfile");
            api.version = (decltype(api.version))GetProcAddress(api.module, "JxlDecoderVersion");
        }
    }
    return api;
}

bool ReadWhole(const std::wstring& path, std::vector<uint8_t>& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        (unsigned long long)size.QuadPart > kMaxJxlBytes) {
        CloseHandle(file);
        return false;
    }
    data.resize((size_t)size.QuadPart);
    DWORD read = 0;
    bool ok = ReadFile(file, data.data(), (DWORD)data.size(), &read, nullptr) != FALSE &&
              read == data.size();
    CloseHandle(file);
    return ok;
}

// JXL codestream signature: 0xFF 0x0A, or an ISOBMFF box starting with the
// JXL container signature.
bool StartsWithJxl(const std::vector<uint8_t>& data) {
    if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0x0A) return true;
    if (data.size() >= 12) {
        static const uint8_t kContainer[12] = {0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ',
                                               0x0D, 0x0A, 0x87, 0x0A};
        if (memcmp(data.data(), kContainer, 12) == 0) return true;
    }
    return false;
}

} // namespace

bool JxlDecoder::IsAvailable() {
    return Api().ready();
}

unsigned int JxlDecoder::RuntimeVersion() {
    return Api().version ? Api().version() : 0;
}

bool JxlDecoder::IsJxlFile(const std::wstring& path) {
    std::vector<uint8_t> data;
    if (!ReadWhole(path, data)) return false;
    return StartsWithJxl(data);
}

bool JxlDecoder::Decode(const std::wstring& path, ID2D1RenderTarget* renderTarget,
                        ID2D1Bitmap** outBitmap, UINT* outWidth, UINT* outHeight) {
    if (!renderTarget || !outBitmap) return false;
    *outBitmap = nullptr;
    if (outWidth) *outWidth = 0;
    if (outHeight) *outHeight = 0;

    JxlApi& api = Api();
    if (!api.ready()) return false;

    std::vector<uint8_t> data;
    if (!ReadWhole(path, data) || !StartsWithJxl(data)) return false;

    JxlDecoder* decoder = api.create(nullptr);
    if (!decoder) return false;

    api.subscribe(decoder, JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE);
    api.setInput(decoder, data.data(), data.size());
    if (api.closeInput) api.closeInput(decoder);

    JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    JxlBasicInfo info = {};
    std::vector<uint8_t> pixels;
    bool complete = false;

    for (int guard = 0; guard < 1000; ++guard) {
        JxlDecoderStatus status = api.process(decoder);
        if (status == JXL_DEC_BASIC_INFO) {
            if (api.basicInfo(decoder, &info) != JXL_DEC_SUCCESS) break;
            if (info.xsize == 0 || info.ysize == 0 ||
                (uint64_t)info.xsize * (uint64_t)info.ysize > kMaxPixels) {
                break;
            }
        } else if (status == JXL_DEC_COLOR_ENCODING) {
            // Preview in sRGB so the picture matches the rest of the app even
            // when the file carries a wide-gamut profile.
            if (api.srgb && api.preferred) {
                JxlColorEncoding encoding = {};
                api.srgb(&encoding, JXL_FALSE);
                api.preferred(decoder, &encoding);
            }
            size_t needed = 0;
            if (api.bufferSize(decoder, &format, &needed) != JXL_DEC_SUCCESS) break;
            if (needed > kMaxPixels * 4) break;
            pixels.resize(needed);
            if (api.setOut(decoder, &format, pixels.data(), pixels.size()) != JXL_DEC_SUCCESS) break;
        } else if (status == JXL_DEC_FULL_IMAGE) {
            complete = true;
            break;
        } else if (status == JXL_DEC_SUCCESS) {
            complete = true;
            break;
        } else if (status == JXL_DEC_NEED_MORE_INPUT || status == JXL_DEC_ERROR) {
            break;
        }
    }

    api.destroy(decoder);

    if (!complete || pixels.empty() || info.xsize == 0 || info.ysize == 0) return false;

    const int width = (int)info.xsize;
    const int height = (int)info.ysize;
    const size_t pixelCount = (size_t)width * (size_t)height;
    if (pixels.size() < pixelCount * 4) return false;

    // libjxl returns RGBA for uint8; Direct2D wants premultiplied BGRA.
    uint8_t* p = pixels.data();
    for (size_t i = 0; i < pixelCount; ++i, p += 4) {
        uint8_t r = p[0];
        uint8_t g = p[1];
        uint8_t b = p[2];
        uint8_t a = p[3];
        if (a != 255) {
            r = (uint8_t)((r * a + 127) / 255);
            g = (uint8_t)((g * a + 127) / 255);
            b = (uint8_t)((b * a + 127) / 255);
        }
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = a;
    }

    D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    HRESULT hr = renderTarget->CreateBitmap(
        D2D1::SizeU((UINT32)width, (UINT32)height), pixels.data(),
        (UINT32)width * 4, properties, outBitmap);
    if (FAILED(hr) || !*outBitmap) {
        *outBitmap = nullptr;
        return false;
    }
    if (outWidth) *outWidth = (UINT)width;
    if (outHeight) *outHeight = (UINT)height;
    return true;
}

} // namespace WinPreview
