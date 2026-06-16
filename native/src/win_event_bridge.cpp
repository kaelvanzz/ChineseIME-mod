#include "win_event_bridge.h"
#include <algorithm>
#include <vector>
#include <windows.h>
#include <imm.h>
#include <debugapi.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

// ============================================================================
// ImeWndProc — the REPLACED window procedure.
// Replaces the original WndProc via SetWindowLongPtr(GWLP_WNDPROC).
// This is the PRIMARY event-driven IME detection path (per AGENTS.md design).
//
// Key design points:
// 1. MUST call the original WndProc for unhandled messages so GLFW/Windows work
// 2. WM_IME_* messages are intercepted and forwarded to WinEventBridge::onImeMessage()
// 3. Runs on the game thread — safe to call ImmGetContext
// 4. This is the approach described in Windows_IME_Event_Driven_HUD.md
// ============================================================================
static LRESULT CALLBACK ImeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& bridge = chineseime::WinEventBridge::get();

    // Route IME/input messages through the bridge for detection
    const UINT imeMsgStart = WM_IME_STARTCOMPOSITION;
    const UINT imeMsgEnd = WM_IME_KEYLAST;
    if ((msg >= imeMsgStart && msg <= imeMsgEnd) || msg == WM_INPUTLANGCHANGE) {
        bridge.onImeMessage(hWnd, msg, wParam, lParam);
    }

    // Forward ALL messages to the original WndProc.
    // This is critical: without this, the game window stops receiving input.
    if (bridge.isHooked() && bridge.getTargetWindow() == hWnd) {
        WNDPROC original = chineseime::WinEventBridge::get().getOriginalWndProc();
        if (original) {
            return CallWindowProc(original, hWnd, msg, wParam, lParam);
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================================
// WinEventBridge implementation
// ============================================================================
namespace chineseime {

WinEventBridge& WinEventBridge::get() {
    static WinEventBridge instance;
    return instance;
}

HWND WinEventBridge::GetWinEventTargetWindow() {
    return WinEventBridge::get().getTargetWindow();
}

WNDPROC WinEventBridge::getOriginalWndProc() const {
    return originalWndProc_;
}

void WinEventBridge::setCallbacks(EventCallbacks&& callbacks) {
    callbacks_ = std::move(callbacks);
    char dbg[128];
    sprintf_s(dbg, "[ChineseIME] WinEventBridge: setCallbacks (preedit=%p, commit=%p, candidate=%p)\n",
        (void*)(bool)callbacks_.preeditCallback,
        (void*)(bool)callbacks_.commitCallback,
        (void*)(bool)callbacks_.candidateCallback);
    OutputDebugStringA(dbg);
}

void WinEventBridge::hookWindow(HWND hwnd) {
    if (!hwnd || hooked_) return;

    // Step 1: Replace the window procedure with ImeWndProc.
    // This is the documented approach from AGENTS.md §WndProc Hook.
    // SetWindowLongPtr atomically replaces GWLP_WNDPROC; the previous WndProc
    // is saved so we can forward unhandled messages to it.
    originalWndProc_ = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)ImeWndProc);
    if (!originalWndProc_) {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] WinEventBridge: SetWindowLongPtr FAILED: %lu\n", GetLastError());
        OutputDebugStringA(dbg);
        return;
    }

    hooked_ = true;
    targetWindow_ = hwnd;

    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] >>> WndProc REPLACED <<< hwnd=0x%p, originalProc=0x%p\n",
        (void*)hwnd, (void*)originalWndProc_);
    OutputDebugStringA(dbg);
}

void WinEventBridge::unhookWindow() {
    if (targetWindow_ && originalWndProc_) {
        // Restore the original WndProc
        SetWindowLongPtrW(targetWindow_, GWLP_WNDPROC, (LONG_PTR)originalWndProc_);
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] WinEventBridge: WndProc RESTORED for hwnd=0x%p\n", (void*)targetWindow_);
        OutputDebugStringA(dbg);
        originalWndProc_ = nullptr;
    }
    targetWindow_ = nullptr;
    hooked_ = false;
}

void WinEventBridge::refreshCandidates() {
    if (!targetWindow_) return;
    HIMC himc = ImmGetContext(targetWindow_);
    if (himc) {
        readCandidates(himc);
        ImmReleaseContext(targetWindow_, himc);
    }
}

