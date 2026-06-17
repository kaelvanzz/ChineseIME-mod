#include "ime_bridge.h"
#include "ime_state_manager.h"
#include "tsf_monitor.h"
#include "imm32_monitor.h"
#include "ime_callback.h"
#include "sta_thread.h"
#include "win_event_bridge.h"
#include <windows.h>
#include <imm.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <future>
#include <algorithm>
#include <debugapi.h>

#pragma comment(lib, "imm32.lib")

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#ifdef CHINESEIME_DEBUG
#define DEBUG_LOG(format, ...) do { \
    char buf[512]; \
    sprintf_s(buf, format, __VA_ARGS__); \
    OutputDebugStringA(buf); \
} while(0)
#define DEBUG_LOG_SIMPLE(msg) OutputDebugStringA(msg)
#else
#define DEBUG_LOG(format, ...)
#define DEBUG_LOG_SIMPLE(msg)
#endif

namespace {

PreeditCallback g_preeditCallback = nullptr;
CommitCallback g_commitCallback = nullptr;
CandidateCallback g_candidateCallback = nullptr;
ImeChangeCallback g_imeChangeCallback = nullptr;
KeyboardCallback g_keyboardCallback = nullptr;

std::unique_ptr<chineseime::StaThread> g_staThread;
std::unique_ptr<chineseime::TsfMonitor> g_tsfMonitor;
std::unique_ptr<chineseime::Imm32Monitor> g_imm32Monitor;
std::atomic<bool> g_tsfInitialized{false};
std::atomic<bool> g_imm32Initialized{false};
static DWORD g_dwTfClientId = TF_CLIENTID_NULL;
static HWND g_targetWindow = nullptr;
static HWND g_hwnd = nullptr;
static HIMC g_himc = nullptr;
static HHOOK g_callWndProcHook = nullptr;
static WNDPROC g_originalWndProc = nullptr;
static std::atomic<bool> g_pollingRunning{false};
static std::thread g_pollingThread;
static void (*g_javaCandidates)(const wchar_t**, int, int) = nullptr;

static inline bool isChinese(wchar_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF);
}

const char* VERSION = "2.2.0";

bool IsChineseLangId(LANGID langId) {
    return langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404;
}

chineseime::InputMethodType DetectInputMethodTypeFromHkl(HKL hkl) {
    LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
    DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
    WORD imeId = HIWORD(hklValue);
    chineseime::InputMethodType type = chineseime::detectInputMethodTypeFromImeId(imeId, langId);
    if (type == chineseime::InputMethodType::OTHER_CHINESE && IsChineseLangId(langId)) {
        WCHAR klName[16] = {0};
        if (GetKeyboardLayoutNameW(klName)) {
            WCHAR layoutLow = klName[0] ? klName[7] : 0;
            WCHAR layoutHigh = klName[0] ? klName[6] : 0;
            switch (layoutLow) {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                type = chineseime::detectInputMethodTypeFromImeId(
                    static_cast<WORD>(layoutLow - L'0' + ((layoutHigh - L'0') << 4)), langId);
                break;
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': {
                WORD lowNibble = (layoutLow >= L'a') ? (WORD)(layoutLow - L'a' + 10) : (WORD)(layoutLow - L'A' + 10);
                WORD highNibble = (layoutHigh >= L'a') ? (WORD)(layoutHigh - L'a' + 10) : (WORD)(layoutHigh - L'A' + 10);
                type = chineseime::detectInputMethodTypeFromImeId(
                    static_cast<WORD>(lowNibble + (highNibble << 4)), langId);
                break;
            }
            }
        }
    }
    return type;
}

