#pragma once

#include <windows.h>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <future>

namespace chineseime {

// STA (Single Threaded Apartment) Thread
// TSF requires STA thread with message loop
class StaThread {
public:
    using Task = std::function<void()>;

    StaThread();
    ~StaThread();

    bool start();
    void stop();
    void submitTask(Task task);
    bool submitTaskAndWait(Task task, int timeoutMs = 5000);
    bool waitForReady(int timeoutMs = 5000);
    bool isRunning() const { return running_.load(); }
    DWORD getThreadId() const { return threadId_; }

private:
    void threadProc();
    void processTasks();
    
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    DWORD threadId_ = 0;
    
    std::queue<Task> taskQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    
    std::mutex initMutex_;
    std::condition_variable initCv_;
};

} // namespace chineseime
