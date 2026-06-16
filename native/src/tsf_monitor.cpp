#include "tsf_monitor.h"
#include "ime_state_manager.h"
#include "sta_thread.h"
#include "jni_callback.h"
#include <comdef.h>
#include <algorithm>
#include <msctf.h>
#include <windows.h>
#include <imm.h>

#pragma comment(lib, "imm32.lib")

const GUID GUID_COMPARTMENT_KEYBOARD_INPUTMODE =
{ 0x5147C989, 0x49A1, 0x46EC, { 0x8F, 0x15, 0x75, 0x9C, 0x45, 0x7D, 0x12, 0x4D } };

const GUID GUID_MS_PINYIN =
{ 0xFA445057, 0x17C6, 0x4973, { 0x93, 0x44, 0xE0, 0xDF, 0xF5, 0xF2, 0x3F, 0x10 } };

const GUID GUID_MS_ZHUYIN =
{ 0xb115690a, 0xea02, 0x48d5, { 0xa2, 0x31, 0xe3, 0x57, 0x8d, 0x2f, 0x0c, 0x52 } };

const GUID GUID_MS_CANGJIE =
{ 0x4bdf9f03, 0xc7d3, 0x11d4, { 0xb2, 0xab, 0x00, 0x80, 0xc8, 0x82, 0x68, 0x7e } };

const GUID GUID_MS_WUBI =
{ 0x82590c13, 0xf4dd, 0x44f4, { 0xba, 0x1d, 0x86, 0x67, 0x24, 0x6f, 0xdf, 0x8e } };

const GUID GUID_MS_SUCHENG =
{ 0x6024b45f, 0x5c54, 0x11d4, { 0xb9, 0x21, 0x00, 0x80, 0xc8, 0x82, 0x68, 0x7e } };

const GUID GUID_PROP_CANDIDATE =
{ 0xf3a465f7, 0x6be7, 0x4dfb, { 0x82, 0x3a, 0xcf, 0x59, 0x47, 0x16, 0x86, 0x1c } };

#ifdef CHINESEIME_DEBUG
#define DEBUG_LOG(format, ...) do { \
    wchar_t buf[512]; \
    swprintf_s(buf, format, __VA_ARGS__); \
    OutputDebugStringW(buf); \
} while(0)
#define DEBUG_LOG_SIMPLE(msg) OutputDebugStringW(msg)
#else
#define DEBUG_LOG(format, ...)
#define DEBUG_LOG_SIMPLE(msg)
#endif

