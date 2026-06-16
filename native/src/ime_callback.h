#pragma once

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

typedef void (__cdecl* PreeditCallback)(const wchar_t* text, int cursorPos, int selStart, int selLen);
typedef void (__cdecl* CommitCallback)(const wchar_t* text);
typedef void (__cdecl* CandidateCallback)(const wchar_t** candidates, int count, int selectedIndex);
typedef void (__cdecl* ImeChangeCallback)(int inputMethodType, int chineseMode);
typedef void (__cdecl* KeyboardCallback)(int capsLock, int shiftMode);

#ifdef __cplusplus
}
#endif

#endif