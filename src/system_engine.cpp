#include "system_engine.h"
#include "d2d_engine.h"
#include <winver.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <algorithm>

namespace WinPreview {

static std::wstring FormatSize(UINT64 bytes) {
    WCHAR buf[64];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        swprintf_s(buf, L"%0.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        swprintf_s(buf, L"%0.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        swprintf_s(buf, L"%0.1f KB", (double)bytes / 1024.0);
    } else {
        swprintf_s(buf, L"%llu B", bytes);
    }
    return buf;
}

static std::wstring FormatFileTime(const FILETIME& ft) {
    FILETIME localFt = {};
    FileTimeToLocalFileTime(&ft, &localFt);
    SYSTEMTIME st = {};
    FileTimeToSystemTime(&localFt, &st);
    WCHAR buf[64];
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

static IWICBitmapSource* LoadIconToWic(IWICImagingFactory* wic, const std::wstring& path) {
    if (!wic) return nullptr;
    HICON hIcon = nullptr;
    if (ExtractIconExW(path.c_str(), 0, &hIcon, NULL, 1) > 0 && hIcon) {
        IWICBitmap* pWicBmp = nullptr;
        if (SUCCEEDED(wic->CreateBitmapFromHICON(hIcon, &pWicBmp)) && pWicBmp) {
            DestroyIcon(hIcon);
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(wic->CreateFormatConverter(&conv)) && conv) {
                if (SUCCEEDED(conv->Initialize(pWicBmp, GUID_WICPixelFormat32bppPBGRA,
                                               WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
                    pWicBmp->Release();
                    return conv;
                }
                conv->Release();
            }
            return pWicBmp;
        }
        DestroyIcon(hIcon);
    }
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon) {
        IWICBitmap* pWicBmp = nullptr;
        if (SUCCEEDED(wic->CreateBitmapFromHICON(sfi.hIcon, &pWicBmp)) && pWicBmp) {
            DestroyIcon(sfi.hIcon);
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(wic->CreateFormatConverter(&conv)) && conv) {
                if (SUCCEEDED(conv->Initialize(pWicBmp, GUID_WICPixelFormat32bppPBGRA,
                                               WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
                    pWicBmp->Release();
                    return conv;
                }
                conv->Release();
            }
            return pWicBmp;
        }
        DestroyIcon(sfi.hIcon);
    }
    return nullptr;
}

SystemEngine::SystemEngine() {}

SystemEngine::~SystemEngine() {
    Close();
}

void SystemEngine::Close() {
    m_isLoaded = false;
    m_filePath.clear();
    m_fileName.clear();
    if (m_iconBitmap) {
        m_iconBitmap->Release();
        m_iconBitmap = nullptr;
    }
    if (m_wicIcon) {
        m_wicIcon->Release();
        m_wicIcon = nullptr;
    }
    m_bin = {};
    m_shortcut = {};
    m_cert = {};
    m_scrollY = 0.0f;
    m_contentHeight = 0.0f;
    m_primaryBtnRect = {};
    m_secondaryBtnRect = {};
}

void SystemEngine::Scroll(float dy) {
    m_scrollY += dy;
    if (m_scrollY > 0.0f) m_scrollY = 0.0f;
    float maxScroll = (std::max)(0.0f, m_contentHeight - 560.0f);
    if (m_scrollY < -maxScroll) m_scrollY = -maxScroll;
}

std::wstring SystemEngine::GetFormatBadge() const {
    if (!m_isLoaded) return L"SYSTEM SPEC";
    switch (m_itemType) {
        case SystemItemType::Binary: {
            std::wstring b = m_bin.binaryType;
            if (!m_bin.architecture.empty()) b += L" • " + m_bin.architecture;
            if (m_bin.isSigned && m_bin.isSignatureValid) b += L" • VERIFIED SIGNATURE";
            return b;
        }
        case SystemItemType::Shortcut: {
            if (m_shortcut.isWebUrl) return L"INTERNET SHORTCUT • " + m_shortcut.webDomain;
            return m_shortcut.targetExists ? L"APPLICATION SHORTCUT • VALID" : L"APPLICATION SHORTCUT • BROKEN LINK";
        }
        case SystemItemType::Certificate: {
            return m_cert.isCurrentlyValid ? L"SECURITY CERTIFICATE • VALID" : L"SECURITY CERTIFICATE • EXPIRED";
        }
    }
    return L"SYSTEM SPEC";
}

bool SystemEngine::LoadBinary(const std::wstring& filePath, IDWriteFactory* dwrite, ID2D1RenderTarget* rt, IWICImagingFactory* wic) {
    Close();
    m_itemType = SystemItemType::Binary;
    m_filePath = filePath;

    size_t slash = filePath.find_last_of(L"\\/");
    m_fileName = (slash != std::wstring::npos) ? filePath.substr(slash + 1) : filePath;

    m_wicIcon = LoadIconToWic(wic, filePath);

    // 1. File stats
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        m_bin.fileSizeBytes = ((UINT64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        m_bin.formattedSize = FormatSize(m_bin.fileSizeBytes);
        m_bin.formattedDate = FormatFileTime(fad.ftLastWriteTime);
    }

    std::wstring ext;
    size_t dot = filePath.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext = filePath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }

    if (ext == L".dll") m_bin.binaryType = L"DYNAMIC LINK LIBRARY";
    else if (ext == L".sys") m_bin.binaryType = L"KERNEL DRIVER";
    else if (ext == L".ocx") m_bin.binaryType = L"ACTIVEX CONTROL";
    else if (ext == L".cpl") m_bin.binaryType = L"CONTROL PANEL APPLET";
    else if (ext == L".scr") m_bin.binaryType = L"SCREEN SAVER";
    else if (ext == L".msi") m_bin.binaryType = L"WINDOWS INSTALLER PACKAGE";
    else m_bin.binaryType = L"WINDOWS APPLICATION";

    // 2. Parse PE Headers
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        IMAGE_DOS_HEADER dosHeader = {};
        DWORD bytesRead = 0;
        if (ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL) && dosHeader.e_magic == IMAGE_DOS_SIGNATURE) {
            LARGE_INTEGER liOffset;
            liOffset.QuadPart = dosHeader.e_lfanew;
            if (SetFilePointerEx(hFile, liOffset, NULL, FILE_BEGIN)) {
                DWORD ntSignature = 0;
                if (ReadFile(hFile, &ntSignature, sizeof(ntSignature), &bytesRead, NULL) && ntSignature == IMAGE_NT_SIGNATURE) {
                    IMAGE_FILE_HEADER fileHeader = {};
                    if (ReadFile(hFile, &fileHeader, sizeof(fileHeader), &bytesRead, NULL)) {
                        m_bin.numSections = fileHeader.NumberOfSections;

                        // Architecture
                        switch (fileHeader.Machine) {
                            case IMAGE_FILE_MACHINE_AMD64: m_bin.architecture = L"x64 (AMD64)"; break;
                            case IMAGE_FILE_MACHINE_ARM64: m_bin.architecture = L"ARM64 (64-bit)"; break;
                            case IMAGE_FILE_MACHINE_I386:  m_bin.architecture = L"x86 (32-bit)"; break;
                            case IMAGE_FILE_MACHINE_ARMNT: m_bin.architecture = L"ARM (32-bit)"; break;
                            default: m_bin.architecture = L"Native Binary"; break;
                        }

                        // Read Optional Header Magic
                        WORD optMagic = 0;
                        if (ReadFile(hFile, &optMagic, sizeof(optMagic), &bytesRead, NULL)) {
                            liOffset.QuadPart = dosHeader.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
                            SetFilePointerEx(hFile, liOffset, NULL, FILE_BEGIN);

                            WORD subSysVal = 0;
                            WORD majSub = 0, minSub = 0;

                            if (optMagic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                                IMAGE_OPTIONAL_HEADER64 opt64 = {};
                                if (ReadFile(hFile, &opt64, sizeof(opt64), &bytesRead, NULL)) {
                                    subSysVal = opt64.Subsystem;
                                    majSub = opt64.MajorSubsystemVersion;
                                    minSub = opt64.MinorSubsystemVersion;
                                    m_bin.imageBase = opt64.ImageBase;
                                    m_bin.entryPointRva = opt64.AddressOfEntryPoint;
                                }
                            } else if (optMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                                IMAGE_OPTIONAL_HEADER32 opt32 = {};
                                if (ReadFile(hFile, &opt32, sizeof(opt32), &bytesRead, NULL)) {
                                    subSysVal = opt32.Subsystem;
                                    majSub = opt32.MajorSubsystemVersion;
                                    minSub = opt32.MinorSubsystemVersion;
                                    m_bin.imageBase = opt32.ImageBase;
                                    m_bin.entryPointRva = opt32.AddressOfEntryPoint;
                                }
                            }

                            switch (subSysVal) {
                                case IMAGE_SUBSYSTEM_WINDOWS_GUI: m_bin.subsystem = L"Windows GUI"; break;
                                case IMAGE_SUBSYSTEM_WINDOWS_CUI: m_bin.subsystem = L"Console (CLI)"; break;
                                case IMAGE_SUBSYSTEM_NATIVE:      m_bin.subsystem = L"Native Kernel Driver"; break;
                                case IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION: m_bin.subsystem = L"Boot Application"; break;
                                default: m_bin.subsystem = L"Windows Subsystem"; break;
                            }
                            m_bin.subsystemVersion = std::to_wstring(majSub) + L"." + std::to_wstring(minSub);

                            // Section names
                            liOffset.QuadPart = dosHeader.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + fileHeader.SizeOfOptionalHeader;
                            SetFilePointerEx(hFile, liOffset, NULL, FILE_BEGIN);

                            for (int i = 0; i < fileHeader.NumberOfSections && i < 5; ++i) {
                                IMAGE_SECTION_HEADER sh = {};
                                if (ReadFile(hFile, &sh, sizeof(sh), &bytesRead, NULL)) {
                                    char sname[9] = {};
                                    memcpy(sname, sh.Name, 8);
                                    WCHAR wsname[16] = {};
                                    MultiByteToWideChar(CP_ACP, 0, sname, -1, wsname, 16);
                                    if (!m_bin.sectionNames.empty()) m_bin.sectionNames += L", ";
                                    m_bin.sectionNames += wsname;
                                }
                            }
                            if (fileHeader.NumberOfSections > 5) {
                                m_bin.sectionNames += L", ...";
                            }
                        }
                    }
                }
            }
        }
        CloseHandle(hFile);
    }

    // 3. Version Info Resource
    DWORD dwHandle = 0;
    DWORD verSize = GetFileVersionInfoSizeW(filePath.c_str(), &dwHandle);
    if (verSize > 0) {
        std::vector<BYTE> verData(verSize);
        if (GetFileVersionInfoW(filePath.c_str(), 0, verSize, verData.data())) {
            VS_FIXEDFILEINFO* pFfi = nullptr;
            UINT len = 0;
            if (VerQueryValueW(verData.data(), L"\\", (LPVOID*)&pFfi, &len) && pFfi) {
                WCHAR vbuf[64];
                swprintf_s(vbuf, L"%u.%u.%u.%u",
                    HIWORD(pFfi->dwFileVersionMS), LOWORD(pFfi->dwFileVersionMS),
                    HIWORD(pFfi->dwFileVersionLS), LOWORD(pFfi->dwFileVersionLS));
                m_bin.fileVersion = vbuf;

                swprintf_s(vbuf, L"%u.%u.%u.%u",
                    HIWORD(pFfi->dwProductVersionMS), LOWORD(pFfi->dwProductVersionMS),
                    HIWORD(pFfi->dwProductVersionLS), LOWORD(pFfi->dwProductVersionLS));
                m_bin.productVersion = vbuf;
            }

            DWORD* pTrans = nullptr;
            UINT transLen = 0;
            DWORD lang = 0x040904B0; // English (US) Unicode default
            if (VerQueryValueW(verData.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&pTrans, &transLen) && pTrans && transLen >= sizeof(DWORD)) {
                lang = *pTrans;
            }

            auto queryStr = [&](const wchar_t* key, std::wstring& out) {
                WCHAR subBlock[128];
                swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\%s", LOWORD(lang), HIWORD(lang), key);
                LPWSTR val = nullptr;
                UINT vLen = 0;
                if (VerQueryValueW(verData.data(), subBlock, (LPVOID*)&val, &vLen) && val && vLen > 0) {
                    out = val;
                } else {
                    // Fallback to neutral 040904E4
                    swprintf_s(subBlock, L"\\StringFileInfo\\040904e4\\%s", key);
                    if (VerQueryValueW(verData.data(), subBlock, (LPVOID*)&val, &vLen) && val && vLen > 0) {
                        out = val;
                    }
                }
            };

            queryStr(L"ProductName", m_bin.productName);
            queryStr(L"FileDescription", m_bin.fileDescription);
            queryStr(L"CompanyName", m_bin.companyName);
            queryStr(L"LegalCopyright", m_bin.copyright);
            queryStr(L"OriginalFilename", m_bin.originalFilename);
        }
    }

    if (m_bin.productName.empty()) {
        m_bin.productName = m_fileName;
    }

    // 4. Authenticode Digital Signature Verification
    WINTRUST_FILE_INFO fileData = {};
    fileData.cbStruct = sizeof(WINTRUST_FILE_INFO);
    fileData.pcwszFilePath = filePath.c_str();

    GUID actionGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA trustData = {};
    trustData.cbStruct = sizeof(WINTRUST_DATA);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileData;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;

    LONG status = WinVerifyTrust(NULL, &actionGuid, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &actionGuid, &trustData);

    if (status == ERROR_SUCCESS) {
        m_bin.isSigned = true;
        m_bin.isSignatureValid = true;
        m_bin.signatureStatus = L"Digitally Signed & Verified";
    } else if (status == (LONG)TRUST_E_NOSIGNATURE) {
        m_bin.isSigned = false;
        m_bin.signatureStatus = L"Not Digitally Signed";
    } else {
        m_bin.isSigned = true;
        m_bin.isSignatureValid = false;
        m_bin.signatureStatus = L"Signed (Untrusted or Expired)";
    }

    // Extract Signer Name from PKCS#7
    if (m_bin.isSigned) {
        HCERTSTORE hStore = NULL;
        HCRYPTMSG hMsg = NULL;
        if (CryptQueryObject(CERT_QUERY_OBJECT_FILE, filePath.c_str(),
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY,
            0, NULL, NULL, NULL, &hStore, &hMsg, NULL) && hMsg) {
            DWORD signerInfoSize = 0;
            if (CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &signerInfoSize) && signerInfoSize > 0) {
                std::vector<BYTE> signerInfoBuf(signerInfoSize);
                if (CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, signerInfoBuf.data(), &signerInfoSize)) {
                    CMSG_SIGNER_INFO* pSignerInfo = (CMSG_SIGNER_INFO*)signerInfoBuf.data();
                    CERT_INFO certInfo = {};
                    certInfo.Issuer = pSignerInfo->Issuer;
                    certInfo.SerialNumber = pSignerInfo->SerialNumber;
                    PCCERT_CONTEXT pCert = CertFindCertificateInStore(hStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                        0, CERT_FIND_SUBJECT_CERT, &certInfo, NULL);
                    if (pCert) {
                        WCHAR signerName[256] = {};
                        CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, signerName, 256);
                        m_bin.signerName = signerName;
                        CertFreeCertificateContext(pCert);
                    }
                }
            }
            if (hStore) CertCloseStore(hStore, 0);
            if (hMsg) CryptMsgClose(hMsg);
        }
    }

    m_isLoaded = true;
    return true;
}