void PollKeyboardState() {
    bool capsLockOn = false;
    bool shiftPressed = false;

    HWND fgWnd = g_targetWindow;
    if (!fgWnd) fgWnd = GetForegroundWindow();
    if (!fgWnd) fgWnd = GetActiveWindow();

    if (fgWnd) {
        DWORD fgThreadId = GetWindowThreadProcessId(fgWnd, nullptr);
        DWORD pollThreadId = GetCurrentThreadId();
        if (fgThreadId != pollThreadId) {
            AttachThreadInput(pollThreadId, fgThreadId, TRUE);
        }

        BYTE keyboardState[256];
        if (GetKeyboardState(keyboardState)) {
            capsLockOn = (keyboardState[VK_CAPITAL] & 0x01) != 0;
            shiftPressed = (keyboardState[VK_SHIFT] & 0x80) != 0;
        }

        if (fgThreadId != pollThreadId) {
            AttachThreadInput(pollThreadId, fgThreadId, FALSE);
        }
    } else {
        BYTE keyboardState[256];
        if (GetKeyboardState(keyboardState)) {
            capsLockOn = (keyboardState[VK_CAPITAL] & 0x01) != 0;
            shiftPressed = (keyboardState[VK_SHIFT] & 0x80) != 0;
        }
    }

    chineseime::ImeStateManager::get().updateKeyboardState(capsLockOn, shiftPressed);
}

void PollIMEState() {
    auto& mgr = chineseime::ImeStateManager::get();

    HWND fgWnd = g_targetWindow;
    if (!fgWnd) fgWnd = GetForegroundWindow();
    if (!fgWnd) fgWnd = GetActiveWindow();
    if (!fgWnd) return;

    HIMC himc = ImmGetContext(fgWnd);
    if (!himc) {
        HWND testWnd = GetForegroundWindow();
        if (testWnd && testWnd != fgWnd) {
            HIMC testHimc = ImmGetContext(testWnd);
            if (testHimc) {
                himc = testHimc;
                fgWnd = testWnd;
            }
        }
    }
    if (!himc) return;

    bool imeOpen = ImmGetOpenStatus(himc) != 0;
    mgr.updateImeOpen(imeOpen);

    bool chineseMode = false;
    DWORD conversion = 0;
    DWORD sentence = 0;
    if (ImmGetConversionStatus(himc, &conversion, &sentence)) {
        chineseMode = (conversion & IME_CMODE_NATIVE) != 0;
    }
    if (!imeOpen) {
        chineseMode = false;
    }
    mgr.updateChineseMode(chineseMode);

    DWORD fgThreadId = GetWindowThreadProcessId(fgWnd, nullptr);
    DWORD pollThreadId = GetCurrentThreadId();
    BOOL attached = FALSE;
    if (fgThreadId != pollThreadId) {
        attached = AttachThreadInput(pollThreadId, fgThreadId, TRUE);
    }

    HKL hkl = GetKeyboardLayout(0);
    chineseime::InputMethodType detectedType = chineseime::InputMethodType::UNKNOWN;
    if (hkl) {
        detectedType = DetectInputMethodTypeFromHkl(hkl);
    }

    if (attached) {
        AttachThreadInput(pollThreadId, fgThreadId, FALSE);
    }

    auto cachedType = mgr.getSnapshot().inputMethodType;
    bool tsfHasSetType = (cachedType != chineseime::InputMethodType::UNKNOWN &&
                          cachedType != chineseime::InputMethodType::ENGLISH);
    if (!imeOpen) {
        if (!tsfHasSetType) {
            mgr.updateInputMethod(chineseime::InputMethodType::ENGLISH);
        }
    } else {
        if (tsfHasSetType) {
            if (detectedType != chineseime::InputMethodType::UNKNOWN &&
                detectedType != chineseime::InputMethodType::ENGLISH &&
                detectedType != cachedType) {
                mgr.updateInputMethod(detectedType);
            }
        } else {
            if (detectedType != chineseime::InputMethodType::UNKNOWN &&
                detectedType != chineseime::InputMethodType::ENGLISH) {
                mgr.updateInputMethod(detectedType);
            } else {
                mgr.updateInputMethod(chineseime::InputMethodType::PINYIN);
            }
        }
    }

    std::wstring composition;
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

    std::vector<std::wstring> candidates;
    int selectedIndex = 0;
    size_t bufSize = ImmGetCandidateList(himc, 0, nullptr, 0);

    char dbgBuf[256];
    sprintf_s(dbgBuf, "[ChineseIME] PollIME: fgWnd=0x%X, himc=0x%X, bufSize=%zu, imeOpen=%d, compLen=%d\n",
        (DWORD)(DWORD_PTR)fgWnd, (DWORD)(DWORD_PTR)himc, bufSize, imeOpen, (int)compLen);
    OutputDebugStringA(dbgBuf);

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
        sprintf_s(dbgBuf, "[ChineseIME] PollIME: got %d candidates, sel=%d\n", (int)count, selectedIndex);
        OutputDebugStringA(dbgBuf);
    }

    mgr.updateCandidates(composition, candidates, selectedIndex);

    ImmReleaseContext(fgWnd, himc);
}

} // anonymous namespace

