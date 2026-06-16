# ChineseIME Mod - Agent Instructions

## 关键构建命令

### 构建 C++ DLL
```bash
cd native
cmake -B build
cmake --build build --config Release
```

### 构建 Java 模组
```bash
./gradlew.bat clean build
```

### 部署到 Minecraft 实例
```bash
# Windows PowerShell
copy build\libs\chineseime-1.0.0.jar "C:\Users\user\AppData\Roaming\PrismLauncher\instances\1.21.4 fabric\minecraft\mods\"
copy natives\Release\chineseime_native.dll "C:\Users\user\AppData\Roaming\PrismLauncher\instances\1.21.4 fabric\minecraft\mods\"
```

## 架构要点

### 双通道 IME 状态同步
**Windows IME 使用两条通道获取数据：**
1. **WndProc Hook (IMM32)** - `win_event_bridge.cpp` 通过 `SetWindowLongPtr` 替换窗口过程，监听 `WM_IME_*` 消息
2. **TSF Polling** - `tsf_monitor.cpp` 通过轮询 `ITfContext` 的 `GUID_PROP_CANDIDATE` 属性获取候选词

**为什么需要两条通道：**
- IMM32 API (`ImmGetCandidateList`) 对 TSF 架构输入法返回 0
- TSF API 通过 `ITfContext` 属性读取可获取候选词
- `WindowsIMEEventBridge.initialize()` 同时启动两者

### 核心架构：Java 信任 C++ DLL
**C++ DLL 统一检测所有 IME 状态，Java 只做监听和 UI 更新：**
- 输入法类型：通过 HKL 的 IME ID 检测（`detectInputMethodTypeFromImeId`）
- 中文模式：通过 IMM32 `ImmGetOpenStatus` 检测（IME 打开=中文模式，关闭=英文模式）
- Caps Lock：通过 `GetKeyboardState` 系统级检测（不是 `GetAsyncKeyState`）
- 候选词：通过 IMM32 或 TSF API 获取

**重要：必须使用 `GetKeyboardState` 而非 `GetAsyncKeyState`**
- `GetAsyncKeyState` 是**线程相关**的，从轮询线程调用时返回错误状态
- `GetKeyboardState` 返回**系统级**键盘状态，对前台窗口有效

**Java `update()` 方法完全信任 C++ 缓存：**
- 直接读取 C++ 缓存中的状态
- 不做重复检测

### 轮询线程
- 约 60fps (每 16ms 轮询一次)
- 所有状态检测在 `PollIMEState()` 统一进行

### 指示器显示逻辑
- **Shift 指示器**（黄色方块）：中文输入法 && 英文模式时显示
- 英文模式 = `ImmGetOpenStatus` 返回 0
- Shift 切换中英文 ↔ IME 打开/关闭状态切换
- **Caps Lock 指示器**（蓝色背景）：使用 `GetKeyboardState(VK_CAPITAL) & 0x01` 检测
- **输入法类型**：无论中英文模式，始终显示输入法简称（拼/注/仓/速/五）

## JNA 关键注意事项

### 典型错误
`Error looking up function 'StartTsfListen': The specified procedure could not be found.`

**解决方案：**
- C++: `extern "C"` + `__declspec(dllexport)`
- Java: 接口继承 `StdCallLibrary`
- 回调接口继承 `StdCallLibrary.StdCallCallback`
- C++ `wchar_t*` → Java `WString`

### DLL 加载
**`NativeImeBridge.isAvailable()` 会自动调用 `getInstance()` 加载 DLL：**
```java
public static boolean isAvailable() {
    if (!loadAttempted) {
        getInstance();  // 懒加载 DLL
    }
    return loaded && INSTANCE != null;
}
```

**DLL 从 JAR 的 `META-INF/natives/amd64/chineseime_native.dll` 提取到 temp 目录**