bool SystemEngine::LoadShortcut(const std::wstring& filePath, IDWriteFactory* dwrite, ID2D1RenderTarget* rt, IWICImagingFactory* wic) {
    Close();
    m_itemType = SystemItemType::Shortcut;
    m_filePath = filePath;

    size_t slash = filePath.find_last_of(L"\\/");
    m_fileName = (slash != std::wstring::npos) ? filePath.substr(slash + 1) : filePath;
    m_shortcut.shortcutName = m_fileName;
    size_t lastDot = m_shortcut.shortcutName.find_last_of(L'.');
    if (lastDot != std::wstring::npos) {
        m_shortcut.shortcutName = m_shortcut.shortcutName.substr(0, lastDot);
    }

    std::wstring ext;
    size_t dot = filePath.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext = filePath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }

    // 1. Internet Shortcut (.url)
    if (ext == L".url") {
        m_shortcut.isWebUrl = true;
        WCHAR urlBuf[2048] = {};
        GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"", urlBuf, 2048, filePath.c_str());
        m_shortcut.webUrl = urlBuf;

        // Extract domain
        std::wstring url = m_shortcut.webUrl;
        size_t p = url.find(L"://");
        if (p != std::wstring::npos) url = url.substr(p + 3);
        size_t slashP = url.find(L'/');
        if (slashP != std::wstring::npos) url = url.substr(0, slashP);
        m_shortcut.webDomain = url;

        m_wicIcon = LoadIconToWic(wic, filePath);
        m_shortcut.targetExists = true;
        m_isLoaded = true;
        return true;
    }

    // 2. Shell Link (.lnk)
    IShellLinkW* psl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl)) && psl) {
        IPersistFile* ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)) && ppf) {
            if (SUCCEEDED(ppf->Load(filePath.c_str(), STGM_READ))) {
                WCHAR targetPath[MAX_PATH] = {};
                WIN32_FIND_DATAW wfd = {};
                psl->GetPath(targetPath, MAX_PATH, &wfd, SLGP_RAWPATH);
                m_shortcut.targetPath = targetPath;

                WCHAR args[1024] = {};
                psl->GetArguments(args, 1024);
                m_shortcut.arguments = args;

                WCHAR dir[MAX_PATH] = {};
                psl->GetWorkingDirectory(dir, MAX_PATH);
                m_shortcut.workingDirectory = dir;

                WCHAR desc[1024] = {};
                psl->GetDescription(desc, 1024);
                m_shortcut.description = desc;

                // Check target existence
                if (!m_shortcut.targetPath.empty()) {
                    DWORD attrs = GetFileAttributesW(m_shortcut.targetPath.c_str());
                    if (attrs != INVALID_FILE_ATTRIBUTES) {
                        m_shortcut.targetExists = true;
                        m_wicIcon = LoadIconToWic(wic, m_shortcut.targetPath);

                        WIN32_FILE_ATTRIBUTE_DATA fad = {};
                        if (GetFileAttributesExW(m_shortcut.targetPath.c_str(), GetFileExInfoStandard, &fad)) {
                            m_shortcut.targetSizeBytes = ((UINT64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
                            m_shortcut.formattedTargetSize = FormatSize(m_shortcut.targetSizeBytes);
                            m_shortcut.targetModifiedDate = FormatFileTime(fad.ftLastWriteTime);
                        }

                        SHFILEINFOW sfi = {};
                        if (SHGetFileInfoW(m_shortcut.targetPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_TYPENAME)) {
                            m_shortcut.targetType = sfi.szTypeName;
                        }
                    }
                }
            }
            ppf->Release();
        }
        psl->Release();
    }

    if (!m_wicIcon) {
        m_wicIcon = LoadIconToWic(wic, filePath);
    }

    m_isLoaded = true;
    return true;
}