void setJavaCallbacks(
    void(*preedit)(const wchar_t*, int, int),
    void(*commit)(const wchar_t*),
    void(*candidates)(const wchar_t**, int, int),
    void(*imeChange)(int, int))
{
}

namespace chineseime {

void onImeStateChanged(int imeType, int chineseMode) {
    WinEventBridge::get().fireImeStateCallback(imeType, chineseMode);
}

void onCandidateChanged(const wchar_t* composition, const wchar_t** candidates, int count, int selectedIndex) {
    WinEventBridge::get().fireCandidateCallback(composition, candidates, count, selectedIndex);
}

void onKeyboardStateChanged(int capsLock, int shiftMode) {
}

} // namespace chineseime

// ── Exported DLL functions ──

extern "C" {

__declspec(dllexport) void SetCallbacks(void* candidateUpdate, void* layoutChange, void* modeChange, void* keyboardState) {
}

__declspec(dllexport) int StartListen(void* hwnd) {
    if (g_tsfInitialized.load() || g_imm32Initialized.load()) return 1;

    g_targetWindow = hwnd ? reinterpret_cast<HWND>(hwnd) : nullptr;

    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        chineseime::InputMethodType type = DetectInputMethodTypeFromHkl(hkl);
        chineseime::ImeStateManager::get().updateInputMethod(type);
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        bool isChineseLang = IsChineseLangId(langId);
        chineseime::ImeStateManager::get().updateChineseMode(isChineseLang);
        chineseime::ImeStateManager::get().updateImeOpen(isChineseLang);
    }

    g_hwnd = g_targetWindow;
    g_himc = ImmGetContext(g_targetWindow);
    if (g_himc) {
        ImmReleaseContext(g_targetWindow, g_himc);
    } else {
        g_himc = ImmCreateContext();
    }

    return 1;
}

