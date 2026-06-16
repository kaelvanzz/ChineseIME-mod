#include "linux_ime_bridge.h"
#include "fcitx_dbus_monitor.h"
#include "rime_monitor.h"
#include "common_linux.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <mutex>
#include <functional>

#ifdef CHINESEIME_DEBUG
#define DEBUG_LOG(fmt, ...) do { \
    fprintf(stderr, "[ChineseIME-Linux] " fmt "\n", ##__VA_ARGS__); \
} while(0)
#else
#define DEBUG_LOG(fmt, ...)
#endif

namespace {

using PreeditCallback = chineseime::PreeditCallback;
using CommitCallback = chineseime::CommitCallback;
using CandidateCallback = chineseime::CandidateCallback;
using ImeChangeCallback = chineseime::ImeChangeCallback;
using KeyboardCallback = chineseime::KeyboardCallback;

struct Callbacks {
    PreeditCallback preedit = nullptr;
    CommitCallback commit = nullptr;
    CandidateCallback candidate = nullptr;
    ImeChangeCallback imeChange = nullptr;
    KeyboardCallback keyboard = nullptr;
};

std::unique_ptr<chineseime::FcitxDBusMonitor> g_fcitxMonitor;
std::unique_ptr<chineseime::RimeMonitor> g_rimeMonitor;
std::atomic<bool> g_listening{false};
std::atomic<bool> g_useRime{false};
std::thread g_pollingThread;
std::mutex g_callbackMutex;
Callbacks g_callbacks;

const char* VERSION = "2.2.0-linux";

void notifyImeChange(int type, bool chineseMode) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    if (g_callbacks.imeChange) {
        g_callbacks.imeChange(type, chineseMode ? 1 : 0);
    }
}

void notifyKeyboard(int caps, int shift) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    if (g_callbacks.keyboard) {
        g_callbacks.keyboard(caps, shift);
    }
}

void notifyCandidate(const wchar_t* comp, const wchar_t** cands, int count, int selIdx) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    (void)comp;
    if (g_callbacks.candidate) {
        g_callbacks.candidate(cands, count, selIdx);
    }
}

void notifyPreedit(const wchar_t* text, int cursorPos, int selStart, int selLen) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    if (g_callbacks.preedit) {
        g_callbacks.preedit(text, cursorPos, selStart, selLen);
    }
}

void notifyCommit(const wchar_t* text) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    if (g_callbacks.commit) {
        g_callbacks.commit(text);
    }
}

void pollingLoop() {
    DEBUG_LOG("Polling thread started");
    auto lastState = chineseime::ImeStateManager::get().getSnapshot();

    while (g_listening.load()) {
        if (g_useRime && g_rimeMonitor) {
            g_rimeMonitor->poll();
        } else if (g_fcitxMonitor) {
            g_fcitxMonitor->poll();
        }

        auto state = chineseime::ImeStateManager::get().getSnapshot();
        auto changes = chineseime::ImeStateManager::get().checkChanges();

        if (changes.inputMethodChanged || changes.chineseModeChanged) {
            notifyImeChange(static_cast<int>(state.inputMethodType), state.chineseMode);
        }

        if (changes.capsLockChanged || changes.shiftModeChanged) {
            bool inShiftMode = (state.inputMethodType != chineseime::InputMethodType::ENGLISH &&
                               state.inputMethodType != chineseime::InputMethodType::UNKNOWN &&
                               !state.chineseMode);
            notifyKeyboard(state.capsLockOn ? 1 : 0, inShiftMode ? 1 : 0);
        }

        if (changes.compositionChanged || changes.candidatesChanged) {
            std::vector<const wchar_t*> ptrs;
            for (const auto& c : state.candidates) {
                ptrs.push_back(c.c_str());
            }
            notifyCandidate(state.composition.c_str(),
                           ptrs.empty() ? nullptr : ptrs.data(),
                           static_cast<int>(ptrs.size()),
                           state.selectedIndex);
        }

        lastState = state;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    DEBUG_LOG("Polling thread stopped");
}

} // anonymous namespace

