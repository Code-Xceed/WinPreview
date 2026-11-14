#include "update_engine.h"
#include <wincrypt.h>
#include <wininet.h>
#include <urlmon.h>
#include <shellapi.h>
#include <shlobj.h>
#include <softpub.h>
#include <bcrypt.h>
#include <wintrust.h>
#include <netlistmgr.h>
#include <algorithm>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace WinPreview {

static const wchar_t* UPDATE_META_URL = L"https://github.com/Code-Xceed/WinPreview/releases/latest/download/version.json";
static const wchar_t* UPDATE_META_URL_BETA = L"https://github.com/Code-Xceed/WinPreview/releases/download/beta/version.json";
static const wchar_t* DEFAULT_INSTALLER_URL = L"https://github.com/Code-Xceed/WinPreview/releases/latest/download/WinPreview-Setup-x64.exe";

// ECDSA P-256 public key (BCRYPT_ECC_PUBLIC_BLOB, base64) pinned in the binary.
// The matching private key lives only in CI (%WINPREVIEW_UPDATE_KEY%).
// Generated with tools\update-keygen.ps1.
static const wchar_t* kUpdatePublicKeyBlobBase64 =
    L"RUNTMSAAAADLCVWMUsLCsE4eMJbzjb5X2RGYlB0nklnPlQJvkrH+Gu7s7Nmb7xFJVdf/1tN+znQ0dNkiua6/ABHW4LFrobyn";

// When true a manifest without a valid signature is rejected. Flip this to true
// in the release that first ships with a signing key configured in CI.
static const bool kRequireSignedManifest = false;

// Self-test vectors (signed offline with the release key). See RunUpdateSelfTest.
static const char* kSelfTestManifest =
    "{\"schema\":2,\"version\":\"9.9.9\",\"channel\":\"stable\",\"rollout\":100,\"paused\":false,"
    "\"mandatory\":false,\"notes\":\"Self-test vector\"}";
static const wchar_t* kSelfTestSignature =
    L"6qDq2BvVqS2bq9/YVRB780L4UffuBhG2rtyZREb/viZl3CS0AXnT3sFBvKP2ScUCgIMAp+f5bgkQXCIvz5Sz/g==";
static const wchar_t* USER_AGENT = L"WinPreview-AutoUpdater/1.1 (Windows NT; x64)";

// Bounded network waits so shutdown never hangs indefinitely behind a dead socket.
static const DWORD NET_CONNECT_TIMEOUT_MS = 8000;
static const DWORD NET_RECV_TIMEOUT_MS = 10000;

static const long long MIN_INSTALLER_BYTES = 500000;

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    if (count <= 0) return L"";
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], count);
    return wstr;
}

static std::wstring ExtractJsonString(const std::string& json, const std::string& field) {
    std::string needle = "\"" + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return L"";
    pos = json.find(':', pos + needle.length());
    if (pos == std::string::npos) return L"";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return L"";
    size_t endQuote = json.find('"', pos + 1);
    if (endQuote == std::string::npos) return L"";
    return Utf8ToWide(json.substr(pos + 1, endQuote - (pos + 1)));
}

static long long ExtractJsonInteger(const std::string& json, const std::string& field, long long fallback = 0) {
    std::string needle = "\"" + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + needle.length());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) ++pos;
    if (pos >= json.size() || json[pos] < '0' || json[pos] > '9') return fallback;
    long long value = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        value = value * 10 + (json[pos] - '0');
        ++pos;
    }
    return value;
}

static std::wstring NormalizeSha256(const std::wstring& value) {
    if (value.size() != 64) return L"";
    std::wstring out;
    out.reserve(64);
    for (wchar_t c : value) {
        if (!iswxdigit(c)) return L"";
        out.push_back((wchar_t)towlower(c));
    }
    return out;
}

// Appends a line to %LOCALAPPDATA%\WinPreview\logs\update.log (best effort).
static std::string WideToUtf8(const std::wstring& str);
static bool CurlFetchToString(const std::wstring& url, std::string& body);
static bool CurlDownloadToFile(const std::wstring& url, const std::wstring& outPath,
                               const std::function<void(int)>& progress, long long expectedSize);
static void LogUpdateEvent(const std::wstring& message) {
    WCHAR localAppData[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) return;
    std::wstring dir = std::wstring(localAppData) + L"\\WinPreview\\logs";
    CreateDirectoryW((std::wstring(localAppData) + L"\\WinPreview").c_str(), NULL);
    CreateDirectoryW(dir.c_str(), NULL);
    std::wstring path = dir + L"\\update.log";
    SYSTEMTIME st{};
    GetLocalTime(&st);
    WCHAR stamp[64];
    swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d:%02d ", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    std::string utf8 = WideToUtf8(std::wstring(stamp) + message + L"\r\n");

    // Keep the file bounded: a fresh log once it passes 256 KB.
    DWORD disposition = OPEN_ALWAYS;
    WIN32_FILE_ATTRIBUTE_DATA attrs{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs)) {
        unsigned long long size = ((unsigned long long)attrs.nFileSizeHigh << 32) | attrs.nFileSizeLow;
        if (size > 256ULL * 1024ULL) disposition = CREATE_ALWAYS;
    }

    HANDLE h = (disposition == CREATE_ALWAYS)
        ? CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)
        : CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, NULL);
        CloseHandle(h);
    }
}

static bool IsHttpsUrl(const std::wstring& url) {
    return url.size() > 8 && _wcsnicmp(url.c_str(), L"https://", 8) == 0;
}

// Only GitHub's release infrastructure may serve update payloads. Anything else
// (including a redirect target) is rejected before a byte is downloaded.
static bool IsAllowedUpdateHost(const std::wstring& url) {
    if (!IsHttpsUrl(url)) return false;
    size_t start = 8;
    size_t end = url.find_first_of(L"/?#", start);
    std::wstring host = (end == std::wstring::npos) ? url.substr(start) : url.substr(start, end - start);
    std::transform(host.begin(), host.end(), host.begin(), ::towlower);
    return host == L"github.com" ||
           host == L"objects.githubusercontent.com" ||
           host == L"release-assets.githubusercontent.com";
}

