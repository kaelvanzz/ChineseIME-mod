#include "tsf_monitor.h"
#include "ime_state_manager.h"
#include "ime_detector.h"
#include "sta_thread.h"
#include "win_event_bridge.h"
#include "ime_callback.h"
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

class TsfEditSession : public ITfEditSession {
public:
    TsfEditSession(ITfContext* pic, std::vector<std::wstring>* candidates, bool* success)
        : pic_(pic), candidates_(candidates), success_(success), refCount_(1) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&refCount_);
    }

    STDMETHODIMP_(ULONG) Release() override {
        LONG count = InterlockedDecrement(&refCount_);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ecReadOnly) override {
        if (!pic_ || !candidates_) return E_FAIL;

        ITfProperty* prop = nullptr;
        HRESULT hr = pic_->GetProperty(GUID_PROP_CANDIDATE, &prop);
        if (FAILED(hr) || !prop) {
            if (success_) *success_ = false;
            return S_OK;
        }

        IEnumTfRanges* enumRanges = nullptr;
        hr = prop->EnumRanges(ecReadOnly, &enumRanges, nullptr);
        prop->Release();
        if (FAILED(hr) || !enumRanges) {
            if (success_) *success_ = false;
            return S_OK;
        }

        ITfRange* range = nullptr;
        while (enumRanges->Next(1, &range, nullptr) == S_OK) {
            if (range) {
                wchar_t buffer[64];
                ULONG fetched = 0;
                hr = range->GetText(ecReadOnly, 0, buffer, 63, &fetched);
                if (SUCCEEDED(hr) && fetched > 0) {
                    buffer[fetched] = 0;
                    candidates_->push_back(buffer);
                }
                range->Release();
            }
        }
        enumRanges->Release();

        if (success_) *success_ = !candidates_->empty();
        return S_OK;
    }

private:
    ITfContext* pic_;
    std::vector<std::wstring>* candidates_;
    bool* success_;
    LONG refCount_;
};

TsfMonitor::TsfMonitor() {
    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfMonitor created\n");
}

TsfMonitor::~TsfMonitor() {
    shutdown();
    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfMonitor destroyed\n");
}

bool TsfMonitor::initialize(IUnknown* pThreadMgr) {
    if (!pThreadMgr) return false;

    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfMonitor::initialize: starting\n");

    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfThreadMgr, (void**)&threadMgr_);
    if (FAILED(hr)) {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] Failed to get ITfThreadMgr\n");
        return false;
    }

    hr = threadMgr_->GetFocus(&docMgr_);
    if (FAILED(hr) || !docMgr_) {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] No focused document manager\n");
    }

    // Get ITfSource from ITfThreadMgr for sink registration
    ITfSource* source = nullptr;
    hr = threadMgr_->QueryInterface(IID_ITfSource, (void**)&source);
    if (FAILED(hr) || !source) {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] QueryInterface ITfSource failed: hr=0x%X\n", hr);
        OutputDebugStringA(dbg);
    } else {
        // QueryInterface gives us refcount=1
        // AdviseSink will addRef, UnadviseSink will Release

        // Register profile activation sink (for detecting IME type changes)
        DWORD profileCookie = TF_INVALID_COOKIE;
        hr = source->AdviseSink(IID_ITfInputProcessorProfileActivationSink,
            static_cast<ITfInputProcessorProfileActivationSink*>(this), &profileCookie);
        if (SUCCEEDED(hr)) {
            profileSinkCookie_ = profileCookie;
            threadMgrSource_ = source;  // Take ownership
            DEBUG_LOG_SIMPLE(L"[ChineseIME] Profile sink registered\n");
        } else {
            char dbg[128];
            sprintf_s(dbg, "[ChineseIME] AdviseSink(Profile) failed: hr=0x%X\n", hr);
            OutputDebugStringA(dbg);
        }

        // Register UI element sink for candidate list notifications
        // Use the SAME source pointer - ITfSource supports multiple sinks
        DWORD uiElemCookie = TF_INVALID_COOKIE;
        hr = source->AdviseSink(IID_ITfUIElementSink,
            static_cast<ITfUIElementSink*>(this), &uiElemCookie);
        if (SUCCEEDED(hr)) {
            uiElementSinkCookie_ = uiElemCookie;
            OutputDebugStringA("[ChineseIME] UIElement sink registered OK\n");
            // If profile sink failed, we still own the source reference via UIElement sink
            if (!threadMgrSource_) {
                threadMgrSource_ = source;  // Take ownership
            }
        } else {
            char dbg[128];
            sprintf_s(dbg, "[ChineseIME] UIElement sink AdviseSink failed: hr=0x%X\n", hr);
            OutputDebugStringA(dbg);
            // If profile sink also failed, release our reference
            if (!threadMgrSource_) {
                source->Release();
                source = nullptr;
            }
        }
    }

    // Get UI element manager for reading candidates from UI elements
    ITfUIElementMgr* uiElemMgr = nullptr;
    hr = threadMgr_->QueryInterface(IID_ITfUIElementMgr, (void**)&uiElemMgr);
    if (SUCCEEDED(hr) && uiElemMgr) {
        uiElementMgr_ = uiElemMgr;
        OutputDebugStringA("[ChineseIME] UIElementMgr obtained\n");
    }

    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfMonitor::initialize: done\n");

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

    // Unregister UI element sink
    if (threadMgrSource_ && uiElementSinkCookie_ != TF_INVALID_COOKIE) {
        threadMgrSource_->UnadviseSink(uiElementSinkCookie_);
        uiElementSinkCookie_ = TF_INVALID_COOKIE;
    }

    // Release UI element manager
    if (uiElementMgr_) {
        uiElementMgr_->Release();
        uiElementMgr_ = nullptr;
    }

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

