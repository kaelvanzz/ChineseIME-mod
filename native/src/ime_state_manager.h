#pragma once

#include "common.h"
#include <atomic>
#include <mutex>
#include <windows.h>

namespace chineseime {

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
    void updateHklState(long hkl);
    long getKeyboardLayout() const;
    bool checkLayoutChanged();
    void clearLayoutChanged();
    bool isChineseInputMethod() const;

private:
    ImeStateManager() = default;
    ~ImeStateManager() = default;
    ImeStateManager(const ImeStateManager&) = delete;
    ImeStateManager& operator=(const ImeStateManager&) = delete;

    mutable std::mutex mutex_;
    IMEState state_;
    ChangeFlags changes_;
    bool isInitialized_ = false;
};

InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId);

} // namespace chineseime