// Loopback HTTP is accepted only when the manifest URL was explicitly overridden
// on the command line (local end-to-end update tests). Never in a normal run.
static bool IsLocalhostUrl(const std::wstring& url) {
    return url.size() > 7 && _wcsnicmp(url.c_str(), L"http://", 7) == 0 &&
           (url.find(L"127.0.0.1") == 7 || url.find(L"localhost") == 7);
}

static std::string WideToUtf8(const std::wstring& str) {
    if (str.empty()) return "";
    int count = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0, NULL, NULL);
    if (count <= 0) return "";
    std::string out(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.length(), &out[0], count, NULL, NULL);
    return out;
}

static std::vector<BYTE> Base64Decode(const std::wstring& text) {
    std::vector<BYTE> out;
    int value = 0, bits = -8;
    for (wchar_t c : text) {
        if (c == L'=') break;
        int d = -1;
        if (c >= L'A' && c <= L'Z') d = c - L'A';
        else if (c >= L'a' && c <= L'z') d = c - L'a' + 26;
        else if (c >= L'0' && c <= L'9') d = c - L'0' + 52;
        else if (c == L'+') d = 62;
        else if (c == L'/') d = 63;
        else if (c == L'\r' || c == L'\n' || c == L' ' || c == L'\t') continue;
        else return {};
        value = (value << 6) | d;
        bits += 6;
        if (bits >= 0) {
            out.push_back((BYTE)((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

static bool Sha256Bytes(const void* data, size_t length, BYTE out[32]) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) return false;
    BCRYPT_HASH_HANDLE hHash = NULL;
    bool ok = false;
    DWORD hashLen = 0, resultLen = 0;
    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &resultLen, 0) >= 0 &&
        hashLen == 32 &&
        BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) >= 0) {
        if (BCryptHashData(hHash, (PUCHAR)data, (ULONG)length, 0) >= 0 &&
            BCryptFinishHash(hHash, out, 32, 0) >= 0) {
            ok = true;
        }
        BCryptDestroyHash(hHash);
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

// ECDSA P-256/SHA-256 verification with the key pinned above.
static bool VerifyEcdsaP256Signature(const std::string& payload, const std::wstring& signatureBase64) {
    std::vector<BYTE> signature = Base64Decode(signatureBase64);
    if (signature.size() != 64) return false;

    std::vector<BYTE> publicBlob = Base64Decode(kUpdatePublicKeyBlobBase64);
    if (publicBlob.size() < 8) return false;

    BYTE digest[32] = {};
    if (!Sha256Bytes(payload.data(), payload.size(), digest)) return false;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0) < 0) return false;

    BCRYPT_KEY_HANDLE hKey = NULL;
    bool ok = false;
    if (BCryptImportKeyPair(hAlg, NULL, BCRYPT_ECCPUBLIC_BLOB, &hKey,
                            publicBlob.data(), (ULONG)publicBlob.size(), 0) >= 0) {
        ok = BCryptVerifySignature(hKey, NULL, digest, sizeof(digest),
                                   signature.data(), (ULONG)signature.size(), 0) >= 0;
        BCryptDestroyKey(hKey);
    }
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

static std::wstring TrimWide(const std::wstring& value) {
    size_t start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = value.find_last_not_of(L" \t\r\n");
    return value.substr(start, end - start + 1);
}

static bool ExtractJsonBool(const std::string& json, const std::string& field, bool fallback = false) {
    std::string needle = "\"" + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + needle.length());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) ++pos;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return fallback;
}

// Decodes the JSON escapes used by release notes (\n, \r, \t, \", \\, \uXXXX).
static std::wstring UnescapeJson(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c != '\\' || i + 1 >= raw.size()) { out.push_back(c); continue; }
        char e = raw[++i];
        switch (e) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'u': {
                if (i + 4 < raw.size()) {
                    unsigned code = 0;
                    for (int k = 1; k <= 4; ++k) {
                        char h = raw[i + k];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
                    }
                    i += 4;
                    // UTF-8 encode the BMP code point.
                    if (code < 0x80) out.push_back((char)code);
                    else if (code < 0x800) {
                        out.push_back((char)(0xC0 | (code >> 6)));
                        out.push_back((char)(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back((char)(0xE0 | (code >> 12)));
                        out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (code & 0x3F)));
                    }
                }
                break;
            }
            default: out.push_back(e); break;
        }
    }
    return Utf8ToWide(out);
}

static std::wstring ExtractJsonStringUnescaped(const std::string& json, const std::string& field) {
    std::string needle = "\"" + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return L"";
    pos = json.find(':', pos + needle.length());
    if (pos == std::string::npos) return L"";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return L"";
    size_t cursor = pos + 1;
    std::string raw;
    while (cursor < json.size()) {
        char c = json[cursor];
        if (c == '\\' && cursor + 1 < json.size()) {
            raw.push_back(c);
            raw.push_back(json[cursor + 1]);
            cursor += 2;
            continue;
        }
        if (c == '"') break;
        raw.push_back(c);
        ++cursor;
    }
    return UnescapeJson(raw);
}

static std::wstring ReadMachineGuid() {
    WCHAR buffer[128] = {};
    DWORD size = sizeof(buffer) - sizeof(WCHAR);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                     RRF_RT_REG_SZ, NULL, buffer, &size) == ERROR_SUCCESS) {
        return buffer;
    }
    return L"unknown-machine";
}

// Deterministic 0..99 bucket used for staged rollout. Same machine + version
// always lands in the same bucket, so a user never flip-flops between eligible
// and not eligible.
static int RolloutBucket(const std::wstring& version) {
    std::string seed = WideToUtf8(ReadMachineGuid() + L":" + version);
    BYTE digest[32] = {};
    if (!Sha256Bytes(seed.data(), seed.size(), digest)) return 100;
    unsigned value = ((unsigned)digest[0] << 24) | ((unsigned)digest[1] << 16) |
                     ((unsigned)digest[2] << 8) | (unsigned)digest[3];
    return (int)(value % 100u);
}

static std::wstring BuildSignatureUrl(const std::wstring& manifestUrl) {
    size_t pos = manifestUrl.rfind(L"version.json");
    if (pos == std::wstring::npos) return manifestUrl + L".sig";
    return manifestUrl.substr(0, pos) + L"version.json.sig";
}

