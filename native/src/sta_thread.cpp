#include "sta_thread.h"
#include <comdef.h>

namespace chineseime {

StaThread::StaThread() = default;

StaThread::~StaThread() {
    stop();
}

bool StaThread::start() {
    if (running_.load()) {
        return true;
    }
    
    running_.store(true);
    ready_.store(false);
    
    thread_ = std::thread(&StaThread::threadProc, this);
    
    return waitForReady();
}

void StaThread::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (threadId_ != 0) {
        PostThreadMessage(threadId_, WM_QUIT, 0, 0);
    }
    
    queueCv_.notify_all();
    
    if (thread_.joinable()) {
        thread_.join();
    }
}

void StaThread::submitTask(Task task) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        taskQueue_.push(std::move(task));
    }
    
    if (threadId_ != 0) {
        PostThreadMessage(threadId_, WM_USER + 1, 0, 0);
    }
    
    queueCv_.notify_one();
}

bool StaThread::submitTaskAndWait(Task task, int timeoutMs) {
    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    submitTask([task = std::move(task), done]() mutable {
        try {
            if (task) task();
            done->set_value();
        } catch (...) {
            done->set_exception(std::current_exception());
        }
    });

    if (future.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready) {
        return false;
    }
    try {
        future.get();
    } catch (...) {
        return false;
    }
    return true;
}

bool StaThread::waitForReady(int timeoutMs) {
    std::unique_lock<std::mutex> lock(initMutex_);
    return initCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), 
        [this] { return ready_.load(); });
}

void StaThread::threadProc() {
    threadId_ = GetCurrentThreadId();
    
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[ChineseIME] CoInitializeEx failed\n");
        running_.store(false);
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(initMutex_);
        ready_.store(true);
    }
    initCv_.notify_one();
    
    OutputDebugStringW(L"[ChineseIME] STA thread ready\n");
    
    MSG msg;
    while (running_.load()) {
        processTasks();
        
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running_.store(false);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    
    CoUninitialize();
    OutputDebugStringW(L"[ChineseIME] STA thread exited\n");
}

void StaThread::processTasks() {
    std::queue<Task> tasks;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        tasks = std::move(taskQueue_);
        taskQueue_ = {};
    }
    
    while (!tasks.empty()) {
        auto& task = tasks.front();
        if (task) {
            task();
        }
        tasks.pop();
    }
}

} // namespace chineseime
