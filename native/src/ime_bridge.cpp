// Simplified IME Bridge — CocoaInput-style event-driven architecture
// Based on libwincocoainput by Korea-Minecraft-Forum/CocoaInput-lib
// v3.1 — Refactored: shared IME detection via ime_detector

#include <windows.h>
#include <imm.h>
#include <string>
#include <vector>
#include <comdef.h>
#include <msctf.h>
#include <memory>
#include <mutex>
#include "ime_bridge.h"
#include "ime_state_manager.h"
#include "ime_detector.h"
#include "sta_thread.h"
#include "tsf_monitor.h"
#include "win_event_bridge.h"

#ifdef min
#undef min
#endif

const char* VERSION = "3.1.0";

static HWND g_hwnd = NULL;
static HIMC g_himc = NULL;
static WNDPROC g_originalWndProc = NULL;
static bool g_compositionLocationNotified = false;
static bool g_hookInstalled = false;
static HHOOK g_messageHook = NULL;
static HHOOK g_callWndProcHook = NULL;
static std::unique_ptr<chineseime::StaThread> g_tsfSta;
static chineseime::TsfMonitor* g_tsfMonitor = nullptr;
static ITfThreadMgr* g_tsfThreadMgr = nullptr;
static bool g_tsfListening = false;
static std::mutex g_tsfMutex;

// Java callbacks
static void (*g_javaPreedit)(const wchar_t* text, int cursor, int selLen) = NULL;
static void (*g_javaCommit)(const wchar_t* text) = NULL;
static void (*g_javaCandidates)(const wchar_t** cands, int count, int selIdx) = NULL;
static void (*g_javaImeChange)(int imeType, int chineseMode) = NULL;

void setJavaCallbacks(
    void (*preedit)(const wchar_t*, int, int),
    void (*commit)(const wchar_t*),
    void (*candidates)(const wchar_t**, int, int),
    void (*imeChange)(int, int)) {
    g_javaPreedit = preedit;
    g_javaCommit = commit;
    g_javaCandidates = candidates;
    g_javaImeChange = imeChange;

    chineseime::WinEventBridge::EventCallbacks callbacks;
    callbacks.preeditCallback = [](const wchar_t* text, int cursor, int, int selLen) {
        if (g_javaPreedit) g_javaPreedit(text, cursor, selLen);
    };
    callbacks.commitCallback = [](const wchar_t* text) {
        if (g_javaCommit) g_javaCommit(text);
    };
    callbacks.candidateCallback = [](const wchar_t** cands, int count, int selected) {
        if (g_javaCandidates) g_javaCandidates(cands, count, selected);
    };
    callbacks.imeModeChangeCallback = [](int type, bool chinese) {
        if (g_javaImeChange) g_javaImeChange(type, chinese ? 1 : 0);
    };
    chineseime::WinEventBridge::get().setCallbacks(std::move(callbacks));
}

static inline bool isChinese(wchar_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF);
}

bool containsChinese(const wchar_t* str) {
    if (!str) return false;
    while (*str) {
        if (isChinese(*str++)) return true;
    }
    return false;
}

bool compositionLocationNotify(HWND hWnd) {
    HWND target = hWnd ? hWnd : g_hwnd;
    if (!target) return false;

    HIMC imc = ImmGetContext(target);
    if (!imc) return false;

    POINT pt = {0, 0};
    CANDIDATEFORM candForm = {};
    candForm.dwIndex = 0;
    candForm.dwStyle = CFS_POINT;
    candForm.ptCurrentPos = pt;
    candForm.rcArea.left = 0;
    candForm.rcArea.top = 0;
    candForm.rcArea.right = 0;
    candForm.rcArea.bottom = 0;

    ImmSetCandidateWindow(imc, &candForm);
    ImmReleaseContext(target, imc);
    return true;
}

// ── IME type detection helper (shared logic) ──

static int detectImeTypeFromHkl(HKL hkl, bool* outIsChinese) {
    using namespace chineseime;

    if (!hkl) {
        if (outIsChinese) *outIsChinese = false;
        return static_cast<int>(InputMethodType::ENGLISH);
    }

    DWORD_PTR hklVal = (DWORD_PTR)hkl;
    LANGID langId = LOWORD(hklVal);

    bool isChinese = IsChineseLangId(langId);
    if (outIsChinese) *outIsChinese = isChinese;

    if (!isChinese) {
        return static_cast<int>(InputMethodType::ENGLISH);
    }

    InputMethodType type = detectInputMethodTypeFromHkl(hkl);
    return static_cast<int>(type);
}