namespace chineseime {

static bool IsChineseLangId(LANGID langId) {
    return langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404;
}

static InputMethodType detectInputMethodTypeFromHklSafe(HKL hkl) {
    if (!hkl) return InputMethodType::UNKNOWN;
    LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
    if (!IsChineseLangId(langId)) return InputMethodType::ENGLISH;
    DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
    WORD imeId = HIWORD(hklValue);
    InputMethodType type = detectInputMethodTypeFromImeId(imeId, langId);
    if (type == InputMethodType::OTHER_CHINESE && IsChineseLangId(langId)) {
        WCHAR klName[16] = {0};
        if (GetKeyboardLayoutNameW(klName) && klName[0]) {
            WCHAR layoutLow = klName[7];
            WCHAR layoutHigh = klName[6];
            if (layoutLow >= L'0' && layoutLow <= L'9') {
                WORD extractedId = static_cast<WORD>((layoutHigh - L'0') * 16 + (layoutLow - L'0'));
                type = detectInputMethodTypeFromImeId(extractedId, langId);
            } else if (layoutLow >= L'A' && layoutLow <= L'F') {
                WORD lowNibble = static_cast<WORD>(layoutLow - L'A' + 10);
                WORD highNibble = static_cast<WORD>(layoutHigh - L'A' + 10);
                WORD extractedId = static_cast<WORD>(lowNibble + (highNibble << 4));
                type = detectInputMethodTypeFromImeId(extractedId, langId);
            }
        }
    }
    return type;
}

TsfMonitor::TsfMonitor() {
    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfMonitor created\n");
}

TsfMonitor::~TsfMonitor() {
    shutdown();
    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfMonitor destroyed\n");
}

bool TsfMonitor::initialize(IUnknown* pThreadMgr) {
    if (!pThreadMgr) return false;

    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfThreadMgr, (void**)&threadMgr_);
    if (FAILED(hr)) {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] Failed to get ITfThreadMgr\n");
        return false;
    }

    hr = threadMgr_->GetFocus(&docMgr_);
    if (FAILED(hr) || !docMgr_) {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] No focused document manager\n");
    }

    ITfSource* source = nullptr;
    hr = threadMgr_->QueryInterface(IID_ITfSource, (void**)&source);
    if (SUCCEEDED(hr) && source) {
        hr = source->AdviseSink(IID_ITfInputProcessorProfileActivationSink,
            static_cast<ITfInputProcessorProfileActivationSink*>(this), &profileSinkCookie_);
        if (SUCCEEDED(hr)) {
            threadMgrSource_ = source;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] Profile sink registered\n");
        } else {
            source->Release();
        }
    }

    if (docMgr_) {
        ITfContext* ctx = nullptr;
        hr = docMgr_->GetTop(&ctx);
        if (SUCCEEDED(hr) && ctx) {
            registerSinks(ctx);
            ctx->Release();
        }
    }

    updateCache();
    return true;
}

void TsfMonitor::shutdown() {
    unregisterSinks();

    if (threadMgrSource_) {
        if (profileSinkCookie_ != TF_INVALID_COOKIE) {
            threadMgrSource_->UnadviseSink(profileSinkCookie_);
            profileSinkCookie_ = TF_INVALID_COOKIE;
        }
        threadMgrSource_->Release();
        threadMgrSource_ = nullptr;
    }
    if (docMgr_) {
        docMgr_->Release();
        docMgr_ = nullptr;
    }
    if (threadMgr_) {
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }

    currentInputMethod_ = InputMethodType::UNKNOWN;
    chineseMode_ = false;
}

void TsfMonitor::refreshState() {
    updateCache();
}