### Caps Lock 检测要点
- **必须使用 `GetKeyboardState`** 而不是 `GetAsyncKeyState`
- `GetKeyboardState(keyboardState)` 返回后检查 `keyboardState[VK_CAPITAL] & 0x01`
- `GetAsyncKeyState(VK_CAPITAL)` 在轮询线程中返回线程相关状态，可能不准确
- Caps Lock 是 toggle 键，按下后保持开启直到再次按下

### 中文模式检测要点
- **必须使用 `ImmGetOpenStatus`** 检测 IME 打开/关闭状态
- `ImmGetOpenStatus(himc) != 0` → IME 打开 → 中文模式
- `ImmGetOpenStatus(himc) == 0` → IME 关闭 → 英文模式（Shift 切换后）
- 这与 Shift 切换中英文的机制完全对应

### TSF Compartment 检测的局限性
- `detectChineseMode()` 中的 TSF compartment 检测可能失败
- 添加了 IMM32 回退机制
- 所有检测都失败时返回当前状态避免误报

## 候选词显示关键修复

### 1. IMM32 对 TSF IME 返回 0
**问题**：`ImmGetCandidateList` 对微软拼音等 TSF 架构输入法返回 0

**解决方案**：TSF 轮询通过 `ITfContext::GetProperty(GUID_PROP_CANDIDATE)` 获取候选词
```cpp
// tsf_monitor.cpp getCandidateListFromProperty()
ITfProperty* prop = nullptr;
pic->GetProperty(GUID_PROP_CANDIDATE, &prop);
```

### 2. WndProc Hook 必须传递消息
**问题**：替换 WndProc 后必须调用原始窗口过程，否则 Windows 的候选窗口无法显示

**正确做法**：
```cpp
LRESULT CALLBACK ImeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 处理 IME 消息...
    if (g_originalWndProc) {
        return CallWindowProc(g_originalWndProc, hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
```

### 3. CandidateHud.visible 计算方式
**错误：`visible = !candidates.isEmpty() || !input.isEmpty()`**
- 当 candidates 为空但 input 有值时，visible=true 但不渲染任何内容

**正确：`visible = !candidates.isEmpty()`**
- 只在有候选词时才显示 HUD

### 4. getCandidatesFromIMM 必须读取 selectedIndex
```cpp
// 错误：selectedIndex 总是 0
selectedIndex = 0;

// 正确：从 CANDIDATELIST 读取
selectedIndex = candList->dwSelection;
```

## IME ID 对照表 (HKL 高位字)

### C++ enum ↔ Java constant 对照表
| C++ InputMethodType | Java IME_TYPE | 显示 | IME ID |
|---------------------|---------------|------|--------|
| PINYIN=2 | IME_TYPE_PINYIN=2 | 拼 | 0x0001, 0x0010, 0xE010, 0xE020 |
| ZHUYIN=3 | IME_TYPE_ZHUYIN=3 | 注 | 0x0003, 0xE001 |
| CANGJIE=4 | IME_TYPE_CANGJIE=4 | 仓 | 0x0004, 0xE002 |
| WUBI=5 | IME_TYPE_WUBI=5 | 五 | 0x0002 |
| SUCHENG=6 | IME_TYPE_SUCHENG=6 | 速 | 0x0005, 0xE003 |
| ENGLISH=1 | IME_TYPE_ENGLISH=1 | En | - |

### IME ID 详细对照表
| IME | IME ID (HKL高位) |
|-----|------------------|
| 微软拼音 | 0x0001, 0x0010, 0xE010, 0xE020 |
| 微软五笔 | 0x0002 |
| 微软注音 | 0x0003, 0xE001 |
| 微软仓颉 | 0x0004, 0xE002 |
| 微软速成 | 0x0005, 0xE003 |

## 版本兼容性
- Minecraft: 1.21.4
- Fabric Loader: 0.16.9+
- Fabric API: 0.110.5+1.21.4
- Java: 21
- Windows: 10/11 (需要 TSF 支持)