void readCandidates(HIMC himc) {
    if (!himc || !g_hwnd) {
        OutputDebugStringA("[ChineseIME] readCandidates: himc or g_hwnd is null\n");
        return;
    }

    DWORD candBufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
    bool hasCandidates = false;
    std::vector<std::wstring> cands;

    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] readCandidates: candBufSize=%u, himc=%p\n", candBufSize, (void*)himc);
    OutputDebugStringA(dbg);

    if (candBufSize > 0) {
        std::vector<char> candBuf(candBufSize);
        CANDIDATELIST* candList = (CANDIDATELIST*)candBuf.data();
        if (ImmGetCandidateListW(himc, 0, candList, candBufSize) > 0 && candList->dwCount > 0) {
            sprintf_s(dbg, "[ChineseIME] readCandidates: IMM32 found %u candidates\n", candList->dwCount);
            OutputDebugStringA(dbg);
            DWORD count = candList->dwCount > 9 ? 9 : candList->dwCount;
            for (DWORD i = 0; i < count; i++) {
                wchar_t* pStr = (wchar_t*)(candBuf.data() + candList->dwOffset[i]);
                cands.push_back(pStr);
            }
            hasCandidates = true;
            chineseime::ImeStateManager::get().updateCandidates(L"", cands, (int)candList->dwSelection);
        }
    }

    if (!hasCandidates) {
        sprintf_s(dbg, "[ChineseIME] readCandidates: IMM32 returned no candidates, trying window enumeration\n");
        OutputDebugStringA(dbg);
        std::vector<std::wstring> windowCandidates = chineseime::collectCandidatesFromWindowEnumeration();
        sprintf_s(dbg, "[ChineseIME] readCandidates: window enumeration found %zu candidates\n", windowCandidates.size());
        OutputDebugStringA(dbg);
        if (!windowCandidates.empty()) {
            cands = windowCandidates;
            hasCandidates = true;
            chineseime::ImeStateManager::get().updateCandidates(L"", cands, 0);
        }
    }

    // Prefer the shared detector's window enumeration for IMEs such as Sogou
    // when IMM32 does not expose a candidate list.
    if (!hasCandidates) {
        std::vector<std::wstring> windowCandidates = chineseime::collectCandidatesFromWindowEnumeration();
        if (!windowCandidates.empty()) {
            cands = std::move(windowCandidates);
            hasCandidates = true;
            chineseime::ImeStateManager::get().updateCandidates(L"", cands, 0);
        }
    }

    std::vector<const wchar_t*> ptrs;
    for (auto& c : cands) ptrs.push_back(c.c_str());
    if (g_javaCandidates) {
        sprintf_s(dbg, "[ChineseIME] readCandidates: invoking callback with %zu candidates\n", cands.size());
        OutputDebugStringA(dbg);
        g_javaCandidates(ptrs.data(), (int)cands.size(), cands.empty() ? 0 :
            (int)chineseime::ImeStateManager::get().getSnapshot().selectedIndex);
    } else {
        OutputDebugStringA("[ChineseIME] readCandidates: g_javaCandidates is NULL\n");
    }
}

// ── Message hook procedures ──

static void processInputLangChange(HKL hkl) {
    bool isChinese = false;
    int imeType = detectImeTypeFromHkl(hkl, &isChinese);

    chineseime::ImeStateManager::get().updateInputMethod(static_cast<chineseime::InputMethodType>(imeType));
    chineseime::ImeStateManager::get().updateChineseMode(isChinese);
    chineseime::ImeStateManager::get().updateImeOpen(isChinese);

    char dbg[128];
    sprintf_s(dbg, "[ChineseIME] IME type change via hook: type=%d (%S), chinese=%d\n",
        imeType, chineseime::getInputMethodTypeName(static_cast<chineseime::InputMethodType>(imeType)), isChinese ? 1 : 0);
    OutputDebugStringA(dbg);

    if (g_javaImeChange) {
        g_javaImeChange(imeType, isChinese ? 1 : 0);
    }
}
static LRESULT CALLBACK MessageCallWndProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        CWPSTRUCT* cwp = (CWPSTRUCT*)lParam;
        if (cwp && cwp->message == WM_INPUTLANGCHANGE) {
            processInputLangChange((HKL)cwp->lParam);
        }
    }
    return CallNextHookEx(g_callWndProcHook, code, wParam, lParam);
}