void TsfMonitor::pollUpdate() {
    // Always try to query the current input method on each poll
    // The HKL might have changed
    queryCurrentInputMethod();

    // If we detected a type, update the state manager
    if (currentInputMethod_ != InputMethodType::UNKNOWN && currentInputMethod_ != InputMethodType::ENGLISH) {
        ImeStateManager::get().updateInputMethod(currentInputMethod_);
    }

    // Also check via HKL for consistency - use foreground window's thread
    HWND fgWnd = GetForegroundWindow();
    if (fgWnd) {
        DWORD fgThreadId = GetWindowThreadProcessId(fgWnd, nullptr);
        HKL hkl = GetKeyboardLayout(fgThreadId);
        if (hkl) {
            InputMethodType hklType = detectInputMethodTypeFromHklSafe(hkl);

            // If HKL indicates non-Chinese keyboard (ENGLISH), force update to ENGLISH
            if (hklType == InputMethodType::ENGLISH || hklType == InputMethodType::UNKNOWN) {
                if (currentInputMethod_ != InputMethodType::ENGLISH) {
                    currentInputMethod_ = InputMethodType::ENGLISH;
                    ImeStateManager::get().updateInputMethod(InputMethodType::ENGLISH);
                }
            } else if (hklType != currentInputMethod_) {
                currentInputMethod_ = hklType;
                ImeStateManager::get().updateInputMethod(hklType);
            }
        }
    }

    // Update candidates and composition from TSF/IMM
    // This is critical for showing candidates in the HUD
    updateCache();

    // FALLBACK: If IMM32 returned 0 candidates but we're in Chinese mode,
    // try reading candidates from the IME's candidate window directly.
    // This is needed for TSF IMEs like Microsoft Pinyin and Sogou that
    // don't reliably expose candidates via IMM32 or TSF UIElement callbacks.
    auto state = ImeStateManager::get().getSnapshot();
    bool needsFallback = (state.inputMethodType != InputMethodType::ENGLISH &&
        state.inputMethodType != InputMethodType::UNKNOWN &&
        state.candidates.empty());
    bool needsFallbackEvenWithComp = needsFallback && !state.composition.empty();
    // Sogou may show candidates even with empty composition
    bool sogouNeedsFallback = (state.inputMethodType == InputMethodType::SOGOU ||
        state.inputMethodType == InputMethodType::PINYIN) && state.candidates.empty();

    if (needsFallbackEvenWithComp || sogouNeedsFallback) {
        std::vector<std::wstring> windowCandidates = chineseime::collectCandidatesFromWindowEnumeration();
        if (!windowCandidates.empty()) {
            char dbg[128];
            sprintf_s(dbg, "[ChineseIME] Window enumeration found %d candidates (type=%d)\n",
                (int)windowCandidates.size(), (int)state.inputMethodType);
            OutputDebugStringA(dbg);
            ImeStateManager::get().updateCandidates(state.composition, windowCandidates, 0);
        }
    }
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
    char dbg[512];
    sprintf_s(dbg, "[ChineseIME] OnActivated: profileType=%d, langid=0x%04X, hkl=0x%IX, flags=0x%X\n"
        "  clsid.Data1=0x%08X, guidProfile.Data1=0x%08X\n",
        (int)dwProfileType, langid, (DWORD64)hkl, dwFlags,
        clsid.Data1, guidProfile.Data1);
    OutputDebugStringA(dbg);

    // updateInputMethodType now updates ImeStateManager internally
    updateInputMethodType(langid, clsid, guidProfile);

    // If TSF detection gave UNKNOWN/ENGLISH, try HKL as fallback
    if (currentInputMethod_ == InputMethodType::UNKNOWN || currentInputMethod_ == InputMethodType::ENGLISH) {
        if (hkl) {
            InputMethodType hklType = detectInputMethodTypeFromHklSafe(hkl);
            sprintf_s(dbg, "[ChineseIME] OnActivated: HKL fallback type=%d\n", (int)hklType);
            OutputDebugStringA(dbg);
            if (hklType != InputMethodType::UNKNOWN && hklType != InputMethodType::ENGLISH) {
                currentInputMethod_ = hklType;
                ImeStateManager::get().updateInputMethod(hklType);
            }
        }
    }

    sprintf_s(dbg, "[ChineseIME] OnActivated: FINAL currentInputMethod_=%d\n", (int)currentInputMethod_);
    OutputDebugStringA(dbg);

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
        char dbg[256];
        sprintf_s(dbg, "[ChineseIME] updateCache: TSF candidatesFound=%d, candCnt=%d\n", 
            candidatesFound ? 1 : 0, (int)candidates.size());
        OutputDebugStringA(dbg);
        ctx->Release();
    } else {
        OutputDebugStringA("[ChineseIME] updateCache: TSF ctx=NULL, using IMM fallback\n");
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
    // Notify Java via WinEventBridge (TSF-initiated composition/candidate change)
    if (oldState.composition != newState.composition || oldState.candidates != newState.candidates) {
        std::vector<const wchar_t*> candidatePtrs;
        for (const auto& cand : newState.candidates) {
            candidatePtrs.push_back(cand.c_str());
        }
        WinEventBridge::get().fireCandidateCallback(
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

    if (ecReadOnly_ == 0) return false;

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
    if (!pic) return false;
    return getCandidateListFromProperty(pic, candidates, selectedIndex) && !candidates.empty();
}

bool TsfMonitor::getCandidateListFromProperty(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex) {
    candidates.clear();
    selectedIndex = 0;
    if (!pic) return false;

    bool success = false;
    TsfEditSession* session = new TsfEditSession(pic, &candidates, &success);
    HRESULT hrSession = S_OK;
    HRESULT hr = pic->RequestEditSession(TF_CLIENTID_NULL, session, TF_ES_SYNC | TF_ES_READ, &hrSession);
    session->Release();

    if (FAILED(hr) || !success) {
        return false;
    }

    return !candidates.empty();
}

void TsfMonitor::updateInputMethodType(LANGID langid, REFCLSID clsid, REFGUID guidProfile) {
    char dbg[1024];
    sprintf_s(dbg, "[ChineseIME] updateInputMethodType: langid=0x%04x, clsid.Data1=0x%08X, guidProfile.Data1=0x%08X\n",
        langid, clsid.Data1, guidProfile.Data1);
    OutputDebugStringA(dbg);

    currentInputMethod_ = detectInputMethodTypeFromGuid(guidProfile, clsid, langid);

    sprintf_s(dbg, "[ChineseIME] updateInputMethodType: detected=%d (%S)\n",
        (int)currentInputMethod_, getInputMethodTypeName(currentInputMethod_));
    OutputDebugStringA(dbg);

    if (currentInputMethod_ != InputMethodType::UNKNOWN && currentInputMethod_ != InputMethodType::ENGLISH) {
        ImeStateManager::get().updateInputMethod(currentInputMethod_);
    } else if (currentInputMethod_ == InputMethodType::ENGLISH) {
        ImeStateManager::get().updateInputMethod(InputMethodType::ENGLISH);
    }
}

void TsfMonitor::queryCurrentInputMethod() {
    static int queryCount = 0;
    static int g_debugCounter = 0;
    static InputMethodType g_lastLoggedType = InputMethodType::UNKNOWN;

    queryCount++;
    if (queryCount <= 3) {
        OutputDebugStringA("[ChineseIME] queryCurrentInputMethod called\n");
    }

    WCHAR klName[16] = {0};
    if (!GetKeyboardLayoutNameW(klName) || !klName[0]) {
        if (queryCount <= 3) OutputDebugStringA("[ChineseIME] queryCurrentInputMethod: klName failed\n");
        return;
    }

    if (queryCount <= 3) {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] queryCurrentInputMethod: klName=%S\n", klName);
        OutputDebugStringA(dbg);
    }

    bool typeFromTsf = false;
    InputMethodType descType = InputMethodType::UNKNOWN;

    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool comInitialized = SUCCEEDED(hr);

        ITfInputProcessorProfiles* profiles = nullptr;
        hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
            CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&profiles);
        if (SUCCEEDED(hr)) {
            // Method 1: Get ITfInputProcessorProfileMgr and use GetActiveProfile (MOST RELIABLE)
            ITfInputProcessorProfileMgr* profileMgr = nullptr;
            hr = profiles->QueryInterface(IID_ITfInputProcessorProfileMgr, (void**)&profileMgr);
            if (SUCCEEDED(hr) && profileMgr) {
                TF_INPUTPROCESSORPROFILE activeFullProfile;
                hr = profileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &activeFullProfile);
                if (SUCCEEDED(hr)) {
                    char dbg[256];
                    sprintf_s(dbg, "[ChineseIME] GetActiveProfile: langid=0x%04X, clsid=0x%08X, guid=%08X, type=%d\n",
                        activeFullProfile.langid, activeFullProfile.clsid.Data1,
                        activeFullProfile.guidProfile.Data1, (int)activeFullProfile.dwProfileType);
                    OutputDebugStringA(dbg);

                    if ((activeFullProfile.langid == 0x0804 || activeFullProfile.langid == 0x0404) &&
                        activeFullProfile.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR) {
                        updateInputMethodType(activeFullProfile.langid, activeFullProfile.clsid, activeFullProfile.guidProfile);
                        if (currentInputMethod_ != InputMethodType::UNKNOWN &&
                            currentInputMethod_ != InputMethodType::ENGLISH &&
                            currentInputMethod_ != InputMethodType::OTHER_CHINESE) {
                            typeFromTsf = true;
                            OutputDebugStringA("[ChineseIME] queryCurrentInputMethod: used GetActiveProfile\n");
                        }
                    }
                }
                profileMgr->Release();
            }

            // Method 3: Enumerate all profiles and find the active one
            IEnumTfLanguageProfiles* enumProfiles = nullptr;
            hr = profiles->EnumLanguageProfiles(0, &enumProfiles);
            if (SUCCEEDED(hr) && enumProfiles) {
                TF_LANGUAGEPROFILE tfProfile;
                ULONG fetched = 0;
                while (enumProfiles->Next(1, &tfProfile, &fetched) == S_OK) {
                    if (tfProfile.langid == 0x0804 || tfProfile.langid == 0x0404) {
                        char dbg[256];
                        sprintf_s(dbg, "[ChineseIME] TSF Enum: langid=0x%04X, clsid=0x%08X, fActive=%d, guidProfile=%08X\n",
                            tfProfile.langid, tfProfile.clsid.Data1, tfProfile.fActive ? 1 : 0,
                            tfProfile.guidProfile.Data1);
                        OutputDebugStringA(dbg);

                        // fActive is TRUE for the currently active profile
                        if (tfProfile.fActive && !typeFromTsf) {
                            updateInputMethodType(tfProfile.langid, tfProfile.clsid, tfProfile.guidProfile);
                            if (currentInputMethod_ != InputMethodType::UNKNOWN &&
                                currentInputMethod_ != InputMethodType::ENGLISH &&
                                currentInputMethod_ != InputMethodType::OTHER_CHINESE) {
                                typeFromTsf = true;
                                char dbg2[128];
                                sprintf_s(dbg2, "[ChineseIME] TSF Enum: accepted active profile type=%d\n", (int)currentInputMethod_);
                                OutputDebugStringA(dbg2);
                                // Don't break - keep enumerating for debug info
                            }
                        }
                    }
                }
                enumProfiles->Release();
            }
            profiles->Release();
        }

        if (comInitialized) CoUninitialize();
    }

    // Use description-based detection as additional check
    if (!typeFromTsf && descType != InputMethodType::UNKNOWN) {
        currentInputMethod_ = descType;
        ImeStateManager::get().updateInputMethod(descType);
        typeFromTsf = true;
        char dbgDesc[128];
        sprintf_s(dbgDesc, "[ChineseIME] queryCurrentInputMethod: used description-based detection -> %d\n", (int)descType);
        OutputDebugStringA(dbgDesc);
    }

    if (!typeFromTsf) {
        // Last resort: HKL-based detection
        HKL hkl = GetKeyboardLayout(0);
        InputMethodType hklType = detectInputMethodTypeFromHklSafe(hkl);
        if (hklType != InputMethodType::UNKNOWN && hklType != InputMethodType::ENGLISH) {
            currentInputMethod_ = hklType;
            ImeStateManager::get().updateInputMethod(hklType);
        }

        g_debugCounter++;
        if (g_debugCounter % 600 == 0 || currentInputMethod_ != g_lastLoggedType) {
            char dbg[128];
            sprintf_s(dbg, "[ChineseIME] IME (HKL fallback): klName=%S, type=%d\n", klName, (int)currentInputMethod_);
            OutputDebugStringA(dbg);
            g_lastLoggedType = currentInputMethod_;
        }
    }
}

} // namespace chineseime