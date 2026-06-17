#pragma once

#include "common.h"

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

namespace chineseime {

void onImeStateChanged(int imeType, int chineseMode);
void onCandidateChanged(const wchar_t* composition, const wchar_t** candidates, int count, int selectedIndex);
void onKeyboardStateChanged(int capsLock, int shiftMode);

} // namespace chineseime

void setJavaCallbacks(
    void(*preedit)(const wchar_t*, int, int),
    void(*commit)(const wchar_t*),
    void(*candidates)(const wchar_t**, int, int),
    void(*imeChange)(int, int)
);

#endif