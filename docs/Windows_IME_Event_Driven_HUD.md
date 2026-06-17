# Windows IME 事件驱动 + 自绘 HUD 方案设计

> ⚠️ **重要说明**：本文档是**早期设计文档**，记录了设计思路和决策过程。**实际代码实现与本文描述已有较大差异**，请以以下文件为准：
> - `native/src/ime_bridge.cpp` - 主 Hook 实现
> - `native/src/win_event_bridge.cpp` - WinEventBridge 实现
> - `src/main/java/.../WindowsIMEBridgeNative.java` - Java 桥接
>
> 主要差异：
> - `WindowsIMEBridgeNative` 仍存在（文档说已删除）
> - 使用 C-style 函数指针而非纯 JNA Callback
> - TSF 监听作为补充而非独立路径
> - 保留了 Java 侧轮询作为降级

---

## 实际实现状态

以下列出本文档描述的设计与实际实现代码的对照：

| 本文描述 | 实际实现 | 状态 |
|----------|----------|------|
| `WindowsIMEBridgeNative` 轮询降级桥接 | ✅ 仍存在，但主要是事件回调 | ⚠️ 部分实现 |
| `EventNativeLibrary` 接口定义 | ✅ 通过 `SetEventCallbacks` (函数指针) 实现 | ✅ 已实现 |
| `HookWindowProc(hwnd)` + `SetEventCallbacks()` 顺序 | ✅ `hookWindow()` 中先 hook 再注册回调 | ✅ 已实现 |
| `tick()` 做状态更新 | ✅ `update()` 仅更新状态指示器 + 轮询降级 | ✅ 已实现 |
| `WindowsIMEEventBridge` 实例字段存储回调 | ✅ `WindowsIMEBridgeNative` 中的回调实例字段 | ✅ 已实现 |
| WndProc 替换方式 | ✅ `SetWindowLongPtr(GWLP_WNDPROC)` → `ImeWndProc` | ✅ 已实现 |
| 候选词获取 | ✅ `WM_IME_NOTIFY` → `readCandidates()` | ✅ 已实现 |
| `refreshCandidates()` 手动触发 | ✅ 保留导出，可用于翻页后刷新 | ✅ 已实现 |
| `SetCallbacks` 旧 4-arg API | ❌ 已删除；被 `SetEventCallbacks` (5-arg) 取代 | ❌ 已删除 |
| 纯事件驱动，无轮询 | ❌ 保留 Java 侧轮询作为降级 | ⚠️ 有差异 |
| TSF 作为主要 IME 检测 | ⚠️ TSF 作为补充，HKL 回退 | ⚠️ 有差异 |

---

## 核心思路（设计文档原文）

**"事件驱动获取 preedit/commit，按需轮询获取候选词"**

结合 CocoaInput 的 Windows 窗口消息劫持方式和 ChineseIME-mod 的自绘候选词 HUD，实现全屏模式下也能正常输入中文的方案。

---

## 架构对比（设计文档原文）

| 维度 | 原轮询方案 | CocoaInput | 混合方案（本设计） |
|------|-----------|------------|----------------|
| 输入获取 | 60fps 轮询 ImmGetCompositionString | 拦截 WM_IME_COMPOSITION | **事件驱动 + 按需轮询** |
| 候选词获取 | 60fps 轮询 ImmGetCandidateList | 不获取（依赖系统窗口） | **事件触发时读取** |
| 文本提交 | 模拟按键 | 直接通过 WndProc 转发 | **事件回调直接获取** |
| 全屏支持 | ✅ 自绘 HUD | ❌ 系统窗口被压制 | ✅ **自绘 HUD** |
| 性能 | 持续 CPU 占用 | 零开销（事件驱动） | **有输入时才有开销** |
| 可靠性 | 可能漏状态 | 高（系统直接通知） | **高** |

---

## 系统架构（设计文档原文）