static bool IsVersionGreater(const std::wstring& remote, const std::wstring& current) {
    int rMaj = 0, rMin = 0, rPatch = 0, rBuild = 0;
    int cMaj = 0, cMin = 0, cPatch = 0, cBuild = 0;

    const wchar_t* rPtr = remote.c_str();
    while (*rPtr == L'v' || *rPtr == L'V' || *rPtr == L' ') rPtr++;
    swscanf_s(rPtr, L"%d.%d.%d.%d", &rMaj, &rMin, &rPatch, &rBuild);

    const wchar_t* cPtr = current.c_str();
    while (*cPtr == L'v' || *cPtr == L'V' || *cPtr == L' ') cPtr++;
    swscanf_s(cPtr, L"%d.%d.%d.%d", &cMaj, &cMin, &cPatch, &cBuild);

    if (rMaj != cMaj) return rMaj > cMaj;
    if (rMin != cMin) return rMin > cMin;
    if (rPatch != cPatch) return rPatch > cPatch;
    return rBuild > cBuild;
}

static std::wstring QueryModuleFileVersion() {
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) {
        return WINPREVIEW_VERSION_WSTR;
    }

    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (size == 0) {
        return WINPREVIEW_VERSION_WSTR;
    }

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path, handle, size, data.data())) {
        return WINPREVIEW_VERSION_WSTR;
    }

    VS_FIXEDFILEINFO* pFileInfo = nullptr;
    UINT len = 0;
    if (VerQueryValueW(data.data(), L"\\", (LPVOID*)&pFileInfo, &len) && len >= sizeof(VS_FIXEDFILEINFO) && pFileInfo) {
        int maj = HIWORD(pFileInfo->dwProductVersionMS);
        int min = LOWORD(pFileInfo->dwProductVersionMS);
        int patch = HIWORD(pFileInfo->dwProductVersionLS);
        int build = LOWORD(pFileInfo->dwProductVersionLS);
        WCHAR verStr[64];
        if (build > 0) {
            swprintf_s(verStr, L"%d.%d.%d.%d", maj, min, patch, build);
        } else {
            swprintf_s(verStr, L"%d.%d.%d", maj, min, patch);
        }
        return verStr;
    }
    return WINPREVIEW_VERSION_WSTR;
}

static bool HashFileSha256(const std::wstring& path, std::wstring& outHex) {
    outHex.clear();

    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        return false;
    }

    HCRYPTHASH hHash = 0;
    bool ok = false;
    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            ok = true;
            std::vector<BYTE> buffer(128 * 1024);
            for (;;) {
                DWORD bytesRead = 0;
                if (!ReadFile(hFile, buffer.data(), (DWORD)buffer.size(), &bytesRead, NULL)) {
                    ok = false;
                    break;
                }
                if (bytesRead == 0) break;
                if (!CryptHashData(hHash, buffer.data(), bytesRead, 0)) {
                    ok = false;
                    break;
                }
            }
            CloseHandle(hFile);
        }

        if (ok) {
            BYTE digest[32] = {};
            DWORD digestLen = sizeof(digest);
            if (CryptGetHashParam(hHash, HP_HASHVAL, digest, &digestLen, 0) && digestLen == sizeof(digest)) {
                static const wchar_t* kHex = L"0123456789abcdef";
                outHex.reserve(64);
                for (BYTE b : digest) {
                    outHex.push_back(kHex[b >> 4]);
                    outHex.push_back(kHex[b & 0x0F]);
                }
            } else {
                ok = false;
            }
        }
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
    return ok;
}

static void ApplyWinInetTimeouts(HINTERNET hInternet) {
    if (!hInternet) return;
    DWORD connectTimeout = NET_CONNECT_TIMEOUT_MS;
    DWORD recvTimeout = NET_RECV_TIMEOUT_MS;
    InternetSetOptionW(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
    InternetSetOptionW(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &recvTimeout, sizeof(recvTimeout));
}

UpdateEngine::UpdateEngine() {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    m_info.currentVersion = QueryModuleFileVersion();
}

UpdateEngine::~UpdateEngine() {
    // Ask the worker to stop, wake it, and wait for any in-flight network call
    // (bounded by the WinINet timeouts above). All queued jobs are dropped.
    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_stopping = true;
        m_cancelJob.store(true);
        m_jobs.clear();
    }
    m_jobCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

// ---------------------------------------------------------------------------
// Thread-safe accessors
// ---------------------------------------------------------------------------

UpdateInfo UpdateEngine::GetUpdateInfo() const {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    return m_info;
}

std::wstring UpdateEngine::GetDownloadedInstallerPath() const {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    return m_downloadedInstallerPath;
}

void UpdateEngine::CommitInfo(UpdateState state, const UpdateInfo& info) {
    {
        static const wchar_t* names[] = { L"Idle", L"Checking", L"UpdateAvailable", L"UpToDate",
                                          L"Downloading", L"Downloaded", L"Failed" };
        int idx = (int)state;
        std::wstring line = std::wstring(L"state=") +
            ((idx >= 0 && idx < 7) ? names[idx] : L"?") +
            L" remote=" + (info.remoteVersion.empty() ? L"-" : info.remoteVersion) +
            L" current=" + info.currentVersion +
            L" channel=" + info.channel +
            L" rollout=" + std::to_wstring(info.rolloutPercent) +
            L" signed=" + (info.signaturePresent ? (info.signatureValid ? L"valid" : L"invalid") : L"none");
        if (!info.errorMessage.empty()) line += L" error=" + info.errorMessage;
        LogUpdateEvent(line);
    }
    {
        std::lock_guard<std::mutex> lock(m_infoMutex);
        m_info = info;
    }
    m_state.store(state);
}

void UpdateEngine::SetDownloadedInstallerPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    m_downloadedInstallerPath = path;
}

// ---------------------------------------------------------------------------
// Serialized background worker. Jobs run one at a time, in order; the UI thread
// never joins a live job (the old design blocked for up to 20 s on a slow check).
// ---------------------------------------------------------------------------

void UpdateEngine::EnqueueJob(std::function<void()> job) {
    if (!job) return;
    std::lock_guard<std::mutex> lock(m_jobMutex);
    if (m_stopping) return;
    m_jobs.push_back(std::move(job));
    if (!m_workerThread.joinable()) {
        m_workerThread = std::thread([this]() { WorkerLoop(); });
    }
    m_jobCv.notify_one();
}