STDMETHODIMP TsfMonitor::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ITfTextEditSink) {
        *ppv = static_cast<ITfTextEditSink*>(this);
    } else if (riid == IID_ITfKeyEventSink) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (riid == IID_ITfInputProcessorProfileActivationSink) {
        *ppv = static_cast<ITfInputProcessorProfileActivationSink*>(this);
    } else if (riid == IID_ITfCompartmentEventSink) {
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
    } else if (riid == IID_ITfUIElementSink) {
        *ppv = static_cast<ITfUIElementSink*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TsfMonitor::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) TsfMonitor::Release() {
    LONG count = InterlockedDecrement(&refCount_);
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP TsfMonitor::OnEndEdit(ITfContext* pic, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord) {
    if (!pic) return E_INVALIDARG;
    ecReadOnly_ = ecReadOnly;

    auto oldState = ImeStateManager::get().getSnapshot();
    updateCache();
    auto newState = ImeStateManager::get().getSnapshot();
    notifyStateChanges(oldState, newState);
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnSetFocus(BOOL fForeground) {
    if (fForeground) {
        updateCache();
    }
    return S_OK;
}

STDMETHODIMP TsfMonitor::BeginUIElement(DWORD dwUIElementId, BOOL* pbShow) {
    if (pbShow) *pbShow = TRUE;
    char buf[128];
    sprintf_s(buf, "[ChineseIME] BeginUIElement: id=%d\n", dwUIElementId);
    OutputDebugStringA(buf);
    return S_OK;
}

STDMETHODIMP TsfMonitor::UpdateUIElement(DWORD dwUIElementId) {
    return S_OK;
}

STDMETHODIMP TsfMonitor::EndUIElement(DWORD dwUIElementId) {
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnActivated(DWORD dwProfileType, LANGID langid, REFCLSID clsid,
                                     REFGUID guidProfile, REFGUID guidCat, HKL hkl, DWORD dwFlags) {
    updateInputMethodType(langid, clsid, guidProfile);

    DEBUG_LOG(L"[ChineseIME] OnActivated: type=%d, langid=0x%04x, hkl=0x%08X, flags=0x%x\n",
        (int)currentInputMethod_, langid, (DWORD)(DWORD_PTR)hkl, dwFlags);

    if (dwFlags & TF_IPSINK_FLAG_ACTIVE) {
        if (currentInputMethod_ != InputMethodType::UNKNOWN) {
            ImeStateManager::get().updateInputMethod(currentInputMethod_);
        } else {
            InputMethodType hklType = detectInputMethodTypeFromHklSafe(hkl);
            if (hklType != InputMethodType::UNKNOWN && hklType != InputMethodType::ENGLISH) {
                currentInputMethod_ = hklType;
                ImeStateManager::get().updateInputMethod(hklType);
            }
        }

        HWND fgWnd = GetForegroundWindow();
        bool imeOpen = false;
        bool isChineseLang = (langid == 0x0804 || langid == 0x0404 ||
                             langid == 0x0C04 || langid == 0x1404);
        if (fgWnd) {
            HIMC himc = ImmGetContext(fgWnd);
            if (himc) {
                imeOpen = ImmGetOpenStatus(himc) != 0;
                ImmReleaseContext(fgWnd, himc);
            }
        }
        chineseMode_ = imeOpen && isChineseLang;
        ImeStateManager::get().updateChineseMode(chineseMode_);
        ImeStateManager::get().updateImeOpen(imeOpen);

        onImeStateChanged(static_cast<int>(currentInputMethod_), chineseMode_);
    }

    return S_OK;
}

STDMETHODIMP TsfMonitor::OnChange(REFGUID rguid) {
    if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        bool newChineseMode = detectChineseMode();
        if (chineseMode_ != newChineseMode) {
            chineseMode_ = newChineseMode;
            ImeStateManager::get().updateChineseMode(newChineseMode);

            if (newChineseMode) {
                updateCache();
            }

            onImeStateChanged(static_cast<int>(currentInputMethod_), newChineseMode);
        }
    }
    return S_OK;
}

bool TsfMonitor::detectChineseMode() {
    HWND fgWnd = GetForegroundWindow();
    if (fgWnd) {
        HIMC himc = ImmGetContext(fgWnd);
        if (himc) {
            bool imeOpen = ImmGetOpenStatus(himc) != 0;
            ImmReleaseContext(fgWnd, himc);
            chineseMode_ = imeOpen;
            return chineseMode_;
        }
    }

    if (threadMgr_) {
        ITfDocumentMgr* docMgr = nullptr;
        HRESULT hr = threadMgr_->GetFocus(&docMgr);
        if (SUCCEEDED(hr) && docMgr) {
            ITfContext* ctx = nullptr;
            hr = docMgr->GetTop(&ctx);
            docMgr->Release();
            if (SUCCEEDED(hr) && ctx) {
                ITfCompartmentMgr* compMgr = nullptr;
                hr = ctx->QueryInterface(IID_ITfCompartmentMgr, (void**)&compMgr);
                ctx->Release();
                if (SUCCEEDED(hr) && compMgr) {
                    ITfCompartment* comp = nullptr;
                    hr = compMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &comp);
                    compMgr->Release();
                    if (SUCCEEDED(hr) && comp) {
                        VARIANT var;
                        VariantInit(&var);
                        hr = comp->GetValue(&var);
                        comp->Release();
                        if (SUCCEEDED(hr) && var.vt == VT_I4) {
                            chineseMode_ = (var.lVal & 0x0001) != 0;
                            VariantClear(&var);
                            return chineseMode_;
                        }
                        VariantClear(&var);
                    }
                }
            }
        }
    }

    return chineseMode_;
}

void TsfMonitor::updateCache() {
    ImeStateManager& mgr = ImeStateManager::get();

    if (currentInputMethod_ == InputMethodType::UNKNOWN) {
        queryCurrentInputMethod();
    }

    mgr.updateChineseMode(chineseMode_);

    std::wstring composition;
    std::vector<std::wstring> candidates;
    int selectedIndex = 0;
    bool candidatesFound = false;

    ITfContext* ctx = getCurrentContext();
    if (ctx) {
        getCompositionString(ctx, composition);
        if (getCandidateList(ctx, candidates, selectedIndex)) {
            candidatesFound = true;
        }
        ctx->Release();
    }

    if (!candidatesFound) {
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd) {
            HIMC himc = ImmGetContext(fgWnd);
            if (himc) {
                LONG compLen = ImmGetCompositionString(himc, GCS_COMPSTR, nullptr, 0);
                if (compLen <= 0) {
                    compLen = ImmGetCompositionString(himc, GCS_COMPREADSTR, nullptr, 0);
                }
                if (compLen > 0) {
                    int wcharLen = compLen / sizeof(wchar_t);
                    std::vector<wchar_t> compBuf(wcharLen + 1);
                    LONG actualLen = ImmGetCompositionString(himc, GCS_COMPSTR, compBuf.data(), compLen);
                    if (actualLen <= 0) {
                        actualLen = ImmGetCompositionString(himc, GCS_COMPREADSTR, compBuf.data(), compLen);
                    }
                    if (actualLen > 0) {
                        int actualWcharLen = actualLen / sizeof(wchar_t);
                        compBuf[actualWcharLen] = 0;
                        composition.assign(compBuf.data(), actualWcharLen);
                    }
                }

                size_t bufSize = ImmGetCandidateList(himc, 0, nullptr, 0);
                if (bufSize > 0) {
                    std::vector<char> candBuf(bufSize);
                    CANDIDATELIST* candList = reinterpret_cast<CANDIDATELIST*>(candBuf.data());
                    ImmGetCandidateList(himc, 0, candList, bufSize);
                    DWORD count = candList->dwCount;
                    selectedIndex = candList->dwSelection;
                    if (count > 10) count = 10;
                    for (DWORD j = 0; j < count; j++) {
                        wchar_t* pStr = (wchar_t*)(candBuf.data() + candList->dwOffset[j]);
                        candidates.push_back(pStr);
                    }
                    candidatesFound = true;
                }

                char debugBuf[512];
                sprintf_s(debugBuf, "[ChineseIME] updateCache: IMM32 comp='%S', candCount=%d, found=%d\n",
                    composition.c_str(), (int)candidates.size(), candidatesFound ? 1 : 0);
                OutputDebugStringA(debugBuf);

                ImmReleaseContext(fgWnd, himc);
            }
        }
    }

    mgr.updateCandidates(composition, candidates, selectedIndex);

    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));

        if (currentInputMethod_ == InputMethodType::UNKNOWN ||
            currentInputMethod_ == InputMethodType::ENGLISH) {
            DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
            WORD imeId = HIWORD(hklValue);
            InputMethodType imType = detectInputMethodTypeFromImeId(imeId, langId);

            bool isChineseInputMethod = (imType != InputMethodType::UNKNOWN &&
                imType != InputMethodType::ENGLISH);

            if (!isChineseInputMethod) {
                mgr.updateInputMethod(InputMethodType::ENGLISH);
                mgr.updateImeOpen(false);
                mgr.updateChineseMode(false);
            } else if (imType != InputMethodType::OTHER_CHINESE) {
                mgr.updateInputMethod(imType);
                HWND fgWnd = GetForegroundWindow();
                bool imeOpen = true;
                if (fgWnd) {
                    HIMC himc = ImmGetContext(fgWnd);
                    if (himc) {
                        imeOpen = ImmGetOpenStatus(himc) != 0;
                        ImmReleaseContext(fgWnd, himc);
                    }
                }
                mgr.updateImeOpen(imeOpen);
            }
        }
    }
}

