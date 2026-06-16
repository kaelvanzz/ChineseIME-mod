#include "common_linux.h"

namespace chineseime {

ImeStateManager& ImeStateManager::get() {
    static ImeStateManager instance;
    return instance;
}

ImeStateManager::ImeStateManager() = default;

void ImeStateManager::updateInputMethod(InputMethodType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.inputMethodType != type) {
        state_.inputMethodType = type;
        state_.layoutChangeCount++;
        changes_.inputMethodChanged = true;
        changes_.shiftModeChanged = true;
    }
}

void ImeStateManager::updateChineseMode(bool isChinese) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.chineseMode != isChinese) {
        state_.chineseMode = isChinese;
        changes_.chineseModeChanged = true;
        changes_.shiftModeChanged = true;
    }
}

void ImeStateManager::updateImeOpen(bool isOpen) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.imeOpen != isOpen) {
        state_.imeOpen = isOpen;
    }
}

void ImeStateManager::updateCapsLock(bool isOn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.capsLockOn != isOn) {
        state_.capsLockOn = isOn;
        changes_.capsLockChanged = true;
    }
}

void ImeStateManager::updateShiftPressed(bool isPressed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.shiftPressed != isPressed) {
        state_.shiftPressed = isPressed;
        changes_.shiftChanged = true;
    }
}

void ImeStateManager::updateComposition(const std::wstring& comp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.composition != comp) {
        state_.composition = comp;
        changes_.compositionChanged = true;
    }
}

void ImeStateManager::updateCandidates(const std::wstring& comp,
                                        const std::vector<std::wstring>& cands,
                                        int selectedIndex) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.composition != comp) {
        state_.composition = comp;
        changes_.compositionChanged = true;
    }
    if (state_.candidates != cands || state_.selectedIndex != selectedIndex) {
        state_.candidates = cands;
        state_.selectedIndex = selectedIndex;
        changes_.candidatesChanged = true;
    }
}

void ImeStateManager::updateKeyboardState(bool capsLock, bool shiftPressed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.capsLockOn != capsLock) {
        state_.capsLockOn = capsLock;
        changes_.capsLockChanged = true;
    }
    if (state_.shiftPressed != shiftPressed) {
        state_.shiftPressed = shiftPressed;
        changes_.shiftChanged = true;
    }
}

IMEState ImeStateManager::getSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

ChangeFlags ImeStateManager::checkChanges() {
    std::lock_guard<std::mutex> lock(mutex_);
    ChangeFlags result = changes_;
    changes_ = ChangeFlags();
    return result;
}

void ImeStateManager::clearChanges() {
    std::lock_guard<std::mutex> lock(mutex_);
    changes_ = ChangeFlags();
}

bool ImeStateManager::checkLayoutChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.layoutChangeCount > 0) {
        state_.layoutChangeCount = 0;
        return true;
    }
    return false;
}

bool ImeStateManager::isChineseInputMethod() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto type = state_.inputMethodType;
    return type != InputMethodType::ENGLISH && type != InputMethodType::UNKNOWN;
}

long ImeStateManager::getKeyboardLayout() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.hkl;
}

void ImeStateManager::updateHklState(long hkl) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.hkl != hkl) {
        state_.hkl = hkl;
        state_.layoutChangeCount++;
    }
}

void ImeStateManager::clearLayoutChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.layoutChangeCount = 0;
}

} // namespace chineseime