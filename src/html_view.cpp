#include "html_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <shlwapi.h>

#include "WebView2.h"

// MinGW's __uuidof needs explicit GUID declarations for interfaces that are not
// part of its own headers; these match the MIDL_INTERFACE strings in WebView2.h.
__CRT_UUID_DECL(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, 0x4e8a3389, 0xc9d8, 0x4bd2, 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d)
__CRT_UUID_DECL(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, 0x6c4819f3, 0xc9b7, 0x4260, 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c)
__CRT_UUID_DECL(ICoreWebView2NavigationCompletedEventHandler, 0xd33a35bf, 0x1c49, 0x4f98, 0x93, 0xab, 0x0, 0x6e, 0x5, 0x33, 0xfe, 0x1c)
__CRT_UUID_DECL(ICoreWebView2AcceleratorKeyPressedEventHandler, 0xb29c7e28, 0xfa79, 0x41a8, 0x8e, 0x44, 0x65, 0x81, 0x1c, 0x76, 0xdc, 0xb2)
__CRT_UUID_DECL(ICoreWebView2ExecuteScriptCompletedHandler, 0x49511172, 0xcc67, 0x4bca, 0x99, 0x23, 0x13, 0x71, 0x12, 0xf4, 0xc4, 0xcc)
__CRT_UUID_DECL(ICoreWebView2Controller2, 0xc979903e, 0xd4ca, 0x4228, 0x92, 0xeb, 0x47, 0xee, 0x3f, 0xa9, 0x6e, 0xab)
__CRT_UUID_DECL(ICoreWebView2Controller3, 0xf9614724, 0x5d2b, 0x41dc, 0xae, 0xf7, 0x73, 0xd6, 0x2b, 0x51, 0x54, 0x3b)
__CRT_UUID_DECL(ICoreWebView2CapturePreviewCompletedHandler, 0x697e05e9, 0x3d8f, 0x45fa, 0x96, 0xf4, 0x8f, 0xfe, 0x1e, 0xde, 0xda, 0xf5)