static LRESULT CALLBACK MessageGetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        MSG* msg = (MSG*)lParam;
        if (msg && msg->hwnd) {
            // Debug: log all messages to diagnose
            static int msgCount = 0;
            static DWORD lastTick = 0;
            DWORD now = GetTickCount();
            if (now - lastTick > 5000) {
                char dbg[128];
                sprintf_s(dbg, "[ChineseIME] WH_GETMESSAGE active: g_hwnd=%I64u, msg=%d\n",
                    (UINT64)g_hwnd, msgCount);
                OutputDebugStringA(dbg);
                msgCount = 0;
                lastTick = now;
            }

            // Always log IME-related messages for diagnosis
            if (msg->message >= WM_IME_STARTCOMPOSITION && msg->message <= WM_IME_KEYLAST) {
                char dbg[256];
                sprintf_s(dbg, "[ChineseIME] IME msg: hwnd=%I64u (g_hwnd=%I64u), msg=0x%X, match=%d\n",
                    (UINT64)msg->hwnd, (UINT64)g_hwnd, msg->message, msg->hwnd == g_hwnd);
                OutputDebugStringA(dbg);
            }

            // Process WM_INPUTLANGCHANGE for any window
            if (msg->message == WM_INPUTLANGCHANGE) {
                processInputLangChange((HKL)msg->lParam);
            }
            else if (msg->hwnd == g_hwnd) {
                if (msg->message == WM_IME_STARTCOMPOSITION) {
                    g_compositionLocationNotified = false;
                    compositionLocationNotify(msg->hwnd);
                    if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
                }
                else if (msg->message == WM_IME_COMPOSITION) {
                    HIMC himc = ImmGetContext(msg->hwnd);
                    if (himc) {
                        if (msg->lParam & GCS_RESULTSTR) {
                            LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, NULL, 0);
                            if (len > 0) {
                                std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                                ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf.data(), len);
                                buf[len / sizeof(wchar_t)] = 0;
                                if (g_javaCommit) g_javaCommit(buf.data());
                            }
                        }
                        if (msg->lParam & GCS_COMPSTR) {
                            LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, NULL, 0);
                            int cursor = 0;
                            if (msg->lParam & GCS_CURSORPOS) {
                                LONG cursorBytes = ImmGetCompositionStringW(himc, GCS_CURSORPOS, NULL, 0);
                                cursor = (cursorBytes >= 0) ? cursorBytes / sizeof(wchar_t) : 0;
                            }
                            if (len > 0) {
                                std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                                ImmGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), len);
                                buf[len / sizeof(wchar_t)] = 0;
                                int selLen = 0;
                                LONG attrLen = ImmGetCompositionStringW(himc, GCS_COMPATTR, NULL, 0);
                                if (attrLen > 0) {
                                    std::vector<char> attrs(attrLen);
                                    ImmGetCompositionStringW(himc, GCS_COMPATTR, attrs.data(), attrLen);
                                    for (int i = 0; i < attrLen; i++) {
                                        if (attrs[i] & ATTR_TARGET_CONVERTED) selLen++;
                                    }
                                }
                                if (g_javaPreedit) g_javaPreedit(buf.data(), cursor, selLen);
                                readCandidates(himc);
                            } else {
                                if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
                            }
                        }
                        ImmReleaseContext(msg->hwnd, himc);
                    }
                }
                else if (msg->message == WM_IME_ENDCOMPOSITION) {
                    g_compositionLocationNotified = false;
                    if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
                }
                else if (msg->message == WM_IME_NOTIFY) {
                    if (msg->wParam == IMN_OPENCANDIDATE || msg->wParam == IMN_CHANGECANDIDATE || msg->wParam == IMN_PRIVATE) {
                        compositionLocationNotify(msg->hwnd);
                        g_compositionLocationNotified = true;
                        HIMC himcTmp = ImmGetContext(msg->hwnd);
                        if (himcTmp) {
                            readCandidates(himcTmp);
                            ImmReleaseContext(msg->hwnd, himcTmp);
                        }
                    } else if (msg->wParam == IMN_CLOSECANDIDATE) {
                        // Sogou may close candidates without opening a new window
                        // Clear candidates on close
                        if (g_javaCandidates) {
                            const wchar_t* empty = nullptr;
                            g_javaCandidates(&empty, 0, 0);
                        }
                    }
                }
            }
        }
    }
    return CallNextHookEx(g_messageHook, code, wParam, lParam);
}

