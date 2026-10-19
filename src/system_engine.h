#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <vector>

namespace WinPreview {

class D2DEngine;

enum class SystemItemType {
    Binary,      // .exe, .dll, .sys, .ocx, .cpl, .scr, .msi
    Shortcut,    // .lnk, .url
    Certificate  // .cer, .crt, .der, .pem
};

struct SystemBinaryInfo {
    std::wstring productName;
    std::wstring fileDescription;
    std::wstring companyName;
    std::wstring fileVersion;
    std::wstring productVersion;
    std::wstring copyright;
    std::wstring originalFilename;
    std::wstring architecture;      // "x64 (AMD64)", "ARM64", "x86 (32-bit)"
    std::wstring subsystem;         // "Windows GUI", "Console (CLI)", "Native Driver"
    std::wstring subsystemVersion;  // "10.0"
    std::wstring signatureStatus;   // "Digitally Signed & Valid", "Not Signed"
    std::wstring signerName;        // "Microsoft Windows", "Google LLC"
    bool isSigned = false;
    bool isSignatureValid = false;
    int numSections = 0;
    std::wstring sectionNames;      // ".text, .rdata, .data, .pdata, .rsrc"
    UINT64 imageBase = 0;
    UINT32 entryPointRva = 0;
    UINT64 fileSizeBytes = 0;
    std::wstring formattedSize;
    std::wstring formattedDate;
    std::wstring binaryType;        // "APPLICATION", "DYNAMIC LIBRARY", "SYSTEM DRIVER"
};

struct SystemShortcutInfo {
    std::wstring shortcutName;
    std::wstring targetPath;
    std::wstring arguments;
    std::wstring workingDirectory;
    std::wstring description;
    std::wstring targetType;
    UINT64 targetSizeBytes = 0;
    std::wstring formattedTargetSize;
    std::wstring targetModifiedDate;
    bool targetExists = false;
    bool isWebUrl = false;
    std::wstring webUrl;
    std::wstring webDomain;
};

struct SystemCertificateInfo {
    std::wstring subjectName;
    std::wstring issuerName;
    std::wstring validFromStr;
    std::wstring validToStr;
    bool isCurrentlyValid = false;
    std::wstring signatureAlgorithm;
    std::wstring publicKeyInfo;
    std::wstring serialNumber;
    std::wstring thumbprint;
};

class SystemEngine {
public:
    SystemEngine();
    ~SystemEngine();

    void Close();

    bool LoadBinary(const std::wstring& filePath, IDWriteFactory* dwrite, ID2D1RenderTarget* rt, IWICImagingFactory* wic);
    bool LoadShortcut(const std::wstring& filePath, IDWriteFactory* dwrite, ID2D1RenderTarget* rt, IWICImagingFactory* wic);
    bool LoadCertificate(const std::wstring& filePath, IDWriteFactory* dwrite, ID2D1RenderTarget* rt, IWICImagingFactory* wic);

    void Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale, int hoveredBtnId);
    void Scroll(float dy);

    bool IsLoaded() const { return m_isLoaded; }
    SystemItemType GetItemType() const { return m_itemType; }
    std::wstring GetFormatBadge() const;
    const std::wstring& GetFilePath() const { return m_filePath; }

    // Hit test action buttons
    D2D1_RECT_F GetPrimaryBtnRect() const { return m_primaryBtnRect; }
    D2D1_RECT_F GetSecondaryBtnRect() const { return m_secondaryBtnRect; }
    void ExecutePrimaryAction();
    void ExecuteSecondaryAction();

private:
    bool m_isLoaded = false;
    SystemItemType m_itemType = SystemItemType::Binary;
    std::wstring m_filePath;
    std::wstring m_fileName;

    ID2D1Bitmap* m_iconBitmap = nullptr;
    IWICBitmapSource* m_wicIcon = nullptr;
    float m_scrollY = 0.0f;
    float m_contentHeight = 0.0f;

    D2D1_RECT_F m_primaryBtnRect = {};
    D2D1_RECT_F m_secondaryBtnRect = {};

    SystemBinaryInfo m_bin;
    SystemShortcutInfo m_shortcut;
    SystemCertificateInfo m_cert;

    void RenderBinary(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float marginX, float contentW, float curY);
    void RenderShortcut(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float marginX, float contentW, float curY);
    void RenderCertificate(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float marginX, float contentW, float curY);
};

} // namespace WinPreview
