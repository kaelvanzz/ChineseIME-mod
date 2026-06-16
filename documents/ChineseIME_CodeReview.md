# ChineseIME Mod - 深度代码审查报告

## 项目信息
- **仓库**: kaelvanzz/ChineseIME-mod
- **目标**: Minecraft 1.21.4 Fabric 中文输入法同步
- **技术栈**: Java 21 + JNA + C++ DLL (IMM32/TSF API)
- **文件规模**: ~3,000+ 行代码 (Java + C++)

---

## 一、架构设计评估

### 1.1 整体架构评分: 6.5/10

**优点:**
- 分层清晰: Java UI → JNA Bridge → C++ Native → Windows API
- 状态管理使用快照+变化检测模式，避免不必要的回调
- 多层级回退: TSF → IMM32 → 内置引擎
- 文档完善 (AGENTS.md 记录了关键设计决策)

**缺点:**
- C++ 层全局状态过多，难以单元测试
- Java 和 C++ 的回调机制存在线程安全问题
- 轮询架构(60fps)对电池/性能不够友好

---

## 二、严重问题 (Critical)

### ISSUE-001: 内存泄漏 - C++ 多处 new[] 无异常安全保护

**文件**: `native/src/ime_bridge.cpp`, `native/src/imm32_monitor.cpp`, `native/src/tsf_monitor.cpp`
**风险**: DLL 长期运行后内存持续增长，可能导致 Minecraft 崩溃

**问题代码示例** (ime_bridge.cpp: PollIMEState):
```cpp
wchar_t* compBuf = new wchar_t[wcharLen + 1];
LONG actualLen = ImmGetCompositionString(himc, GCS_COMPSTR, compBuf, compLen);
if (actualLen <= 0) {
    actualLen = ImmGetCompositionString(himc, GCS_COMPREADSTR, compBuf, compLen);
}
// 如果这里发生异常(如 ImmGetCompositionString 返回错误)，delete[] 不会执行
// ...
delete[] compBuf;  // 可能永远不会执行
```

**同样问题在**:
- `tsf_monitor.cpp` 的 `updateCache()` 中 `new char[bufSize]`
- `imm32_monitor.cpp` 的 `processComposition()` 中 `new wchar_t[]`
- `imm32_monitor.cpp` 的 `processCandidate()` 中 `new char[]`

**修复建议**:
```cpp
// 使用 std::vector 自动管理内存
std::vector<wchar_t> compBuf(wcharLen + 1);
LONG actualLen = ImmGetCompositionString(himc, GCS_COMPSTR, compBuf.data(), compLen);
// 无需手动 delete，异常安全
```

---

### ISSUE-002: 轮询线程竞争条件 - 可能导致死锁或崩溃

**文件**: `native/src/ime_bridge.cpp`
**风险**: DLL 卸载时可能死锁，Minecraft 退出时崩溃

**问题代码**:
```cpp
void StopTsfListen(void) {
    if (g_pollingRunning.load()) {
        g_pollingRunning.store(false);
        if (g_pollingThread.joinable()) {
            g_pollingThread.join();  // 如果线程卡在 Sleep(16) 或 Windows API 中...
        }
    }
    // ... 然后尝试在 STA 线程中执行 shutdown
    if (g_staThread) {
        g_staThread->submitTask([...] { ... });
        g_staThread->stop();  // 可能和上面的 join 竞争
    }
}
```

**问题分析**:
1. `g_pollingThread` 在 `Sleep(16)` 期间，`g_pollingRunning.store(false)` 后需要等待最多 16ms
2. 但如果线程卡在 `GetKeyboardState()` 或 `ImmGetContext()` 等 Windows API 中(这些 API 可能阻塞)，join 可能永远等待
3. `DllMain` 中调用 `StopTsfListen()`，而 `DllMain` 有严格限制(Loader Lock)，可能导致死锁

**修复建议**:
```cpp
// 使用条件变量 + 超时机制
void StopTsfListen(void) {
    g_pollingRunning.store(false);
    g_pollingCv.notify_all();  // 唤醒 Sleep

    if (g_pollingThread.joinable()) {
        auto status = g_pollingThread.wait_for(std::chrono::seconds(2));
        if (status == std::future_status::timeout) {
            // 强制 detach，避免阻塞主线程
            g_pollingThread.detach();
        }
    }
}
```

---

### ISSUE-003: JNA 回调线程安全问题

**文件**: `src/main/java/.../NativeImeBridge.java`, `src/main/java/.../WindowsIMEBridgeNative.java`
**风险**: C++ 回调在轮询线程执行，直接修改 Java 对象状态，可能导致并发问题

**问题分析**:
```java
// WindowsIMEBridgeNative.update() 在 ClientTick 线程执行
public void update() {
    // 这些调用触发 C++ 回调，回调在 C++ 轮询线程执行
    NativeImeBridge.refreshImeState();

    // 但 candidateHud 的状态可能被 C++ 回调同时修改
    if (candidateHud != null) {
        candidateHud.updateCandidatesKeepSelection(...);  // 主线程
    }
}
```