LRESULT CALLBACK ImeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_IME_STARTCOMPOSITION: {
            g_compositionLocationNotified = false;
            compositionLocationNotify(hWnd);
            if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
            return TRUE;
        }

        case WM_IME_COMPOSITION: {
            HIMC himc = ImmGetContext(hWnd);
            if (!himc) break;

            if (lParam & GCS_RESULTSTR) {
                LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, NULL, 0);
                if (len > 0) {
                    std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                    ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf.data(), len);
                    buf[len / sizeof(wchar_t)] = 0;
                    if (g_javaCommit) g_javaCommit(buf.data());
                }
            }

            if (lParam & GCS_COMPSTR) {
                LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, NULL, 0);
                int cursor = 0;
                if (lParam & GCS_CURSORPOS) {
                    LONG cursorBytes = ImmGetCompositionStringW(himc, GCS_CURSORPOS, NULL, 0);
                    cursor = (cursorBytes >= 0) ? cursorBytes / sizeof(wchar_t) : 0;
                }

                if (len > 0) {
                    std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                    ImmGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), len);
                    buf[len / sizeof(wchar_t)] = 0;

                    int selLen = 0;
                    LONG attrLen = ImmGetCompositionStringW(himc, GCS_COMPATTR, NULL, 0);
                    if (attrLen > 0) {
                        std::vector<char> attrs(attrLen);
                        ImmGetCompositionStringW(himc, GCS_COMPATTR, attrs.data(), attrLen);
                        for (int i = 0; i < attrLen; i++) {
                            if (attrs[i] & ATTR_TARGET_CONVERTED) selLen++;
                        }
                    }

                    if (g_javaPreedit) g_javaPreedit(buf.data(), cursor, selLen);

                    if (!g_compositionLocationNotified) {
                        compositionLocationNotify(hWnd);
                        g_compositionLocationNotified = true;
                    }

                    readCandidates(himc);
                } else {
                    if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
                }
            }

            ImmReleaseContext(hWnd, himc);
            return TRUE;
        }

        case WM_IME_ENDCOMPOSITION: {
            g_compositionLocationNotified = false;
            if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
            return TRUE;
        }

        case WM_IME_NOTIFY: {
            if (wParam == IMN_OPENCANDIDATE || wParam == IMN_CHANGECANDIDATE || wParam == IMN_PRIVATE) {
                compositionLocationNotify(hWnd);
                g_compositionLocationNotified = true;
                HIMC himcTmp = ImmGetContext(hWnd);
                if (himcTmp) {
                    readCandidates(himcTmp);
                    ImmReleaseContext(hWnd, himcTmp);
                }
                return TRUE;
            } else if (wParam == IMN_CLOSECANDIDATE) {
                return TRUE;
            }
            break;
        }

        case WM_INPUTLANGCHANGE: {
            processInputLangChange((HKL)lParam);
            break;
        }
    }

    if (g_originalWndProc) {
        return CallWindowProc(g_originalWndProc, hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ── Exported DLL functions ──

extern "C" {

__declspec(dllexport) const wchar_t* GetDllVersion() {
    return L"3.1.0";
}

__declspec(dllexport) int HookWindowProc(void* hwnd) {
    if (!hwnd) return 0;
    HWND h = (HWND)hwnd;
    if (g_hwnd == h) return 1;

    g_originalWndProc = (WNDPROC)GetWindowLongPtr(h, GWLP_WNDPROC);
    if (!g_originalWndProc) return 0;

    LONG_PTR result = SetWindowLongPtr(h, GWLP_WNDPROC, (LONG_PTR)ImeWndProc);
    if (result == 0) {
        g_originalWndProc = NULL;
        return 0;
    }

    g_hwnd = h;
    g_himc = ImmGetContext(h);
    if (g_himc) {
        ImmReleaseContext(h, g_himc);
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
    DWORD currentPid = GetCurrentProcessId();
    sprintf_s(dbg, "[ChineseIME] Window PID: %u, current PID: %u\n", pid, currentPid);
    OutputDebugStringA(dbg);

    // CRITICAL: Reject windows from other processes - they can't be hooked reliably
    if (pid != currentPid) {
        sprintf_s(dbg, "[ChineseIME] REJECTED: Window PID %u != Current PID %u - cross-process hook unreliable\n", pid, currentPid);
        OutputDebugStringA(dbg);
        return 0;
    }

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

    SetLastError(0);
    LONG_PTR result = SetWindowLongPtr(h, GWLP_WNDPROC, (LONG_PTR)ImeWndProc);
    DWORD errAfterSet = GetLastError();
    sprintf_s(dbg, "[ChineseIME] SetWindowLongPtr result: 0x%IX, oldProc: 0x%IX, error: %d\n",
        (UINT64)result, (UINT64)oldProc, errAfterSet);
    OutputDebugStringA(dbg);

    if (errAfterSet == 5) {
        OutputDebugStringA("[ChineseIME] ERROR_ACCESS_DENIED (5)\n");
    } else if (errAfterSet != 0) {
        sprintf_s(dbg, "[ChineseIME] SetWindowLongPtr error: %d\n", errAfterSet);
        OutputDebugStringA(dbg);
    }

    if (errAfterSet != 0) {
        g_originalWndProc = NULL;
        g_hwnd = NULL;
        return 0;
    }

    g_hwnd = h;
    chineseime::WinEventBridge::get().setTargetWindow(h);
    g_hookInstalled = true;
    g_himc = ImmGetContext(h);
    if (g_himc) {
        ImmReleaseContext(h, g_himc);
    } else {
        g_himc = ImmCreateContext();
    }
    sprintf_s(dbg, "[ChineseIME] Hook success, hwnd=0x%IX, oldProc=0x%IX\n", (UINT64)h, (UINT64)oldProc);
    OutputDebugStringA(dbg);
    return 1;
}

__declspec(dllexport) void UnhookWindowProc() {
    StopTsfListen();
    if (g_hookInstalled && g_hwnd) {
        if (g_originalWndProc) {
            SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
        }
        g_originalWndProc = NULL;
        g_hwnd = NULL;
        g_hookInstalled = false;
    }
    if (g_messageHook) {
        UnhookWindowsHookEx(g_messageHook);
        g_messageHook = NULL;
    }
    if (g_callWndProcHook) {
        UnhookWindowsHookEx(g_callWndProcHook);
        g_callWndProcHook = NULL;
    }
}

__declspec(dllexport) int InstallMessageHook(ULONG_PTR hwnd) {
    if (!hwnd) return 0;
    HWND h = (HWND)hwnd;
    if (!IsWindow(h)) return 0;

    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] InstallMessageHook: hwnd=%I64u\n", (UINT64)hwnd);
    OutputDebugStringA(dbg);

    // Validate PID before accepting
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    DWORD currentPid = GetCurrentProcessId();
    if (pid != currentPid) {
        sprintf_s(dbg, "[ChineseIME] InstallMessageHook REJECTED: Window PID %u != Current PID %u\n", pid, currentPid);
        OutputDebugStringA(dbg);
        return 0;
    }

    g_hwnd = h;

    sprintf_s(dbg, "[ChineseIME] InstallMessageHook: g_hwnd=%I64u,传入 hwnd=%I64u, currentThreadId=%u\n",
        (UINT64)g_hwnd, (UINT64)hwnd, GetCurrentThreadId());
    OutputDebugStringA(dbg);

    // Clean up any existing hooks
    if (g_messageHook) {
        UnhookWindowsHookEx(g_messageHook);
        g_messageHook = NULL;
    }
    if (g_callWndProcHook) {
        UnhookWindowsHookEx(g_callWndProcHook);
        g_callWndProcHook = NULL;
    }

    g_callWndProcHook = SetWindowsHookEx(WH_CALLWNDPROC, MessageCallWndProc, NULL, GetCurrentThreadId());
    if (g_callWndProcHook) {
        OutputDebugStringA("[ChineseIME] WH_CALLWNDPROC hook installed\n");
    } else {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] WH_CALLWNDPROC hook failed, error=%d\n", GetLastError());
        OutputDebugStringA(dbg);
    }

    g_messageHook = SetWindowsHookEx(WH_GETMESSAGE, MessageGetMsgProc, NULL, GetCurrentThreadId());
    if (g_messageHook) {
        g_hookInstalled = true;
        OutputDebugStringA("[ChineseIME] WH_GETMESSAGE hook installed\n");
        return 1;
    } else {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] WH_GETMESSAGE hook failed, error=%d\n", GetLastError());
        OutputDebugStringA(dbg);
        return 0;
    }
}

__declspec(dllexport) void SetEventCallbacks(
    void* preedit,
    void* commit,
    void* candidates,
    void* imeChange) {
    setJavaCallbacks(
        (void(*)(const wchar_t*, int, int))preedit,
        (void(*)(const wchar_t*))commit,
        (void(*)(const wchar_t**, int, int))candidates,
        (void(*)(int, int))imeChange
    );
}

__declspec(dllexport) int StartTsfListen() {
    std::lock_guard<std::mutex> lock(g_tsfMutex);
    if (g_tsfListening) return 1;
    if (!g_hwnd) return 0;

    g_tsfSta = std::make_unique<chineseime::StaThread>();
    if (!g_tsfSta->start()) {
        g_tsfSta.reset();
        return 0;
    }

    bool taskCompleted = g_tsfSta->submitTaskAndWait([] {
        HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITfThreadMgr, reinterpret_cast<void**>(&g_tsfThreadMgr));
        if (FAILED(hr) || !g_tsfThreadMgr) return;

        TfClientId clientId = TF_CLIENTID_NULL;
        hr = g_tsfThreadMgr->Activate(&clientId);
        if (FAILED(hr)) {
            g_tsfThreadMgr->Release();
            g_tsfThreadMgr = nullptr;
            return;
        }

        g_tsfMonitor = new chineseime::TsfMonitor();
        if (!g_tsfMonitor->initialize(g_tsfThreadMgr)) {
            g_tsfMonitor->Release();
            g_tsfMonitor = nullptr;
            g_tsfThreadMgr->Deactivate();
            g_tsfThreadMgr->Release();
            g_tsfThreadMgr = nullptr;
        }
    });

    if (!taskCompleted || !g_tsfMonitor) {
        g_tsfSta->stop();
        g_tsfSta.reset();
        return 0;
    }
    g_tsfListening = true;
    return 1;
}