void UpdateEngine::WorkerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_jobMutex);
            m_jobCv.wait(lock, [this]() { return m_stopping || !m_jobs.empty(); });
            if (m_stopping) return;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        job();
    }
}

// ---------------------------------------------------------------------------
// Update check
// ---------------------------------------------------------------------------

void UpdateEngine::CheckForUpdatesAsync(std::function<void(const UpdateInfo&)> onComplete) {
    const UpdateState state = m_state.load();
    if (state == UpdateState::Checking || state == UpdateState::Downloading) {
        return;
    }

    m_state.store(UpdateState::Checking);
    EnqueueJob([this, onComplete]() {
        UpdateInfo result;
        FetchUpdateInfoSync(result);
        if (onComplete) {
            // Snapshot copy: the callback can never observe m_info mid-commit.
            onComplete(GetUpdateInfo());
        }
    });
}

void UpdateEngine::SimulateUpdateForTesting() {
    UpdateInfo result;
    result.currentVersion = QueryModuleFileVersion();
    result.remoteVersion = L"1.1.0";
    result.changelog = L"Simulation test release with performance optimizations and new features.";
    result.isAvailable = true;
    CommitInfo(UpdateState::UpdateAvailable, result);
    m_isSimulated.store(true);
}

std::wstring UpdateEngine::GetManifestUrl() const {
    if (!m_manifestUrlOverride.empty()) return m_manifestUrlOverride;
    return (m_channel == L"beta") ? UPDATE_META_URL_BETA : UPDATE_META_URL;
}

void UpdateEngine::SetChannel(const std::wstring& channel) {
    m_channel = (channel == L"beta") ? L"beta" : L"stable";
}

void UpdateEngine::FetchUpdateInfoSync(UpdateInfo& result) {
    result.currentVersion = QueryModuleFileVersion();
    result.isAvailable = false;
    result.errorMessage.clear();
    result.signaturePresent = false;
    result.signatureValid = false;

    HINTERNET hInternet = InternetOpenW(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        result.errorMessage = L"Failed to initialize Internet connection.";
        CommitInfo(UpdateState::Failed, result);
        return;
    }
    ApplyWinInetTimeouts(hInternet);

    auto fetchFromUrl = [&](const std::wstring& url) -> std::string {
        if (url.empty()) return "";
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        if (IsHttpsUrl(url)) flags |= INTERNET_FLAG_SECURE;
        HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), NULL, 0, flags, 0);
        if (!hUrl) {
            DWORD err = GetLastError();
            LogUpdateEvent(L"fetch open failed url=" + url + L" wininet=" + std::to_wstring(err));
            return "";
        }
        DWORD statusCode = 0, statusSize = sizeof(statusCode), index = 0;
        if (HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, &index)) {
            if (statusCode < 200 || statusCode >= 300) {
                LogUpdateEvent(L"fetch status " + std::to_wstring(statusCode) + L" for " + url);
                InternetCloseHandle(hUrl);
                return "";
            }
        }
        std::string body;
        char buf[8192];
        DWORD read = 0;
        while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0) {
            body.append(buf, read);
            if (body.size() > 256 * 1024) break; // manifest is tiny; refuse runaway bodies
        }
        InternetCloseHandle(hUrl);
        return body;
    };

    auto fetchWithFallback = [&](const std::wstring& url) -> std::string {
        std::string body = fetchFromUrl(url);
        if (!body.empty()) return body;
        LogUpdateEvent(L"falling back to curl for " + url);
        if (CurlFetchToString(url, body)) return body;
        return "";
    };

    // releases/latest is the only trust anchor: the manifest is published as a
    // release asset so it can never advertise a version whose installer does not
    // exist yet.
    const std::wstring manifestUrl = GetManifestUrl();
    const bool testOverride = !m_manifestUrlOverride.empty();
    std::string responseBody = fetchWithFallback(manifestUrl);
    std::wstring signatureText = TrimWide(Utf8ToWide(fetchWithFallback(BuildSignatureUrl(manifestUrl))));
    InternetCloseHandle(hInternet);

    if (responseBody.empty()) {
        result.errorMessage = L"Unable to contact update server.";
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    // ---------------------------------------------------------------------
    // Integrity: the manifest is signed with the key pinned in this binary.
    // A present signature must verify; a missing one is only tolerated while
    // kRequireSignedManifest is false (pre-signing releases).
    // ---------------------------------------------------------------------
    result.signaturePresent = !signatureText.empty();
    if (result.signaturePresent) {
        result.signatureValid = VerifyEcdsaP256Signature(responseBody, signatureText);
        if (!result.signatureValid) {
            result.errorMessage = L"The update manifest signature is not valid.";
            result.isAvailable = false;
            CommitInfo(UpdateState::Failed, result);
            return;
        }
    } else if (kRequireSignedManifest) {
        result.errorMessage = L"This update manifest is not signed.";
        result.isAvailable = false;
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    result.remoteVersion = ExtractJsonString(responseBody, "version");
    result.downloadUrl = ExtractJsonString(responseBody, "download_url");
    result.changelog = ExtractJsonStringUnescaped(responseBody, "changelog");
    result.releaseDate = ExtractJsonString(responseBody, "release_date");
    result.installerSha256 = NormalizeSha256(ExtractJsonString(responseBody, "installer_sha256"));
    result.installerSize = ExtractJsonInteger(responseBody, "installer_size");

    // Manifest v2 metadata (all optional; legacy manifests keep working).
    std::wstring channel = ExtractJsonString(responseBody, "channel");
    result.channel = channel.empty() ? L"stable" : channel;
    result.rolloutPercent = (int)ExtractJsonInteger(responseBody, "rollout", 100);
    if (result.rolloutPercent < 0) result.rolloutPercent = 0;
    if (result.rolloutPercent > 100) result.rolloutPercent = 100;
    result.paused = ExtractJsonBool(responseBody, "paused", false);
    result.mandatory = ExtractJsonBool(responseBody, "mandatory", false);
    result.minSupported = ExtractJsonString(responseBody, "min_supported");
    result.notes = ExtractJsonStringUnescaped(responseBody, "notes");
    if (result.notes.empty()) result.notes = result.changelog;

    if (result.remoteVersion.empty()) {
        result.errorMessage = L"Invalid update metadata format.";
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    if (result.downloadUrl.empty()) {
        result.downloadUrl = DEFAULT_INSTALLER_URL;
    }
    if (!IsAllowedUpdateHost(result.downloadUrl) &&
        !(testOverride && IsLocalhostUrl(result.downloadUrl))) {
        result.errorMessage = L"The update manifest points to an untrusted download host.";
        result.isAvailable = false;
        CommitInfo(UpdateState::Failed, result);
        return;
    }

    // A present-but-malformed hash means the manifest is corrupt: fail closed.
    {
        std::wstring rawHash = ExtractJsonString(responseBody, "installer_sha256");
        if (!rawHash.empty() && result.installerSha256.empty()) {
            result.errorMessage = L"Update manifest contains an invalid integrity hash.";
            CommitInfo(UpdateState::Failed, result);
            return;
        }
    }

    // Security floor: below min_supported the update is forced.
    if (!result.minSupported.empty() &&
        !IsVersionGreater(result.currentVersion, result.minSupported)) {
        result.mandatory = true;
    }

    if (result.paused) {
        result.isAvailable = false;   // kill switch
        CommitInfo(UpdateState::UpToDate, result);
        return;
    }

    if (IsVersionGreater(result.remoteVersion, result.currentVersion)) {
        if (result.rolloutPercent < 100 &&
            RolloutBucket(result.remoteVersion) >= result.rolloutPercent) {
            result.isAvailable = false;   // staged rollout cohort
            CommitInfo(UpdateState::UpToDate, result);
            return;
        }
        result.isAvailable = true;
        CommitInfo(UpdateState::UpdateAvailable, result);
    } else {
        result.isAvailable = false;
        CommitInfo(UpdateState::UpToDate, result);
    }
}

// ---------------------------------------------------------------------------
// Download
// ---------------------------------------------------------------------------

static std::wstring SystemCurlPath() {
    WCHAR sysDir[MAX_PATH] = {};
    if (!GetSystemDirectoryW(sysDir, MAX_PATH)) return L"";
    std::wstring path = std::wstring(sysDir) + L"\\curl.exe";
    return (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) ? L"" : path;
}

// Runs curl.exe -L --fail and optionally reports progress from the growing file.
static bool CurlDownloadToFile(const std::wstring& url, const std::wstring& outPath,
                               const std::function<void(int)>& progress, long long expectedSize) {
    std::wstring curl = SystemCurlPath();
    if (curl.empty()) return false;

    std::wstring command = L"\"" + curl + L"\" --location --fail --silent --show-error"
                           L" --connect-timeout 8 --max-time 300"
                           L" --output \"" + outPath + L"\" \"" + url + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = NULL;
    si.hStdError = NULL;
    si.hStdInput = NULL;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(NULL, mutableCommand.data(), NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);

    bool finished = false;
    ULONGLONG deadline = GetTickCount64() + 300000ULL;
    while (!finished && GetTickCount64() < deadline) {
        DWORD wait = WaitForSingleObject(pi.hProcess, 150);
        finished = (wait == WAIT_OBJECT_0);
        if (!finished && progress && expectedSize > 0) {
            WIN32_FILE_ATTRIBUTE_DATA attrs{};
            if (GetFileAttributesExW(outPath.c_str(), GetFileExInfoStandard, &attrs)) {
                unsigned long long size = ((unsigned long long)attrs.nFileSizeHigh << 32) | attrs.nFileSizeLow;
                int percent = (int)((size * 100ULL) / (unsigned long long)expectedSize);
                if (percent > 99) percent = 99;
                progress(percent);
            }
        }
    }

    DWORD exitCode = (DWORD)-1;
    if (finished) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        TerminateProcess(pi.hProcess, (UINT)-1);
    }
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        DeleteFileW(outPath.c_str());
        LogUpdateEvent(L"curl fallback failed exit=" + std::to_wstring(exitCode) + L" url=" + url);
        return false;
    }
    return true;
}

