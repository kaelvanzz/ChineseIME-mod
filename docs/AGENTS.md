# ChineseIME Mod - Agent Instructions

## 关键构建命令

### 构建 C++ DLL
```bash
cd native
cmake -B build
cmake --build build --config Release
# 输出: natives/Release/chineseime_native.dll
```

### 构建 Java 模组
```bash
./gradlew.bat clean build
# 输出: build/libs/chineseime-1.0.0.jar
```

### 部署到 Minecraft 实例
```powershell
# Windows PowerShell
Copy-Item "build\libs\chineseime-1.0.0.jar" "C:\Users\user\AppData\Roaming\PrismLauncher\instances\1.21.4 fabric\minecraft\mods\" -Force
```

**注意**: DLL 已通过 `build.gradle.kts` 的 `processResources` 任务自动打包到 JAR 的 `META-INF/natives/amd64/` 目录

## 架构要点

### 当前架构：事件驱动 Hook + 多层降级 (2026-05-16)

**2026-05-16 重构版本 3.0.0**：事件驱动优先 + 多层降级架构

1. **主路径 - WinEventBridge (事件驱动)**:
   - C++ DLL 通过 `SetWindowLongPtr(GWLP_WNDPROC)` hook Minecraft 窗口
   - `ImeWndProc` 拦截 `WM_IME_STARTCOMPOSITION`、`WM_IME_COMPOSITION`、`WM_IME_ENDCOMPOSITION`、`WM_IME_NOTIFY`、`WM_INPUTLANGCHANGE`
   - 通过 `EventCallbacks` (std::function) 回调 Java

2. **降级路径 1 - WH_GETMESSAGE/WH_CALLWNDPROC Hook**:
   - 当 WndProc subclassing 失败时自动启用
   - `WH_GETMESSAGE`: 捕获通过 `GetMessage`/`PeekMessage` 获取的消息
   - `WH_CALLWNDPROC`: 捕获通过 `SendMessage` 发送的消息（如 `WM_INPUTLANGCHANGE`）

3. **降级路径 2 - Java 轮询** (每 tick):
   - `WindowsIMEBridgeNative.update()` 调用 `GetCompositionString()`、`GetCandidates()` 等
   - 检查 IME 类型、大小写、Shift 模式等状态
   - 更新 HUD 显示

4. **关键修复 (2026-05-16)**:
   - 移除复杂的 JNA Callback 机制，改用 C-style 函数指针
   - Java 通过 `hookWindow(long hwnd)` 启动 hook
   - HWND 获取使用 `EnumWindows` 遍历当前进程窗口
   - TSF 监听作为 IME 类型检测的补充

### WndProc Hook (IMM32)
`ImeWndProc` 拦截 Windows 消息：
- `WM_IME_STARTCOMPOSITION` - 输入开始
- `WM_IME_COMPOSITION` - 组合字符串变化（GCS_COMPSTR/GCS_RESULTSTR）
- `WM_IME_ENDCOMPOSITION` - 输入结束
- `WM_IME_NOTIFY` - 候选词变化（IMN_OPENCANDIDATE/CHANGECANDIDATE/CLOSECANDIDATE）
- `WM_INPUTLANGCHANGE` - 输入法切换

### 指示器显示逻辑
- **Shift 指示器**（黄色方块）：中文输入法 && 英文模式时显示
- 英文模式 = `ImmGetOpenStatus` 返回 0
- Shift 切换中英文 ↔ IME 打开/关闭状态切换
- **Caps Lock 指示器**（蓝色背景）：使用 `GetAsyncKeyState(VK_CAPITAL) & 0x01` 检测
- **输入法类型**：无论中英文模式，始终显示输入法简称（拼/注/仓/速/五）

## DLL 导出函数 (v3.0.0)

```cpp
// 生命周期
const wchar_t* GetDllVersion();                    // 返回 "3.0.0"
int HookWindowProc(void* hwnd);                   // 替换窗口过程
int HookWindowProcRaw(ULONG_PTR hwnd);            // Raw 版本，返回成功/失败
int InstallMessageHook(ULONG_PTR hwnd);           // 安装 WH_GETMESSAGE/WH_CALLWNDPROC hook
void UnhookWindowProc();                          // 恢复原始窗口过程
int IsWindowHooked();                             // 返回 1 表示已 hook

// 状态查询
int GetCompositionString(wchar_t* buffer, int bufferSize);
int GetCandidateCount();
int GetCandidate(int index, wchar_t* buffer, int bufferSize);
int GetSelectedCandidateIndex();
int GetImeOpenStatus();                           // 返回 1 表示 IME 打开
int GetChineseMode();                             // 返回 1 表示中文模式
int GetShiftMode();                               // 返回 1 表示 Shift 模式
int GetCapsLockState();                           // 返回 1 表示 Caps Lock 开启
int GetInputMethodType();                         // 返回 IME 类型 (1-6)

// 候选词
void RefreshCandidates();

// 事件回调注册
void SetEventCallbacks(
    void* preeditCallback,      // (const wchar_t*, int, int, int)
    void* commitCallback,        // (const wchar_t*)
    void* candidateCallback,     // (const wchar_t**, int, int)
    void* imeChangeCallback,    // (int, int)
    void* keyboardCallback       // (int, int) - 可选
);
```

