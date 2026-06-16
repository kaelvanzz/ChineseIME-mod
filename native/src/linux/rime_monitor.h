#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

namespace chineseime {

class RimeMonitor {
public:
    RimeMonitor();
    ~RimeMonitor();

    bool initialize();
    void shutdown();
    void poll();

    bool isInitialized() const { return initialized_.load(); }

private:
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::mutex mutex_;
};

} // namespace chineseime