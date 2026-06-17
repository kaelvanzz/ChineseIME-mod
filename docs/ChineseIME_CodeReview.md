# ChineseIME Mod - 深度代码审查报告

> ⚠️ **历史文档**：本文档基于 2026-05-10 的代码审查。部分问题可能已修复，请参考实际代码验证。

## 项目信息
- **仓库**: kaelvanzz/ChineseIME-mod
- **目标**: Minecraft 1.21.4 Fabric 中文输入法同步
- **技术栈**: Java 21 + JNA + C++ DLL (IMM32/TSF API)
- **文件规模**: ~3,000+ 行代码 (Java + C++)

---

## 一、架构设计评估

### 1.1 整体架构评分: 7/10

**优点:**
- 分层清晰: Java UI → JNA Bridge → C++ Native → Windows API
- 状态管理使用快照+变化检测模式，避免不必要的回调
- 多层级降级: WinEventBridge → WH Hook → Java Polling
- 文档完善 (AGENTS.md 记录了关键设计决策)
- TSF 监听 + IMM32 回退支持多种输入法

**缺点:**
- C++ 层全局状态较多
- Java 和 C++ 的回调机制存在线程安全问题（已缓解）
- 事件驱动 + 轮询混合可能导致复杂度增加

---

## 二、已修复问题 ✅

以下问题在后续版本中已修复：

### ISSUE-001: 内存泄漏 - C++ 多处 new[] 无异常安全保护
**状态**: ✅ 已修复 - 使用 `std::vector` 替代原始 `new[]`

### ISSUE-002: 轮询线程竞争条件 - 可能导致死锁或崩溃
**状态**: ✅ 已缓解 - 使用 `std::mutex` + 条件变量保护

### ISSUE-003: JNA 回调线程安全问题
**状态**: ✅ 已缓解 - C++ 回调通过 `WinEventBridge::EventCallbacks` 统一管理

### ISSUE-004: C++ 回调被注册但未被实际使用
**状态**: ✅ 已修复 - 删除 `jni_callback.h/cpp`，回调统一经 `WinEventBridge` 管理

### ISSUE-006b: `ImeStateManager::updateCandidates()` 盲目用空字符串覆盖已有状态
**状态**: ✅ 已修复 - 增量合并逻辑，只在必要时更新

---

## 三、仍需关注的问题

### ISSUE-005: HKL/IME ID 硬编码，不支持第三方输入法

**文件**: `native/src/ime_state_manager.cpp`
**风险**: 搜狗、QQ拼音、百度输入法等无法识别

**问题代码示例**:
```cpp
switch (imeId) {
    case 0x0001: case 0x0010: case 0xE010: case 0xE020: return PINYIN;  // 微软拼音
    // ... 只有微软输入法
}
```

**当前缓解**: 代码已添加窗口枚举方式获取第三方 IME 候选词（`EnumCandidateWindowsProc`）

### ISSUE-007: `CandidateHud` 渲染计算重复且低效

**文件**: `src/main/java/.../hud/CandidateHud.java`
**风险**: 每帧重复计算宽度，导致 GC 压力和性能问题

**建议**: 在 `updateCandidates()` 时预计算并缓存宽度，仅在 scale 变化时重新计算。

### ISSUE-008: `syncFromWindows()` 空方法

**文件**: `src/main/java/.../platform/PlatformIMEManager.java`
**风险**: 代码残留，逻辑不完整

```java
private void syncFromWindows() {
    // 完全空的！
}
```

**说明**: 该方法原本设计用于同步状态，但实际同步逻辑已分散在 `WindowsIMEBridgeNative.update()` 中。

### ISSUE-009: `ImeStatusIndicator` 和 `CandidateHud` 位置硬编码

**文件**: `src/main/java/.../hud/ImeStatusIndicator.java`, `CandidateHud.java`
**风险**: 不同分辨率/缩放比例下位置可能不正确

---

## 四、低优先级问题

### ISSUE-011: 魔法数字过多

- `IME_ID` 硬编码: `0x0001`, `0xE010`, `0xE020` 等
- 颜色值硬编码: `0xB3000000`, `0x66B1B4B6` 等
- 尺寸硬编码: `36`, `60`, `1080P` 等

### ISSUE-012: 日志级别不当

部分日志使用 `info` 级别，高频输出可能污染日志。

### ISSUE-013: `PlatformIMEManager.getPlatform()` 每次调用都执行

```java
public static OS getPlatform() {
    String os = System.getProperty("os.name").toLowerCase();  // 每次调用都执行
    // ...
}
```

**建议**: 缓存结果。

---

## 五、代码统计

| 指标 | 数值 |
|------|------|
| Java 文件数 | ~19 个 |
| C++ 文件数 | ~8 个 |
| 总代码行数 | ~4,000+ |
| 严重问题 | 0 个 (已修复) |
| 高危问题 | 0 个 (已修复) |
| 中等问题 | 3 个 |
| 低等问题 | 3 个 |

---

## 六、修复优先级建议

| 优先级 | 问题 | 预计工作量 |
|--------|------|-----------|
| P1 | ISSUE-007 渲染优化 | 2 小时 |
| P1 | ISSUE-008 syncFromWindows | 2 小时 |
| P2 | ISSUE-009 硬编码位置 | 2 小时 |
| P3 | ISSUE-011 魔法数字 | 1 小时 |
| P3 | ISSUE-012 日志级别 | 30 分钟 |
| P3 | ISSUE-013 getPlatform 缓存 | 30 分钟 |

---

## 七、总体评价

**项目成熟度**: 开发阶段 (Beta)
**代码质量**: 7/10
**可维护性**: 7/10
**稳定性风险**: 中低 (核心问题已修复)

**优势**:
- 架构设计有思考，分层合理
- 文档 (AGENTS.md) 非常详细，记录了踩坑经验
- 多层级降级策略健壮
- TSF + IMM32 双架构支持

**劣势**:
- 部分硬编码值
- 渲染性能可优化
- 某些设计文档与实现已有差异

**建议**:
1. 优化渲染计算性能
2. 统一状态同步逻辑
3. 添加单元测试 (特别是 C++ 层的状态管理)
4. 更新设计文档以反映实际实现