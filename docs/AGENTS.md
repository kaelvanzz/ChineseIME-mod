# ChineseIME Mod - Agent Instructions

## 关键构建命令

### 构建 C++ DLL
```powershell
cd native
# Configure CMake (only needed once or when CMakeLists.txt changes)
cmake -G "Visual Studio 17 2022" -A x64 -S . -B build

# Build
cmake --build build --config Release
# 输出: natives/Release/chineseime_native.dll
```

### 构建 Java 模组
```powershell
./gradlew.bat build
# 输出: build/libs/chineseime-1.0.0.jar
```

### 部署到 Minecraft 实例
```powershell
# Windows PowerShell
Copy-Item "build\libs\chineseime-1.0.0.jar" "你的Minecraft mods目录" -Force
```

**注意**: DLL 已通过 `build.gradle.kts` 的 `processResources` 任务自动打包到 JAR 的 `META-INF/natives/amd64/` 目录

---

## 架构要点

### 当前架构：事件驱动 Hook + 多层降级 + UI Automation Fallback (2026-05-24)

**问题背景**：Minecraft 在某些环境下（如使用 Iris mod）会运行在独立的进程中，导致 IMM32 API 无法跨进程获取候选词。

**2026-05-24 架构**：

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

4. **降级路径 3 - UI Automation API** (跨进程候选词获取):
   - 当 IMM32 返回 0 候选词时，通过 Windows UI Automation API 尝试从目标进程窗口获取候选词
   - 实现文件: `native/src/uia_candidate_provider.cpp`
   - 函数: `GetCandidateCountUIA()`, `GetCandidatesViaUIA()`

5. **最终降级 - 内置词典**:
   - 当所有 Native 方法都失败时，使用 `PinyinDictionary.getSuggestions()` 等内置词典

### 跨进程问题根因

当 Minecraft 运行在独立进程中时：
- `ImmGetContext(hwnd)` 返回 NULL（无法获取其他进程的 IME 上下文）
- 日志显示: `[ChineseIME] GetCandidateCount: ImmGetContext failed`
- 原因: Windows IME 上下文属于输入法进程，不属于目标窗口进程

**解决方案**：
- UI Automation API 可以枚举并读取其他进程窗口的 UI 元素
- 但需要候选词窗口是标准 Win32 控件且可见

---

## DLL 导出函数 (v3.0.0)

```cpp
// 生命周期
const wchar_t* GetDllVersion();                    // 返回 "3.0.0"
int HookWindowProc(void* hwnd);                     // 替换窗口过程
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

// UI Automation 候选词获取 (跨进程)
int GetCandidateCountUIA();                       // 通过 UI Automation 获取候选词数量
int GetCandidatesViaUIA(int maxCandidates, wchar_t(*candidates)[64]);  // 获取候选词列表

// 事件回调注册
void SetEventCallbacks(
    void* preeditCallback,      // (const wchar_t*, int, int, int)
    void* commitCallback,      // (const wchar_t*)
    void* candidateCallback,   // (const wchar_t**, int, int)
    void* imeChangeCallback,   // (int, int)
    void* keyboardCallback     // (int, int) - 可选
);
```

---

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

---

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

---

## 版本兼容性
- Minecraft: 1.21.4
- Fabric Loader: 0.19.1+
- Fabric API: 0.119.4+1.21.4
- Java: 21
- Windows: 10/11