C++ 回调通过 JNA 直接调用 Java 方法，执行在 C++ 轮询线程，而 `candidateHud` 的渲染在 Minecraft 主线程。虽然当前代码在 `update()` 中统一读取状态后再更新 HUD，但如果未来扩展，容易引入并发 Bug。

**当前缓解措施**: C++ 回调实际上被忽略了(见 ISSUE-004)，所以这个问题目前不触发。

---

## 三、高危问题 (High)

### ISSUE-004: C++ 回调被注册但未被实际使用

**文件**: `native/src/ime_bridge.cpp`, `src/main/java/.../NativeImeBridge.java`
**风险**: 设计冗余，回调机制实际上走不通，全靠轮询

**问题分析**:
C++ 层提供了回调机制:
```cpp
__declspec(dllexport) void SetCallbacks(void* candidateUpdate, void* layoutChange, ...) {
    chineseime::setCandidateCallback(reinterpret_cast<CandidateUpdateFunc>(candidateUpdate));
    // ...
}
```

Java 层也注册了回调:
```java
public static void registerCallbacks(...) {
    INSTANCE.SetCallbacks(candidateCallback, layoutCallback, modeCallback, keyboardCallback);
}
```

**但 Java 层从未调用 `registerCallbacks()`！**

查看 `WindowsIMEBridgeNative.initialize()`:
```java
public boolean initialize() {
    NativeImeBridge.getInstance();
    // ... 没有调用 registerCallbacks() ...
    NativeImeBridge.startListening(0L);
    NativeImeBridge.startTsfListening();
    return true;
}
```

**结果**: C++ 层的 `onCandidateChanged()`、`onImeStateChanged()` 等回调函数被调用，但函数指针是 `nullptr`，所以回调被静默忽略。所有状态同步全靠 Java 层的 `update()` 轮询读取。

**建议**: 要么移除回调机制简化代码，要么实现真正的回调驱动架构。

---

### ISSUE-005: HKL/IME ID 硬编码，不支持第三方输入法

**文件**: `native/src/ime_state_manager.cpp`
**风险**: 搜狗、QQ拼音、百度输入法等无法识别

**问题代码**:
```cpp
InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId) {
    switch (imeId) {
        case 0x0001: case 0x0010: case 0xE010: case 0xE020: return PINYIN;  // 微软拼音
        case 0x0002: case 0xE011: return WUBI;  // 微软五笔
        // ... 只有微软输入法
    }
}
```

**搜狗拼音的 IME ID 可能是 0xE020 或其他值，但这里只匹配微软的 ID。**

**修复建议**:
```cpp
// 使用输入法名称检测作为回退
InputMethodType detectFromImeName(const wchar_t* name) {
    if (wcsstr(name, L"搜狗") || wcsstr(name, L"Sogou")) return PINYIN;
    if (wcsstr(name, L"QQ")) return PINYIN;
    // ...
}
```

---

### ISSUE-006: `GetKeyboardStateForPolling` 使用 `GetAsyncKeyState` 而非 `GetKeyboardState`

**文件**: `native/src/ime_bridge.cpp`
**风险**: 与 AGENTS.md 文档矛盾，可能导致 Caps Lock 检测不准确

**问题代码**:
```cpp
__declspec(dllexport) int GetKeyboardStateForPolling(int vKey) {
    return (GetAsyncKeyState(vKey) & 0x8000) ? 1 : 0;  // 错误！
}
```

AGENTS.md 明确说:
> **必须使用 `GetKeyboardState` 而非 `GetAsyncKeyState`**
> `GetAsyncKeyState` 是线程相关的，从轮询线程调用时返回错误状态

但 `PollKeyboardState()` 使用了正确的 `GetKeyboardState`，而这个导出函数给 Java 调用的却用了错误的 API。

---

## 四、中等问题 (Medium)

### ISSUE-007: `CandidateHud` 渲染计算重复且低效

**文件**: `src/main/java/.../hud/CandidateHud.java`
**风险**: 每帧重复计算宽度，导致 GC 压力和性能问题

**问题**:
```java
public void render(DrawContext ctx) {
    // 每帧都重新计算所有宽度
    int[] itemWidths = computeItemWidths(start, end, scale);
    // computeItemWidths 内部每次 new int[]
    // 且每次调用 font.getWidth() 可能有缓存查找开销
}
```

**建议**: 在 `updateCandidates()` 时预计算并缓存宽度，仅在 scale 变化时重新计算。

---

### ISSUE-008: `syncFromWindows()` 空方法

**文件**: `src/main/java/.../platform/PlatformIMEManager.java`
**风险**: 代码残留，逻辑不完整