__declspec(dllexport) int HookWindowProcRaw(ULONG_PTR hwnd) {
    if (!hwnd) return 0;
    HWND h = (HWND)hwnd;
    if (g_hwnd == h) return 1;

    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] Hook attempt, hwnd=0x%IX\n", (UINT64)hwnd);
    OutputDebugStringA(dbg);

    if (!IsWindow(h)) {
        OutputDebugStringA("[ChineseIME] Not a valid window\n");
        return 0;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    sprintf_s(dbg, "[ChineseIME] Window PID: %u, current PID: %u\n", pid, GetCurrentProcessId());
    OutputDebugStringA(dbg);

    LONG_PTR oldProc = GetWindowLongPtr(h, GWLP_WNDPROC);
    DWORD errAfterGet = GetLastError();
    sprintf_s(dbg, "[ChineseIME] Original WndProc: 0x%IX, GetLastError: %d\n", (UINT64)oldProc, errAfterGet);
    OutputDebugStringA(dbg);

    if (oldProc) g_originalWndProc = (WNDPROC)oldProc;

    LONG_PTR style = GetWindowLongPtr(h, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtr(h, GWL_EXSTYLE);
    sprintf_s(dbg, "[ChineseIME] Window styles: style=0x%IX, exStyle=0x%IX\n", style, exStyle);
    OutputDebugStringA(dbg);

    if (exStyle & WS_EX_LAYERED) {
        OutputDebugStringA("[ChineseIME] WARNING: Window has WS_EX_LAYERED\n");
    }
    if (exStyle & WS_EX_TOOLWINDOW) {
        OutputDebugStringA("[ChineseIME] WARNING: Window has WS_EX_TOOLWINDOW\n");
    }
    if (!(style & WS_VISIBLE)) {
        OutputDebugStringA("[ChineseIME] WARNING: Window is not visible\n");
    }

    char className[256];
    int classLen = GetClassNameA(h, className, sizeof(className) - 1);
    className[classLen] = 0;
    sprintf_s(dbg, "[ChineseIME] Window class: '%s'\n", className);
    OutputDebugStringA(dbg);

    if (strstr(className, "GLFW") != nullptr) {
        OutputDebugStringA("[ChineseIME] GLFW window detected\n");
    }

    g_tsfInitialized.store(true);

    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        chineseime::InputMethodType type = DetectInputMethodTypeFromHkl(hkl);
        chineseime::ImeStateManager::get().updateInputMethod(type);
        chineseime::ImeStateManager::get().updateChineseMode(IsChineseLangId(langId));
        chineseime::ImeStateManager::get().updateImeOpen(IsChineseLangId(langId));
    }

    g_pollingRunning.store(true);
    g_pollingThread = std::thread([]() {
        DEBUG_LOG_SIMPLE("[ChineseIME] Polling thread started\n");
        PollKeyboardState();
        PollIMEState();
        {
            auto initialState = chineseime::ImeStateManager::get().getSnapshot();
            bool isChineseIM = initialState.inputMethodType != chineseime::InputMethodType::ENGLISH &&
                initialState.inputMethodType != chineseime::InputMethodType::UNKNOWN;
            char buf[256];
            sprintf_s(buf, "[ChineseIME] Init: IME=%d, CMode=%d, Caps=%d, ShiftM=%d\n",
                (int)initialState.inputMethodType, initialState.chineseMode ? 1 : 0,
                initialState.capsLockOn ? 1 : 0, (isChineseIM && !initialState.chineseMode && initialState.imeOpen) ? 1 : 0);
            OutputDebugStringA(buf);
            chineseime::onImeStateChanged(static_cast<int>(initialState.inputMethodType), initialState.chineseMode);
            chineseime::onKeyboardStateChanged(initialState.capsLockOn ? 1 : 0,
                (isChineseIM && !initialState.chineseMode && initialState.imeOpen) ? 1 : 0);
        }
        while (g_pollingRunning.load()) {
            PollKeyboardState();
            PollIMEState();

            auto changes = chineseime::ImeStateManager::get().checkChanges();
            auto state = chineseime::ImeStateManager::get().getSnapshot();

            if (changes.inputMethodChanged || changes.chineseModeChanged) {
                char buf[256];
                sprintf_s(buf, "[ChineseIME] State: IME=%d, CMode=%d\n",
                    (int)state.inputMethodType, state.chineseMode ? 1 : 0);
                OutputDebugStringA(buf);
                chineseime::onImeStateChanged(static_cast<int>(state.inputMethodType), state.chineseMode);
            }

            if (changes.candidatesChanged || changes.compositionChanged) {
                std::vector<const wchar_t*> ptrs;
                for (const auto& c : state.candidates) {
                    ptrs.push_back(c.c_str());
                }
                char buf[256];
                sprintf_s(buf, "[ChineseIME] Candidates: comp='%S', count=%d\n",
                    state.composition.c_str(), (int)ptrs.size());
                OutputDebugStringA(buf);
                chineseime::onCandidateChanged(
                    state.composition.c_str(),
                    ptrs.empty() ? nullptr : ptrs.data(),
                    static_cast<int>(ptrs.size()),
                    state.selectedIndex
                );
            }

            if (changes.capsLockChanged || changes.shiftModeChanged) {
                bool inShiftMode = (state.inputMethodType != chineseime::InputMethodType::ENGLISH &&
                    state.inputMethodType != chineseime::InputMethodType::UNKNOWN &&
                    !state.chineseMode);
                char buf[256];
                sprintf_s(buf, "[ChineseIME] Kbd: Caps=%d, ShiftM=%d\n",
                    state.capsLockOn ? 1 : 0, inShiftMode ? 1 : 0);
                OutputDebugStringA(buf);
                chineseime::onKeyboardStateChanged(state.capsLockOn ? 1 : 0, inShiftMode ? 1 : 0);
            }

            Sleep(16);
        }
        DEBUG_LOG_SIMPLE("[ChineseIME] Polling thread stopped\n");
    });
    return 1;
}