__declspec(dllexport) void StopTsfListen() {
    std::lock_guard<std::mutex> lock(g_tsfMutex);
    if (!g_tsfSta) return;

    if (g_tsfMonitor) {
        g_tsfSta->submitTaskAndWait([] {
            g_tsfMonitor->shutdown();
            g_tsfMonitor->Release();
            g_tsfMonitor = nullptr;
            if (g_tsfThreadMgr) {
                g_tsfThreadMgr->Deactivate();
                g_tsfThreadMgr->Release();
                g_tsfThreadMgr = nullptr;
            }
        });
    }
    g_tsfSta->stop();
    g_tsfSta.reset();
    g_tsfListening = false;
}

__declspec(dllexport) int IsTsfListening() {
    std::lock_guard<std::mutex> lock(g_tsfMutex);
    return g_tsfListening ? 1 : 0;
}

__declspec(dllexport) void RefreshImeState() {
    std::lock_guard<std::mutex> lock(g_tsfMutex);
    if (g_tsfListening && g_tsfSta && g_tsfMonitor) {
        g_tsfSta->submitTask([monitor = g_tsfMonitor] { monitor->pollUpdate(); });
    }
}

__declspec(dllexport) int GetCompositionString(wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;

    {
        auto state = chineseime::ImeStateManager::get().getSnapshot();
        if (!state.composition.empty()) {
            int len = (int)state.composition.size();
            if (len >= bufferSize) len = bufferSize - 1;
            wcsncpy_s(buffer, bufferSize, state.composition.c_str(), len);
            buffer[len] = 0;
            return len;
        }
    }

    HWND hwndToTry = g_hwnd;
    if (!hwndToTry || !IsWindow(hwndToTry)) {
        hwndToTry = GetForegroundWindow();
    }
    if (!hwndToTry) return 0;

    HIMC himc = ImmGetContext(hwndToTry);
    if (!himc) {
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd && fgWnd != hwndToTry) {
            himc = ImmGetContext(fgWnd);
            if (himc) {
                hwndToTry = fgWnd;
            }
        }
        if (!himc) return 0;
    }

    LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, NULL, 0);
    if (len > 0 && len <= bufferSize * (LONG)sizeof(wchar_t)) {
        ImmGetCompositionStringW(himc, GCS_COMPSTR, buffer, len);
        ImmReleaseContext(hwndToTry, himc);
        return len / sizeof(wchar_t);
    }
    ImmReleaseContext(hwndToTry, himc);
    return 0;
}

