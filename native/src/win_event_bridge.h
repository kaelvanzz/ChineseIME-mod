#pragma once

#include "ime_state_manager.h"
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace chineseime {

// ============================================================================
// WinEventBridge — implements the documented event-driven IME architecture.
// Uses SetWindowLongPtr(GWLP_WNDPROC) to replace the window procedure, just
// like a debugger would. This gives us first-class interception of all messages
// including WM_IME_STARTCOMPOSITION, WM_IME_COMPOSITION, WM_IME_ENDCOMPOSITION,
// WM_IME_NOTIFY, and WM_INPUTLANGCHANGE.
//
// The original window procedure is preserved and called for all unhandled
// messages so Windows and GLFW continue to function normally.
// ============================================================================

struct WinEventBridge {
    struct EventCallbacks {
        // WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION (GCS_COMPSTR)
        std::function<void(const wchar_t* text, int cursorPos, int selStart, int selLen)> preeditCallback;
        // WM_IME_COMPOSITION (GCS_RESULTSTR)
        std::function<void(const wchar_t* text)> commitCallback;
        // WM_IME_NOTIFY (IMN_CHANGECANDIDATE)
        std::function<void(const wchar_t** candidates, int count, int selIdx)> candidateCallback;
        // WM_INPUTLANGCHANGE — called when the keyboard layout changes
        std::function<void(int layout)> layoutChangeCallback;
        // TSF mode-change sink (OnActivated/OnChange) — called when Chinese mode toggles
        std::function<void(int inputMethodType, bool chineseMode)> imeModeChangeCallback;
    };

    static WinEventBridge& get();

    void setCallbacks(EventCallbacks&& callbacks);
    void hookWindow(HWND hwnd);
    void setTargetWindow(HWND hwnd) { targetWindow_ = hwnd; }
    void unhookWindow();
    // Refresh candidates — called from Java via RefreshCandidates export
    void refreshCandidates();
    // Fire callbacks directly from other modules (e.g. TsfMonitor).
    // These bypass the WndProc path for TSF-initiated state changes.
    void fireLayoutChangeCallback(int layout);
    void fireImeModeChangeCallback(int inputMethodType, bool chineseMode);
    void fireCandidateCallback(const wchar_t* comp, const wchar_t** cands,
                              int count, int selIdx);

    // Entry point — called from ImeWndProc (the replaced WndProc)
    void onImeMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool isHooked() const { return hooked_; }
    WNDPROC getOriginalWndProc() const;
    HWND getTargetWindow() const { return targetWindow_; }

    // Get the target window from WinEventBridge (for use by other modules)
    static HWND GetWinEventTargetWindow();

private:
    WinEventBridge() = default;

    // Internal: read preedit text from IMM context and fire preedit callback
    void readComposition(HIMC himc, LPARAM lParam);
    void readCandidates(HIMC himc);
    void processImeComposition(HWND hwnd, LPARAM lParam);
    void processImeEndComposition(HWND hwnd);
    void processImeNotify(WPARAM wParam, LPARAM lParam);

    EventCallbacks callbacks_;
    HWND targetWindow_ = nullptr;
    std::wstring lastComposition_;
    std::vector<std::wstring> lastCandidates_;
    int lastSelectedIndex_ = 0;
    int lastCursorPos_ = 0;
    bool hooked_ = false;

    // WndProc replacement (SetWindowLongPtr path)
    WNDPROC originalWndProc_ = nullptr;
};

} // namespace chineseime
