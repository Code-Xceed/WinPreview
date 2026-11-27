#include "accessibility.h"
#include "update_engine.h"

#include <oleauto.h>
#include <uiautomationcoreapi.h>

// The property/pattern ids live in uiautomationclient.h, whose mingw version is
// not C++-clean; only a handful are needed here.
#define UIA_ValuePatternId 10002
#define UIA_ControlTypePropertyId 30003
#define UIA_NamePropertyId 30005
#define UIA_HasKeyboardFocusPropertyId 30008
#define UIA_IsKeyboardFocusablePropertyId 30009
#define UIA_IsEnabledPropertyId 30010
#define UIA_AutomationIdPropertyId 30011
#define UIA_ClassNamePropertyId 30012
#define UIA_HelpTextPropertyId 30013
#define UIA_IsControlElementPropertyId 30016
#define UIA_IsContentElementPropertyId 30017
#define UIA_FrameworkIdPropertyId 30024
#define UIA_ValueValuePropertyId 30045
#define UIA_ValueIsReadOnlyPropertyId 30046
#define UIA_DocumentControlTypeId 50030
#ifndef UIA_E_NOTSUPPORTED
#define UIA_E_NOTSUPPORTED ((HRESULT)0x80131515)
#endif

namespace WinPreview {

namespace {

BSTR AllocBstr(const std::wstring& text) {
    return SysAllocStringLen(text.c_str(), (UINT)text.length());
}

} // namespace

class UiaDocument : public IRawElementProviderSimple, public IValueProvider {
public:
    explicit UiaDocument(Accessibility* owner) : m_owner(owner) {}

    void SetOwner(Accessibility* owner) { m_owner = owner; }

    // ---- IUnknown ------------------------------------------------------
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** result) override {
        if (!result) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
            *result = static_cast<IRawElementProviderSimple*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IValueProvider) {
            *result = static_cast<IValueProvider*>(this);
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refs);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) delete this;
        return (ULONG)refs;
    }

    // ---- IRawElementProviderSimple -------------------------------------
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override {
        if (!options) return E_INVALIDARG;
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID patternId,
                                                 IUnknown** provider) override {
        if (!provider) return E_INVALIDARG;
        *provider = nullptr;
        if (patternId == UIA_ValuePatternId) {
            *provider = static_cast<IValueProvider*>(this);
            AddRef();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId,
                                               VARIANT* value) override {
        if (!value) return E_INVALIDARG;
        VariantInit(value);

        const std::wstring name = m_owner ? m_owner->Name() : std::wstring();
        const std::wstring help = m_owner ? m_owner->Help() : std::wstring();
        const std::wstring caption = m_owner ? m_owner->Value() : std::wstring();

        switch (propertyId) {
            case UIA_NamePropertyId:
                value->vt = VT_BSTR;
                value->bstrVal = AllocBstr(name.empty() ? L"WinPreview" : name);
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            case UIA_HelpTextPropertyId:
                value->vt = VT_BSTR;
                value->bstrVal = AllocBstr(help);
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            case UIA_ValueValuePropertyId:
                value->vt = VT_BSTR;
                value->bstrVal = AllocBstr(caption);
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            case UIA_ValueIsReadOnlyPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = VARIANT_TRUE;
                return S_OK;
            case UIA_ControlTypePropertyId:
                value->vt = VT_I4;
                value->lVal = UIA_DocumentControlTypeId;
                return S_OK;
            case UIA_IsContentElementPropertyId:
            case UIA_IsControlElementPropertyId:
            case UIA_IsEnabledPropertyId:
            case UIA_IsKeyboardFocusablePropertyId:
                value->vt = VT_BOOL;
                value->boolVal = VARIANT_TRUE;
                return S_OK;
            case UIA_HasKeyboardFocusPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = (m_owner && m_owner->Window() &&
                                  GetFocus() == m_owner->Window()) ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            case UIA_AutomationIdPropertyId:
                value->vt = VT_BSTR;
                value->bstrVal = AllocBstr(L"WinPreviewDocument");
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            case UIA_ClassNamePropertyId:
                value->vt = VT_BSTR;
                value->bstrVal = AllocBstr(L"WinPreviewMainWindow");
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            case UIA_FrameworkIdPropertyId:
                value->vt = VT_BSTR;
                value->bstrVal = AllocBstr(L"WinPreview");
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            default:
                return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** provider) override {
        if (!provider) return E_INVALIDARG;
        *provider = nullptr;
        if (!m_owner || !m_owner->Window()) return S_OK;
        return UiaHostProviderFromHwnd(m_owner->Window(), provider);
    }

    // ---- IValueProvider ------------------------------------------------
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR) override {
        return UIA_E_NOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE get_Value(BSTR* value) override {
        if (!value) return E_INVALIDARG;
        *value = AllocBstr(m_owner ? m_owner->Value() : std::wstring());
        return *value ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* readOnly) override {
        if (!readOnly) return E_INVALIDARG;
        *readOnly = TRUE;
        return S_OK;
    }

private:
    ~UiaDocument() = default;

    Accessibility* m_owner = nullptr;
    LONG m_refs = 1;
};

Accessibility::Accessibility() {}

Accessibility::~Accessibility() {
    Detach();
}

void Accessibility::Attach(HWND hwnd) {
    Detach();
    m_hwnd = hwnd;
    if (hwnd) m_document = new UiaDocument(this);
}

void Accessibility::Detach() {
    if (m_document) {
        m_document->SetOwner(nullptr);
        m_document->Release();
        m_document = nullptr;
    }
    m_hwnd = nullptr;
}

LRESULT Accessibility::HandleGetObject(WPARAM wParam, LPARAM lParam) {
    if (!m_document || !m_hwnd) return 0;
    const LONG objectId = (LONG)lParam;
    if (objectId == UiaRootObjectId || objectId == OBJID_CLIENT) {
        LRESULT result = UiaReturnRawElementProvider(m_hwnd, wParam, lParam, m_document);
        m_lastObjectResult = (long)result;
        return result;
    }
    return 0;
}

void Accessibility::SetIdentity(const std::wstring& name, const std::wstring& value,
                                const std::wstring& help) {
    const bool nameChanged = (m_name != name);
    const bool valueChanged = (m_value != value);
    m_name = name;
    m_value = value;
    m_help = help;
    if (nameChanged) Announce(name);
    else if (valueChanged) Announce(value);
}

void Accessibility::SetValue(const std::wstring& value) {
    if (m_value == value) return;
    m_value = value;
    Announce(value);
}

void Accessibility::SetHelp(const std::wstring& help) {
    m_help = help;
}

void Accessibility::Announce(const std::wstring& text) {
    if (!m_document || text.empty()) return;
    if (!UiaClientsAreListening()) return;
    BSTR display = SysAllocStringLen(text.c_str(), (UINT)text.length());
    BSTR activity = SysAllocString(L"WinPreview.Status");
    if (display) {
        UiaRaiseNotificationEvent(m_document, NotificationKind_ActionCompleted,
                                  NotificationProcessing_ImportantAll, display, activity);
        SysFreeString(display);
    }
    if (activity) SysFreeString(activity);
}

} // namespace WinPreview
