#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <dbus/dbus.h>

namespace chineseime {

class FcitxDBusMonitor {
public:
    FcitxDBusMonitor();
    ~FcitxDBusMonitor();

    bool initialize();
    void shutdown();
    void poll();

    bool isConnected() const { return connected_.load(); }

private:
    bool connectToFcitx();
    void updateFromFcitx();

    struct ICStatus {
        bool enabled = false;
        std::string icid;
        std::string engine;
        std::string layout;
    };

    struct IMState {
        std::wstring composition;
        std::vector<std::wstring> candidates;
        int selectedIndex = 0;
        bool enabled = false;
        std::string inputMethod;
    };

    ICStatus getCurrentICStatus();
    IMState getInputState(const std::string& icid);

    DBusConnection* conn_ = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    IMState lastState_;
    std::mutex mutex_;
};

} // namespace chineseime