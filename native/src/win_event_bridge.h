#pragma once

#ifdef _WIN32
    #include <windows.h>
    #include <imm.h>
    #include <string>
    #include <vector>
    #include <functional>

    namespace chineseime {

    struct EventCallbacks {
        std::function<void(const wchar_t* composition, int cursorPos, int selStart, int selLen)> preeditCallback;
        std::function<void(const wchar_t* commit)> commitCallback;
        std::function<void(const wchar_t** candidates, int count, int selectedIndex)> candidateCallback;
        std::function<void(int inputMethodType, int chineseMode)> imeChangeCallback;
        std::function<void(int capsLock, int shiftMode)> keyboardCallback;
    };

    class WinEventBridge {
    public:
        static WinEventBridge& get();

        void setCallbacks(EventCallbacks&& callbacks);
        void hookWindow(HWND hwnd);
        void unhookWindow();
        void refreshCandidates();

        bool isHooked() const { return hooked_; }
        HWND getTargetWindow() const { return targetWindow_; }
        const EventCallbacks& getCallbacks() const { return callbacks_; }

        void processImeComposition(HWND hwnd, LPARAM lParam);
        void processImeEndComposition(HWND hwnd);
        void processImeNotify(WPARAM wParam, LPARAM lParam);

    private:
        WinEventBridge() = default;
        ~WinEventBridge() = default;
        WinEventBridge(const WinEventBridge&) = delete;
        WinEventBridge& operator=(const WinEventBridge&) = delete;

        static LRESULT CALLBACK ImeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        void readComposition(HIMC himc, LPARAM lParam);
        void readCandidates(HIMC himc);
        void readCompositionCursor(HIMC himc);

        EventCallbacks callbacks_;
        WNDPROC originalWndProc_ = nullptr;
        HWND targetWindow_ = nullptr;
        bool hooked_ = false;

        std::wstring lastComposition_;
        int lastCursorPos_ = 0;
        std::vector<std::wstring> lastCandidates_;
        int lastSelectedIndex_ = 0;
    };

    } // namespace chineseime
#endif