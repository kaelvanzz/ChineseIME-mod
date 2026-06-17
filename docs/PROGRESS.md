# ChineseIME Mod 重构进度

## 项目概述
- **目标**: Fabric 1.21.4 中文输入法显示模组，支持 Windows
- **C++ DLL**: 处理 IME 状态检测和事件捕获
- **Java Mod**: 接收 DLL 数据并显示 UI
- **架构**: 事件驱动 Hook + 轮询降级 (CocoaInput 风格)

---

## 已完成 ✅

### 架构重构 (2026-05-16) — 事件驱动优先
- [x] **v3.0.0 事件驱动架构** - 基于 CocoaInput 风格
  - `WinEventBridge` + `ImeWndProc` (SetWindowLongPtr hook) 作为主路径
  - `WH_GETMESSAGE` + `WH_CALLWNDPROC` hook 作为 WndProc 失败时的降级
  - Java 侧轮询作为最终降级方案
  - 移除复杂的 JNA Callback 机制，改用 C-style 函数指针回调
  - C++ 直接处理 IME 消息，Java 通过回调接收事件

- [x] **WndProc Hook** - `SetWindowLongPtr(GWLP_WNDPROC)`
  - 拦截 `WM_IME_STARTCOMPOSITION`、`WM_IME_COMPOSITION`、`WM_IME_ENDCOMPOSITION`、`WM_IME_NOTIFY`
  - 降级：安装 `WH_GETMESSAGE` 和 `WH_CALLWNDPROC` hook

- [x] **HWND 获取** - `EnumWindows` 遍历当前进程窗口

- [x] **DLL 导出函数简化**:
  - `HookWindowProc(void* hwnd)` / `UnhookWindowProc()`
  - `GetCompositionString()` / `GetCandidateCount()` / `GetCandidate()`
  - `GetInputMethodType()` / `GetChineseMode()` / `GetShiftMode()` / `GetCapsLockState()`
  - `SetEventCallbacks()` - 注册事件回调

- [x] **TSF 监听** (补充主要路径):
  - `ITfInputProcessorProfileActivationSink` - IME 类型检测
  - `ITfUIElementSink` - 候选词列表通知
  - `ITfCompartmentEventSink` - 中英文模式切换

- [x] **状态管理** - `ImeStateManager` 单例作为单一数据源

### UI 组件
- [x] **ImeStatusIndicator** - 极简风格，半透明背景
  - 聊天室显示输入法类型（拼/注/倉/五/速）
  - Caps Lock 时蓝色背景
  - Shift 中英文切换时黄色方块指示器
- [x] **CandidateHud** - 候选词界面（横式）
- [x] **VerticalCandidateHud** - 候选词界面（竖式，仓颉/速成/注音）

### 功能
- [x] DLL 内嵌到 JAR（`META-INF/natives/amd64/`）
- [x] 键盘布局检测（拼/注/倉/五/速）
- [x] Shift 中英文切换检测
- [x] Caps Lock 大写锁定检测
- [x] composition（输入内容）显示支持多语言文字
- [x] 内置引擎回退（PinyinDictionary / CangjieDictionary）

### 修复的问题
- [x] **JNA Callback 失败** - 改用函数指针
- [x] **HWND 获取失败** - 使用 `EnumWindows` 遍历
- [x] **IME 类型检测** - TSF GUID 优先，HKL 回退
- [x] **Caps Lock 检测** - 使用 `GetAsyncKeyState`
- [x] **Shift 模式检测** - `GetShiftMode()` 返回 `(!chineseMode && imeOpen)`
- [x] **updateCandidates 覆盖问题** - 增量合并避免 composition 被意外清空

---

## 进行中 🔄

### Hook 成功验证
- **问题**: `Hooked window: false` - HWND 可能不正确
- **待验证**: 日志显示 `Using HWND from enum: xxx`
- **2026-05-16 状态**: EnumWindows 找到窗口，但 hook 仍可能失败（分层窗口 WS_EX_LAYERED）

### 候选词显示验证
- **待验证**: hook 成功后候选词是否正常显示
- **2026-05-16 状态**: 因 Hook 失败未能完整测试

---

## 待办 🔧

### 高优先级
- [ ] **修复 Hook 失败** - 验证 HWND 正确性，考虑 WS_EX_LAYERED 问题
- [ ] **验证候选词显示** - hook 成功后完整测试

### 中优先级
- [ ] 简化 ConfigScreen - Windows 只显示 UI 缩放
- [ ] 验证 ImeStatusIndicator 在聊天室内正确显示
- [ ] 修复 UI 缩放功能（hudScale 实际应用到 HUD）

---

## 文件结构 (v3.0.0)