__declspec(dllexport) void StopTsfListen(void) {
    g_pollingRunning.store(false);

    if (g_pollingThread.joinable()) {
        std::thread tmpThread = std::move(g_pollingThread);
        tmpThread.detach();
    }
}

__declspec(dllexport) int IsTsfListening(void) {
    return g_tsfInitialized.load() ? 1 : 0;
}

__declspec(dllexport) int GetCandidateCount(void) {
    if (!g_hwnd) return 0;
    HIMC himc = ImmGetContext(g_hwnd);
    if (!himc) return 0;
    DWORD count = 0;
    DWORD bufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
    if (bufSize > 0) {
        std::vector<char> buf(bufSize);
        CANDIDATELIST* candList = (CANDIDATELIST*)buf.data();
        if (ImmGetCandidateListW(himc, 0, candList, bufSize) > 0) {
            count = candList->dwCount;
        }
    }
    ImmReleaseContext(g_hwnd, himc);
    return (int)count;
}

__declspec(dllexport) int GetCompositionString(wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;

    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (!state.composition.empty()) {
        int len = std::min(bufferSize - 1, (int)state.composition.size());
        wcsncpy_s(buffer, bufferSize, state.composition.c_str(), len);
        buffer[len] = 0;
        return len;
    }
    return 0;
}

__declspec(dllexport) int GetCandidate(int index, wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (index >= 0 && index < (int)state.candidates.size()) {
        const std::wstring& cand = state.candidates[index];
        int len = std::min(bufferSize - 1, (int)cand.size());
        wcsncpy_s(buffer, bufferSize, cand.c_str(), len);
        buffer[len] = 0;
        return len;
    }
    return 0;
}

__declspec(dllexport) int GetSelectedCandidateIndex(void) {
    return chineseime::ImeStateManager::get().getSnapshot().selectedIndex;
}

__declspec(dllexport) int GetImeOpenStatus(void) {
    return chineseime::ImeStateManager::get().getSnapshot().imeOpen ? 1 : 0;
}

__declspec(dllexport) int GetTsfChineseMode(void) {
    return chineseime::ImeStateManager::get().getSnapshot().chineseMode ? 1 : 0;
}

__declspec(dllexport) int HasTsfLayoutChanged(void) {
    return chineseime::ImeStateManager::get().checkLayoutChanged() ? 1 : 0;
}

__declspec(dllexport) int GetInputMethodType(void) {
    return (int)chineseime::ImeStateManager::get().getSnapshot().inputMethodType;
}

__declspec(dllexport) int GetShiftMode(void) {
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    bool isChineseInputMethod = state.inputMethodType != chineseime::InputMethodType::ENGLISH &&
        state.inputMethodType != chineseime::InputMethodType::UNKNOWN;
    bool inShiftMode = isChineseInputMethod && !state.chineseMode && state.imeOpen;
    return inShiftMode ? 1 : 0;
}

__declspec(dllexport) int GetKeyboardStateForPolling(int vKey) {
    BYTE keyboardState[256];
    if (GetKeyboardState(keyboardState)) {
        return (keyboardState[vKey] & 0x80) ? 1 : 0;
    }
    return 0;
}