## JNA 关键注意事项

### DLL 加载流程
```java
// 1. NativeImeBridge.isAvailable() 自动懒加载
// 2. DLL 从 JAR 的 /META-INF/natives/amd64/chineseime_native.dll 提取到 temp
// 3. 通过 Native.load() 加载
```

### 典型错误
```
Error: The specified procedure could not be found
```
**解决方案**：
- C++: `extern "C"` + `__declspec(dllexport)`
- Java: 接口继承 `StdCallLibrary`

## IME 类型常量

| Java IME_TYPE | 值 | 显示 | C++ InputMethodType |
|---------------|-----|------|---------------------|
| IME_TYPE_ENGLISH | 1 | En | ENGLISH |
| IME_TYPE_PINYIN | 2 | 拼 | PINYIN |
| IME_TYPE_ZHUYIN | 3 | 注 | ZHUYIN |
| IME_TYPE_CANGJIE | 4 | 倉 | CANGJIE |
| IME_TYPE_WUBI | 5 | 五 | WUBI |
| IME_TYPE_SUCHENG | 6 | 速 | SUCHENG |

### IME ID 对照表 (HKL 高位字)
| IME | IME ID (HKL高位) |
|-----|------------------|
| 微软拼音 | 0x0001, 0x0010, 0xE010, 0xE020 |
| 微软五笔 | 0x0002 |
| 微软注音 | 0x0003, 0xE001 |
| 微软仓颉 | 0x0004, 0xE002 |
| 微软速成 | 0x0005, 0xE003 |

## 版本兼容性
- Minecraft: 1.21.4
- Fabric Loader: 0.19.1+
- Fabric API: 0.119.4+1.21.4
- Java: 21
- Windows: 10/11

## 目录结构
```
native/src/                    # C++ DLL 源码
├── ime_bridge.cpp             # 主入口、Hook、导出函数
├── ime_bridge.h               # 头文件
├── ime_state_manager.cpp      # 状态管理单例
├── imm32_monitor.cpp          # IMM32 监控（降级用）
├── tsf_monitor.cpp            # TSF 监控（事件驱动补充）
├── win_event_bridge.cpp       # 窗口事件 Bridge（主事件路径）
├── win_event_bridge.h         # EventCallbacks 定义
├── sta_thread.cpp             # STA 线程管理
└── common.h                   # 共享类型

src/main/java/com/example/chineseime/
├── ChineseIMEInitializer.java # 主初始化器、HWND 查找
├── platform/
│   ├── PlatformIMEManager.java # 平台 IME 管理
│   └── win32/
│       ├── NativeImeBridge.java   # JNA 接口定义
│       └── WindowsIMEBridgeNative.java  # Windows IME Bridge
├── hud/
│   ├── CandidateHud.java      # 候选词 HUD (横式)
│   ├── VerticalCandidateHud.java # 候选词 HUD (竖式)
│   └── ImeStatusIndicator.java # 输入法状态指示器
└── engine/
    ├── PinyinDictionary.java  # 内置拼音引擎
    └── CangjieDictionary.java # 内置仓颉引擎

natives/Release/chineseime_native.dll # 输出的 native DLL
```

## 核心文件说明

### ime_bridge.cpp - ImeWndProc()
CocoaInput 风格的 WndProc Hook，拦截 Windows IME 消息：
```cpp
LRESULT CALLBACK ImeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_IME_STARTCOMPOSITION: ...
        case WM_IME_COMPOSITION: {
            // GCS_RESULTSTR - 提交文本
            // GCS_COMPSTR - 组合字符串
            // GCS_CURSORPOS - 光标位置
            // GCS_COMPATTR - 属性（用于选择长度）
            ...
        }
        case WM_IME_ENDCOMPOSITION: ...
        case WM_IME_NOTIFY: {
            // IMN_OPENCANDIDATE/CHANGECANDIDATE/CLOSECANDIDATE
            ...
        }
        case WM_INPUTLANGCHANGE: {
            // IME 类型切换通知
            ...
        }
    }
    return CallWindowProc(g_originalWndProc, hWnd, msg, wParam, lParam);
}
```

### win_event_bridge.cpp - WinEventBridge
事件驱动的核心 Bridge，使用 std::function 回调：
```cpp
struct WinEventBridge::EventCallbacks {
    std::function<void(const wchar_t* text, int cursorPos, int selStart, int selLen)> preeditCallback;
    std::function<void(const wchar_t* text)> commitCallback;
    std::function<void(const wchar_t** candidates, int count, int selIdx)> candidateCallback;
    std::function<void(int layout)> layoutChangeCallback;
    std::function<void(int inputMethodType, bool chineseMode)> imeModeChangeCallback;
};
```

