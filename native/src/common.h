#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#ifdef CHINESEIME_EXPORTS
#define CHINESEIME_API __declspec(dllexport)
#else
#define CHINESEIME_API __declspec(dllimport)
#endif
#else
#define CHINESEIME_API
#endif

namespace chineseime {

enum class InputMethodType {
    UNKNOWN = 0,
    ENGLISH = 1,
    PINYIN = 2,
    ZHUYIN = 3,
    CANGJIE = 4,
    WUBI = 5,
    SUCHENG = 6,
    OTHER_CHINESE = 99
};

struct IMEState {
    bool isValid = false;
    long hkl = 0;
    InputMethodType inputMethodType = InputMethodType::UNKNOWN;
    bool imeOpen = false;
    bool chineseMode = false;
    bool inShiftMode = false;
    bool capsLockOn = false;
    bool shiftPressed = false;
    std::wstring composition;
    std::vector<std::wstring> candidates;
    int selectedIndex = 0;
    int layoutChangeCount = 0;
};

struct ChangeFlags {
    bool inputMethodChanged = false;
    bool chineseModeChanged = false;
    bool capsLockChanged = false;
    bool candidatesChanged = false;
    bool compositionChanged = false;
    bool shiftChanged = false;
    bool shiftModeChanged = false;
};

} // namespace chineseime