bool TsfMonitor::registerSinks(ITfContext* pic) {
    if (!pic || context_ == pic) return false;
    if (context_) {
        unregisterSinks();
    }
    context_ = pic;
    context_->AddRef();

    HRESULT hr = pic->QueryInterface(IID_ITfSource, (void**)&contextSource_);
    if (FAILED(hr) || !contextSource_) {
        return false;
    }

    hr = contextSource_->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this), &editSinkCookie_);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[ChineseIME] Failed to register edit sink\n");
    } else {
        OutputDebugStringW(L"[ChineseIME] Edit sink registered\n");
    }

    return true;
}

void TsfMonitor::unregisterSinks() {
    if (contextSource_) {
        if (editSinkCookie_ != TF_INVALID_COOKIE) {
            contextSource_->UnadviseSink(editSinkCookie_);
            editSinkCookie_ = TF_INVALID_COOKIE;
        }
        contextSource_->Release();
        contextSource_ = nullptr;
    }
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
}

ITfContext* TsfMonitor::getCurrentContext() {
    if (!threadMgr_) return nullptr;
    ITfDocumentMgr* docMgr = nullptr;
    HRESULT hr = threadMgr_->GetFocus(&docMgr);
    if (FAILED(hr) || !docMgr) {
        return nullptr;
    }
    ITfContext* ctx = nullptr;
    hr = docMgr->GetTop(&ctx);
    docMgr->Release();
    return ctx;
}