__declspec(dllexport) int GetCandidateCount() {
    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] GetCandidateCount called, g_hwnd=%I64u\n", (UINT64)g_hwnd);
    OutputDebugStringA(dbg);

    // First check ImeStateManager (where TSF monitor stores candidates from window enumeration)
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (!state.candidates.empty()) {
        sprintf_s(dbg, "[ChineseIME] GetCandidateCount: returning %zu from ImeStateManager\n", state.candidates.size());
        OutputDebugStringA(dbg);
        return (int)state.candidates.size();
    }

    // Try multiple window sources
    HWND hwndToTry = NULL;
    HWND hwndOptions[3] = {g_hwnd, GetForegroundWindow(), GetActiveWindow()};

    for (int i = 0; i < 3; i++) {
        hwndToTry = hwndOptions[i];
        if (!hwndToTry || !IsWindow(hwndToTry)) continue;

        sprintf_s(dbg, "[ChineseIME] GetCandidateCount: trying hwnd=%I64u (option %d)\n", (UINT64)hwndToTry, i);
        OutputDebugStringA(dbg);

        HIMC himc = ImmGetContext(hwndToTry);
        if (himc) {
            sprintf_s(dbg, "[ChineseIME] GetCandidateCount: ImmGetContext succeeded for hwnd=%I64u\n", (UINT64)hwndToTry);
            OutputDebugStringA(dbg);

            DWORD bufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
            sprintf_s(dbg, "[ChineseIME] GetCandidateCount: bufSize=%u\n", bufSize);
            OutputDebugStringA(dbg);

            DWORD count = 0;
            if (bufSize > 0) {
                std::vector<char> buf(bufSize);
                CANDIDATELIST* candList = (CANDIDATELIST*)buf.data();
                if (ImmGetCandidateListW(himc, 0, candList, bufSize) > 0) {
                    count = candList->dwCount;
                    sprintf_s(dbg, "[ChineseIME] GetCandidateCount: found %u candidates\n", count);
                    OutputDebugStringA(dbg);
                    if (count > 0) {
                        std::vector<std::wstring> cands;
                        DWORD cnt = count > 9 ? 9 : count;
                        for (DWORD j = 0; j < cnt; j++) {
                            wchar_t* pStr = (wchar_t*)(buf.data() + candList->dwOffset[j]);
                            cands.push_back(pStr);
                        }
                        chineseime::ImeStateManager::get().updateCandidates(L"", cands, (int)candList->dwSelection);
                    }
                }
            }
            ImmReleaseContext(hwndToTry, himc);
            return count;
        }
    }

    OutputDebugStringA("[ChineseIME] GetCandidateCount: no window had valid IME context\n");
    return 0;
}

__declspec(dllexport) int GetCandidate(int index, wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;

    {
        auto state = chineseime::ImeStateManager::get().getSnapshot();
        if (index >= 0 && index < (int)state.candidates.size()) {
            const std::wstring& cand = state.candidates[index];
            int len = (int)cand.size();
            if (len >= bufferSize) len = bufferSize - 1;
            wcsncpy_s(buffer, bufferSize, cand.c_str(), len);
            buffer[len] = 0;
            return len;
        }
    }

    // Fall back to IMM32 - try g_hwnd first, then foreground window
    HWND hwndToTry = g_hwnd;
    if (!hwndToTry || !IsWindow(hwndToTry)) {
        hwndToTry = GetForegroundWindow();
    }

    if (!hwndToTry) return 0;

    HIMC himc = ImmGetContext(hwndToTry);
    if (!himc) {
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd && fgWnd != hwndToTry) {
            himc = ImmGetContext(fgWnd);
            hwndToTry = fgWnd;
        }
        if (!himc) return 0;
    }

    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] GetCandidate: trying hwnd=%I64u\n", (UINT64)hwndToTry);
    OutputDebugStringA(dbg);

    DWORD bufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
    sprintf_s(dbg, "[ChineseIME] GetCandidate: bufSize=%u\n", bufSize);
    OutputDebugStringA(dbg);

    if (bufSize > 0) {
        std::vector<char> buf(bufSize);
        CANDIDATELIST* candList = (CANDIDATELIST*)buf.data();
        if (ImmGetCandidateListW(himc, 0, candList, bufSize) > 0 && candList->dwCount > 0) {
            sprintf_s(dbg, "[ChineseIME] GetCandidate: found %u candidates\n", candList->dwCount);
            OutputDebugStringA(dbg);
            // Sync to ImeStateManager for polling path
            std::vector<std::wstring> cands;
            DWORD count = candList->dwCount > 9 ? 9 : candList->dwCount;
            for (DWORD i = 0; i < count; i++) {
                wchar_t* pStr = (wchar_t*)(buf.data() + candList->dwOffset[i]);
                cands.push_back(pStr);
            }
            chineseime::ImeStateManager::get().updateCandidates(L"", cands, (int)candList->dwSelection);

            if (index >= 0 && index < (int)candList->dwCount && bufferSize > 0) {
                wchar_t* pStr = (wchar_t*)(buf.data() + candList->dwOffset[index]);
                int len = wcslen(pStr);
                if (len >= bufferSize) len = bufferSize - 1;
                wcsncpy_s(buffer, bufferSize, pStr, len);
                buffer[len] = 0;
                ImmReleaseContext(hwndToTry, himc);
                return len;
            }
        }
    }
    ImmReleaseContext(hwndToTry, himc);
    return 0;
}

