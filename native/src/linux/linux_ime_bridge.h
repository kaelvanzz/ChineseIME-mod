#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CHINESEIME_API
#ifdef _WIN32
#ifdef CHINESEIME_EXPORTS
#define CHINESEIME_API __declspec(dllexport)
#else
#define CHINESEIME_API __declspec(dllimport)
#endif
#else
#define CHINESEIME_API __attribute__((visibility("default")))
#endif
#endif

enum LinuxImeType {
    LINUX_IME_UNKNOWN = 0,
    LINUX_IME_FCITX = 1,
    LINUX_IME_RIME = 2,
    LINUX_IME_IBUS = 3
};

CHINESEIME_API int LinuxStartListen(void);
CHINESEIME_API void LinuxStopListen(void);
CHINESEIME_API int IsLinuxListening(void);

CHINESEIME_API int LinuxIsChineseMode(void);
CHINESEIME_API int LinuxGetInputMethodType(void);
CHINESEIME_API int LinuxGetImeOpenStatus(void);
CHINESEIME_API int LinuxGetCapsLockState(void);
CHINESEIME_API int LinuxGetShiftMode(void);

CHINESEIME_API int LinuxGetCompositionString(wchar_t* buffer, int bufferSize);
CHINESEIME_API int LinuxGetCandidateCount(void);
CHINESEIME_API int LinuxGetCandidate(int index, wchar_t* buffer, int bufferSize);
CHINESEIME_API int LinuxGetSelectedCandidateIndex(void);

CHINESEIME_API void LinuxSetEventCallbacks(
    void* preeditCallback,
    void* commitCallback,
    void* candidateCallback,
    void* imeChangeCallback,
    void* keyboardCallback);

CHINESEIME_API const char* LinuxGetDllVersion(void);

#ifdef __cplusplus
}
#endif