void TsfMonitor::notifyStateChanges(const IMEState& oldState, const IMEState& newState) {
    if (oldState.composition != newState.composition || oldState.candidates != newState.candidates) {
        std::vector<const wchar_t*> candidatePtrs;
        for (const auto& cand : newState.candidates) {
            candidatePtrs.push_back(cand.c_str());
        }
        onCandidateChanged(
            newState.composition.c_str(),
            candidatePtrs.empty() ? nullptr : candidatePtrs.data(),
            static_cast<int>(newState.candidates.size()),
            newState.selectedIndex
        );
    }

if (oldState.inputMethodType != newState.inputMethodType || oldState.chineseMode != newState.chineseMode) {
        onImeStateChanged(static_cast<int>(newState.inputMethodType), newState.chineseMode);
    }
}

bool TsfMonitor::getCompositionString(ITfContext* pic, std::wstring& result) {
    result.clear();
    if (!pic) return false;

    ITfProperty* prop = nullptr;
    HRESULT hr = pic->GetProperty(GUID_PROP_COMPOSING, &prop);
    if (FAILED(hr) || !prop) {
        return false;
    }

    IEnumTfRanges* enumRanges = nullptr;
    hr = prop->EnumRanges(ecReadOnly_, &enumRanges, nullptr);
    prop->Release();
    if (FAILED(hr) || !enumRanges) {
        return false;
    }

    ITfRange* range = nullptr;
    std::wstring composition;

    while (enumRanges->Next(1, &range, nullptr) == S_OK) {
        if (range) {
            wchar_t buffer[64];
            ULONG fetched = 0;
            hr = range->GetText(ecReadOnly_, 0, buffer, 63, &fetched);
            if (SUCCEEDED(hr) && fetched > 0) {
                buffer[fetched] = 0;
                composition += buffer;
            }
            range->Release();
        }
    }
    enumRanges->Release();

    if (!composition.empty()) {
        result = composition;
        return true;
    }
    return false;
}

bool TsfMonitor::getCandidateList(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex) {
    candidates.clear();
    selectedIndex = 0;
    return getCandidateListFromProperty(pic, candidates, selectedIndex);
}

