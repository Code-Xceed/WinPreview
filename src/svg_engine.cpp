#include "svg_engine.h"
#include <shlwapi.h>
#include <fstream>
#include <sstream>

namespace WinPreview {

SvgEngine::SvgEngine() : m_svgDocument(nullptr) {}

SvgEngine::~SvgEngine() {
    Close();
}

void SvgEngine::Close() {
    if (m_svgDocument) {
        m_svgDocument->Release();
        m_svgDocument = nullptr;
    }
    m_originalSize = { 800.0f, 600.0f };
    m_filePath.clear();
    m_fileSizeBytes = 0;
}

bool SvgEngine::LoadFromFile(ID2D1RenderTarget* rt, const std::wstring& filePath) {
    Close();
    if (!rt) return false;

    ID2D1DeviceContext5* dc5 = nullptr;
    HRESULT hr = rt->QueryInterface(IID_ID2D1DeviceContext5, (void**)&dc5);
    if (FAILED(hr) || !dc5) {
        return false;
    }

    IStream* stream = nullptr;
    hr = SHCreateStreamOnFileEx(filePath.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &stream);
    if (FAILED(hr) || !stream) {
        dc5->Release();
        return false;
    }

    // Default initial viewport
    D2D1_SIZE_F initialViewport = D2D1::SizeF(800.0f, 600.0f);
    hr = dc5->CreateSvgDocument(stream, initialViewport, &m_svgDocument);
    stream->Release();
    dc5->Release();

    if (FAILED(hr) || !m_svgDocument) {
        return false;
    }

    m_filePath = filePath;
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        m_fileSizeBytes = ((UINT64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    }

    // Query root attributes to determine natural dimensions
    ID2D1SvgElement* root = nullptr;
    m_svgDocument->GetRoot(&root);
    if (root) {
        D2D1_SVG_LENGTH wLength = {}, hLength = {};
        bool hasW = SUCCEEDED(root->GetAttributeValue(L"width", &wLength)) && (wLength.units == D2D1_SVG_LENGTH_UNITS_NUMBER) && (wLength.value > 0);
        bool hasH = SUCCEEDED(root->GetAttributeValue(L"height", &hLength)) && (hLength.units == D2D1_SVG_LENGTH_UNITS_NUMBER) && (hLength.value > 0);

        D2D1_SVG_VIEWBOX viewBox = {};
        bool hasVb = SUCCEEDED(root->GetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &viewBox, sizeof(viewBox))) &&
                     (viewBox.width > 0) && (viewBox.height > 0);

        if (hasW && hasH) {
            m_originalSize = D2D1::SizeF(wLength.value, hLength.value);
        } else if (hasVb) {
            m_originalSize = D2D1::SizeF(viewBox.width, viewBox.height);
        } else {
            m_originalSize = D2D1::SizeF(800.0f, 600.0f);
        }

        m_svgDocument->SetViewportSize(m_originalSize);
        root->Release();
    }

    return true;
}

void SvgEngine::Render(ID2D1RenderTarget* rt, const D2D1_RECT_F& targetRect, float zoom, float panX, float panY) {
    if (!rt || !m_svgDocument) return;

    ID2D1DeviceContext5* dc5 = nullptr;
    if (FAILED(rt->QueryInterface(IID_ID2D1DeviceContext5, (void**)&dc5)) || !dc5) {
        return;
    }

    // Save transform
    D2D1_MATRIX_3X2_F oldTransform;
    dc5->GetTransform(&oldTransform);

    float canvasW = targetRect.right - targetRect.left;
    float canvasH = targetRect.bottom - targetRect.top;
    float cx = targetRect.left + canvasW / 2.0f + panX;
    float cy = targetRect.top + canvasH / 2.0f + panY;

    // Calculate fitted destination scale
    float fitScale = (std::min)(canvasW / (std::max)(m_originalSize.width, 1.0f),
                               canvasH / (std::max)(m_originalSize.height, 1.0f));
    float finalScale = fitScale * zoom;

    float renderedW = m_originalSize.width * finalScale;
    float renderedH = m_originalSize.height * finalScale;

    D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Scale(finalScale, finalScale) *
                                  D2D1::Matrix3x2F::Translation(cx - renderedW / 2.0f, cy - renderedH / 2.0f) *
                                  oldTransform;

    dc5->SetTransform(transform);
    dc5->DrawSvgDocument(m_svgDocument);
    dc5->SetTransform(oldTransform);

    dc5->Release();
}

std::wstring SvgEngine::GetMetadataString() const {
    WCHAR buf[128];
    swprintf_s(buf, L"SVG VECTOR • %0.0f × %0.0f • %0.1f KB",
               m_originalSize.width, m_originalSize.height, (float)m_fileSizeBytes / 1024.0f);
    return buf;
}

} // namespace WinPreview