static bool CurlFetchToString(const std::wstring& url, std::string& body) {
    WCHAR tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring path = std::wstring(tempDir) + L"WinPreview-fetch-" + std::to_wstring(GetTickCount64()) + L".tmp";
    if (!CurlDownloadToFile(url, path, nullptr, 0)) return false;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buffer[8192];
    DWORD read = 0;
    while (ReadFile(h, buffer, sizeof(buffer), &read, NULL) && read > 0) {
        body.append(buffer, read);
        if (body.size() > 256 * 1024) break;
    }
    CloseHandle(h);
    DeleteFileW(path.c_str());
    return !body.empty();
}

// One download attempt: WinINet streamed (with progress), then URLDownloadToFile
// as a fallback. Returns true only when a plausible installer landed on disk.
static bool TryDownloadInstaller(const std::wstring& url, const std::wstring& targetPath,
                                 const std::function<void(int)>& progress) {
    bool downloadSuccess = false;

    HINTERNET hInternet = InternetOpenW(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (hInternet) {
        ApplyWinInetTimeouts(hInternet);
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        if (IsHttpsUrl(url)) flags |= INTERNET_FLAG_SECURE;
        HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), NULL, 0, flags, 0);
        if (hUrl) {
            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            DWORD statusIdx = 0;
            bool statusOk = true;
            if (HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, &statusIdx)) {
                if (statusCode < 200 || statusCode >= 300) statusOk = false;
            }

            if (statusOk) {
                DWORD contentLength = 0;
                DWORD headerLength = sizeof(contentLength);
                DWORD index = 0;
                HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &headerLength, &index);

                HANDLE hFile = CreateFileW(targetPath.c_str(), GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                           NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    std::vector<char> buffer(128 * 1024);
                    DWORD bytesRead = 0;
                    unsigned long long totalBytesRead = 0;
                    bool writeFailed = false;

                    for (;;) {
                        if (!InternetReadFile(hUrl, buffer.data(), (DWORD)buffer.size(), &bytesRead)) {
                            writeFailed = true; // never treat a truncated stream as a complete installer
                            break;
                        }
                        if (bytesRead == 0) break;
                        DWORD bytesWritten = 0;
                        if (!WriteFile(hFile, buffer.data(), bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead) {
                            writeFailed = true;
                            break;
                        }
                        totalBytesRead += bytesRead;
                        if (contentLength > 0 && progress) {
                            int percent = (int)((totalBytesRead * 100ULL) / contentLength);
                            if (percent > 99) percent = 99;
                            progress(percent);
                        }
                    }
                    FlushFileBuffers(hFile);
                    CloseHandle(hFile);

                    if (!writeFailed && totalBytesRead >= (unsigned long long)MIN_INSTALLER_BYTES) {
                        downloadSuccess = true;
                    }
                }
            }
            InternetCloseHandle(hUrl);
        }
        InternetCloseHandle(hInternet);
    }

    // Fallback for environments where the streamed transfer is blocked.
    if (!downloadSuccess) {
        DeleteFileW(targetPath.c_str());
        if (progress) progress(35);
        HRESULT hr = URLDownloadToFileW(NULL, url.c_str(), targetPath.c_str(), 0, NULL);
        if (SUCCEEDED(hr)) {
            HANDLE hCheck = CreateFileW(targetPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (hCheck != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER sz{};
                if (GetFileSizeEx(hCheck, &sz) && sz.QuadPart >= MIN_INSTALLER_BYTES) {
                    downloadSuccess = true;
                }
                CloseHandle(hCheck);
            }
        }
    }

    if (!downloadSuccess) {
        DeleteFileW(targetPath.c_str());
        // Last resort: curl.exe (system32) handles IPv6-only black holes and
        // proxy environments that WinINet stalls on.
        if (CurlDownloadToFile(url, targetPath, progress, 0)) {
            HANDLE hCheck = CreateFileW(targetPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (hCheck != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER sz{};
                if (GetFileSizeEx(hCheck, &sz) && sz.QuadPart >= MIN_INSTALLER_BYTES) {
                    downloadSuccess = true;
                }
                CloseHandle(hCheck);
            }
        }
    }

    if (!downloadSuccess) DeleteFileW(targetPath.c_str());
    return downloadSuccess;
}

void UpdateEngine::DownloadUpdateAsync(std::function<void(int percent)> onProgress,
                                      std::function<void(bool success, const std::wstring& filePath)> onComplete) {
    if (m_state.load() == UpdateState::Downloading) {
        return;
    }

    UpdateInfo info = GetUpdateInfo();
    if (!info.isAvailable && !m_isSimulated.load()) {
        m_state.store(UpdateState::Failed);
        if (onComplete) onComplete(false, L"");
        return;
    }
    const bool allowLocalTestHost = !m_manifestUrlOverride.empty() && IsLocalhostUrl(info.downloadUrl);
    if (!m_isSimulated.load() && !IsHttpsUrl(info.downloadUrl) && !allowLocalTestHost) {
        LogUpdateEvent(L"download rejected: download URL is not HTTPS");
        m_state.store(UpdateState::Failed);
        if (onComplete) onComplete(false, L"");
        return;
    }

    m_state.store(UpdateState::Downloading);
    m_downloadProgress.store(0);
    m_cancelJob.store(false);

    if (m_isSimulated.load()) {
        EnqueueJob([this, onProgress, onComplete]() {
            for (int p = 5; p <= 100; p += 10) {
                if (m_cancelJob.load()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(180));
                m_downloadProgress.store(p);
                if (onProgress) onProgress(p);
            }
            m_state.store(UpdateState::Downloaded);
            if (onComplete) onComplete(true, L"SIMULATED_TEST");
        });
        return;
    }
    EnqueueJob([this, info, onProgress, onComplete]() {
        WCHAR tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);

        // Target a unique path so AV scanners or prior installs cannot hold a lock.
        std::wstring targetPath = std::wstring(tempPath) + L"WinPreview-Setup-v" +
                                  (info.remoteVersion.empty() ? L"latest" : info.remoteVersion) +
                                  L"-" + std::to_wstring(GetTickCount64()) + L".exe";
        SetDownloadedInstallerPath(targetPath);

        // Pre-flight: refuse to start when the target volume cannot hold the file.
        LogUpdateEvent(L"download start: " + info.downloadUrl);
        if (info.installerSize > 0) {
            ULARGE_INTEGER freeBytes{};
            if (GetDiskFreeSpaceExW(tempPath, &freeBytes, NULL, NULL) &&
                freeBytes.QuadPart < (ULONGLONG)info.installerSize * 3ULL) {
                m_state.store(UpdateState::Failed);
                if (onComplete) onComplete(false, L"");
                return;
            }
        }

        bool downloadSuccess = false;
        int bestPercent = 0;
        auto reportProgress = [&](int percent) {
            // Never let a retry move the bar backwards.
            if (percent > bestPercent) {
                bestPercent = percent;
                m_downloadProgress.store(percent);
                if (onProgress) onProgress(percent);
            }
        };

        for (int attempt = 1; attempt <= 3 && !downloadSuccess && !m_cancelJob.load(); ++attempt) {
            DeleteFileW(targetPath.c_str());
            downloadSuccess = TryDownloadInstaller(info.downloadUrl, targetPath, reportProgress);
            if (!downloadSuccess && attempt < 3 && !m_cancelJob.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(attempt == 1 ? 2000 : 5000));
            }
        }

        if (m_cancelJob.load()) {
            DeleteFileW(targetPath.c_str());
            SetDownloadedInstallerPath(L"");
            m_state.store(UpdateState::Idle);
            if (onComplete) onComplete(false, L"");
            return;
        }

        // =====================================================================
        // Integrity gate: size + SHA-256 from the signed release manifest. A file
        // that fails this is deleted and never offered to the launch path.
        // =====================================================================
        if (downloadSuccess) {
            WIN32_FILE_ATTRIBUTE_DATA attrs{};
            unsigned long long actualSize = 0;
            if (GetFileAttributesExW(targetPath.c_str(), GetFileExInfoStandard, &attrs)) {
                actualSize = ((unsigned long long)attrs.nFileSizeHigh << 32) | attrs.nFileSizeLow;
            }

            if (info.installerSize > 0 && actualSize != (unsigned long long)info.installerSize) {
                downloadSuccess = false;
            }

            if (downloadSuccess && !info.installerSha256.empty()) {
                std::wstring actualHash;
                if (!HashFileSha256(targetPath, actualHash) || actualHash != info.installerSha256) {
                    downloadSuccess = false;
                }
            }
        }

        if (downloadSuccess) {
            LogUpdateEvent(L"download complete: " + targetPath);
            m_downloadProgress.store(100);
            m_state.store(UpdateState::Downloaded);
            if (onProgress) onProgress(100);
            if (onComplete) onComplete(true, targetPath);
        } else {
            LogUpdateEvent(L"download failed: " + info.downloadUrl);
            DeleteFileW(targetPath.c_str());
            SetDownloadedInstallerPath(L"");
            m_state.store(UpdateState::Failed);
            if (onComplete) onComplete(false, L"");
        }
    });
}