// ============================================================================
// onImeMessage — central IME event dispatcher (called from ImeWndProc, on game thread)
// ============================================================================
void WinEventBridge::onImeMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_IME_STARTCOMPOSITION: {
        char dbg[64];
        sprintf_s(dbg, "[ChineseIME] ImeWndProc→WM_IME_STARTCOMPOSITION, hwnd=0x%p\n", (void*)hwnd);
        OutputDebugStringA(dbg);
        HIMC himc = ImmGetContext(hwnd);
        if (himc) {
            processImeComposition(hwnd, GCS_COMPSTR);
            ImmReleaseContext(hwnd, himc);
        }
        break;
    }
    case WM_IME_COMPOSITION: {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] ImeWndProc→WM_IME_COMPOSITION, lParam=0x%IX\n", (SIZE_T)lParam);
        OutputDebugStringA(dbg);
        processImeComposition(hwnd, (LPARAM)lParam);
        break;
    }
    case WM_IME_ENDCOMPOSITION: {
        char dbg[64];
        sprintf_s(dbg, "[ChineseIME] ImeWndProc→WM_IME_ENDCOMPOSITION\n");
        OutputDebugStringA(dbg);
        processImeEndComposition(hwnd);
        break;
    }
    case WM_IME_NOTIFY: {
        char dbg[64];
        sprintf_s(dbg, "[ChineseIME] ImeWndProc→WM_IME_NOTIFY, wParam=0x%IX\n", (SIZE_T)wParam);
        OutputDebugStringA(dbg);
        processImeNotify(wParam, lParam);
        break;
    }
    case WM_INPUTLANGCHANGE: {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] ImeWndProc→WM_INPUTLANGCHANGE, lParam=0x%IX\n", (DWORD64)lParam);
        OutputDebugStringA(dbg);

        HKL hkl = reinterpret_cast<HKL>(lParam);
        if (hkl) {
            LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
            bool isChinese = (langId == 0x0804 || langId == 0x0404 ||
                             langId == 0x0C04 || langId == 0x1404);

             ImeStateManager::get().updateChineseMode(isChinese);
             ImeStateManager::get().updateImeOpen(isChinese);
             ImeStateManager::get().updateHklState((LONG_PTR)hkl);

            // WM_INPUTLANGCHANGE: notify Java of IME type change via imeModeChangeCallback
            auto& snap = ImeStateManager::get().getSnapshot();
            fireImeModeChangeCallback(static_cast<int>(snap.inputMethodType), snap.chineseMode);

            targetWindow_ = hwnd;
        }
        break;
    }
    }
}

// ============================================================================
// Internal helpers — all called on game thread via ImeWndProc
// ============================================================================
void WinEventBridge::readComposition(HIMC himc, LPARAM lParam) {
    std::wstring newComposition;
    int cursorPos = 0;

    if (lParam & GCS_COMPSTR) {
        LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
        if (len > 0) {
            std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
            LONG actual = ImmGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), len);
            if (actual > 0) {
                int wcharLen = actual / sizeof(wchar_t);
                buf[wcharLen] = 0;
                newComposition.assign(buf.data(), wcharLen);
            }
        }
    }
    if (lParam & GCS_CURSORPOS) {
        LONG cursorBytes = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
        cursorPos = (cursorBytes >= 0) ? cursorBytes / sizeof(wchar_t) : 0;
    }

    ImeStateManager::get().updateComposition(newComposition);
    lastComposition_ = newComposition;
    lastCursorPos_ = cursorPos;

    // Fire preedit callback (EventCallbacks path — no duplicate global callback)
    if (callbacks_.preeditCallback) {
        callbacks_.preeditCallback(
            newComposition.empty() ? nullptr : newComposition.c_str(),
            cursorPos, 0, 0);
    }
}

void WinEventBridge::readCandidates(HIMC himc) {
    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] readCandidates: lastComposition_='%S'\n", lastComposition_.c_str());
    OutputDebugStringA(dbg);

    DWORD bufSize = ImmGetCandidateListW(himc, 0, nullptr, 0);
    sprintf_s(dbg, "[ChineseIME] readCandidates: bufSize=%u, lastCandidates_.size()=%zu\n",
        bufSize, lastCandidates_.size());
    OutputDebugStringA(dbg);

    if (bufSize > 0) {
        std::vector<char> buf(bufSize);
        CANDIDATELIST* candList = reinterpret_cast<CANDIDATELIST*>(buf.data());
        if (!ImmGetCandidateListW(himc, 0, candList, bufSize)) {
            sprintf_s(dbg, "[ChineseIME] readCandidates: ImmGetCandidateListW failed\n");
            OutputDebugStringA(dbg);
            return;
        }

        lastCandidates_.clear();
        DWORD count = candList->dwCount;
        DWORD pageSize = candList->dwPageSize;
        DWORD pageStart = candList->dwPageStart;
        lastSelectedIndex_ = static_cast<int>(candList->dwSelection);
        if (count > 20) count = 20;

        sprintf_s(dbg, "[ChineseIME] readCandidates: count=%u, sel=%d, pageSize=%u, pageStart=%u\n",
            count, lastSelectedIndex_, pageSize, pageStart);
        OutputDebugStringA(dbg);

        for (DWORD i = 0; i < count; i++) {
            wchar_t* str = reinterpret_cast<wchar_t*>(buf.data() + candList->dwOffset[i]);
            lastCandidates_.push_back(str);
            sprintf_s(dbg, "[ChineseIME]   cand[%u]='%S'\n", i, str);
            OutputDebugStringA(dbg);
        }

        ImeStateManager::get().updateCandidates(lastComposition_, lastCandidates_, lastSelectedIndex_);

        if (!lastCandidates_.empty()) {
            std::vector<const wchar_t*> ptrs;
            for (const auto& c : lastCandidates_) {
                ptrs.push_back(c.c_str());
            }
            if (callbacks_.candidateCallback) {
                callbacks_.candidateCallback(ptrs.data(), static_cast<int>(ptrs.size()), lastSelectedIndex_);
            }
        }
    } else {
        sprintf_s(dbg, "[ChineseIME] readCandidates: no candidates (bufSize=0)\n");
        OutputDebugStringA(dbg);
    }
}

