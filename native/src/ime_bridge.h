#pragma once

#ifdef _WIN32
    #ifdef CHINESEIME_EXPORTS
        #define CHINESEIME_API __declspec(dllexport)
    #else
        #define CHINESEIME_API __declspec(dllimport)
    #endif
#else
    #define CHINESEIME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle
CHINESEIME_API int StartListen(void* hwnd);
CHINESEIME_API void StopListen(void);
CHINESEIME_API int IsListening(void);

// State query (legacy WinEvent hook)
CHINESEIME_API int IsChineseMode(void);
CHINESEIME_API int HasLayoutChanged(void);
// CHINESEIME_API must not be used here because the function uses __declspec(dllexport) directly
long GetKeyboardLayoutHKL(void);
CHINESEIME_API const char* GetDllVersion(void);

// TSF-based IME data (new API)
CHINESEIME_API int StartTsfListen(void);
CHINESEIME_API void StopTsfListen(void);
CHINESEIME_API int IsTsfListening(void);

// Get composition string (UTF-16, caller must free with FreeBuffer)
// Returns: length in wchar_t (not bytes), or 0 if no composition
CHINESEIME_API int GetCompositionString(wchar_t* buffer, int bufferSize);

// Get candidate list
CHINESEIME_API int GetCandidateCount(void);
CHINESEIME_API int GetCandidate(int index, wchar_t* buffer, int bufferSize);
CHINESEIME_API int GetSelectedCandidateIndex(void);

// Get cached state
CHINESEIME_API int GetImeOpenStatus(void);
CHINESEIME_API int GetTsfChineseMode(void);
CHINESEIME_API int HasTsfLayoutChanged(void);

// Get input method type
// Returns: 0=Unknown, 1=English, 2=Pinyin, 3=Zhuyin, 4=Cangjie, 5=Wubi, 6=OtherChinese
CHINESEIME_API int GetInputMethodType(void);

// Force refresh (call from Java poll loop)
CHINESEIME_API void RefreshImeState(void);

// Poll individual key state (for real-time Shift detection)
CHINESEIME_API int GetKeyboardStateForPolling(int vKey);

// Memory management
CHINESEIME_API void FreeBuffer(void* ptr);

// Callback registration
CHINESEIME_API void SetCallbacks(void* candidateUpdate, void* layoutChange, void* modeChange, void* keyboardState);

// Event-driven API (WndProc hook for IME events)
CHINESEIME_API void HookWindowProc(void* hwnd);
CHINESEIME_API void UnhookWindowProc(void);
CHINESEIME_API void RefreshCandidates(void);
CHINESEIME_API int IsWindowHooked(void);

// Event callbacks registration (for event-driven mode)
CHINESEIME_API void SetEventCallbacks(
    void* preeditCallback,
    void* commitCallback,
    void* candidateCallback,
    void* imeChangeCallback,
    void* keyboardCallback);

#ifdef __cplusplus
}
#endif