bool SystemEngine::LoadCertificate(const std::wstring& filePath, IDWriteFactory* dwrite, ID2D1RenderTarget* rt, IWICImagingFactory* wic) {
    Close();
    m_itemType = SystemItemType::Certificate;
    m_filePath = filePath;

    size_t slash = filePath.find_last_of(L"\\/");
    m_fileName = (slash != std::wstring::npos) ? filePath.substr(slash + 1) : filePath;
    m_wicIcon = LoadIconToWic(wic, filePath);

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize > 16 * 1024 * 1024) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<BYTE> data(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, data.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    PCCERT_CONTEXT pCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, data.data(), (DWORD)data.size());
    if (!pCert) {
        // Try PEM base64 decode
        DWORD binSize = 0;
        if (CryptStringToBinaryA((LPCSTR)data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64HEADER, NULL, &binSize, NULL, NULL) && binSize > 0) {
            std::vector<BYTE> binData(binSize);
            if (CryptStringToBinaryA((LPCSTR)data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64HEADER, binData.data(), &binSize, NULL, NULL)) {
                pCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, binData.data(), binSize);
            }
        }
    }

    if (!pCert) return false;

    // Subject
    WCHAR szSubject[256] = {};
    CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, szSubject, 256);
    m_cert.subjectName = szSubject;

    // Issuer
    WCHAR szIssuer[256] = {};
    CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, NULL, szIssuer, 256);
    m_cert.issuerName = szIssuer;

    // Dates
    m_cert.validFromStr = FormatFileTime(pCert->pCertInfo->NotBefore);
    m_cert.validToStr = FormatFileTime(pCert->pCertInfo->NotAfter);

    FILETIME nowFt = {};
    GetSystemTimeAsFileTime(&nowFt);
    m_cert.isCurrentlyValid = (CompareFileTime(&nowFt, &pCert->pCertInfo->NotBefore) >= 0 &&
                               CompareFileTime(&nowFt, &pCert->pCertInfo->NotAfter) <= 0);

    // Public Key Info
    DWORD keyBits = CertGetPublicKeyLength(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &pCert->pCertInfo->SubjectPublicKeyInfo);
    if (keyBits > 0) {
        m_cert.publicKeyInfo = std::to_wstring(keyBits) + L" Bits";
    }

    // Signature Algorithm
    if (pCert->pCertInfo->SignatureAlgorithm.pszObjId) {
        PCCRYPT_OID_INFO pOid = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY, pCert->pCertInfo->SignatureAlgorithm.pszObjId, CRYPT_SIGN_ALG_OID_GROUP_ID);
        if (pOid && pOid->pwszName) {
            m_cert.signatureAlgorithm = pOid->pwszName;
        } else {
            WCHAR wOid[128] = {};
            MultiByteToWideChar(CP_ACP, 0, pCert->pCertInfo->SignatureAlgorithm.pszObjId, -1, wOid, 128);
            m_cert.signatureAlgorithm = wOid;
        }
    }

    // Serial Number
    for (DWORD i = 0; i < pCert->pCertInfo->SerialNumber.cbData; ++i) {
        WCHAR hex[8];
        swprintf_s(hex, L"%02X", pCert->pCertInfo->SerialNumber.pbData[pCert->pCertInfo->SerialNumber.cbData - 1 - i]);
        m_cert.serialNumber += hex;
    }

    // SHA-1 Thumbprint
    BYTE thumb[20] = {};
    DWORD thumbSize = sizeof(thumb);
    if (CertGetCertificateContextProperty(pCert, CERT_SHA1_HASH_PROP_ID, thumb, &thumbSize)) {
        for (DWORD i = 0; i < thumbSize; ++i) {
            WCHAR hex[8];
            swprintf_s(hex, L"%02X", thumb[i]);
            if (i > 0 && i % 2 == 0) m_cert.thumbprint += L" ";
            m_cert.thumbprint += hex;
        }
    }

    CertFreeCertificateContext(pCert);
    m_isLoaded = true;
    return true;
}