```java
private void syncFromWindows() {
    // 完全空的！AGENTS.md 中详细描述了这个方法应该做什么
}
```

AGENTS.md 说应该有内置引擎回退逻辑:
```java
if (!winComposition.isEmpty()) {
    if (!winCandidates.isEmpty()) {
        hud.updateCandidates(winCandidates, winComposition);
    } else {
        // 使用内置引擎生成候选词回退
        List cands = getSuggestions(winComposition);
        hud.updateCandidates(cands, winComposition);
    }
}
```

但实际代码中这部分逻辑分散在 `WindowsIMEBridgeNative.update()` 中，没有统一封装。

---

### ISSUE-009: `ImeStatusIndicator` 和 `CandidateHud` 位置硬编码

**文件**: `src/main/java/.../hud/ImeStatusIndicator.java`, `CandidateHud.java`
**风险**: 不同分辨率/缩放比例下位置可能不正确

```java
int chatInputTop = scaledH - 22 - 14;  // 硬编码 22 和 14
int y = chatInputTop - 2 - size;       // 硬编码 2
```

这些魔法数字来自 Minecraft 的聊天框高度，但可能在不同版本或配置下变化。

---

### ISSUE-010: C++ 头文件 `#include` 被截断

**文件**: `native/src/ime_bridge.cpp`, `native/src/tsf_monitor.cpp`
**风险**: 编译可能失败，或使用了错误的头文件

查看代码:
```cpp
#include "ime_bridge.h"
#include "ime_state_manager.h"
#include "tsf_monitor.h"
#include "imm32_monitor.h"
#include "jni_callback.h"
#include "sta_thread.h"
#include   // 这里被截断了！
#include   // 被截断
#include   // 被截断
#include   // 被截断
#include   // 被截断
#include   // 被截断
#include   // 被截断
#include   // 被截断
```

实际应该是:
```cpp
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include <imm.h>
#include <msctf.h>
```

虽然 GitHub API 返回的内容可能被截断显示，但实际源码文件应该是完整的。需要确认编译是否正常。

---

## 五、低等问题 (Low)

### ISSUE-011: 魔法数字过多

- `IME_ID` 硬编码: `0x0001`, `0xE010`, `0xE020` 等
- 颜色值硬编码: `0xB3000000`, `0x66B1B4B6` 等
- 尺寸硬编码: `36`, `60`, `1080P` 等

建议提取为常量或配置文件。

### ISSUE-012: 日志级别不当

```java
if (tickCounter % 20 == 0) {
    ChineseIMEInitializer.LOGGER.info("[ChineseIME] Poll: IME=...");  // 每 20 tick 输出 info
}
```

高频日志应该使用 `debug` 级别，避免污染日志文件。

### ISSUE-013: `PlatformIMEManager.getPlatform()` 每次调用都执行

```java
public static OS getPlatform() {
    String os = System.getProperty("os.name").toLowerCase();  // 每次调用都执行
    // ...
}
```

应该缓存结果。

---

## 六、代码统计

| 指标 | 数值 |
|------|------|
| Java 文件数 | ~15 个 |
| C++ 文件数 | ~10 个 |
| 总代码行数 | ~3,000+ |
| 严重问题 | 3 个 |
| 高危问题 | 3 个 |
| 中等问题 | 4 个 |
| 低等问题 | 3 个 |

---

## 七、修复优先级建议

| 优先级 | 问题 | 预计工作量 |
|--------|------|-----------|
| P0 | ISSUE-001 内存泄漏 | 2 小时 |
| P0 | ISSUE-002 线程竞争 | 4 小时 |
| P1 | ISSUE-004 回调未使用 | 1 小时 (移除或实现) |
| P1 | ISSUE-005 第三方输入法 | 4 小时 |
| P1 | ISSUE-006 GetAsyncKeyState | 30 分钟 |
| P2 | ISSUE-007 渲染优化 | 2 小时 |
| P2 | ISSUE-008 syncFromWindows | 2 小时 |
| P3 | ISSUE-009 硬编码位置 | 2 小时 |

---

## 八、总体评价

**项目成熟度**: 原型/开发阶段 (Alpha)
**代码质量**: 6/10
**可维护性**: 5/10
**稳定性风险**: 中高 (内存泄漏 + 线程问题)

**优势**:
- 架构设计有思考，分层合理
- 文档 (AGENTS.md) 非常详细，记录了踩坑经验
- 多层级回退策略健壮

**劣势**:
- C++ 层内存管理不规范
- 线程同步设计有缺陷
- 大量硬编码值
- 回调机制未实际使用但代码残留

**建议**:
1. 立即修复内存泄漏 (使用 RAII/智能指针)
2. 重构线程退出机制 (使用条件变量 + 超时)
3. 统一状态同步逻辑 (移除或实现回调)
4. 添加单元测试 (特别是 C++ 层的状态管理)
5. 使用静态分析工具 (如 Clang Static Analyzer, Coverity)
