#include "common.h"
#include <algorithm>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <cstring>

namespace chineseime {

InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId) {
    switch (imeId) {
        case 0x0001: case 0x0010: case 0xE010: case 0xE020: return InputMethodType::PINYIN;
        case 0x0002: case 0xE011: return InputMethodType::WUBI;
        case 0x0003: case 0xE001: return InputMethodType::ZHUYIN;
        case 0x0004: case 0xE002: return InputMethodType::CANGJIE;
        case 0x0005: case 0xE003: return InputMethodType::SUCHENG;
        default: return InputMethodType::OTHER_CHINESE;
    }
}

class ImeStateManagerImpl {
public:
    static ImeStateManagerImpl& get() {
        static ImeStateManagerImpl instance;
        return instance;
    }

    void updateInputMethod(InputMethodType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.inputMethodType != type) {
            state_.inputMethodType = type;
            notifyStateChange();
        }
    }

    void updateChineseMode(bool isChinese) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.chineseMode != isChinese) {
            state_.chineseMode = isChinese;
            notifyStateChange();
        }
    }

    void updateImeOpen(bool open) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.imeOpen != open) {
            state_.imeOpen = open;
        }
    }

    void updateKeyboardState(bool capsLock, bool shiftPressed) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool changed = false;
        if (state_.capsLockOn != capsLock) {
            state_.capsLockOn = capsLock;
            changed = true;
        }
        if (state_.shiftPressed != shiftPressed) {
            state_.shiftPressed = shiftPressed;
        }
        if (changed) {
            notifyKeyboardChange();
        }
    }

    void updateCandidates(const std::wstring& composition,
                          const std::vector<std::wstring>& candidates,
                          int selectedIndex) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool compChanged = state_.composition != composition;
        bool candChanged = state_.candidates.size() != candidates.size() ||
                           state_.selectedIndex != selectedIndex;

        state_.composition = composition;
        state_.candidates = candidates;
        state_.selectedIndex = selectedIndex;

        if (compChanged || candChanged) {
            notifyCandidateChange();
        }
    }

    IMEState getSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    ChangeFlags checkChanges() {
        std::lock_guard<std::mutex> lock(mutex_);
        ChangeFlags flags;
        IMEState current = state_;

        flags.inputMethodChanged = (lastState_.inputMethodType != current.inputMethodType);
        flags.chineseModeChanged = (lastState_.chineseMode != current.chineseMode);
        flags.capsLockChanged = (lastState_.capsLockOn != current.capsLockOn);
        flags.candidatesChanged = (lastState_.candidates != current.candidates);
        flags.compositionChanged = (lastState_.composition != current.composition);
        flags.shiftModeChanged = (lastState_.shiftPressed != current.shiftPressed);

        lastState_ = current;
        return flags;
    }

    void setCallbacks(Callbacks cbs) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_ = cbs;
    }

private:
    void notifyStateChange() {
        if (callbacks_.imeChange) {
            callbacks_.imeChange(static_cast<int>(state_.inputMethodType), state_.chineseMode);
        }
    }

    void notifyKeyboardChange() {
        if (callbacks_.keyboard) {
            bool inShiftMode = (state_.inputMethodType != InputMethodType::ENGLISH &&
                               state_.inputMethodType != InputMethodType::UNKNOWN &&
                               !state_.chineseMode);
            callbacks_.keyboard(state_.capsLockOn, inShiftMode);
        }
    }

    void notifyCandidateChange() {
        if (callbacks_.candidate) {
            std::vector<const wchar_t*> ptrs;
            for (const auto& c : state_.candidates) {
                ptrs.push_back(c.c_str());
            }
            callbacks_.candidate(
                state_.composition.c_str(),
                ptrs.empty() ? nullptr : ptrs.data(),
                static_cast<int>(ptrs.size()),
                state_.selectedIndex
            );
        }
    }

    struct Callbacks {
        ImeChangeCallback imeChange = nullptr;
        KeyboardCallback keyboard = nullptr;
        CandidateCallback candidate = nullptr;
    };

    mutable std::mutex mutex_;
    IMEState state_;
    IMEState lastState_;
    Callbacks callbacks_;
};

ImeStateManager& ImeStateManager::get() {
    static ImeStateManagerImpl impl;
    return reinterpret_cast<ImeStateManager&>(impl);
}

}