void SystemEngine::ExecutePrimaryAction() {
    if (!m_isLoaded) return;
    if (m_itemType == SystemItemType::Shortcut) {
        if (m_shortcut.isWebUrl && !m_shortcut.webUrl.empty()) {
            ShellExecuteW(NULL, L"open", m_shortcut.webUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        } else if (!m_shortcut.targetPath.empty()) {
            ShellExecuteW(NULL, L"open", m_shortcut.targetPath.c_str(), m_shortcut.arguments.empty() ? NULL : m_shortcut.arguments.c_str(),
                          m_shortcut.workingDirectory.empty() ? NULL : m_shortcut.workingDirectory.c_str(), SW_SHOWNORMAL);
        }
    } else {
        ShellExecuteW(NULL, L"open", m_filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

void SystemEngine::ExecuteSecondaryAction() {
    if (!m_isLoaded) return;
    std::wstring path = (m_itemType == SystemItemType::Shortcut && m_shortcut.targetExists && !m_shortcut.targetPath.empty()) ?
                        m_shortcut.targetPath : m_filePath;
    std::wstring params = L"/select,\"" + path + L"\"";
    ShellExecuteW(NULL, L"open", L"explorer.exe", params.c_str(), NULL, SW_SHOWNORMAL);
}

void SystemEngine::Render(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float dpiScale, int hoveredBtnId) {
    if (!rt || !m_isLoaded) return;

    if (!m_iconBitmap && m_wicIcon) {
        rt->CreateBitmapFromWicBitmap(m_wicIcon, &m_iconBitmap);
    }

    float clientW = targetRect.right - targetRect.left;
    float clientH = targetRect.bottom - targetRect.top;
    float marginX = (std::max)(36.0f, (clientW - 900.0f) / 2.0f);
    float contentW = clientW - marginX * 2.0f;
    float curY = targetRect.top + 32.0f + m_scrollY;

    switch (m_itemType) {
        case SystemItemType::Binary:
            RenderBinary(rt, d2d, targetRect, marginX, contentW, curY);
            break;
        case SystemItemType::Shortcut:
            RenderShortcut(rt, d2d, targetRect, marginX, contentW, curY);
            break;
        case SystemItemType::Certificate:
            RenderCertificate(rt, d2d, targetRect, marginX, contentW, curY);
            break;
    }

    // Interactive Action Buttons at bottom
    float btnW = 190.0f;
    float btnH = 38.0f;
    float btnGap = 16.0f;
    float totalBtnW = btnW * 2.0f + btnGap;
    float btnX = marginX + (contentW - totalBtnW) / 2.0f;
    float btnY = targetRect.bottom - 56.0f;

    m_primaryBtnRect = D2D1::RectF(btnX, btnY, btnX + btnW, btnY + btnH);
    m_secondaryBtnRect = D2D1::RectF(btnX + btnW + btnGap, btnY, btnX + totalBtnW, btnY + btnH);

    // Primary Button
    D2D1_ROUNDED_RECT rPri = D2D1::RoundedRect(m_primaryBtnRect, 19.0f, 19.0f);
    rt->FillRoundedRectangle(rPri, d2d.BrushAccent);
    if (hoveredBtnId == 1001) {
        rt->DrawRoundedRectangle(rPri, d2d.BrushText, 1.2f);
    }

    std::wstring priText = L"Open File (Enter)";
    if (m_itemType == SystemItemType::Binary) {
        priText = (m_bin.binaryType == L"WINDOWS APPLICATION") ? L"Run Application (Enter)" : L"Open File (Enter)";
    } else if (m_itemType == SystemItemType::Shortcut) {
        priText = m_shortcut.isWebUrl ? L"Open in Browser (Enter)" : L"Open Target (Enter)";
    }
    rt->DrawText(priText.c_str(), (UINT32)priText.length(), d2d.FontButton, m_primaryBtnRect, d2d.BrushText);

    // Secondary Button
    D2D1_ROUNDED_RECT rSec = D2D1::RoundedRect(m_secondaryBtnRect, 19.0f, 19.0f);
    rt->FillRoundedRectangle(rSec, d2d.BrushCard);
    rt->DrawRoundedRectangle(rSec, (hoveredBtnId == 1002) ? d2d.BrushAccent : d2d.BrushBorder, 1.0f);
    const wchar_t* secText = L"Show in Folder";
    rt->DrawText(secText, (UINT32)wcslen(secText), d2d.FontButton, m_secondaryBtnRect, d2d.BrushText);
}

void SystemEngine::RenderBinary(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float marginX, float contentW, float curY) {
    float iconSize = 56.0f;
    float iconX = marginX + 4.0f;
    float iconY = curY;

    // Elevated Icon Card
    D2D1_ROUNDED_RECT iconBox = D2D1::RoundedRect(D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize), 14.0f, 14.0f);
    rt->FillRoundedRectangle(iconBox, d2d.BrushCard);
    rt->DrawRoundedRectangle(iconBox, d2d.BrushBorder, 1.2f);

    if (m_iconBitmap) {
        D2D1_SIZE_F sz = m_iconBitmap->GetSize();
        float dw = (std::min)(38.0f, sz.width);
        float dh = (std::min)(38.0f, sz.height);
        D2D1_RECT_F dst = D2D1::RectF(iconX + (iconSize - dw) / 2.0f, iconY + (iconSize - dh) / 2.0f,
                                      iconX + (iconSize + dw) / 2.0f, iconY + (iconSize + dh) / 2.0f);
        rt->DrawBitmap(m_iconBitmap, dst);
    } else {
        d2d.DrawIconFileDocument(iconX + iconSize / 2.0f, iconY + iconSize / 2.0f, 26.0f, d2d.BrushAccent);
    }

    // Hero Product Title & File Description
    float textX = iconX + iconSize + 18.0f;
    float titleW = contentW - iconSize - 22.0f;

    std::wstring displayTitle = !m_bin.fileDescription.empty() ? m_bin.fileDescription :
                                (!m_bin.productName.empty() ? m_bin.productName : m_fileName);
    IDWriteTextLayout* tl = nullptr;
    d2d.GetDWriteFactory()->CreateTextLayout(displayTitle.c_str(), (UINT32)displayTitle.length(),
        d2d.FontHero ? d2d.FontHero : d2d.FontTitle, titleW, 34.0f, &tl);
    if (tl) {
        tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* trimSign = nullptr;
        d2d.GetDWriteFactory()->CreateEllipsisTrimmingSign(d2d.FontHero ? d2d.FontHero : d2d.FontTitle, &trimSign);
        tl->SetTrimming(&trim, trimSign);
        if (trimSign) trimSign->Release();
        rt->DrawTextLayout(D2D1::Point2F(textX, curY), tl, d2d.BrushText);
        tl->Release();
    }

    std::wstring subLine = m_fileName;
    if (!m_bin.companyName.empty()) {
        subLine += L" • " + m_bin.companyName;
    } else if (!m_bin.productName.empty() && m_bin.productName != displayTitle) {
        subLine += L" • " + m_bin.productName;
    }

    IDWriteTextLayout* pl = nullptr;
    d2d.GetDWriteFactory()->CreateTextLayout(subLine.c_str(), (UINT32)subLine.length(),
        d2d.FontCaption ? d2d.FontCaption : d2d.FontBody, titleW, 20.0f, &pl);
    if (pl) {
        pl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* trimSign = nullptr;
        d2d.GetDWriteFactory()->CreateEllipsisTrimmingSign(d2d.FontCaption ? d2d.FontCaption : d2d.FontBody, &trimSign);
        pl->SetTrimming(&trim, trimSign);
        if (trimSign) trimSign->Release();
        rt->DrawTextLayout(D2D1::Point2F(textX, curY + 34.0f), pl, d2d.BrushTextMuted);
        pl->Release();
    }

    curY += 70.0f;

    // Badges Row: Architecture, Subsystem, Signature, Version
    auto drawPill = [&](const std::wstring& txt, float x, ID2D1SolidColorBrush* bg, ID2D1SolidColorBrush* fg) -> float {
        float pw = 16.0f + (float)txt.length() * 7.2f;
        D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(x, curY, x + pw, curY + 24.0f), 12.0f, 12.0f);
        rt->FillRoundedRectangle(r, bg);
        rt->DrawRoundedRectangle(r, d2d.BrushBorder, 1.0f);

        IDWriteTextLayout* btl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(txt.c_str(), (UINT32)txt.length(), d2d.FontBadge, pw, 24.0f, &btl);
        if (btl) {
            btl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            btl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(x, curY), btl, fg);
            btl->Release();
        }
        return pw + 8.0f;
    };

    float bx = marginX;
    if (!m_bin.architecture.empty()) {
        bx += drawPill(m_bin.architecture, bx, d2d.BrushCard, d2d.BrushAccent);
    }
    if (!m_bin.subsystem.empty()) {
        bx += drawPill(m_bin.subsystem, bx, d2d.BrushCard, d2d.BrushText);
    }
    if (m_bin.isSigned) {
        std::wstring sig = m_bin.isSignatureValid ? L"SIGNED & VERIFIED" : L"UNVERIFIED SIGNATURE";
        if (!m_bin.signerName.empty()) sig += L": " + m_bin.signerName;
        bx += drawPill(sig, bx, d2d.BrushCard, m_bin.isSignatureValid ? d2d.BrushGreen : d2d.BrushAmber);
    } else {
        bx += drawPill(L"UNSIGNED BINARY", bx, d2d.BrushCard, d2d.BrushTextMuted);
    }
    if (!m_bin.fileVersion.empty()) {
        bx += drawPill(L"v" + m_bin.fileVersion, bx, d2d.BrushCard, d2d.BrushTextMuted);
    }

    curY += 38.0f;
    rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY), d2d.BrushBorderSubtle, 1.0f);
    curY += 16.0f;

    // Two Glass Cards (Split Spec View)
    float cardW = (contentW - 18.0f) / 2.0f;
    float lblW = 115.0f;
    float valX = 18.0f + lblW + 10.0f;
    float valW = cardW - valX - 16.0f;

    struct MeasuredRow {
        std::wstring key;
        std::wstring val;
        IDWriteTextLayout* layout = nullptr;
        float height = 18.0f;
    };

    auto measureCard = [&](const std::vector<std::pair<std::wstring, std::wstring>>& rows, std::vector<MeasuredRow>& outRows) -> float {
        float totalH = 46.0f;
        for (const auto& kv : rows) {
            MeasuredRow mr;
            mr.key = kv.first;
            mr.val = kv.second;

            d2d.GetDWriteFactory()->CreateTextLayout(mr.val.c_str(), (UINT32)mr.val.length(),
                d2d.FontBody, valW, 38.0f, &mr.layout);
            if (mr.layout) {
                DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                IDWriteInlineObject* trimSign = nullptr;
                d2d.GetDWriteFactory()->CreateEllipsisTrimmingSign(d2d.FontBody, &trimSign);
                mr.layout->SetTrimming(&trim, trimSign);
                if (trimSign) trimSign->Release();

                DWRITE_TEXT_METRICS tm;
                if (SUCCEEDED(mr.layout->GetMetrics(&tm))) {
                    mr.height = (std::max)(18.0f, tm.height);
                }
            }
            totalH += mr.height + 10.0f;
            outRows.push_back(mr);
        }
        totalH += 8.0f;
        return totalH;
    };

    std::vector<std::pair<std::wstring, std::wstring>> col1 = {
        { L"Product Name", !m_bin.productName.empty() ? m_bin.productName : L"—" },
        { L"Description", !m_bin.fileDescription.empty() ? m_bin.fileDescription : L"—" },
        { L"Publisher", !m_bin.companyName.empty() ? m_bin.companyName : L"—" },
        { L"File Version", !m_bin.fileVersion.empty() ? m_bin.fileVersion : L"—" },
        { L"Product Version", !m_bin.productVersion.empty() ? m_bin.productVersion : L"—" },
        { L"Copyright", !m_bin.copyright.empty() ? m_bin.copyright : L"—" }
    };

    WCHAR entryHex[64] = {};
    if (m_bin.entryPointRva) swprintf_s(entryHex, L"0x%08X", m_bin.entryPointRva);
    else wcscpy_s(entryHex, L"—");

    std::wstring peSectStr = L"—";
    if (m_bin.numSections > 0) {
        peSectStr = std::to_wstring(m_bin.numSections);
        if (!m_bin.sectionNames.empty()) peSectStr += L" (" + m_bin.sectionNames + L")";
    }

    std::vector<std::pair<std::wstring, std::wstring>> col2 = {
        { L"Architecture", !m_bin.architecture.empty() ? m_bin.architecture : L"—" },
        { L"Subsystem", !m_bin.subsystem.empty() ? (m_bin.subsystem + L" (" + m_bin.subsystemVersion + L")") : L"—" },
        { L"PE Sections", peSectStr },
        { L"Entry Point", entryHex },
        { L"File Size", m_bin.formattedSize + L" (" + std::to_wstring(m_bin.fileSizeBytes) + L" bytes)" },
        { L"Modified", m_bin.formattedDate }
    };

    std::vector<MeasuredRow> mCol1, mCol2;
    float h1 = measureCard(col1, mCol1);
    float h2 = measureCard(col2, mCol2);
    float cardH = (std::max)({ 260.0f, h1, h2 });

    auto renderMeasuredCard = [&](float cx, float cy, const wchar_t* title, const std::vector<MeasuredRow>& rows) {
        D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(cx, cy, cx + cardW, cy + cardH), 16.0f, 16.0f);
        rt->FillRoundedRectangle(r, d2d.BrushCard);
        rt->DrawRoundedRectangle(r, d2d.BrushBorder, 1.0f);

        D2D1_RECT_F tRect = D2D1::RectF(cx + 18.0f, cy + 14.0f, cx + cardW - 18.0f, cy + 34.0f);
        rt->DrawText(title, (UINT32)wcslen(title), d2d.FontBadge, tRect, d2d.BrushAccent);

        float rowY = cy + 42.0f;
        for (const auto& mr : rows) {
            D2D1_RECT_F lblR = D2D1::RectF(cx + 18.0f, rowY + 1.0f, cx + 18.0f + lblW, rowY + 19.0f);
            rt->DrawText(mr.key.c_str(), (UINT32)mr.key.length(), d2d.FontCaption, lblR, d2d.BrushTextMuted);

            if (mr.layout) {
                rt->DrawTextLayout(D2D1::Point2F(cx + valX, rowY), mr.layout, d2d.BrushText);
            }
            rowY += mr.height + 10.0f;
        }
    };

    renderMeasuredCard(marginX, curY, L"PRODUCT & IDENTITY", mCol1);
    renderMeasuredCard(marginX + cardW + 18.0f, curY, L"BINARY ARCHITECTURE & SPECS", mCol2);

    for (auto& mr : mCol1) { if (mr.layout) mr.layout->Release(); }
    for (auto& mr : mCol2) { if (mr.layout) mr.layout->Release(); }

    m_contentHeight = (curY + cardH + 80.0f) - targetRect.top;
}

