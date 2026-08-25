#include <windows.h>
#include <uiautomation.h>
#include <vector>
#include <string>
#include <comdef.h>
#include <atomic>

#pragma comment(lib, "uiautomationcore.lib")

static std::atomic<bool> g_uiaInitialized(false);
static IUIAutomation* g_pUIA = nullptr;

static bool InitializeUIA() {
    if (g_uiaInitialized.load(std::memory_order_acquire)) {
        return g_pUIA != nullptr;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        OutputDebugStringA("[ChineseIME] UIA: CoInitializeEx failed\n");
        g_uiaInitialized.store(true, std::memory_order_release);
        return false;
    }

    hr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER,
        IID_IUIAutomation,
        (void**)&g_pUIA
    );

    if (FAILED(hr) || !g_pUIA) {
        OutputDebugStringA("[ChineseIME] UIA: CoCreateInstance failed\n");
        CoUninitialize();
        g_pUIA = nullptr;
        g_uiaInitialized.store(true, std::memory_order_release);
        return false;
    }

    OutputDebugStringA("[ChineseIME] UIA: initialized successfully\n");
    g_uiaInitialized.store(true, std::memory_order_release);
    return true;
}

struct UIAutoCandidateResult {
    std::vector<std::wstring> candidates;
    int selectedIndex = 0;
};

static void GetCandidatesFromElement(IUIAutomationElement* elem, UIAutoCandidateResult& result) {
    if (!elem) return;

    BSTR name = nullptr;
    if (elem->get_CurrentName(&name) == S_OK && name && SysStringLen(name) > 0) {
        std::wstring text(name, SysStringLen(name));
        SysFreeString(name);

        bool hasChinese = false;
        for (wchar_t c : text) {
            if (c >= 0x4E00 && c <= 0x9FFF) {
                hasChinese = true;
                break;
            }
        }

        if (hasChinese && text.size() <= 30) {
            result.candidates.push_back(text);
        }
    }
}

static void SearchElementTree(IUIAutomationElement* parent, UIAutoCandidateResult& result, int depth) {
    if (!parent || depth > 15 || result.candidates.size() >= 10) return;

    IUIAutomationTreeWalker* walker = nullptr;
    if (!g_pUIA) return;

    HRESULT hr = g_pUIA->get_ControlViewWalker(&walker);
    if (FAILED(hr) || !walker) return;

    IUIAutomationElement* child = nullptr;
    hr = walker->GetFirstChildElement(parent, &child);

    while (SUCCEEDED(hr) && child) {
        CONTROLTYPEID ctrlType = 0;
        child->get_CurrentControlType(&ctrlType);

        if (ctrlType == UIA_ListItemControlTypeId) {
            GetCandidatesFromElement(child, result);
        }

        SearchElementTree(child, result, depth + 1);

        IUIAutomationElement* next = nullptr;
        hr = walker->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }

    walker->Release();
}

static bool GetCandidatesFromWindow(HWND hwnd, UIAutoCandidateResult& result) {
    if (!hwnd || !g_pUIA) return false;

    IUIAutomationElement* windowElem = nullptr;
    HRESULT hr = g_pUIA->ElementFromHandle(hwnd, &windowElem);
    if (FAILED(hr) || !windowElem) return false;

    BSTR name = nullptr;
    windowElem->get_CurrentName(&name);
    if (name) {
        char dbg[256];
        sprintf_s(dbg, "[ChineseIME] UIA: Window name='%S'\n", name);
        OutputDebugStringA(dbg);
        SysFreeString(name);
    }

    SearchElementTree(windowElem, result, 0);
    windowElem->Release();

    return !result.candidates.empty();
}