---

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
├── uia_candidate_provider.cpp # UI Automation 候选词获取（跨进程）
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
build/libs/chineseime-1.0.0.jar       # 打包后的 mod JAR
```

---

## 当前问题 (P1 - 未解决)

### 问题 1：候选词同步失败

**现象**：模组显示内置词典的候选词，而非 Windows IME 的候选词

**原因分析**：
1. Minecraft 窗口可能在独立进程中（特别是使用 Iris mod 时）
2. `ImmGetContext(hwnd)` 跨进程调用失败，返回 NULL
3. 日志：`[ChineseIME] GetCandidateCount: ImmGetContext failed`
4. 即使 UI Automation 也可能无法找到候选词（IME 候选词窗口可能不是标准控件）

**尝试的解决方案**：
1. ✅ 多窗口 fallback：`g_hwnd` → `GetForegroundWindow()` → `GetActiveWindow()`
2. ✅ PID 验证：拒绝跨进程 hook（因为不可靠）
3. ✅ UI Automation API：尝试从目标进程窗口枚举 UI 元素获取候选词
4. ⏳ UI Automation 仍未成功找到候选词

**DebugView 关键日志**：
```
[ChineseIME] Hook attempt, hwnd=200420
[ChineseIME] Current PID: 13092, Window PID: 6612
[ChineseIME] REJECTED: Window PID 6612 != Current PID 13092 - cross-process hook unreliable
[ChineseIME] GetCandidateCount called, g_hwnd=0
[ChineseIME] GetCandidateCount: g_hwnd invalid, using foreground=4068424
[ChineseIME] GetCandidateCount: trying hwnd=4068424
[ChineseIME] GetCandidateCount: ImmGetContext failed
```

**下一步可能方案**：
1. 使用 UI Access (UIAccess) 权限提升来访问其他进程
2. 实现自定义 IME 注入到目标进程
3. 使用 Windows Text Input Framework (TIF) 替代方案

### 问题 2：Shift 模式检测

**状态**：待调查

---

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

### 跨进程场景日志
```
[ChineseIME] Hook attempt, hwnd=200420
[ChineseIME] Current PID: 36788, Window PID: 6612
[ChineseIME] REJECTED: Window PID 6612 != Current PID 36788 - cross-process hook unreliable
[ChineseIME] InstallMessageHook: hwnd=200420
[ChineseIME] InstallMessageHook REJECTED: Window PID 6612 != Current PID 36788
[ChineseIME] GetCandidateCount called, g_hwnd=0
[ChineseIME] Poll: comp='', nativeCandCount=0
[ChineseIME] IMM32 returned 0 candidates, trying UI Automation...
[ChineseIME] GetCandidateCountUIA: called
[ChineseIME] GetCandidateCountUIA: fgPid=6612, currentPid=36788
[ChineseIME] GetCandidateCountUIA: no candidates found in process 6612
```

### UI Automation 成功日志
```
[ChineseIME] Poll: comp='ni', nativeCandCount=0
[ChineseIME] IMM32 returned 0 candidates, trying UI Automation...
[ChineseIME] GetCandidateCountUIA: called
[ChineseIME] GetCandidateCountUIA: fgPid=6612, currentPid=36788
[ChineseIME] UIA: Searching for candidates in PID 6612
[ChineseIME] UIA: found 9 candidates
[ChineseIME] UI Automation found 9 candidates
```

### 调试检查清单

1. **确认 Hook 状态**
   - `Hooked window: true` - hook 成功
   - `REJECTED: Window PID X != Current PID Y` - 正常，跨进程 hook 被拒绝

2. **检查候选词来源**
   - 如果每帧看到 `getFallbackCandidates for mode: PINYIN` → 候选词为空，使用内置词典
   - 如果看到 `UI Automation found N candidates` → UI Automation 成功

3. **UI Automation 调试**
   - `GetCandidateCountUIA: called` - 函数被调用
   - `GetCandidateCountUIA: found N candidates` - 成功
   - `GetCandidateCountUIA: no candidates found` - 失败（可能候选词窗口不是标准控件）

---

## TSF vs IMM32 vs Hook vs UI Automation 架构

| 组件 | 职责 | 状态 |
|------|------|------|
| **WinEventBridge + ImeWndProc** | 主事件驱动路径 | ✅ 工作（仅同进程） |
| **WH_GETMESSAGE/WH_CALLWNDPROC** | Hook 降级路径 | ✅ 工作（仅同进程） |
| **TsfMonitor** | TSF 事件监听 | ✅ 工作 |
| **UI Automation (uia_candidate_provider.cpp)** | 跨进程候选词获取 | ⏳ 部分工作 |
| **Java Polling + 内置词典** | 最终降级 | ✅ 工作 |

**关键洞察**：
- 同进程情况下，IMM32 和事件驱动工作正常
- 跨进程情况下，IMM32 失败，UI Automation 可能找到候选词（如果候选词窗口是标准 List/ListItem 控件）
- 最终降级是内置词典，保证始终有候选词可用

---

## 代码质量问题

### P1 - 当前问题
1. **候选词同步失败** - IMM32 跨进程失败，UI Automation 尚未成功
2. **Shift 模式检测不工作** - 待调查

### P2 - 已修复
1. ✅ **WindowsIMEBridgeNative.update() 日志过多** - 已优化
2. ✅ **syncFromWindows() 是空方法** - 已移除
3. ✅ **hasLayoutChanged() 返回 false** - 已标记为 @Deprecated
4. ✅ **imm32_monitor.cpp 使用 GCS_COMPREADSTR** - 已修复为 GCS_COMPSTR
5. ✅ **跨进程 PID 验证** - 已添加，拒绝不可靠的跨进程 hook
6. ✅ **多窗口 fallback** - g_hwnd → foreground → active
7. ✅ **UI Automation API** - 已实现但尚未成功找到候选词