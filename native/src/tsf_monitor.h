#pragma once

#include "ime_state_manager.h"
#include "sta_thread.h"
#include <msctf.h>
#include <string>
#include <vector>

#ifndef GUID_COMPARTMENT_KEYBOARD_INPUTMODE
extern const GUID GUID_COMPARTMENT_KEYBOARD_INPUTMODE;
#endif

extern const GUID GUID_MS_PINYIN;
extern const GUID GUID_MS_ZHUYIN;
extern const GUID GUID_MS_CANGJIE;
extern const GUID GUID_MS_WUBI;
extern const GUID GUID_MS_SUCHENG;

#ifndef GUID_PROP_CANDIDATE
extern const GUID GUID_PROP_CANDIDATE;
#endif

namespace chineseime {

class TsfMonitor : public ITfTextEditSink,
                   public ITfKeyEventSink,
                   public ITfInputProcessorProfileActivationSink,
                   public ITfCompartmentEventSink,
                   public ITfUIElementSink {
public:
    TsfMonitor();
    virtual ~TsfMonitor();

    bool initialize(IUnknown* pThreadMgr);
    void shutdown();
    void refreshState();

    bool isChineseMode() const { return chineseMode_; }
    InputMethodType getInputMethodType() const { return currentInputMethod_; }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP OnEndEdit(ITfContext* pic, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord) override;

    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;

    STDMETHODIMP OnActivated(DWORD dwProfileType, LANGID langid, REFCLSID clsid,
                             REFGUID guidProfile, REFGUID guidCat, HKL hkl, DWORD dwFlags) override;

    STDMETHODIMP OnChange(REFGUID rguid) override;

    STDMETHODIMP BeginUIElement(DWORD dwUIElementId, BOOL* pbShow) override;
    STDMETHODIMP UpdateUIElement(DWORD dwUIElementId) override;
    STDMETHODIMP EndUIElement(DWORD dwUIElementId) override;

private:
    void updateInputMethodType(LANGID langid, REFCLSID clsid, REFGUID guidProfile);
    bool detectChineseMode();
    void updateCache();
    void notifyStateChanges(const IMEState& oldState, const IMEState& newState);
    bool registerSinks(ITfContext* pic);
    void unregisterSinks();
    ITfContext* getCurrentContext();
    bool getCompositionString(ITfContext* pic, std::wstring& result);
    bool getCandidateList(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex);
    bool getCandidateListFromProperty(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex);
    void queryCurrentInputMethod();

    InputMethodType currentInputMethod_ = InputMethodType::UNKNOWN;
    LONG refCount_ = 1;
    ITfThreadMgr* threadMgr_ = nullptr;
    ITfDocumentMgr* docMgr_ = nullptr;
    ITfContext* context_ = nullptr;
    ITfSource* contextSource_ = nullptr;
    ITfSource* threadMgrSource_ = nullptr;

    DWORD editSinkCookie_ = TF_INVALID_COOKIE;
    DWORD keyEventSinkCookie_ = TF_INVALID_COOKIE;
    DWORD profileSinkCookie_ = TF_INVALID_COOKIE;
    DWORD compartmentSinkCookie_ = TF_INVALID_COOKIE;

    bool chineseMode_ = false;
    TfEditCookie ecReadOnly_ = 0;
};

} // namespace chineseime