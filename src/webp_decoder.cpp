#include "webp_decoder.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "webp/src/webp/decode.h"

namespace WinPreview {

namespace {

const size_t kMaxWebpBytes = 256u * 1024u * 1024u;

bool ReadWhole(const std::wstring& path, std::vector<uint8_t>& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        (unsigned long long)size.QuadPart > kMaxWebpBytes) {
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

bool StartsWithWebp(const std::vector<uint8_t>& data) {
    return data.size() >= 12 && memcmp(data.data(), "RIFF", 4) == 0 &&
           memcmp(data.data() + 8, "WEBP", 4) == 0;
}

} // namespace

bool WebpDecoder::IsWebpFile(const std::wstring& path) {
    std::vector<uint8_t> data;
    if (!ReadWhole(path, data)) return false;
    return StartsWithWebp(data);
}

bool WebpDecoder::Decode(const std::wstring& path, ID2D1RenderTarget* renderTarget,
                         ID2D1Bitmap** outBitmap, UINT* outWidth, UINT* outHeight) {
    if (!renderTarget || !outBitmap) return false;
    *outBitmap = nullptr;
    if (outWidth) *outWidth = 0;
    if (outHeight) *outHeight = 0;

    std::vector<uint8_t> data;
    if (!ReadWhole(path, data) || !StartsWithWebp(data)) return false;

    int width = 0;
    int height = 0;
    if (!WebPGetInfo(data.data(), data.size(), &width, &height) || width <= 0 || height <= 0 ||
        width > 32768 || height > 32768) {
        return false;
    }

    std::vector<uint8_t> pixels((size_t)width * (size_t)height * 4);
    if (!WebPDecodeBGRAInto(data.data(), data.size(), pixels.data(),
                            pixels.size(), (size_t)width * 4)) {
        return false;
    }

    // Direct2D wants premultiplied alpha; libwebp returns straight BGRA.
    uint8_t* p = pixels.data();
    const size_t count = (size_t)width * (size_t)height;
    for (size_t i = 0; i < count; ++i, p += 4) {
        const uint32_t a = p[3];
        if (a == 255) continue;
        if (a == 0) {
            p[0] = p[1] = p[2] = 0;
            continue;
        }
        p[0] = (uint8_t)((p[0] * a + 127) / 255);
        p[1] = (uint8_t)((p[1] * a + 127) / 255);
        p[2] = (uint8_t)((p[2] * a + 127) / 255);
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