bool TsfMonitor::getCandidateListFromProperty(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex) {
    candidates.clear();
    selectedIndex = 0;

    ITfProperty* prop = nullptr;
    HRESULT hr = pic->GetProperty(GUID_PROP_CANDIDATE, &prop);
    if (FAILED(hr) || !prop) {
        return false;
    }

    IEnumTfRanges* enumRanges = nullptr;
    hr = prop->EnumRanges(ecReadOnly_, &enumRanges, nullptr);
    prop->Release();
    if (FAILED(hr) || !enumRanges) {
        return false;
    }

    ITfRange* range = nullptr;
    while (enumRanges->Next(1, &range, nullptr) == S_OK) {
        if (range) {
            wchar_t buffer[64];
            ULONG fetched = 0;
            hr = range->GetText(ecReadOnly_, 0, buffer, 63, &fetched);
            if (SUCCEEDED(hr) && fetched > 0) {
                buffer[fetched] = 0;
                candidates.push_back(buffer);
            }
            range->Release();
        }
    }
    enumRanges->Release();
    return !candidates.empty();
}

void TsfMonitor::updateInputMethodType(LANGID langid, REFCLSID clsid, REFGUID guidProfile) {
    DEBUG_LOG(L"[ChineseIME] updateInputMethodType: langid=0x%04x\n", langid);

    if (langid == 0x0804 || langid == 0x0404 || langid == 0x0C04 || langid == 0x1404) {
        bool detected = false;

        if (IsEqualGUID(guidProfile, GUID_MS_PINYIN) || IsEqualGUID(clsid, GUID_MS_PINYIN)) {
            currentInputMethod_ = InputMethodType::PINYIN;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> PINYIN (GUID match)\n");
        } else if (IsEqualGUID(guidProfile, GUID_MS_ZHUYIN) || IsEqualGUID(clsid, GUID_MS_ZHUYIN)) {
            currentInputMethod_ = InputMethodType::ZHUYIN;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> ZHUYIN (GUID match)\n");
        } else if (IsEqualGUID(guidProfile, GUID_MS_CANGJIE) || IsEqualGUID(clsid, GUID_MS_CANGJIE)) {
            currentInputMethod_ = InputMethodType::CANGJIE;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> CANGJIE (GUID match)\n");
        } else if (IsEqualGUID(guidProfile, GUID_MS_WUBI) || IsEqualGUID(clsid, GUID_MS_WUBI)) {
            currentInputMethod_ = InputMethodType::WUBI;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> WUBI (GUID match)\n");
        } else if (IsEqualGUID(guidProfile, GUID_MS_SUCHENG) || IsEqualGUID(clsid, GUID_MS_SUCHENG)) {
            currentInputMethod_ = InputMethodType::SUCHENG;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> SUCHENG (GUID match)\n");
        }

        if (!detected) {
            currentInputMethod_ = InputMethodType::OTHER_CHINESE;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> OTHER_CHINESE (no GUID match)\n");
        }
    } else {
        currentInputMethod_ = InputMethodType::ENGLISH;
        DEBUG_LOG_SIMPLE(L"[ChineseIME] -> ENGLISH\n");
    }
}

void TsfMonitor::queryCurrentInputMethod() {
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&profiles);
    if (FAILED(hr)) return;

    LANGID langid;
    CLSID clsid;
    GUID guidProfile;
    hr = profiles->GetActiveLanguageProfile(clsid, &langid, &guidProfile);
    if (SUCCEEDED(hr)) {
        updateInputMethodType(langid, clsid, guidProfile);
        char buf[256];
        sprintf_s(buf, "[ChineseIME] queryCurrentInputMethod: langid=0x%04x, type=%d\n",
            langid, (int)currentInputMethod_);
        OutputDebugStringA(buf);
    }
    profiles->Release();
}

} // namespace chineseime