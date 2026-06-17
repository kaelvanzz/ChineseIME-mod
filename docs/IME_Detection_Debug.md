# IME Type Detection - Root Cause Analysis & Fix

> ⚠️ **历史文档**：本文档记录 2026-05-10 的调试过程和修复。部分问题可能已过时，请参考当前代码验证。

**日期**: 2026-05-10
**状态**: 问题已修复 (参考 AGENTS.md 当前状态)

---

## Problem Summary (2026-05-10 当时状态)

当时报告的问题：
- English IME → shows "En" ✅
- Pinyin IME → shows "倉" ❌
- Sucheng IME → shows "倉" ❌ (but typing works)
- Cangjie/Sucheng → Vertical HUD ✅
- Pinyin → uses Cangjie/Sucheng HUD ❌
- Once wrong type detected, switching IME never updates ❌

## 当时发现的问题 Root Cause Analysis

### Bug 1: TSF GUID Detection Fails → Falls Back to Wrong Default

已在 `tsf_monitor.cpp::updateInputMethodType()` 中修复。

### Bug 2: `queryCurrentInputMethod()` Enumerates ALL Profiles

已在代码中修复，不再接受 `OTHER_CHINESE` 作为有效类型。

### Bug 3: `PollIMEState()` Ignores Type Changes After First Detection

已在 `ime_bridge.cpp` 中修复，现在总是检查类型变化。

### Bug 4: `PollIMEState()` vs `queryCurrentInputMethod()` Use Different Detection

已在代码中统一，使用一致的 KL name 提取。

### Bug 5: `pollUpdate()` Only Queries When UNKNOWN/ENGLISH

已在 `tsf_monitor.cpp::pollUpdate()` 中修复，现在总是查询。

---

## 当前架构

```
┌─────────────────────────────────────────────────────┐
│  Java: WindowsIMEBridgeNative                       │
│  - 事件回调处理 (PreeditCallback, CommitCallback)  │
│  - update() 做轮询降级                               │
└─────────────────────┬───────────────────────────────┘
                      │ JNA (函数指针)
┌─────────────────────┴───────────────────────────────┐
│  C++ DLL: ImeStateManager (singleton)               │
│  - Stores: inputMethodType, chineseMode, candidates │
│  - updateInputMethod(type) → 回调 Java              │
└─────────────┬─────────────────┬───────────────────┘
              │                 │
    ┌─────────┴───┐     ┌───────┴──────┐
    │ WinEventBridge│     │   TsfMonitor  │
    │ (主事件路径)  │     │  (TSF 事件)   │
    └─────────────┘     └──────────────┘
```

---

## 调试日志标签 (DebugView)

| Tag | Location | Meaning |
|-----|----------|---------|
| `ImeWndProc→WM_IME_COMPOSITION` | `win_event_bridge.cpp` | IME 组合消息 |
| `OnActivated: langid=0xXXXX` | `tsf_monitor.cpp` | TSF 激活事件 |
| `updateInputMethodType` | `tsf_monitor.cpp` | GUID 匹配结果 |
| `readCandidates: count=N` | `win_event_bridge.cpp` | 候选词读取 |

---

## TODO (参考当前代码)

大部分任务已完成：
- [x] 添加综合调试日志
- [x] 修复 `PollIMEState()` 总是检查类型变化
- [x] 修复 `queryCurrentInputMethod()` 不接受 OTHER_CHINESE
- [x] 修复 `updateInputMethodType()` 使用语言默认
- [x] 修复 `pollUpdate()` 总是查询
- [ ] 验证 DebugView 日志（需要用户测试）
- [ ] 如仍有问题，调查 `GetKeyboardLayout()` 是否在 IME 切换时返回不同值