namespace WinPreview {

namespace {

const wchar_t* kHostClass = L"WinPreviewHtmlHost";

typedef HRESULT(STDAPICALLTYPE* PFN_CreateEnvironment)(
    PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

typedef HRESULT(STDAPICALLTYPE* PFN_GetBrowserVersion)(PCWSTR browserExecutableFolder,
                                                      LPWSTR* versionInfo);

struct Loader {
    HMODULE module = nullptr;
    PFN_CreateEnvironment createEnvironment = nullptr;
    PFN_GetBrowserVersion browserVersion = nullptr;

    bool ready() const { return module && createEnvironment; }
};

Loader& Api() {
    static Loader loader;
    static bool attempted = false;
    if (!attempted) {
        attempted = true;
        loader.module = LoadLibraryW(L"WebView2Loader.dll");
        if (loader.module) {
            loader.createEnvironment = (PFN_CreateEnvironment)GetProcAddress(
                loader.module, "CreateCoreWebView2EnvironmentWithOptions");
            loader.browserVersion = (PFN_GetBrowserVersion)GetProcAddress(
                loader.module, "GetAvailableCoreWebView2BrowserVersionString");
        }
    }
    return loader;
}

bool DisabledByEnvironment() {
    wchar_t value[16] = {};
    DWORD length = GetEnvironmentVariableW(L"WINPREVIEW_DISABLE_WEBVIEW2", value, 15);
    return length > 0 && value[0] == L'1';
}

bool PumpUntil(const std::function<bool()>& done, DWORD timeoutMs) {
    ULONGLONG start = GetTickCount64();
    MSG message;
    while (!done()) {
        if (GetTickCount64() - start > timeoutMs) return false;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage((int)message.wParam);
                return false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, 0);
    }
    return true;
}

std::wstring LocalAppDataPath() {
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, (DWORD)(MAX_PATH * 2 - 1));
    if (length == 0) return std::wstring();
    return buffer;
}

std::wstring ToFileUrl(const std::wstring& path) {
    std::wstring normalized = path;
    for (wchar_t& c : normalized) {
        if (c == L'\\') c = L'/';
    }
    std::wstring url = L"file:///";
    for (wchar_t c : normalized) {
        if (c == L' ') {
            url += L"%20";
        } else if (c == L'#') {
            url += L"%23";
        } else if (c == L'?') {
            url += L"%3F";
        } else if (c == L'%') {
            url += L"%25";
        } else if (c < 0x80) {
            url += c;
        } else {
            char utf8[8] = {};
            int bytes = WideCharToMultiByte(CP_UTF8, 0, &c, 1, utf8, sizeof(utf8), nullptr, nullptr);
            for (int i = 0; i < bytes; ++i) {
                wchar_t escape[8] = {};
                swprintf_s(escape, L"%%%02X", (unsigned char)utf8[i]);
                url += escape;
            }
        }
    }
    return url;
}

void EnsureDirectory(const std::wstring& path) {
    std::wstring current;
    for (size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if (path[i] == L'\\' || i + 1 == path.size()) {
            if (current.size() > 3) CreateDirectoryW(current.c_str(), nullptr);
        }
    }
}

bool EnsureHostClass() {
    static bool registered = [] {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.lpszClassName = kHostClass;
        return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    return registered;
}

std::wstring DecodeJsonString(const std::wstring& json) {
    if (json.size() < 2 || json.front() != L'"' || json.back() != L'"') return json;
    std::wstring out;
    out.reserve(json.size());
    for (size_t i = 1; i + 1 < json.size(); ++i) {
        wchar_t c = json[i];
        if (c != L'\\' || i + 2 >= json.size()) {
            out += c;
            continue;
        }
        wchar_t escape = json[++i];
        switch (escape) {
            case L'n': out += L'\n'; break;
            case L'r': out += L'\r'; break;
            case L't': out += L'\t'; break;
            case L'"': out += L'"'; break;
            case L'\\': out += L'\\'; break;
            case L'/': out += L'/'; break;
            case L'u': {
                if (i + 4 < json.size()) {
                    unsigned code = 0;
                    bool valid = true;
                    for (int k = 1; k <= 4; ++k) {
                        wchar_t digit = json[i + k];
                        int value;
                        if (digit >= L'0' && digit <= L'9') value = digit - L'0';
                        else if (digit >= L'a' && digit <= L'f') value = 10 + digit - L'a';
                        else if (digit >= L'A' && digit <= L'F') value = 10 + digit - L'A';
                        else { valid = false; break; }
                        code = code * 16 + (unsigned)value;
                    }
                    if (valid) {
                        out += (wchar_t)code;
                        i += 4;
                        break;
                    }
                }
                out += L'u';
                break;
            }
            default: out += escape; break;
        }
    }
    return out;
}

// ---- COM handlers ---------------------------------------------------------

template <typename Interface>
class ComHolder {
public:
    ComHolder() = default;
    ~ComHolder() { Reset(); }
    ComHolder(const ComHolder&) = delete;
    ComHolder& operator=(const ComHolder&) = delete;

    Interface* get() const { return pointer; }
    Interface** put() {
        Reset();
        return &pointer;
    }
    Interface* operator->() const { return pointer; }
    explicit operator bool() const { return pointer != nullptr; }

    void Attach(Interface* value) {
        Reset();
        pointer = value;
        if (pointer) pointer->AddRef();
    }

    void Reset() {
        if (pointer) {
            pointer->Release();
            pointer = nullptr;
        }
    }

private:
    Interface* pointer = nullptr;
};

class EnvironmentHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
    using Callback = std::function<void(HRESULT, ICoreWebView2Environment*)>;
    explicit EnvironmentHandler(Callback callback) : m_callback(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** result) override {
        if (!result) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
            *result = this;
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) delete this;
        return (ULONG)refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,
                                     ICoreWebView2Environment* environment) override {
        m_callback(errorCode, environment);
        return S_OK;
    }

private:
    ~EnvironmentHandler() = default;
    LONG m_refs = 1;
    Callback m_callback;
};

class ControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    using Callback = std::function<void(HRESULT, ICoreWebView2Controller*)>;
    explicit ControllerHandler(Callback callback) : m_callback(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** result) override {
        if (!result) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
            *result = this;
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) delete this;
        return (ULONG)refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode,
                                     ICoreWebView2Controller* controller) override {
        m_callback(errorCode, controller);
        return S_OK;
    }