extern "C" {

CHINESEIME_API int LinuxStartListen(void) {
    if (g_listening.load()) return 1;

    DEBUG_LOG("Starting Linux IME listening...");

    g_fcitxMonitor = std::make_unique<chineseime::FcitxDBusMonitor>();
    if (g_fcitxMonitor->initialize()) {
        DEBUG_LOG("Fcitx DBus monitor initialized");
        g_useRime = false;
    } else {
        DEBUG_LOG("Fcitx DBus not available, trying Rime...");
        g_fcitxMonitor.reset();

        g_rimeMonitor = std::make_unique<chineseime::RimeMonitor>();
        if (g_rimeMonitor->initialize()) {
            DEBUG_LOG("Rime monitor initialized");
            g_useRime = true;
        } else {
            DEBUG_LOG("Neither Fcitx nor Rime available");
            g_rimeMonitor.reset();
            return 0;
        }
    }

    chineseime::ImeStateManager::get().updateInputMethod(
        g_useRime ? chineseime::InputMethodType::RIME : chineseime::InputMethodType::PINYIN);
    chineseime::ImeStateManager::get().updateChineseMode(true);
    chineseime::ImeStateManager::get().updateImeOpen(true);

    g_listening.store(true);
    g_pollingThread = std::thread(pollingLoop);

    DEBUG_LOG("Linux IME listening started");
    return 1;
}

CHINESEIME_API void LinuxStopListen(void) {
    if (!g_listening.load()) return;

    DEBUG_LOG("Stopping Linux IME listening...");
    g_listening.store(false);

    if (g_pollingThread.joinable()) {
        g_pollingThread.join();
    }

    if (g_fcitxMonitor) {
        g_fcitxMonitor->shutdown();
        g_fcitxMonitor.reset();
    }

    if (g_rimeMonitor) {
        g_rimeMonitor->shutdown();
        g_rimeMonitor.reset();
    }

    DEBUG_LOG("Linux IME listening stopped");
}

CHINESEIME_API int IsLinuxListening(void) {
    return g_listening.load() ? 1 : 0;
}

CHINESEIME_API int LinuxIsChineseMode(void) {
    return chineseime::ImeStateManager::get().getSnapshot().chineseMode ? 1 : 0;
}

CHINESEIME_API int LinuxGetInputMethodType(void) {
    return static_cast<int>(chineseime::ImeStateManager::get().getSnapshot().inputMethodType);
}

CHINESEIME_API int LinuxGetImeOpenStatus(void) {
    return chineseime::ImeStateManager::get().getSnapshot().imeOpen ? 1 : 0;
}

CHINESEIME_API int LinuxGetCapsLockState(void) {
    return chineseime::ImeStateManager::get().getSnapshot().capsLockOn ? 1 : 0;
}

CHINESEIME_API int LinuxGetShiftMode(void) {
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    bool isChineseInputMethod = state.inputMethodType != chineseime::InputMethodType::ENGLISH &&
        state.inputMethodType != chineseime::InputMethodType::UNKNOWN;
    bool inShiftMode = isChineseInputMethod && !state.chineseMode && state.imeOpen;
    return inShiftMode ? 1 : 0;
}

CHINESEIME_API int LinuxGetCompositionString(wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (state.composition.empty()) {
        buffer[0] = 0;
        return 0;
    }
    int len = (int)state.composition.length();
    if (len >= bufferSize) len = bufferSize - 1;
    wcsncpy(buffer, state.composition.c_str(), len);
    buffer[len] = 0;
    return len;
}

CHINESEIME_API int LinuxGetCandidateCount(void) {
    return (int)chineseime::ImeStateManager::get().getSnapshot().candidates.size();
}

CHINESEIME_API int LinuxGetCandidate(int index, wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (index < 0 || index >= (int)state.candidates.size()) {
        buffer[0] = 0;
        return 0;
    }
    const std::wstring& cand = state.candidates[index];
    int len = (int)cand.length();
    if (len >= bufferSize) len = bufferSize - 1;
    wcsncpy(buffer, cand.c_str(), len);
    buffer[len] = 0;
    return len;
}

CHINESEIME_API int LinuxGetSelectedCandidateIndex(void) {
    return chineseime::ImeStateManager::get().getSnapshot().selectedIndex;
}

CHINESEIME_API void LinuxSetEventCallbacks(
    void* preedit,
    void* commit,
    void* candidate,
    void* imeChange,
    void* keyboard) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_callbacks.preedit = reinterpret_cast<PreeditCallback>(preedit);
    g_callbacks.commit = reinterpret_cast<CommitCallback>(commit);
    g_callbacks.candidate = reinterpret_cast<CandidateCallback>(candidate);
    g_callbacks.imeChange = reinterpret_cast<ImeChangeCallback>(imeChange);
    g_callbacks.keyboard = reinterpret_cast<KeyboardCallback>(keyboard);

    DEBUG_LOG("Linux event callbacks registered");
}

CHINESEIME_API const char* LinuxGetDllVersion(void) {
    return VERSION;
}

} // extern "C"