### WindowsIMEBridgeNative.java - hookWindow()
```java
public boolean hookWindow(long hwnd) {
    // 尝试 WndProc subclassing
    NativeImeBridge.hookWindowProc(hwnd);
    hooked = NativeImeBridge.isWindowHooked();

    // 如果失败，尝试 WH_GETMESSAGE hook
    if (!hooked) {
        int msgHookResult = NativeImeBridge.getInstance().InstallMessageHook(hwnd);
        hooked = (msgHookResult != 0);
    }

    // 注册回调
    NativeImeBridge.registerCallbacks(preeditCallback, commitCallback, candidatesCallback, imeChangeCallback);
    return hooked;
}
```

### ChineseIMEInitializer.java - findMinecraftWindow()
通过 `EnumWindows` 遍历所有窗口，找到属于当前进程的窗口：
```java
private long findMinecraftWindow() {
    int currentPid = (int) ProcessHandle.current().pid();
    // EnumWindows 遍历所有窗口，找到 PID 匹配的
    // 返回第一个包含 "Minecraft" 标题的窗口
}
```

## 调试日志

### 启动日志
```
[ChineseIME] Initializing...
[ChineseIME] DLL loaded successfully
[ChineseIME] DLL version: 3.0.0
[ChineseIME] WindowsIMEBridgeNative initialized (event-driven)
[ChineseIME] GLFW window handle: 2306097278400
[ChineseIME] Found window: 'Minecraft 1.21.4' (PID=42564, HWND=xxx)
[ChineseIME] Using HWND from enum: xxx
[ChineseIME] Hooked window: true
[ChineseIME] Window hook result: true
```

### 运行日志（正常）
```
[ChineseIME] Poll: IME=LATIN(1), CMode=false, Caps=false, ShiftM=false, ImeOpen=false, CandCnt=0, Comp=''
[ChineseIME] Indicator: IME=拼, CapsLock=false, ShiftMode=false, ChineseMode=false
```

### 调试建议
1. 检查日志确认 `Hooked window: true`
2. 如果 `Hooked window: false`，检查 HWND 是否正确
3. 注意 `WS_EX_LAYERED` 警告 - 分层窗口可能导致 SetWindowLongPtr 失败
4. 使用 Windows Spy++ 或类似工具验证窗口句柄

## 常见问题

### 指示器和候选词不显示
1. 检查 `Hooked window: true` - 如果是 false，hook 失败
2. 检查 HWND 是否正确 - 使用 `EnumWindows` 验证
3. 确认输入法已打开（中文模式）
4. 查看是否有 `WS_EX_LAYERED` 警告

### Hook 失败 (Hooked window: false)
**可能原因**：
1. HWND 格式不正确
2. 窗口已被另一个 Hook 替换
3. Minecraft 窗口是分层窗口 (WS_EX_LAYERED)
4. 权限问题

**排查步骤**：
1. 确认日志显示 `Using HWND from enum: xxx`
2. 检查 HWND 是否为有效值（通常 > 0x10000）
3. 查看是否有 `WARNING: Window has WS_EX_LAYERED` 日志
4. 降级 hook (WH_GETMESSAGE) 应该会自动启用

### DLL 版本不匹配
```
[ChineseIME] DLL version: 3.0.0
```
确保 JAR 中的 DLL 是最新版本（删除旧 JAR 重新构建）

## TSF vs IMM32 vs Hook 架构

| 组件 | 职责 | 备注 |
|------|------|------|
| **WinEventBridge + ImeWndProc** | 主事件驱动路径 | 拦截 WM_IME_* 消息 |
| **WH_GETMESSAGE/WH_CALLWNDPROC** | Hook 降级路径 | WndProc 失败时自动启用 |
| **TsfMonitor** | TSF 事件监听 | 补充 IME 类型检测 |
| **Imm32Monitor** | IMM32 监控 | 降级备用 |
| **Java Polling** | 轮询降级路径 | 最终保底 |

**关键洞察**：
- IMM32 `ImmGetCandidateList` 对大多数输入法都有效
- 事件驱动失效时自动降级到 hook，hook 失效时自动降级到轮询
- 当前实现是多层降级，保证稳定性

## 代码质量问题（待修复）

### P1 - 中等优先级
1. **`WindowsIMEBridgeNative.update()` 日志过多** - 每帧 stateChanged 都打印日志
2. **`syncFromWindows()` 是空方法** - 原本设计的状态同步逻辑未实现
3. **`hasLayoutChanged()` 返回 `false`** - 总是返回 false，无实际功能

### P2 - 低优先级
1. **两套回调系统并存** - std::function (WinEventBridge) 和 C-style 函数指针 (ime_bridge.cpp)
2. **`imm32_monitor.cpp` 使用 `GCS_COMPREADSTR`** - 注释说这是 raw pinyin，可能不适合所有 IME
3. **魔法数字** - 各种硬编码的尺寸、颜色值等