```
ChineseIME-Fabric-1.21.4/
├── native/
│   ├── CMakeLists.txt
│   └── src/
│       ├── ime_bridge.cpp       # 主入口、Hook、导出函数
│       ├── ime_bridge.h         # 头文件
│       ├── ime_state_manager.cpp # 状态管理单例
│       ├── imm32_monitor.cpp    # IMM32 监控（降级用）
│       ├── tsf_monitor.cpp      # TSF 监控（事件驱动补充）
│       ├── win_event_bridge.cpp # 窗口事件 Bridge（主事件路径）
│       ├── win_event_bridge.h   # EventCallbacks (std::function)
│       ├── sta_thread.cpp       # STA 线程管理
│       └── common.h             # 共享类型定义
├── src/main/java/com/example/chineseime/
│   ├── ChineseIMEInitializer.java  # 主初始化器、HWND 查找
│   ├── config/
│   │   └── ModConfig.java        # 配置管理
│   ├── hud/
│   │   ├── CandidateHud.java     # 候选词 HUD (横式)
│   │   └── ImeStatusIndicator.java # 输入法状态指示器
│   ├── platform/
│   │   ├── PlatformIMEManager.java # 平台 IME 管理
│   │   └── win32/
│   │       ├── NativeImeBridge.java     # JNA 接口
│   │       └── WindowsIMEBridgeNative.java # Windows IME Bridge
│   └── engine/
│       ├── PinyinDictionary.java # 内置拼音引擎
│       └── CangjieDictionary.java # 内置仓颉引擎
└── build/libs/chineseime-1.0.0.jar
```

---

## 当前状态 (2026-05-16)

- **C++ DLL**: `natives/Release/chineseime_native.dll` (v3.0.0)
- **Java Mod**: `build/libs/chineseime-1.0.0.jar`
- **DLL 加载**: 正常工作 (DLL version: 3.0.0)
- **WndProc Hook**: 待验证（`Hooked window: false`）
- **IME 类型检测**: 正常工作（日志显示 `IME=拼`）
- **Caps Lock 检测**: 正常工作
- **Shift 检测**: 待验证

---

## 当前未解决问题 (2026-05-16)

### ⚠️ WndProc Hook 失败
- **现象**: `Hooked window: false`
- **可能原因**:
  1. Minecraft 窗口可能有 `WS_EX_LAYERED` 标志，导致 `SetWindowLongPtr` 失败
  2. 窗口已被另一个 Hook 替换
  3. 权限问题
- **排查**: 日志显示 `Using HWND from enum: xxx (of N windows)` - HWND 有效
- **降级**: WH_GETMESSAGE/WH_CALLWNDPROC hook 作为自动降级

### ⚠️ 候选词和状态指示器不显示
- **原因**: 因 Hook 失败，IME 消息未被捕获
- **修复**: 先解决 Hook 问题

---

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| Ctrl+Shift+T | 显示测试候选词（开发用） |
| ↑/↓ 或 ←/→ | 选择候选词 |
| [ / ] | 上一页/下一页 |

---

## 技术笔记

### v3.0.0 架构 — 事件驱动优先，多层降级

```
┌─────────────────────────────────────────────────────┐
│  事件驱动路径（主）                                  │
│  WinEventBridge::hookWindow() → SetWindowLongPtr    │
│  → ImeWndProc → WM_IME_* 消息 → 回调 Java           │
└─────────────────────────────────────────────────────┘
           ↓ 失败时自动降级
┌─────────────────────────────────────────────────────┐
│  Hook 降级路径                                       │
│  InstallMessageHook() → WH_GETMESSAGE +            │
│  WH_CALLWNDPROC hook → MessageGetMsgProc/           │
│  MessageCallWndProc → 回调 Java                      │
└─────────────────────────────────────────────────────┘
           ↓ 事件驱动失效时
┌─────────────────────────────────────────────────────┐
│  轮询降级路径（Java 侧）                             │
│  WindowsIMEBridgeNative.update() 每 tick 轮询       │
│  → GetCompositionString/GetCandidates 等           │
└─────────────────────────────────────────────────────┘
```

### IME 类型检测优先级
1. **TSF GUID 匹配** (最精确) - `OnActivated` 中的 `guidProfile`
2. **HKL 检测** - `GetKeyboardLayout` 返回的 IME ID
3. **语言默认** - zh-CN → PINYIN, zh-TW → CANGJIE

### 调试方法
- Java 调试: 查看游戏日志中的 `[ChineseIME]` 前缀日志
- **关键日志**:
  - `[ChineseIME] Using HWND from enum: xxx` - HWND 获取成功
  - `[ChineseIME] Hooked window: true/false` - Hook 结果
  - `[ChineseIME] WndProc REPLACED` - WinEventBridge hook 成功
  - `[ChineseIME] WH_GETMESSAGE hook installed` - 降级 hook 成功
  - `[ChineseIME] Poll: IME=X, CMode=Y, Caps=Z` - 轮询状态