void SystemEngine::RenderShortcut(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float marginX, float contentW, float curY) {
    float iconSize = 56.0f;
    float iconX = marginX + 4.0f;
    float iconY = curY;

    D2D1_ROUNDED_RECT iconBox = D2D1::RoundedRect(D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize), 14.0f, 14.0f);
    rt->FillRoundedRectangle(iconBox, d2d.BrushCard);
    rt->DrawRoundedRectangle(iconBox, d2d.BrushBorder, 1.2f);

    if (m_iconBitmap) {
        D2D1_SIZE_F sz = m_iconBitmap->GetSize();
        float dw = (std::min)(38.0f, sz.width);
        float dh = (std::min)(38.0f, sz.height);
        D2D1_RECT_F dst = D2D1::RectF(iconX + (iconSize - dw) / 2.0f, iconY + (iconSize - dh) / 2.0f,
                                      iconX + (iconSize + dw) / 2.0f, iconY + (iconSize + dh) / 2.0f);
        rt->DrawBitmap(m_iconBitmap, dst);
    } else {
        d2d.DrawIconFileDocument(iconX + iconSize / 2.0f, iconY + iconSize / 2.0f, 26.0f, d2d.BrushAccent);
    }

    float textX = iconX + iconSize + 18.0f;
    float titleW = contentW - iconSize - 22.0f;

    std::wstring displayTitle = m_shortcut.shortcutName;
    IDWriteTextLayout* tl = nullptr;
    d2d.GetDWriteFactory()->CreateTextLayout(displayTitle.c_str(), (UINT32)displayTitle.length(),
        d2d.FontHero ? d2d.FontHero : d2d.FontTitle, titleW, 34.0f, &tl);
    if (tl) {
        tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* trimSign = nullptr;
        d2d.GetDWriteFactory()->CreateEllipsisTrimmingSign(d2d.FontHero ? d2d.FontHero : d2d.FontTitle, &trimSign);
        tl->SetTrimming(&trim, trimSign);
        if (trimSign) trimSign->Release();
        rt->DrawTextLayout(D2D1::Point2F(textX, curY), tl, d2d.BrushText);
        tl->Release();
    }

    std::wstring subLine = m_shortcut.isWebUrl ? m_shortcut.webUrl : m_shortcut.targetPath;
    IDWriteTextLayout* pl = nullptr;
    d2d.GetDWriteFactory()->CreateTextLayout(subLine.c_str(), (UINT32)subLine.length(),
        d2d.FontCaption ? d2d.FontCaption : d2d.FontBody, titleW, 20.0f, &pl);
    if (pl) {
        pl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* trimSign = nullptr;
        d2d.GetDWriteFactory()->CreateEllipsisTrimmingSign(d2d.FontCaption ? d2d.FontCaption : d2d.FontBody, &trimSign);
        pl->SetTrimming(&trim, trimSign);
        if (trimSign) trimSign->Release();
        rt->DrawTextLayout(D2D1::Point2F(textX, curY + 34.0f), pl, d2d.BrushTextMuted);
        pl->Release();
    }

    curY += 70.0f;

    auto drawPill = [&](const std::wstring& txt, float x, ID2D1SolidColorBrush* bg, ID2D1SolidColorBrush* fg) -> float {
        float pw = 16.0f + (float)txt.length() * 7.2f;
        D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(x, curY, x + pw, curY + 24.0f), 12.0f, 12.0f);
        rt->FillRoundedRectangle(r, bg);
        rt->DrawRoundedRectangle(r, d2d.BrushBorder, 1.0f);

        IDWriteTextLayout* btl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(txt.c_str(), (UINT32)txt.length(), d2d.FontBadge, pw, 24.0f, &btl);
        if (btl) {
            btl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            btl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(x, curY), btl, fg);
            btl->Release();
        }
        return pw + 8.0f;
    };

    float bx = marginX;
    if (m_shortcut.isWebUrl) {
        bx += drawPill(L"INTERNET SHORTCUT", bx, d2d.BrushCard, d2d.BrushAccent);
        if (!m_shortcut.webDomain.empty()) bx += drawPill(m_shortcut.webDomain, bx, d2d.BrushCard, d2d.BrushText);
    } else {
        bx += drawPill(L"SHELL SHORTCUT", bx, d2d.BrushCard, d2d.BrushAccent);
        if (m_shortcut.targetExists) {
            bx += drawPill(L"TARGET VALID", bx, d2d.BrushCard, d2d.BrushGreen);
            if (!m_shortcut.targetType.empty()) bx += drawPill(m_shortcut.targetType, bx, d2d.BrushCard, d2d.BrushText);
        } else {
            bx += drawPill(L"BROKEN TARGET LINK", bx, d2d.BrushCard, d2d.BrushAmber);
        }
    }

    curY += 38.0f;
    rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY), d2d.BrushBorderSubtle, 1.0f);
    curY += 16.0f;

    std::vector<std::pair<std::wstring, std::wstring>> rows;
    if (m_shortcut.isWebUrl) {
        rows.push_back({ L"Target URL", m_shortcut.webUrl });
        rows.push_back({ L"Domain", m_shortcut.webDomain });
        rows.push_back({ L"Protocol", L"Hypertext Transfer Protocol (Web)" });
    } else {
        rows.push_back({ L"Target Path", !m_shortcut.targetPath.empty() ? m_shortcut.targetPath : L"—" });
        rows.push_back({ L"Arguments", !m_shortcut.arguments.empty() ? m_shortcut.arguments : L"—" });
        rows.push_back({ L"Working Dir", !m_shortcut.workingDirectory.empty() ? m_shortcut.workingDirectory : L"—" });
        rows.push_back({ L"Target Type", !m_shortcut.targetType.empty() ? m_shortcut.targetType : L"—" });
        rows.push_back({ L"Target Size", m_shortcut.targetExists ? m_shortcut.formattedTargetSize : L"—" });
    }

    float lblW = 120.0f;
    float valX = 18.0f + lblW + 12.0f;
    float valW = contentW - valX - 18.0f;

    struct MeasuredRow {
        std::wstring key;
        std::wstring val;
        IDWriteTextLayout* layout = nullptr;
        float height = 18.0f;
    };

    std::vector<MeasuredRow> mRows;
    float totalH = 46.0f;
    for (const auto& kv : rows) {
        MeasuredRow mr;
        mr.key = kv.first;
        mr.val = kv.second;

        d2d.GetDWriteFactory()->CreateTextLayout(mr.val.c_str(), (UINT32)mr.val.length(),
            d2d.FontBody, valW, 54.0f, &mr.layout);
        if (mr.layout) {
            DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            IDWriteInlineObject* trimSign = nullptr;
            d2d.GetDWriteFactory()->CreateEllipsisTrimmingSign(d2d.FontBody, &trimSign);
            mr.layout->SetTrimming(&trim, trimSign);
            if (trimSign) trimSign->Release();

            DWRITE_TEXT_METRICS tm;
            if (SUCCEEDED(mr.layout->GetMetrics(&tm))) {
                mr.height = (std::max)(18.0f, tm.height);
            }
        }
        totalH += mr.height + 12.0f;
        mRows.push_back(mr);
    }
    totalH += 8.0f;
    float cardH = (std::max)(200.0f, totalH);

    D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(marginX, curY, marginX + contentW, curY + cardH), 16.0f, 16.0f);
    rt->FillRoundedRectangle(r, d2d.BrushCard);
    rt->DrawRoundedRectangle(r, d2d.BrushBorder, 1.0f);

    D2D1_RECT_F tRect = D2D1::RectF(marginX + 18.0f, curY + 14.0f, marginX + contentW - 18.0f, curY + 34.0f);
    rt->DrawText(L"SHORTCUT PROPERTIES", (UINT32)wcslen(L"SHORTCUT PROPERTIES"), d2d.FontBadge, tRect, d2d.BrushAccent);

    float rowY = curY + 44.0f;
    for (const auto& mr : mRows) {
        D2D1_RECT_F lblR = D2D1::RectF(marginX + 18.0f, rowY + 1.0f, marginX + 18.0f + lblW, rowY + 19.0f);
        rt->DrawText(mr.key.c_str(), (UINT32)mr.key.length(), d2d.FontCaption, lblR, d2d.BrushTextMuted);

        if (mr.layout) {
            rt->DrawTextLayout(D2D1::Point2F(marginX + valX, rowY), mr.layout, d2d.BrushText);
        }
        rowY += mr.height + 12.0f;
    }

    for (auto& mr : mRows) { if (mr.layout) mr.layout->Release(); }

    m_contentHeight = (curY + cardH + 80.0f) - targetRect.top;
}