__declspec(dllexport) int GetSelectedCandidateIndex() {
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (!state.candidates.empty()) {
        return state.selectedIndex;
    }

    if (!g_hwnd) return 0;
    HIMC himc = ImmGetContext(g_hwnd);
    if (!himc) return 0;
    int selIdx = 0;
    DWORD bufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
    if (bufSize > 0) {
        std::vector<char> buf(bufSize);
        CANDIDATELIST* candList = (CANDIDATELIST*)buf.data();
        if (ImmGetCandidateListW(himc, 0, candList, bufSize) > 0 && candList->dwCount > 0) {
            selIdx = candList->dwSelection;
            // Sync to ImeStateManager
            std::vector<std::wstring> cands;
            DWORD count = candList->dwCount > 9 ? 9 : candList->dwCount;
            for (DWORD i = 0; i < count; i++) {
                wchar_t* pStr = (wchar_t*)(buf.data() + candList->dwOffset[i]);
                cands.push_back(pStr);
            }
            chineseime::ImeStateManager::get().updateCandidates(L"", cands, selIdx);
        }
    }
    ImmReleaseContext(g_hwnd, himc);
    return selIdx;
}

__declspec(dllexport) int GetImeOpenStatus() {
    {
        auto state = chineseime::ImeStateManager::get().getSnapshot();
        if (state.isValid) {
            return state.imeOpen ? 1 : 0;
        }
    }
    
    if (!g_hwnd) return 0;
    DWORD threadId = GetWindowThreadProcessId(g_hwnd, NULL);
    DWORD currentThreadId = GetCurrentThreadId();
    BOOL wasAttached = FALSE;

    // First try direct access (works if same thread)
    HIMC himc = ImmGetContext(g_hwnd);
    if (!himc && threadId != currentThreadId) {
        // Try with thread attachment
        wasAttached = AttachThreadInput(currentThreadId, threadId, TRUE);
        if (wasAttached) {
            himc = ImmGetContext(g_hwnd);
        }
    }

    if (himc) {
        int open = ImmGetOpenStatus(himc);
        ImmReleaseContext(g_hwnd, himc);
        if (wasAttached) {
            AttachThreadInput(currentThreadId, threadId, FALSE);
        }
        return open;
    }

    if (wasAttached) {
        AttachThreadInput(currentThreadId, threadId, FALSE);
    }
    return 0;
}

__declspec(dllexport) int GetChineseMode() {
    {
        auto state = chineseime::ImeStateManager::get().getSnapshot();
        if (state.isValid) {
            return state.chineseMode ? 1 : 0;
        }
    }
    
    if (!g_hwnd) return 0;

    DWORD threadId = GetWindowThreadProcessId(g_hwnd, NULL);
    DWORD currentThreadId = GetCurrentThreadId();
    BOOL wasAttached = FALSE;

    // First try direct access (works if same thread)
    HIMC himc = ImmGetContext(g_hwnd);
    if (!himc && threadId != currentThreadId) {
        // Try with thread attachment
        wasAttached = AttachThreadInput(currentThreadId, threadId, TRUE);
        if (wasAttached) {
            himc = ImmGetContext(g_hwnd);
        }
    }

    if (himc) {
        DWORD conversion = 0, sentence = 0;
        ImmGetConversionStatus(himc, &conversion, &sentence);
        ImmReleaseContext(g_hwnd, himc);
        if (wasAttached) {
            AttachThreadInput(currentThreadId, threadId, FALSE);
        }
        return (conversion & IME_CMODE_NATIVE) ? 1 : 0;
    }

    if (wasAttached) {
        AttachThreadInput(currentThreadId, threadId, FALSE);
    }
    return 0;
}