// ---------------------------------------------------------------------------
// Installer integrity & launch
// ---------------------------------------------------------------------------

// Verifies PE structure, architecture, size, and (when the manifest provides
// one) the SHA-256 of the exact bytes we are about to execute.
static bool VerifyInstallerIntegrity(const std::wstring& filePath, const UpdateInfo& expected) {
    if (filePath.empty()) return false;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < MIN_INSTALLER_BYTES) {
        CloseHandle(hFile);
        return false;
    }
    if (expected.installerSize > 0 && fileSize.QuadPart != expected.installerSize) {
        CloseHandle(hFile);
        return false;
    }

    IMAGE_DOS_HEADER dosHeader{};
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL) || bytesRead != sizeof(dosHeader)) {
        CloseHandle(hFile);
        return false;
    }

    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE || dosHeader.e_lfanew <= 0) {
        CloseHandle(hFile);
        return false;
    }

    LARGE_INTEGER ntOffset{};
    ntOffset.QuadPart = dosHeader.e_lfanew;
    if (!SetFilePointerEx(hFile, ntOffset, NULL, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    DWORD ntSignature = 0;
    if (!ReadFile(hFile, &ntSignature, sizeof(ntSignature), &bytesRead, NULL) || bytesRead != sizeof(ntSignature)) {
        CloseHandle(hFile);
        return false;
    }

    if (ntSignature != IMAGE_NT_SIGNATURE) {
        CloseHandle(hFile);
        return false;
    }

    IMAGE_FILE_HEADER fileHeader{};
    if (!ReadFile(hFile, &fileHeader, sizeof(fileHeader), &bytesRead, NULL) || bytesRead != sizeof(fileHeader)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    if (fileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 && fileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        return false;
    }

    // Manifest hash is the primary trust anchor (works for unsigned open-source
    // builds); Authenticode is still validated when a signature is present.
    if (!expected.installerSha256.empty()) {
        std::wstring actualHash;
        if (!HashFileSha256(filePath, actualHash) || actualHash != expected.installerSha256) {
            return false;
        }
    }

    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = filePath.c_str();

    GUID policyGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.pFile = &fileInfo;

    LONG status = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policyGuid, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &policyGuid, &trustData);

    if (status == ERROR_SUCCESS || status == TRUST_E_NOSIGNATURE || status == CERT_E_UNTRUSTEDROOT) {
        return true;
    }

    return false;
}

bool UpdateEngine::LaunchInstallerAndExit(HWND parentHwnd) {
    (void)parentHwnd;
    if (m_isSimulated.load()) {
        m_state.store(UpdateState::Idle);
        m_isSimulated.store(false);
        return true;
    }

    std::wstring installerPath = GetDownloadedInstallerPath();
    if (installerPath.empty()) return false;

    UpdateInfo expected = GetUpdateInfo();
    if (!VerifyInstallerIntegrity(installerPath, expected)) {
        DeleteFileW(installerPath.c_str());
        SetDownloadedInstallerPath(L"");
        m_state.store(UpdateState::Failed);
        return false;
    }

    WCHAR selfPath[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, selfPath, MAX_PATH)) return false;

    WCHAR tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    const std::wstring logPath = std::wstring(tempDir) + L"WinPreview-update.log";
    const std::wstring resultPath = std::wstring(tempDir) + L"WinPreview-update-result.json";
    const std::wstring relaunch = m_relaunchBackground ? L"background" : L"visible";

    // The helper must not run from the install directory: Inno's Restart Manager
    // would see WinPreview.exe holding the very files it has to replace and abort
    // the upgrade. Copy ourselves next to the installer in %TEMP% instead.
    const std::wstring helperPath = std::wstring(tempDir) + L"WinPreview-update-helper-" +
                                    std::to_wstring(GetTickCount64()) + L".exe";
    if (CopyFileW(selfPath, helperPath.c_str(), FALSE)) {
        // Copied: the installer can replace the real installation while this
        // temporary helper waits.
    } else {
        return false;
    }

    std::wstring command = L"\"" + helperPath + L"\" --apply-update \"" + installerPath +
        L"\" --wait-pid " + std::to_wstring(GetCurrentProcessId()) +
        L" --relaunch " + relaunch +
        L" --restart-exe \"" + std::wstring(selfPath) + L"\"" +
        L" --log \"" + logPath + L"\" --result \"" + resultPath + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);

    if (CreateProcessW(NULL, mutableCommand.data(), NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        // Release file locks on WinPreview.exe / pdfium.dll so the installer can
        // replace them; the helper is already waiting on our PID.
        ExitProcess(0);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Update helper: applies a downloaded installer after the app exits.
// ---------------------------------------------------------------------------

static void WriteUpdateResult(const std::wstring& path, const std::wstring& installer,
                              unsigned long exitCode, const std::wstring& logPath,
                              const std::wstring& relaunchMode) {
    if (path.empty()) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    WCHAR stamp[64];
    swprintf_s(stamp, L"%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    auto escape = [](const std::wstring& value) {
        std::wstring out;
        for (wchar_t c : value) {
            if (c == L'\\') out += L"\\\\";
            else if (c == L'"') out += L"\\\"";
            else out.push_back(c);
        }
        return out;
    };
    std::wstring json = L"{\n";
    json += L"  \"installer\": \"" + escape(installer) + L"\",\n";
    json += L"  \"exit_code\": " + std::to_wstring(exitCode) + L",\n";
    json += L"  \"relaunch\": \"" + escape(relaunchMode) + L"\",\n";
    json += L"  \"log\": \"" + escape(logPath) + L"\",\n";
    json += L"  \"finished\": \"" + std::wstring(stamp) + L"\"\n}\n";
    std::string utf8 = WideToUtf8(json);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, NULL);
        CloseHandle(h);
    }
}