void SystemEngine::RenderCertificate(ID2D1RenderTarget* rt, D2DEngine& d2d, const D2D1_RECT_F& targetRect, float marginX, float contentW, float curY) {
    float iconSize = 56.0f;
    float iconX = marginX + 4.0f;
    float iconY = curY;

    D2D1_ROUNDED_RECT iconBox = D2D1::RoundedRect(D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize), 14.0f, 14.0f);
    rt->FillRoundedRectangle(iconBox, d2d.BrushCard);
    rt->DrawRoundedRectangle(iconBox, d2d.BrushBorder, 1.2f);

    if (m_iconBitmap) {
        D2D1_SIZE_F sz = m_iconBitmap->GetSize();
        float dw = (std::min)(38.0f, sz.width);
        float dh = (std::min)(38.0f, sz.height);
        D2D1_RECT_F dst = D2D1::RectF(iconX + (iconSize - dw) / 2.0f, iconY + (iconSize - dh) / 2.0f,
                                      iconX + (iconSize + dw) / 2.0f, iconY + (iconSize + dh) / 2.0f);
        rt->DrawBitmap(m_iconBitmap, dst);
    } else {
        d2d.DrawIconFileDocument(iconX + iconSize / 2.0f, iconY + iconSize / 2.0f, 26.0f, d2d.BrushAccent);
    }

    float textX = iconX + iconSize + 18.0f;
    float titleW = contentW - iconSize - 22.0f;

    std::wstring displayTitle = !m_cert.subjectName.empty() ? m_cert.subjectName : m_fileName;
    IDWriteTextLayout* tl = nullptr;
    d2d.GetDWriteFactory()->CreateTextLayout(displayTitle.c_str(), (UINT32)displayTitle.length(),
        d2d.FontHero ? d2d.FontHero : d2d.FontTitle, titleW, 34.0f, &tl);
    if (tl) {
        tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* trimSign = nullptr;
        d2d.GetDWriteFactory()->CreateEllipsisTrimmingSign(d2d.FontHero ? d2d.FontHero : d2d.FontTitle, &trimSign);
        tl->SetTrimming(&trim, trimSign);
        if (trimSign) trimSign->Release();
        rt->DrawTextLayout(D2D1::Point2F(textX, curY), tl, d2d.BrushText);
        tl->Release();
    }

    std::wstring subLine = L"Issued by: " + (!m_cert.issuerName.empty() ? m_cert.issuerName : L"—");
    IDWriteTextLayout* pl = nullptr;
    d2d.GetDWriteFactory()->CreateTextLayout(subLine.c_str(), (UINT32)subLine.length(),
        d2d.FontCaption ? d2d.FontCaption : d2d.FontBody, titleW, 20.0f, &pl);
    if (pl) {
        pl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* trimSign = nullptr;
        d2d.GetDWriteFactory()->CreateEllipsisTrimmingSign(d2d.FontCaption ? d2d.FontCaption : d2d.FontBody, &trimSign);
        pl->SetTrimming(&trim, trimSign);
        if (trimSign) trimSign->Release();
        rt->DrawTextLayout(D2D1::Point2F(textX, curY + 34.0f), pl, d2d.BrushTextMuted);
        pl->Release();
    }

    curY += 70.0f;

    auto drawPill = [&](const std::wstring& txt, float x, ID2D1SolidColorBrush* bg, ID2D1SolidColorBrush* fg) -> float {
        float pw = 16.0f + (float)txt.length() * 7.2f;
        D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(x, curY, x + pw, curY + 24.0f), 12.0f, 12.0f);
        rt->FillRoundedRectangle(r, bg);
        rt->DrawRoundedRectangle(r, d2d.BrushBorder, 1.0f);

        IDWriteTextLayout* btl = nullptr;
        d2d.GetDWriteFactory()->CreateTextLayout(txt.c_str(), (UINT32)txt.length(), d2d.FontBadge, pw, 24.0f, &btl);
        if (btl) {
            btl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            btl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextLayout(D2D1::Point2F(x, curY), btl, fg);
            btl->Release();
        }
        return pw + 8.0f;
    };

    float bx = marginX;
    bx += drawPill(L"X.509 CERTIFICATE", bx, d2d.BrushCard, d2d.BrushAccent);
    bx += drawPill(m_cert.isCurrentlyValid ? L"VALID CERTIFICATE" : L"EXPIRED CERTIFICATE", bx, d2d.BrushCard,
                   m_cert.isCurrentlyValid ? d2d.BrushGreen : d2d.BrushAmber);
    if (!m_cert.publicKeyInfo.empty()) bx += drawPill(m_cert.publicKeyInfo, bx, d2d.BrushCard, d2d.BrushTextMuted);

    curY += 38.0f;
    rt->DrawLine(D2D1::Point2F(marginX, curY), D2D1::Point2F(marginX + contentW, curY), d2d.BrushBorderSubtle, 1.0f);
    curY += 16.0f;

    std::vector<std::pair<std::wstring, std::wstring>> rows = {
        { L"Issued To", m_cert.subjectName },
        { L"Issued By", m_cert.issuerName },
        { L"Valid From", m_cert.validFromStr },
        { L"Valid To", m_cert.validToStr },
        { L"Signature Alg", m_cert.signatureAlgorithm },
        { L"Public Key", m_cert.publicKeyInfo },
        { L"Serial Number", m_cert.serialNumber },
        { L"Thumbprint (SHA-1)", m_cert.thumbprint }
    };

    float lblW = 145.0f;
    float valX = 18.0f + lblW + 12.0f;
    float valW = contentW - valX - 18.0f;

    struct MeasuredRow {
        std::wstring key;
        std::wstring val;
        IDWriteTextLayout* layout = nullptr;
        float height = 18.0f;
    };

    std::vector<MeasuredRow> mRows;
    float totalH = 46.0f;
    for (const auto& kv : rows) {
        MeasuredRow mr;
        mr.key = kv.first;
        mr.val = kv.second;

        d2d.GetDWriteFactory()->CreateTextLayout(mr.val.c_str(), (UINT32)mr.val.length(),
            d2d.FontBody, valW, 54.0f, &mr.layout);
        if (mr.layout) {
            DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            IDWriteInlineObject* trimSign = nullptr;
            d2d.GetDWriteFactory()->CreateEllipsisTrimmingSign(d2d.FontBody, &trimSign);
            mr.layout->SetTrimming(&trim, trimSign);
            if (trimSign) trimSign->Release();

            DWRITE_TEXT_METRICS tm;
            if (SUCCEEDED(mr.layout->GetMetrics(&tm))) {
                mr.height = (std::max)(18.0f, tm.height);
            }
        }
        totalH += mr.height + 10.0f;
        mRows.push_back(mr);
    }
    totalH += 8.0f;
    float cardH = (std::max)(260.0f, totalH);

    D2D1_ROUNDED_RECT r = D2D1::RoundedRect(D2D1::RectF(marginX, curY, marginX + contentW, curY + cardH), 16.0f, 16.0f);
    rt->FillRoundedRectangle(r, d2d.BrushCard);
    rt->DrawRoundedRectangle(r, d2d.BrushBorder, 1.0f);

    D2D1_RECT_F tRect = D2D1::RectF(marginX + 18.0f, curY + 14.0f, marginX + contentW - 18.0f, curY + 34.0f);
    rt->DrawText(L"CERTIFICATE DETAILS", (UINT32)wcslen(L"CERTIFICATE DETAILS"), d2d.FontBadge, tRect, d2d.BrushAccent);

    float rowY = curY + 42.0f;
    for (const auto& mr : mRows) {
        D2D1_RECT_F lblR = D2D1::RectF(marginX + 18.0f, rowY + 1.0f, marginX + 18.0f + lblW, rowY + 19.0f);
        rt->DrawText(mr.key.c_str(), (UINT32)mr.key.length(), d2d.FontCaption, lblR, d2d.BrushTextMuted);

        if (mr.layout) {
            rt->DrawTextLayout(D2D1::Point2F(marginX + valX, rowY), mr.layout, d2d.BrushText);
        }
        rowY += mr.height + 10.0f;
    }

    for (auto& mr : mRows) { if (mr.layout) mr.layout->Release(); }

    m_contentHeight = (curY + cardH + 80.0f) - targetRect.top;
}

} // namespace WinPreview
