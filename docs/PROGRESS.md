# ChineseIME Mod 开发状态

> **最后更新**: 2026-05-22
> **状态**: 部分功能工作，候选词获取存在严重问题

## 项目概述
- **目标**: Fabric 1.21.4 中文输入法显示模组，支持 Windows
- **C++ DLL**: 处理 IME 状态检测和事件捕获
- **Java Mod**: 接收 DLL 数据并显示 UI
- **架构**: 事件驱动 Hook + 轮询降级 (CocoaInput 风格)

---

## 功能状态

| 功能 | 状态 | 说明 |
|------|------|------|
| DLL 加载 | ✅ 正常 | DLL version: 3.0.0 |
| HWND 获取 | ✅ 正常 | EnumWindows 成功 |
| Hook 安装 | ✅ 正常 | WH_GETMESSAGE hook 成功安装 |
| IME 类型检测 | ✅ 正常 | 显示 拼/倉/注/五/速 |
| Caps Lock 检测 | ✅ 正常 | |
| Shift 模式检测 | ⚠️ 有问题 | ShiftMode 始终为 false |
| 候选词获取 | ❌ 失败 | getCandidates() 返回空 |
| 回退词库 | ✅ 备用 | 当 IME 候选词为空时使用内置词库 |
| HUD 渲染 | ⚠️ 有问题 | 回退到内置词库而非 IME 候选词 |
| 竖式 HUD 布局 | ⚠️ 有问题 | 位置/显示问题 |

---

## 已知严重问题

### 问题 1: 候选词不从 Windows IME 获取

**现象**:
- 日志中持续出现 `getFallbackCandidates for mode: PINYIN`
- 说明 `currentCandidates` 始终为空

**可能原因**:
1. `GetCandidateCount()` 返回 0
2. `GetCandidates()` 未能正确读取候选词
3. 回调 `handleCandidates` 未被调用或收到空列表

**排查步骤**:
1. 检查 `handleCandidates: cands=X` 日志是否有出现
2. 如果没有出现 → 回调未触发
3. 如果出现但 cands=0 → native 端问题

### 问题 2: Shift 模式检测不工作

**现象**:
- 切换到英文模式时 `ShiftMode` 仍为 `false`

**代码位置**:
```cpp
// ime_bridge.cpp GetShiftMode()
__declspec(dllexport) int GetShiftMode() {
    int open = GetImeOpenStatus();
    int chinese = GetChineseMode();
    return (open && !chinese) ? 1 : 0;
}
```

**逻辑分析**:
- Shift 切换中英文 ↔ IME 打开/关闭状态切换
- 但 `GetImeOpenStatus()` 和 `GetChineseMode()` 可能返回意外的值

### 问题 3: 竖式 HUD (仓颉/速成) 不显示

**可能原因**:
- `currentCandidates` 为空导致 `updateHud()` 调用 `getFallbackCandidates()`
- `isVerticalLayout` 判断可能不准确

---

## 日志分析（latest.log 片段）

```
[23:26:22] [Render thread/INFO]: [ChineseIME] Indicator: IME=En, CapsLock=false, ShiftMode=false, ChineseMode=false
[23:26:23] [Render thread/INFO]: [ChineseIME] IME change: type=2, chineseMode=1
[23:26:23] [Render thread/INFO]: [ChineseIME] Indicator: IME=拼, CapsLock=false, ShiftMode=false, ChineseMode=true
[23:26:23] [Render thread/INFO]: [ChineseIME] Poll detected IME type change: 1 -> 2
[23:26:23] [Render thread/INFO]: [ChineseIME] IME change: type=2, chineseMode=0
[23:26:23] [Render thread/INFO]: [ChineseIME] Indicator: IME=拼, CapsLock=false, ShiftMode=false, ChineseMode=false
[23:26:24] [Render thread/INFO]: [ChineseIME] getFallbackCandidates for mode: PINYIN  ← 候选词为空，使用回退
[23:26:24] [Render thread/INFO]: [ChineseIME] getFallbackCandidates for mode: PINYIN  ← 每帧都调用
[23:26:35] [Render thread/INFO]: [ChineseIME] IME change: type=4, chineseMode=1
[23:26:35] [Render thread/INFO]: [ChineseIME] Indicator: IME=倉, CapsLock=false, ShiftMode=false, ChineseMode=true
```

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
│       ├── imm32_monitor.cpp    # IMM32 监控
│       ├── tsf_monitor.cpp      # TSF 监控
│       ├── win_event_bridge.cpp # 窗口事件 Bridge
│       ├── win_event_bridge.h   # EventCallbacks
│       ├── sta_thread.cpp       # STA 线程管理
│       └── common.h             # 共享类型
├── src/main/java/com/example/chineseime/
│   ├── ChineseIMEInitializer.java  # 主初始化器
│   ├── hud/
│   │   ├── CandidateHud.java     # 横式 HUD
│   │   ├── VerticalCandidateHud.java # 竖式 HUD
│   │   └── ImeStatusIndicator.java # 状态指示器
│   ├── platform/
│   │   ├── PlatformIMEManager.java # 平台管理
│   │   └── win32/
│   │       ├── NativeImeBridge.java     # JNA 接口
│   │       └── WindowsIMEBridgeNative.java # Windows Bridge
│   └── engine/
│       ├── PinyinDictionary.java # 内置拼音引擎
│       └── CangjieDictionary.java # 内置仓颉引擎
```

---

## 调试日志标签

| 标签 | 位置 | 含义 |
|------|------|------|
| `handleCandidates: cands=X` | WindowsIMEBridgeNative | 回调收到候选词 |
| `getFallbackCandidates` | WindowsIMEBridgeNative | 使用内置词库 |
| `GetCandidateCount` | NativeImeBridge | 获取候选词数量 |
| `Content changed` | WindowsIMEBridgeNative | 内容变化 |
| `IME change` | WindowsIMEBridgeNative | IME 类型/模式变化 |

---

## 待办事项

### 高优先级
- [ ] 调试候选词获取 - 为什么 `GetCandidates()` 返回空
- [ ] 调试 Shift 模式检测 - `GetShiftMode()` 逻辑问题
- [ ] 验证竖式 HUD 显示

### 中优先级
- [ ] 减少日志刷屏 - `getFallbackCandidates` 应降低为 debug 级别
- [ ] 验证 HUD 在各种场景下正确渲染

### 低优先级
- [ ] 文档与代码同步更新
- [ ] 清理未使用的代码

---

## 技术笔记

### 候选词获取流程

```
NativeImeBridge.getCandidates()
    ↓
NativeLibrary.GetCandidateCount()  ← 检查有多少候选词
    ↓
NativeLibrary.GetCandidate(index)  ← 逐个获取
```

### 回调流程

```
WM_IME_COMPOSITION (with GCS_COMPSTR)
    ↓
ImeWndProc / MessageGetMsgProc
    ↓
readCandidates(himc)
    ↓
g_javaCandidates(ptrs.data(), count, selIdx)  ← 回调 Java
    ↓
handleCandidates(List<String>, int)
```

### 可能的问题点

1. **回调未注册** - `SetEventCallbacks` 是否被正确调用？
2. **回调参数不匹配** - C++ 回调签名 vs Java JNA 接口
3. **ImmGetCandidateList 返回空** - 某些 IME 不支持
4. **候选词读取时机** - 需要在 `IMN_OPENCANDIDATE` 时读取

---

## 参考

- AGENTS.md - 完整的开发指南
- Windows_IME_Event_Driven_HUD.md - 架构设计文档