void WinEventBridge::processImeComposition(HWND hwnd, LPARAM lParam) {
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return;

    bool hasComp = (lParam & GCS_COMPSTR) != 0;
    bool hasResult = (lParam & GCS_RESULTSTR) != 0;

    if (hasComp) {
        readComposition(himc, lParam);
        readCandidates(himc);
    }

    if (hasResult) {
        // Always read candidates on result too - some IMEs show candidates before commit
        readCandidates(himc);
        LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
        if (len > 0) {
            std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
            LONG actual = ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf.data(), len);
            if (actual > 0) {
                int wcharLen = actual / sizeof(wchar_t);
                buf[wcharLen] = 0;

                // Always fire commit callback when GCS_RESULTSTR is received.
                // The callback handles filtering (e.g., ignore empty or Latin-only text).
                if (callbacks_.commitCallback) {
                    callbacks_.commitCallback(buf.data());
                }

                lastComposition_.clear();
                lastCandidates_.clear();
                lastSelectedIndex_ = 0;
                ImeStateManager::get().updateCandidates(L"", {}, 0);

                if (callbacks_.preeditCallback) callbacks_.preeditCallback(nullptr, 0, 0, 0);
                if (callbacks_.candidateCallback) callbacks_.candidateCallback(nullptr, 0, 0);
            }
        }
    }

    ImmReleaseContext(hwnd, himc);
}

void WinEventBridge::processImeEndComposition(HWND hwnd) {
    (void)hwnd;
    if (!lastComposition_.empty() || !lastCandidates_.empty()) {
        lastComposition_.clear();
        lastCandidates_.clear();
        lastSelectedIndex_ = 0;
        ImeStateManager::get().updateCandidates(L"", {}, 0);

        // Clear preedit/candidate display (EventCallbacks path)
        if (callbacks_.preeditCallback) callbacks_.preeditCallback(nullptr, 0, 0, 0);
        if (callbacks_.candidateCallback) callbacks_.candidateCallback(nullptr, 0, 0);
    }
}

void WinEventBridge::processImeNotify(WPARAM wParam, LPARAM) {
    if (wParam == IMN_OPENCANDIDATE || wParam == IMN_CHANGECANDIDATE || wParam == IMN_CLOSECANDIDATE) {
        if (targetWindow_) {
            HIMC himc = ImmGetContext(targetWindow_);
            if (himc) {
                if (wParam == IMN_CLOSECANDIDATE) {
                    if (!lastCandidates_.empty()) {
                        lastCandidates_.clear();
                        lastSelectedIndex_ = 0;
                        ImeStateManager::get().updateCandidates(lastComposition_, {}, 0);
                        // Clear candidate display (EventCallbacks path)
                        if (callbacks_.candidateCallback) callbacks_.candidateCallback(nullptr, 0, 0);
                    }
                } else {
                    readCandidates(himc);
                }
                ImmReleaseContext(targetWindow_, himc);
            }
        }
    }
}

// Fire layout change callback (WM_INPUTLANGCHANGE) — also callable from TsfMonitor
void WinEventBridge::fireLayoutChangeCallback(int layout) {
    if (callbacks_.layoutChangeCallback) {
        callbacks_.layoutChangeCallback(layout);
    }
}

// Fire IME mode change callback (TSF OnActivated/OnChange) — callable from TsfMonitor
void WinEventBridge::fireImeModeChangeCallback(int inputMethodType, bool chineseMode) {
    if (callbacks_.imeModeChangeCallback) {
        callbacks_.imeModeChangeCallback(inputMethodType, chineseMode);
    }
}

// Fire candidate callback directly — used by TsfMonitor::notifyStateChanges
void WinEventBridge::fireCandidateCallback(const wchar_t* comp, const wchar_t** cands,
                                           int count, int selIdx) {
    if (callbacks_.candidateCallback) {
        callbacks_.candidateCallback(cands, count, selIdx);
    }
    // Also update ImeStateManager for Java-side polling
    if (comp || (cands && count > 0)) {
        ImeStateManager::get().updateCandidates(
            comp ? std::wstring(comp) : std::wstring(),
            [&]() {
                std::vector<std::wstring> result;
                if (cands && count > 0) {
                    for (int i = 0; i < count; ++i) {
                        result.emplace_back(cands[i] ? cands[i] : L"");
                    }
                }
                return result;
            }(),
            selIdx
        );
    }
}

} // namespace chineseime