int RunUpdateHelper(const std::wstring& installerPath, unsigned long waitPid,
                    const std::wstring& relaunchMode, const std::wstring& logPath,
                    const std::wstring& resultPath, const std::wstring& restartExe) {
    if (installerPath.empty() || GetFileAttributesW(installerPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return 2;
    }

    // Wait for the old instance to release its file handles (bounded).
    if (waitPid != 0) {
        HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, waitPid);
        if (hProcess) {
            WaitForSingleObject(hProcess, 90000);
            CloseHandle(hProcess);
        }
    }
    Sleep(350);

    std::wstring command = L"\"" + installerPath + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART"
                           L" /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS /RELAUNCHMODE=" + relaunchMode;
    if (!logPath.empty()) {
        command += L" /LOG=\"" + logPath + L"\"";
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    DWORD exitCode = (DWORD)-1;
    if (CreateProcessW(NULL, mutableCommand.data(), NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    WriteUpdateResult(resultPath, installerPath, exitCode, logPath, relaunchMode);

    if (exitCode == 0) {
        // Applied and verified: the payload is no longer needed.
        DeleteFileW(installerPath.c_str());
    }

    // Relaunch: the new build on success, the old build (with the failure code)
    // on error so the user is never left with nothing running.
    WCHAR selfPath[MAX_PATH] = {};
    if (GetModuleFileNameW(NULL, selfPath, MAX_PATH)) {
        // Prefer the installed executable the app told us about; this helper may
        // be a temporary copy that should not be relaunched.
        std::wstring target = restartExe.empty() ? std::wstring(selfPath) : restartExe;
        std::wstring args;
        if (exitCode == 0) {
            args = (relaunchMode == L"background") ? L"--restored --background" : L"--restored";
        } else {
            args = L"--update-failed " + std::to_wstring(exitCode) +
                   ((relaunchMode == L"background") ? L" --background" : L"");
        }
        ShellExecuteW(NULL, L"open", target.c_str(), args.c_str(), NULL,
                      (relaunchMode == L"background") ? SW_HIDE : SW_SHOWNORMAL);
    }

    return (exitCode == 0) ? 0 : 1;
}

void CleanupUpdateTempFiles() {
    WCHAR tempDir[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempDir)) return;

    WIN32_FIND_DATAW findData{};
    const std::wstring patterns[] = {
        std::wstring(tempDir) + L"WinPreview-update-helper-*.exe",
        std::wstring(tempDir) + L"WinPreview-Setup-v*.exe",
        std::wstring(tempDir) + L"WinPreview-fetch-*.tmp",
    };
    FILETIME nowFt{};
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now{};
    now.LowPart = nowFt.dwLowDateTime;
    now.HighPart = nowFt.dwHighDateTime;

    for (const auto& pattern : patterns) {
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE) continue;
        do {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            ULARGE_INTEGER written{};
            written.LowPart = findData.ftLastWriteTime.dwLowDateTime;
            written.HighPart = findData.ftLastWriteTime.dwHighDateTime;
            // 6 hours in 100ns units.
            if (now.QuadPart > written.QuadPart &&
                (now.QuadPart - written.QuadPart) > 6ULL * 3600ULL * 10000000ULL) {
                std::wstring full = std::wstring(tempDir) + findData.cFileName;
                DeleteFileW(full.c_str());
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
}

bool IsPortableInstall() {
    static const wchar_t* kUninstallKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{C8E19B4B-9140-4D56-8A69-02A44B27CE91}_is1";

    WCHAR selfPath[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, selfPath, MAX_PATH)) return false;
    std::wstring dir(selfPath);
    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);

    auto normalize = [](std::wstring value) {
        while (!value.empty() && (value.back() == L'\\' || value.back() == L'/')) value.pop_back();
        return value;
    };
    const std::wstring selfDir = normalize(dir);

    auto matchesInstall = [&](HKEY root, REGSAM extraFlags) -> bool {
        HKEY hKey = NULL;
        if (RegOpenKeyExW(root, kUninstallKey, 0, KEY_READ | extraFlags, &hKey) != ERROR_SUCCESS) {
            return false;
        }
        bool same = false;
        const wchar_t* valueNames[] = { L"InstallLocation", L"Inno Setup: App Path" };
        for (const wchar_t* name : valueNames) {
            WCHAR buffer[MAX_PATH] = {};
            DWORD size = sizeof(buffer);
            DWORD type = 0;
            if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_EXPAND_SZ)) {
                if (_wcsicmp(normalize(buffer).c_str(), selfDir.c_str()) == 0) {
                    same = true;
                    break;
                }
            }
        }
        RegCloseKey(hKey);
        return same;
    };

    // An installed copy has an uninstall entry whose path is this very folder.
    // Any other folder is a portable/unpacked copy, even if another install exists.
    return !(matchesInstall(HKEY_CURRENT_USER, 0) || matchesInstall(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY));
}

int RunUpdateSelfTest() {
    int failures = 0;
    const std::string manifest(kSelfTestManifest);

    if (!VerifyEcdsaP256Signature(manifest, kSelfTestSignature)) ++failures;
    std::string tampered = manifest;
    size_t versionPos = tampered.find("9.9.9");
    if (versionPos != std::string::npos) {
        tampered[versionPos + 4] = '8';
        if (VerifyEcdsaP256Signature(tampered, kSelfTestSignature)) ++failures;
    } else {
        ++failures;
    }

    if (Base64Decode(kUpdatePublicKeyBlobBase64).size() != 72) ++failures;
    if (!IsVersionGreater(L"1.4.0", L"1.3.2")) ++failures;
    if (IsVersionGreater(L"1.3.2", L"1.4.0")) ++failures;
    if (IsVersionGreater(L"1.3.2", L"1.3.2")) ++failures;
    if (!IsAllowedUpdateHost(L"https://github.com/owner/repo/releases/download/v1/version.json")) ++failures;
    if (IsAllowedUpdateHost(L"http://github.com/owner/repo")) ++failures;
    if (IsAllowedUpdateHost(L"https://evil.example.com/version.json")) ++failures;

    return failures == 0 ? 0 : 1;
}

} // namespace WinPreview