__declspec(dllexport) int GetShiftMode() {
    // Use ImeStateManager state which is updated via WM_INPUTLANGCHANGE events
    // This is more reliable than direct IMM32 queries which may be inconsistent
    auto state = chineseime::ImeStateManager::get().getSnapshot();

    // Shift mode = IME is open but NOT in Chinese mode
    // This corresponds to the "English mode" toggle in Chinese IMEs
    if (state.imeOpen && !state.chineseMode) {
        return 1;  // Shift indicator should be shown
    }

    // Fallback: Check direct IMM32 state if ImeStateManager state is unclear
    int open = GetImeOpenStatus();
    if (!open) return 0;

    int chinese = GetChineseMode();
    if (open && !chinese) {
        return 1;
    }

    // Additional fallback: check if Shift key is currently held
    // This can detect shift toggling that hasn't been fully processed by IME
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        // Shift is pressed - check if it would toggle English mode
        // Only return 1 if IME is open and we're not in an active composition
        if (open && state.composition.empty()) {
            return 1;
        }
    }

    return 0;
}

__declspec(dllexport) int GetCapsLockState() {
    return (GetKeyState(VK_CAPITAL) & 0x01) ? 1 : 0;
}

// ── GetInputMethodType: unified HKL detection ──
// Priority: ImeStateManager (TSF/event-driven) → Foreground window HKL → Hooked window HKL

static int detectTypeFromHklInternal(HKL hkl) {
    if (!hkl) return 0;
    using namespace chineseime;
    DWORD_PTR val = (DWORD_PTR)hkl;
    LANGID langId = LOWORD(val);
    if (!IsChineseLangId(langId)) return static_cast<int>(InputMethodType::ENGLISH);
    return static_cast<int>(detectInputMethodTypeFromHkl(hkl));
}

__declspec(dllexport) int GetInputMethodType() {
        {
            auto state = chineseime::ImeStateManager::get().getSnapshot();
            if (state.inputMethodType != chineseime::InputMethodType::UNKNOWN) {
                return static_cast<int>(state.inputMethodType);
            }
        }

        HWND fgWnd = GetForegroundWindow();
        if (fgWnd) {
            HKL hkl = GetKeyboardLayout(GetWindowThreadProcessId(fgWnd, NULL));
            if (hkl) {
                // Update ImeStateManager with HKL state for better consistency
                chineseime::ImeStateManager::get().updateHklState((LONG_PTR)hkl);
                int t = detectTypeFromHklInternal(hkl);
                if (t > 0) return t;
            }
        }

        if (g_hwnd) {
            HKL hkl = GetKeyboardLayout(GetWindowThreadProcessId(g_hwnd, NULL));
            if (!hkl) hkl = GetKeyboardLayout(0);
            if (hkl) {
                // Update ImeStateManager with HKL state for better consistency
                chineseime::ImeStateManager::get().updateHklState((LONG_PTR)hkl);
                return detectTypeFromHklInternal(hkl);
            }
        }

        return static_cast<int>(chineseime::InputMethodType::ENGLISH);
}

__declspec(dllexport) int IsWindowHooked() {
    return g_hookInstalled ? 1 : 0;
}

__declspec(dllexport) int IsWindowValid(HWND hwnd) {
    return ::IsWindow(hwnd) ? 1 : 0;
}

__declspec(dllexport) void RefreshCandidates() {
    if (g_hwnd) {
        HIMC himc = ImmGetContext(g_hwnd);
        if (himc) {
            DWORD bufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
            if (bufSize > 0) {
                std::vector<char> buf(bufSize);
                CANDIDATELIST* candList = (CANDIDATELIST*)buf.data();
                if (ImmGetCandidateListW(himc, 0, candList, bufSize) > 0 && candList->dwCount > 0) {
                    std::vector<std::wstring> cands;
                    DWORD count = candList->dwCount > 9 ? 9 : candList->dwCount;
                    for (DWORD i = 0; i < count; i++) {
                        wchar_t* pStr = (wchar_t*)(buf.data() + candList->dwOffset[i]);
                        cands.push_back(pStr);
                    }
                    // Sync to ImeStateManager so polling path can access
                    chineseime::ImeStateManager::get().updateCandidates(L"", cands, (int)candList->dwSelection);
                    std::vector<const wchar_t*> ptrs;
                    for (auto& c : cands) ptrs.push_back(c.c_str());
                    int selIdx = (int)candList->dwSelection;
                    if (selIdx >= (int)count) selIdx = (int)count - 1;
                    if (g_javaCandidates) {
                        g_javaCandidates(ptrs.data(), (int)ptrs.size(), selIdx);
                    }
                }
            }
            ImmReleaseContext(g_hwnd, himc);
        }
    }
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        if (g_hwnd && g_originalWndProc) {
            SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
        }
        if (g_messageHook) {
            UnhookWindowsHookEx(g_messageHook);
            g_messageHook = NULL;
        }
        if (g_callWndProcHook) {
            UnhookWindowsHookEx(g_callWndProcHook);
            g_callWndProcHook = NULL;
        }
    }
    return TRUE;
}