private:
    ~ControllerHandler() = default;
    LONG m_refs = 1;
    Callback m_callback;
};

class NavigationHandler : public ICoreWebView2NavigationCompletedEventHandler {
public:
    using Callback = std::function<void(bool)>;
    explicit NavigationHandler(Callback callback) : m_callback(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** result) override {
        if (!result) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(ICoreWebView2NavigationCompletedEventHandler)) {
            *result = this;
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) delete this;
        return (ULONG)refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) override {
        BOOL success = FALSE;
        if (args) args->get_IsSuccess(&success);
        m_callback(success != FALSE);
        return S_OK;
    }

private:
    ~NavigationHandler() = default;
    LONG m_refs = 1;
    Callback m_callback;
};

class ScriptHandler : public ICoreWebView2ExecuteScriptCompletedHandler {
public:
    using Callback = std::function<void(bool, const std::wstring&)>;
    explicit ScriptHandler(Callback callback) : m_callback(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** result) override {
        if (!result) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(ICoreWebView2ExecuteScriptCompletedHandler)) {
            *result = this;
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) delete this;
        return (ULONG)refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, LPCWSTR resultJson) override {
        m_callback(SUCCEEDED(errorCode), resultJson ? resultJson : L"");
        return S_OK;
    }

private:
    ~ScriptHandler() = default;
    LONG m_refs = 1;
    Callback m_callback;
};

class CaptureHandler : public ICoreWebView2CapturePreviewCompletedHandler {
public:
    using Callback = std::function<void(bool)>;
    explicit CaptureHandler(Callback callback) : m_callback(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** result) override {
        if (!result) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(ICoreWebView2CapturePreviewCompletedHandler)) {
            *result = this;
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) delete this;
        return (ULONG)refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode) override {
        m_callback(SUCCEEDED(errorCode));
        return S_OK;
    }

private:
    ~CaptureHandler() = default;
    LONG m_refs = 1;
    Callback m_callback;
};

class AcceleratorHandler : public ICoreWebView2AcceleratorKeyPressedEventHandler {
public:
    AcceleratorHandler(HWND owner, UINT key) : m_owner(owner), m_key(key) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** result) override {
        if (!result) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(ICoreWebView2AcceleratorKeyPressedEventHandler)) {
            *result = this;
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) delete this;
        return (ULONG)refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2Controller*,
        ICoreWebView2AcceleratorKeyPressedEventArgs* args) override {
        if (!args) return S_OK;
        COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
        args->get_KeyEventKind(&kind);
        if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
            kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
            return S_OK;
        }
        UINT virtualKey = 0;
        args->get_VirtualKey(&virtualKey);
        if (virtualKey == m_key) {
            PostMessageW(m_owner, WM_KEYDOWN, m_key, 0);
            args->put_Handled(TRUE);
        }
        return S_OK;
    }

private:
    ~AcceleratorHandler() = default;
    LONG m_refs = 1;
    HWND m_owner = nullptr;
    UINT m_key = 0;
};

} // namespace

struct HtmlView::Impl {
    ComHolder<ICoreWebView2Environment> environment;
    ComHolder<ICoreWebView2Controller> controller;
    ComHolder<ICoreWebView2> webview;
    EventRegistrationToken navigationToken = {};
    bool hasNavigationToken = false;
    EventRegistrationToken acceleratorToken = {};
    bool hasAcceleratorToken = false;
};

HtmlView::HtmlView() = default;

HtmlView::~HtmlView() {
    Close();
}