## 目录结构
```
native/src/                    # C++ DLL 源码
├── ime_bridge.cpp             # 主入口、轮询线程、导出函数
├── win_event_bridge.cpp       # WndProc Hook (IMM32 通道)
├── tsf_monitor.cpp            # TSF 监控、候选词读取
├── imm32_monitor.cpp          # IMM32 备用监控
├── ime_state_manager.cpp      # 状态管理、变化检测
├── jni_callback.cpp           # JNA 回调
└── sta_thread.cpp             # STA 线程管理

src/main/java/com/example/chineseime/
├── ChineseIMEInitializer.java # 主初始化器
├── platform/win32/
│   ├── NativeImeBridge.java   # JNA 接口定义
│   ├── WindowsIMEEventBridge.java  # 事件回调处理
│   └── WindowsIMEBridgeNative.java # IME 桥接
├── hud/
│   ├── CandidateHud.java      # 候选词 HUD
│   ├── ImeStatusIndicator.java # 输入法状态指示器
│   └── VerticalCandidateHud.java # 垂直候选词（仓颉/注音/速成）
└── engine/
    └── PinyinDictionary.java  # 内置拼音引擎

build/libs/chineseime-1.0.0.jar      # 输出的 mod JAR
natives/Release/chineseime_native.dll # 输出的 native DLL
```

## 核心文件说明

### win_event_bridge.cpp - ImeWndProc()
WndProc Hook 的消息处理：
1. `WM_IME_STARTCOMPOSITION` - 调用 `processImeEndComposition` 清空
2. `WM_IME_COMPOSITION` - 调用 `processImeComposition` 读取组合字符串和候选词
3. `WM_IME_ENDCOMPOSITION` - 调用 `processImeEndComposition` 清空
4. `WM_IME_NOTIFY` - 处理 `IMN_OPENCANDIDATE/CHANGECANDIDATE/CLOSECANDIDATE`
5. 所有消息最后调用 `CallWindowProc(g_originalWndProc, ...)` 传递

### ime_bridge.cpp - StartTsfListen()
TSF 轮询的启动：
1. 创建 STA 线程
2. 在 STA 线程中创建 `ITfThreadMgr`
3. 调用 `g_tsfMonitor->initialize(pThreadMgr)`
4. 启动轮询线程，每 16ms 调用 `updateCache()`

### tsf_monitor.cpp - updateCache()
TSF 轮询的主要函数：
1. 获取 `ITfContext`
2. 调用 `getCompositionString()` 读取组合字符串
3. 调用 `getCandidateList()` 读取候选词（通过 `GUID_PROP_CANDIDATE`）
4. 如果 TSF 失败，回退到 IMM32 `ImmGetCandidateList`

### WindowsIMEEventBridge.java - initialize()
初始化事件驱动 IME：
```java
public void initialize(long hwnd) {
    NativeImeBridge.setEventCallbacks(preeditCallback, commitCallback,
        candidateCallback, imeChangeCallback, keyboardCallback);
    NativeImeBridge.hookWindowProc(hwnd);      // 启动 IMM32 WndProc Hook
    NativeImeBridge.startTsfListening();       // 启动 TSF 轮询
}
```

## 调试日志

### C++ 调试输出 (DebugView)
`WinEventBridge` 日志：
```
[ChineseIME] ImeWndProc: msg=0x0102, hwnd=0x00001A2B  # 键盘消息
[ChineseIME] WM_IME_COMPOSITION hwnd=0x..., lParam=0x00000085
[ChineseIME] readCandidates: bufSize=0  # IMM32 无数据
```

`TsfMonitor` 日志：
```
[ChineseIME] updateCache: IMM32 comp='ni', candCount=0, found=0  # 无候选词
[ChineseIME] BeginUIElement: id=123  # TSF UI 元素
```

### Java 调试 (Minecraft 日志)
```
[ChineseIME] Loading native library...           # DLL 加载中
[ChineseIME] DLL loaded successfully             # DLL 加载成功
[ChineseIME] WindowsIMEEventBridge.initialize() called with hwnd=0x1A2B
[ChineseIME] TSF listening started: 1            # TSF 启动成功
[ChineseIME] Preedit: 'ni', cursor=2              # 收到预编辑文本
[ChineseIME] Candidates: count=9, sel=0           # 收到候选词
```

