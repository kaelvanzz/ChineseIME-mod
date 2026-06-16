#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

namespace chineseime {

enum class InputMethodType {
    UNKNOWN = 0,
    ENGLISH = 1,
    PINYIN = 2,
    ZHUYIN = 3,
    CANGJIE = 4,
    WUBI = 5,
    SUCHENG = 6,
    RIME = 7,
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

class ImeStateManager {
public:
    static ImeStateManager& get();

    void updateInputMethod(InputMethodType type);
    void updateChineseMode(bool isChinese);
    void updateImeOpen(bool isOpen);
    void updateCapsLock(bool isOn);
    void updateShiftPressed(bool isPressed);
    void updateComposition(const std::wstring& comp);
    void updateCandidates(const std::wstring& comp, const std::vector<std::wstring>& cands, int selectedIndex);
    void updateKeyboardState(bool capsLock, bool shiftPressed);

    IMEState getSnapshot() const;
    ChangeFlags checkChanges();
    void clearChanges();
    bool checkLayoutChanged();
    bool isChineseInputMethod() const;
    long getKeyboardLayout() const;
    void updateHklState(long hkl);
    void clearLayoutChanged();

private:
    ImeStateManager();

    mutable std::mutex mutex_;
    IMEState state_;
    IMEState lastState_;
    ChangeFlags changes_;
};

typedef void (*PreeditCallback)(const wchar_t* text, int cursorPos, int selStart, int selLen);
typedef void (*CommitCallback)(const wchar_t* text);
typedef void (*CandidateCallback)(const wchar_t** candidates, int count, int selectedIndex);
typedef void (*ImeChangeCallback)(int inputMethodType, int chineseMode);
typedef void (*KeyboardCallback)(int capsLock, int shiftMode);

}