bool HtmlView::Create(HWND owner, const std::wstring& filePath, float dpiScale) {
    Close();
    if (!owner || filePath.empty() || DisabledByEnvironment()) return false;

    Loader& api = Api();
    if (!api.ready() || !EnsureHostClass()) return false;

    // Version check doubles as runtime detection: without an Evergreen runtime
    // there is nothing to host and the reader view takes over.
    if (api.browserVersion) {
        LPWSTR version = nullptr;
        if (SUCCEEDED(api.browserVersion(nullptr, &version)) && version) {
            m_runtimeVersion = version;
            CoTaskMemFree(version);
        }
    }
    if (m_runtimeVersion.empty()) return false;

    m_owner = owner;
    m_dpiScale = dpiScale > 0.1f ? dpiScale : 1.0f;
    // Created off-screen: the controller exists before the first bounds call and
    // must not flash a white square in the corner.
    m_host = CreateWindowExW(0, kHostClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE,
                             -10000, -10000, 10, 10, owner, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (!m_host) return false;

    m_impl = new Impl();

    std::wstring dataDir = LocalAppDataPath() + L"\\WinPreview\\WebView2";
    EnsureDirectory(dataDir);

    bool environmentDone = false;
    HRESULT environmentResult = E_FAIL;
    auto* environmentHandler = new EnvironmentHandler(
        [&](HRESULT hr, ICoreWebView2Environment* environment) {
            environmentResult = hr;
            m_impl->environment.Attach(environment);
            environmentDone = true;
        });
    HRESULT hr = api.createEnvironment(nullptr, dataDir.c_str(), nullptr, environmentHandler);
    environmentHandler->Release();
    if (FAILED(hr)) {
        Close();
        return false;
    }
    if (!PumpUntil([&] { return environmentDone; }, 12000) ||
        FAILED(environmentResult) || !m_impl->environment) {
        Close();
        return false;
    }

    bool controllerDone = false;
    HRESULT controllerResult = E_FAIL;
    auto* controllerHandler = new ControllerHandler(
        [&](HRESULT result, ICoreWebView2Controller* controller) {
            controllerResult = result;
            m_impl->controller.Attach(controller);
            controllerDone = true;
        });
    hr = m_impl->environment->CreateCoreWebView2Controller(m_host, controllerHandler);
    controllerHandler->Release();
    if (FAILED(hr) ||
        !PumpUntil([&] { return controllerDone; }, 12000) ||
        FAILED(controllerResult) || !m_impl->controller) {
        Close();
        return false;
    }

    if (FAILED(m_impl->controller->get_CoreWebView2(m_impl->webview.put())) ||
        !m_impl->webview) {
        Close();
        return false;
    }

    // Show the page; never execute its script or allow navigation UI to take
    // over the preview window.
    ICoreWebView2Settings* settings = nullptr;
    if (SUCCEEDED(m_impl->webview->get_Settings(&settings)) && settings) {
        settings->put_IsScriptEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_IsWebMessageEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->put_IsBuiltInErrorPageEnabled(FALSE);
        settings->put_AreHostObjectsAllowed(FALSE);
        settings->Release();
    }

    auto* accelerator = new AcceleratorHandler(m_owner, VK_ESCAPE);
    if (SUCCEEDED(m_impl->controller->add_AcceleratorKeyPressed(
            accelerator, &m_impl->acceleratorToken))) {
        m_impl->hasAcceleratorToken = true;
    }
    accelerator->Release();

    // White paper behind the page avoids a black flash before first paint.
    ICoreWebView2Controller2* controller2 = nullptr;
    if (SUCCEEDED(m_impl->controller->QueryInterface(
            __uuidof(ICoreWebView2Controller2), (void**)&controller2)) && controller2) {
        COREWEBVIEW2_COLOR background = { 255, 255, 255, 255 };
        controller2->put_DefaultBackgroundColor(background);
        controller2->Release();
    }
    ICoreWebView2Controller3* controller3 = nullptr;
    if (SUCCEEDED(m_impl->controller->QueryInterface(
            __uuidof(ICoreWebView2Controller3), (void**)&controller3)) && controller3) {
        controller3->put_RasterizationScale((double)m_dpiScale);
        controller3->put_ShouldDetectMonitorScaleChanges(FALSE);
        controller3->Release();
    }

    bool navigationDone = false;
    bool navigationOk = false;
    auto* navigation = new NavigationHandler([&](bool success) {
        navigationDone = true;
        navigationOk = success;
    });
    if (SUCCEEDED(m_impl->webview->add_NavigationCompleted(
            navigation, &m_impl->navigationToken))) {
        m_impl->hasNavigationToken = true;
    }
    navigation->Release();

    D2D1_RECT_F initial = D2D1::RectF(0, 0, 800, 600);
    SetBounds(initial, m_dpiScale);

    std::wstring url = ToFileUrl(filePath);
    if (FAILED(m_impl->webview->Navigate(url.c_str()))) {
        Close();
        return false;
    }
    if (!PumpUntil([&] { return navigationDone; }, 15000) || !navigationOk) {
        Close();
        return false;
    }

    m_active = true;
    return true;
}

void HtmlView::SetBounds(const D2D1_RECT_F& contentRectDip, float dpiScale) {
    if (!m_host) return;
    float scale = dpiScale > 0.1f ? dpiScale : m_dpiScale;
    m_dpiScale = scale;
    int x = (int)std::lround(contentRectDip.left * scale);
    int y = (int)std::lround(contentRectDip.top * scale);
    int width = (int)std::lround((contentRectDip.right - contentRectDip.left) * scale);
    int height = (int)std::lround((contentRectDip.bottom - contentRectDip.top) * scale);
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    SetWindowPos(m_host, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (m_impl && m_impl->controller) {
        RECT bounds = { 0, 0, (LONG)std::lround(contentRectDip.right - contentRectDip.left),
                        (LONG)std::lround(contentRectDip.bottom - contentRectDip.top) };
        m_impl->controller->put_Bounds(bounds);
        ICoreWebView2Controller3* controller3 = nullptr;
        if (SUCCEEDED(m_impl->controller->QueryInterface(
                __uuidof(ICoreWebView2Controller3), (void**)&controller3)) && controller3) {
            controller3->put_RasterizationScale((double)scale);
            controller3->Release();
        }
    }
}

bool HtmlView::GetPageText(std::wstring& text) {
    text.clear();
    if (!m_active || !m_impl || !m_impl->webview) return false;

    bool done = false;
    std::wstring result;
    auto* handler = new ScriptHandler([&](bool ok, const std::wstring& value) {
        if (ok) result = value;
        done = true;
    });
    HRESULT hr = m_impl->webview->ExecuteScript(
        L"document.body ? document.body.innerText : ''", handler);
    handler->Release();
    if (FAILED(hr) || !PumpUntil([&] { return done; }, 4000)) return false;
    text = DecodeJsonString(result);
    return true;
}

bool HtmlView::CapturePreview(const std::wstring& path) {
    if (!m_active || !m_impl || !m_impl->webview || path.empty()) return false;

    IStream* stream = nullptr;
    HRESULT hr = SHCreateStreamOnFileEx(path.c_str(),
                                        STGM_CREATE | STGM_WRITE | STGM_SHARE_DENY_WRITE,
                                        FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream);
    if (FAILED(hr) || !stream) return false;

    bool done = false;
    bool ok = false;
    auto* handler = new CaptureHandler([&](bool success) {
        ok = success;
        done = true;
    });
    hr = m_impl->webview->CapturePreview(
        COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream, handler);
    handler->Release();
    if (FAILED(hr) || !PumpUntil([&] { return done; }, 12000)) {
        stream->Release();
        return false;
    }
    stream->Release();
    return ok;
}

void HtmlView::Close() {
    if (m_impl) {
        if (m_impl->controller && m_impl->hasAcceleratorToken) {
            m_impl->controller->remove_AcceleratorKeyPressed(m_impl->acceleratorToken);
            m_impl->hasAcceleratorToken = false;
        }
        if (m_impl->webview && m_impl->hasNavigationToken) {
            m_impl->webview->remove_NavigationCompleted(m_impl->navigationToken);
            m_impl->hasNavigationToken = false;
        }
        if (m_impl->controller) {
            m_impl->controller->Close();
        }
        m_impl->webview.Reset();
        m_impl->controller.Reset();
        m_impl->environment.Reset();
        delete m_impl;
        m_impl = nullptr;
    }
    if (m_host) {
        DestroyWindow(m_host);
        m_host = nullptr;
    }
    m_active = false;
    m_owner = nullptr;
    m_runtimeVersion.clear();
}

} // namespace WinPreview