```
┌─────────────────────────────────────────┐
│  Minecraft Java (Fabric)                │
│  ├── CandidateHud (自绘候选词 HUD)        │ ← 保留现有实现
│  ├── ImeStatusIndicator (状态指示器)     │ ← 保留现有实现
│  └── WindowsIMEEventBridge (事件桥接)    │ ← 新增/替换轮询
├─────────────────────────────────────────┤
│  C++ DLL                                │
│  ├── wrapper_wndProc (替换 GLFW WndProc) │ ← 参考 CocoaInput
│  │   ├── WM_IME_STARTCOMPOSITION        │
│  │   ├── WM_IME_COMPOSITION              │
│  │   │   ├── GCS_COMPSTR → 回调 preedit  │
│  │   │   ├── GCS_RESULTSTR → 回调 commit │
│  │   │   └── GCS_CURSORPOS → 回调光标位置│
│  │   ├── WM_IME_ENDCOMPOSITION           │
│  │   └── WM_IME_NOTIFY (候选词变化)      │
│  ├── IMM32 候选词读取 (OnDemand)         │ ← 触发时读取
│  └── 轮询线程 (降级备用方案)              │ ← 保留
└─────────────────────────────────────────┘
```

---

## C++ DLL 实现要点（设计文档原文）

### 1. 窗口过程替换

实际代码在 `ime_bridge.cpp` 和 `win_event_bridge.cpp` 中实现。

### 2. IME 消息处理

实际代码在 `ImeWndProc` 函数中处理。

### 3. 候选词读取

使用 `ImmGetCandidateListW` 和窗口枚举两种方式。

---

## Java 层实现要点（设计文档原文）

### JNA 接口定义

实际代码在 `NativeImeBridge.java` 中定义。

### 事件桥接类

实际代码在 `WindowsIMEBridgeNative.java` 中实现。

---

## 翻页处理（设计文档原文）

在 KeyboardMixin 中拦截方向键处理翻页。

---

## 非拼音输入法兼容性（设计文档原文）

| 输入法 | 兼容性 | 风险点 |
|--------|--------|--------|
| **拼音** | ✅ 完美 | 基准测试 |
| **注音 (Bopomofo)** | 🟡 可能可用 | 空格确认行为、候选词索引语义 |
| **仓颉** | 🟡 可能可用 | preedit 显示字根或汉字 |
| **五笔** | 🟡 可能可用 | 类似拼音，字根编码 |
| **速成** | 🟡 可能可用 | 同仓颉 |

---

## 降级方案（设计文档原文）

> ⚠️ **`WindowsIMEBridgeNative.java` 已被保留**，但主要使用事件回调，Java 轮询仅作为最终降级。

当前实现的降级顺序：
1. **WinEventBridge::hookWindow** (SetWindowLongPtr)
2. **InstallMessageHook** (WH_GETMESSAGE/WH_CALLWNDPROC)
3. **Java 轮询** (WindowsIMEBridgeNative.update)

---

## 实现优先级（设计文档原文）

| 阶段 | 任务 | 预计时间 |
|------|------|---------|
| 1 | C++ WndProc 替换 + 基本 IME 消息处理 | 1 天 |
| 2 | Java 层 JNA 回调 + HUD 更新 | 0.5 天 |
| 3 | 文本插入 Mixin (ChatScreen textField) | 0.5 天 |
| 4 | 候选词按需读取 + 翻页处理 | 0.5 天 |
| 5 | 调试日志 + 多输入法测试 | 1 天 |
| 6 | 与现有轮询方案整合/降级 | 1 天 |

---

## 核心优势（设计文档原文）

1. **实时性**：preedit/commit 事件驱动，零延迟
2. **准确性**：commit 文本直接获取，无需模拟按键
3. **性能**：无输入时零 CPU 开销
4. **全屏支持**：自绘 HUD 不受窗口层级影响
5. **可降级**：事件驱动失效时自动切换轮询

---

## 参考资源

- [CocoaInput Windows 实现](https://github.com/Korea-Minecraft-Forum/CocoaInput-lib/blob/master/src/win/libwincocoainput.c)
- [Microsoft WM_IME_COMPOSITION 文档](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-composition)
- [ImmGetCompositionString 函数](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immgetcompositionstringw)