static bool GetCandidatesFromProcessWindows(DWORD targetPid, UIAutoCandidateResult& result) {
    if (!InitializeUIA()) return false;

    if (!g_pUIA) return false;

    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] UIA: Searching for candidates in PID %u\n", targetPid);
    OutputDebugStringA(dbg);

    IUIAutomationCondition* pidCondition = nullptr;
    VARIANT pidVar;
    pidVar.vt = VT_I4;
    pidVar.lVal = (LONG)targetPid;
    HRESULT hr = g_pUIA->CreatePropertyCondition(UIA_ProcessIdPropertyId, pidVar, &pidCondition);
    if (FAILED(hr) || !pidCondition) return false;

    IUIAutomationElement* rootElement = nullptr;
    hr = g_pUIA->GetRootElement(&rootElement);
    if (FAILED(hr) || !rootElement) {
        pidCondition->Release();
        return false;
    }

    IUIAutomationElementArray* found = nullptr;
    hr = rootElement->FindAll(TreeScope_Children, pidCondition, &found);

    rootElement->Release();
    pidCondition->Release();

    if (FAILED(hr) || !found) {
        sprintf_s(dbg, "[ChineseIME] UIA: FindAll failed, hr=0x%X\n", hr);
        OutputDebugStringA(dbg);
        return false;
    }

    int count = 0;
    found->get_Length(&count);
    sprintf_s(dbg, "[ChineseIME] UIA: Found %d top-level windows in target process\n", count);
    OutputDebugStringA(dbg);

    for (int i = 0; i < count && result.candidates.empty(); i++) {
        IUIAutomationElement* window = nullptr;
        if (found->GetElement(i, &window) == S_OK && window) {
            BSTR name = nullptr;
            window->get_CurrentName(&name);
            if (name) {
                sprintf_s(dbg, "[ChineseIME] UIA: Window[%d] name='%S'\n", i, name);
                OutputDebugStringA(dbg);
                SysFreeString(name);
            }

            SearchElementTree(window, result, 0);
            window->Release();
        }
    }

    found->Release();

    if (!result.candidates.empty()) {
        sprintf_s(dbg, "[ChineseIME] UIA: Found %zu candidates\n", result.candidates.size());
        OutputDebugStringA(dbg);
    }

    return !result.candidates.empty();
}

extern "C" {

__declspec(dllexport) int GetCandidatesViaUIA(int maxCandidates, wchar_t(*candidates)[64]) {
    if (!InitializeUIA()) {
        OutputDebugStringA("[ChineseIME] UIA: not initialized\n");
        return 0;
    }

    char dbg[256];

    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) {
        OutputDebugStringA("[ChineseIME] UIA: no foreground window\n");
        return 0;
    }

    DWORD fgPid = 0;
    GetWindowThreadProcessId(fgWnd, &fgPid);
    DWORD currentPid = GetCurrentProcessId();

    sprintf_s(dbg, "[ChineseIME] UIA: GetCandidatesViaUIA called, fgPid=%u, currentPid=%u\n", fgPid, currentPid);
    OutputDebugStringA(dbg);

    if (fgPid == currentPid || fgPid == 0) {
        OutputDebugStringA("[ChineseIME] UIA: skipping same process\n");
        return 0;
    }

    UIAutoCandidateResult result;

    if (!GetCandidatesFromProcessWindows(fgPid, result)) {
        sprintf_s(dbg, "[ChineseIME] UIA: no candidates found from process %u\n", fgPid);
        OutputDebugStringA(dbg);
        return 0;
    }

    sprintf_s(dbg, "[ChineseIME] UIA: found %zu candidates\n", result.candidates.size());
    OutputDebugStringA(dbg);

    int count = (int)result.candidates.size();
    if (count > maxCandidates) count = maxCandidates;

    for (int i = 0; i < count; i++) {
        wcsncpy_s(candidates[i], 64, result.candidates[i].c_str(), 63);
        candidates[i][63] = 0;
    }

    return count;
}

__declspec(dllexport) int GetCandidateCountUIA() {
    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] GetCandidateCountUIA: called\n");
    OutputDebugStringA(dbg);

    if (!InitializeUIA()) {
        OutputDebugStringA("[ChineseIME] GetCandidateCountUIA: UIA not initialized\n");
        return 0;
    }

    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) {
        OutputDebugStringA("[ChineseIME] GetCandidateCountUIA: no foreground window\n");
        return 0;
    }

    DWORD fgPid = 0;
    GetWindowThreadProcessId(fgWnd, &fgPid);
    DWORD currentPid = GetCurrentProcessId();

    sprintf_s(dbg, "[ChineseIME] GetCandidateCountUIA: fgPid=%u, currentPid=%u\n", fgPid, currentPid);
    OutputDebugStringA(dbg);

    if (fgPid == currentPid || fgPid == 0) {
        OutputDebugStringA("[ChineseIME] GetCandidateCountUIA: skipping same process\n");
        return 0;
    }

    UIAutoCandidateResult result;

    if (!GetCandidatesFromProcessWindows(fgPid, result)) {
        sprintf_s(dbg, "[ChineseIME] GetCandidateCountUIA: no candidates found in process %u\n", fgPid);
        OutputDebugStringA(dbg);
        return 0;
    }

    sprintf_s(dbg, "[ChineseIME] GetCandidateCountUIA: found %zu candidates\n", result.candidates.size());
    OutputDebugStringA(dbg);

    return (int)result.candidates.size();
}

} // extern "C"