__declspec(dllexport) void SetTargetWindow(void* hwnd) {
    g_targetWindow = hwnd ? reinterpret_cast<HWND>(hwnd) : nullptr;
}

__declspec(dllexport) void RefreshImeState(void) {
    PollKeyboardState();
    PollIMEState();
}

__declspec(dllexport) void FreeBuffer(void* ptr) {
    if (ptr) CoTaskMemFree(ptr);
}

__declspec(dllexport) const char* GetDllVersion(void) {
    return VERSION;
}

__declspec(dllexport) int HasLayoutChanged(void) {
    return chineseime::ImeStateManager::get().checkLayoutChanged() ? 1 : 0;
}

__declspec(dllexport) void SetEventCallbacks(
    void* preedit,
    void* commit,
    void* candidate,
    void* imeChange,
    void* keyboard) {
    g_preeditCallback = reinterpret_cast<PreeditCallback>(preedit);
    g_commitCallback = reinterpret_cast<CommitCallback>(commit);
    g_candidateCallback = reinterpret_cast<CandidateCallback>(candidate);
    g_imeChangeCallback = reinterpret_cast<ImeChangeCallback>(imeChange);
    g_keyboardCallback = reinterpret_cast<KeyboardCallback>(keyboard);

    chineseime::WinEventBridge::get().setCallbacks({
        [preedit](const wchar_t* text, int cursorPos, int selStart, int selLen) {
            if (g_preeditCallback) g_preeditCallback(text, cursorPos, selStart, selLen);
        },
        [commit](const wchar_t* text) {
            if (g_commitCallback) g_commitCallback(text);
        },
        [candidate](const wchar_t** cands, int count, int selIdx) {
            if (g_candidateCallback) g_candidateCallback(cands, count, selIdx);
        },
        [](int imeType, int cmode) {
            if (g_imeChangeCallback) g_imeChangeCallback(imeType, cmode);
        },
        [](int caps, int shift) {
            if (g_keyboardCallback) g_keyboardCallback(caps, shift);
        }
    });

    OutputDebugStringA("[ChineseIME] Event callbacks registered\n");
}

__declspec(dllexport) void HookWindowProc(void* hwnd) {
    char dbg[128];
    sprintf_s(dbg, "[ChineseIME] HookWindowProc called with hwnd=0x%llX\n", (unsigned long long)hwnd);
    OutputDebugStringA(dbg);

    HWND h = hwnd ? reinterpret_cast<HWND>(hwnd) : nullptr;
    sprintf_s(dbg, "[ChineseIME] HookWindowProc: HWND from void* = 0x%p\n", (void*)h);
    OutputDebugStringA(dbg);

    if (h) {
        SetTargetWindow(hwnd);
        sprintf_s(dbg, "[ChineseIME] HookWindowProc: calling hookWindow\n");
        OutputDebugStringA(dbg);
    }
    chineseime::WinEventBridge::get().hookWindow(h);
    sprintf_s(dbg, "[ChineseIME] HookWindowProc: hookWindow returned, hooked=%d\n",
        chineseime::WinEventBridge::get().isHooked() ? 1 : 0);
    OutputDebugStringA(dbg);
}

__declspec(dllexport) void UnhookWindowProc(void) {
    chineseime::WinEventBridge::get().unhookWindow();
}

__declspec(dllexport) void RefreshCandidates(void) {
    chineseime::WinEventBridge::get().refreshCandidates();
}

__declspec(dllexport) int IsWindowHooked(void) {
    return chineseime::WinEventBridge::get().isHooked() ? 1 : 0;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        DEBUG_LOG_SIMPLE("[ChineseIME] DLL loaded\n");
        break;
    case DLL_PROCESS_DETACH:
        StopTsfListen();
        DEBUG_LOG_SIMPLE("[ChineseIME] DLL unloaded\n");
        break;
    }
    return TRUE;
}