### Java 调试标志
```java
System.setProperty("jna.debug_load", "true");
System.setProperty("jna.debug_load.jna", "true");
```

### 调试建议
1. 使用 Windows DebugView 工具查看 C++ 端的 OutputDebugStringA 输出
2. 检查 Minecraft 日志中是否有 `[ChineseIME] DLL loaded successfully`
3. 在 `WindowsIMEEventBridge` 的回调中添加日志验证数据流
4. 如果 DebugView 没有 `[ChineseIME] ImeWndProc:` 消息，说明 Hook 失败

## 常见问题

### 候选词界面不显示
1. 检查 `ImmGetCandidateList` 是否返回 0（TSF IME 正常返回 0）
2. 检查 TSF 轮询是否启动（日志中应有 `TSF listening started`）
3. 检查 `updateCache()` 是否通过 TSF API 获取到候选词
4. 检查 `CandidateHud.updateCandidates()` 的 `visible` 计算是否正确

### WndProc Hook 失败
**现象**：DebugView 没有 `[ChineseIME] ImeWndProc:` 消息

**可能原因**：
1. `SetWindowLongPtr` 被 GLFW 阻止
2. HWND 格式不正确（需要 `glfwGetWin32Window`）
3. 窗口已被另一个 Hook 替换

**排查步骤**：
1. 确认日志显示 `WindowsIMEEventBridge.initialize() called`
2. 确认日志显示 `hookWindowProc returned`
3. 确认日志显示 `hooked=X`

### DLL 导出问题
1. 检查 CMakeLists.txt 包含所有源文件
2. 确保所有导出函数使用 `__declspec(dllexport)`
3. 验证 DLL 大小（正常约 200-500KB）

### TSF 初始化失败
- TSF 操作必须在 STA 线程中执行
- 使用 `StaThread` 类管理 STA 线程
- `CoCreateInstance` 需要 COM 初始化

### 输入法类型不显示（仍显示"拼"）
- 检查 `detectInputMethodTypeFromImeId` 是否处理所有 IME ID
- 检查 `OnActivated` 是否正确使用 HKL 检测
- 检查 TSF GUID 匹配是否失败导致覆盖

### Caps Lock 蓝色高光闪烁或消失
- **必须使用 `GetKeyboardState`** 而不是 `GetAsyncKeyState`
- `GetKeyboardState` 返回系统级状态
- `GetAsyncKeyState` 在轮询线程中不可靠

### 黄色方块一直显示不消失
- 检查 `ImmGetOpenStatus` 是否正确返回 IME 状态
- IME 关闭 = 英文模式 = 黄色方块显示
- IME 打开 = 中文模式 = 黄色方块消失

### 黄色方块按下 Shift 后不显示
- 检查 `ImmGetOpenStatus` 在 Shift 切换后是否正确返回 0
- Shift 切换会导致 IME 关闭（英文模式）

## TSF vs IMM32 架构

| 输入法 | 架构 | 候选词获取 | WndProc Hook |
|--------|------|------------|--------------|
| 微软拼音 | TSF | TSF API (GUID_PROP_CANDIDATE) | IMM32 返回 0 |
| 微软五笔 | TSF | TSF API (GUID_PROP_CANDIDATE) | IMM32 返回 0 |
| 微软注音 | TSF | TSF API (GUID_PROP_CANDIDATE) | IMM32 返回 0 |
| 微软仓颉 | IMM32 | ImmGetCandidateList 正常 | Hook 正常 |
| 微软速成 | IMM32 | ImmGetCandidateList 正常 | Hook 正常 |

**关键洞察**：
- WndProc Hook 收到 `WM_IME_COMPOSITION` 但 `ImmGetCandidateList` 返回 0 是正常的
- TSF 轮询是获取候选词的必要通道
- 两条通道需要同时启用