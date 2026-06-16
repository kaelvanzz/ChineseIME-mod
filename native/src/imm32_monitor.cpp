#include "imm32_monitor.h"
#include "jni_callback.h"
#include "ime_state_manager.h"
#include <imm.h>
#include <algorithm>

#pragma comment(lib, "imm32.lib")

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

static Imm32Monitor* g_imm32Monitor = nullptr;
static std::atomic<bool> g_imm32Running{false};
static std::thread g_imm32PollThread;

Imm32Monitor::Imm32Monitor() {
    DEBUG_LOG_SIMPLE(L"[ChineseIME] Imm32Monitor created\n");
}

Imm32Monitor::~Imm32Monitor() {
    shutdown();
    DEBUG_LOG_SIMPLE(L"[ChineseIME] Imm32Monitor destroyed\n");
}

bool Imm32Monitor::initialize() {
    if (isInitialized_) return true;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"ChineseIME_Imm32Monitor_WndClass";

    if (!RegisterClassExW(&wc)) {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] Failed to register Imm32Monitor window class\n");
        return false;
    }

    parentWnd_ = GetForegroundWindow();
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"Imm32Monitor",
                            WS_POPUP, 0, 0, 1, 1, parentWnd_, nullptr, wc.hInstance, nullptr);

    if (!hwnd_) {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] Failed to create Imm32Monitor window\n");
        return false;
    }

    SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    g_imm32Monitor = this;
    isInitialized_ = true;
    DEBUG_LOG_SIMPLE(L"[ChineseIME] Imm32Monitor initialized\n");

    // Start a background message processing thread for WM_IME_NOTIFY
    std::thread([] {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] Imm32Monitor message thread started\n");
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        DEBUG_LOG_SIMPLE(L"[ChineseIME] Imm32Monitor message thread exited\n");
    }).detach();
    return true;
}

void Imm32Monitor::shutdown() {
    if (hwnd_) {
        SetWindowLongPtr(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    g_imm32Monitor = nullptr;
    isInitialized_ = false;
    DEBUG_LOG_SIMPLE(L"[ChineseIME] Imm32Monitor shutdown\n");
}

void Imm32Monitor::update() {
    if (!isInitialized_) return;

    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) return;

    HIMC himc = ImmGetContext(fgWnd);
    if (!himc) {
        ImeStateManager::get().updateCandidates(L"", {}, 0);
        return;
    }

    bool imeOpen = ImmGetOpenStatus(himc) != 0;
    ImeStateManager::get().updateImeOpen(imeOpen);

    if (imeOpen) {
        processComposition(fgWnd, himc);
        processCandidate(fgWnd, himc);
    }

    ImmReleaseContext(fgWnd, himc);
}

void Imm32Monitor::processComposition(HWND hwnd, HIMC himc) {
    std::wstring composition;

    LONG compLen = ImmGetCompositionString(himc, GCS_COMPREADSTR, nullptr, 0);
    if (compLen > 0) {
        int wcharLen = compLen / sizeof(wchar_t);
        std::vector<wchar_t> compBuf(wcharLen + 1);
        ImmGetCompositionString(himc, GCS_COMPREADSTR, compBuf.data(), compLen);
        compBuf[wcharLen] = 0;
        composition.assign(compBuf.data(), wcharLen);
    }

    ImeStateManager::get().updateComposition(composition);
}

void Imm32Monitor::processCandidate(HWND hwnd, HIMC himc) {
    std::vector<std::wstring> candidates;
    int selectedIndex = 0;

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

        std::wstring comp;
        auto state = ImeStateManager::get().getSnapshot();
        comp = state.composition;
        ImeStateManager::get().updateCandidates(comp, candidates, selectedIndex);
    }
}

void Imm32Monitor::detectInputMethodType(HKL hkl) {
    LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
    DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
    WORD imeId = HIWORD(hklValue);

    InputMethodType type = detectInputMethodTypeFromImeId(imeId, langId);
    ImeStateManager::get().updateInputMethod(type);
}

LRESULT CALLBACK Imm32Monitor::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_IME_COMPOSITION) {
        if (g_imm32Monitor) {
            HWND fgWnd = GetForegroundWindow();
            if (fgWnd) {
                HIMC himc = ImmGetContext(fgWnd);
                if (himc) {
                    if (lParam & GCS_COMPREADSTR) {
                        g_imm32Monitor->processComposition(fgWnd, himc);
                    }
                    ImmReleaseContext(fgWnd, himc);
                }
            }
        }
    } else if (msg == WM_IME_NOTIFY) {
        if (wParam == IMN_CHANGECANDIDATE || wParam == IMN_OPENCANDIDATE || wParam == IMN_CLOSECANDIDATE) {
            if (g_imm32Monitor) {
                HWND fgWnd = GetForegroundWindow();
                if (fgWnd) {
                    HIMC himc = ImmGetContext(fgWnd);
                    if (himc) {
                        g_imm32Monitor->processCandidate(fgWnd, himc);
                        ImmReleaseContext(fgWnd, himc);
                    }
                }
            }
        }
    } else if (msg == WM_INPUTLANGCHANGE) {
        if (g_imm32Monitor) {
            HKL hkl = reinterpret_cast<HKL>(lParam);
            g_imm32Monitor->detectInputMethodType(hkl);
        }
    } else if (msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_ENDCOMPOSITION) {
        // Composition lifecycle events
        if (g_imm32Monitor) {
            HWND fgWnd = GetForegroundWindow();
            if (fgWnd) {
                HIMC himc = ImmGetContext(fgWnd);
                if (himc) {
                    g_imm32Monitor->processComposition(fgWnd, himc);
                    g_imm32Monitor->processCandidate(fgWnd, himc);
                    ImmReleaseContext(fgWnd, himc);
                }
            }
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace chineseime