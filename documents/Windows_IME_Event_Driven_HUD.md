# Windows IME 事件驱动 + 自绘 HUD 方案设计

## 核心思路

**"事件驱动获取 preedit/commit，按需轮询获取候选词"**

结合 CocoaInput 的 Windows 窗口消息劫持方式和 ChineseIME-mod 的自绘候选词 HUD，实现全屏模式下也能正常输入中文的方案。

---

## 架构对比

| 维度 | 原轮询方案 | CocoaInput | 混合方案（本设计） |
|------|-----------|------------|----------------|
| 输入获取 | 60fps 轮询 ImmGetCompositionString | 拦截 WM_IME_COMPOSITION | **事件驱动 + 按需轮询** |
| 候选词获取 | 60fps 轮询 ImmGetCandidateList | 不获取（依赖系统窗口） | **事件触发时读取** |
| 文本提交 | 模拟按键 | 直接通过 WndProc 转发 | **事件回调直接获取** |
| 全屏支持 | ✅ 自绘 HUD | ❌ 系统窗口被压制 | ✅ **自绘 HUD** |
| 性能 | 持续 CPU 占用 | 零开销（事件驱动） | **有输入时才有开销** |
| 可靠性 | 可能漏状态 | 高（系统直接通知） | **高** |

---

## 系统架构

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

## 数据流

```
用户按键
    ↓
系统 IME 处理
    ↓
产生 WM_IME_COMPOSITION 消息
    ↓
GLFW WndProc 被拦截
    ↓
├─ GCS_COMPSTR ──→ 读取 preedit 文本 ──→ 回调 Java ──→ 更新 HUD preedit
├─ GCS_RESULTSTR ─→ 读取 commit 文本 ──→ 回调 Java ──→ 插入文本到游戏
└─ 触发候选词读取 ─→ ImmGetCandidateList ─→ 回调 Java ──→ 更新 HUD 候选词
```

---

## C++ DLL 实现

### 1. 窗口过程替换

```cpp
// native/src/win_event_bridge.cpp
#include <windows.h>
#include <imm.h>
#include <vector>
#include <string>

static WNDPROC g_originalWndProc = nullptr;
static HWND g_hwnd = nullptr;

// Java 回调函数指针
void (*g_javaPreeditCallback)(const wchar_t* text, int cursorPos, int selStart, int selLen) = nullptr;
void (*g_javaCommitCallback)(const wchar_t* text) = nullptr;
void (*g_javaCandidateCallback)(const wchar_t** candidates, int count, int selectedIndex) = nullptr;

// 状态缓存
std::wstring g_lastComposition;
std::vector<std::wstring> g_lastCandidates;
int g_lastSelectedIndex = 0;
```

### 2. IME 消息处理

```cpp
LRESULT CALLBACK ImeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_IME_STARTCOMPOSITION: {
            // 开始新的输入会话，清空状态
            g_lastComposition.clear();
            g_lastCandidates.clear();
            if (g_javaPreeditCallback) {
                g_javaPreeditCallback(L"", 0, 0, 0);
            }
            return 0; // 阻止默认处理，避免 GLFW 重复
        }

        case WM_IME_COMPOSITION: {
            HIMC himc = ImmGetContext(hWnd);
            if (!himc) break;

            // 1. 处理 preedit (组合中) 文本
            if (lParam & GCS_COMPSTR) {
                LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
                if (len > 0) {
                    std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                    ImmGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), len);
                    buf[len / sizeof(wchar_t)] = 0;

                    // 获取光标位置
                    LONG cursor = 0;
                    if (lParam & GCS_CURSORPOS) {
                        cursor = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
                    }

                    // 获取选中范围 (ATTR_TARGET_CONVERTED)
                    LONG selStart = 0, selLen = 0;
                    if (lParam & GCS_COMPATTR) {
                        LONG attrLen = ImmGetCompositionStringW(himc, GCS_COMPATTR, nullptr, 0);
                        std::vector<char> attrs(attrLen);
                        ImmGetCompositionStringW(himc, GCS_COMPATTR, attrs.data(), attrLen);
                        for (int i = 0; i < attrLen; i++) {
                            if (attrs[i] == ATTR_TARGET_CONVERTED) {
                                if (selLen == 0) selStart = i;
                                selLen++;
                            }
                        }
                    }

                    g_lastComposition = buf.data();

                    // 回调 Java 更新 preedit
                    if (g_javaPreeditCallback) {
                        g_javaPreeditCallback(g_lastComposition.c_str(), (int)cursor, selStart, selLen);
                    }
                }
            }

            // 2. 处理 commit (确认) 文本
            if (lParam & GCS_RESULTSTR) {
                LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
                if (len > 0) {
                    std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                    ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf.data(), len);
                    buf[len / sizeof(wchar_t)] = 0;

                    // 回调 Java 插入文本
                    if (g_javaCommitCallback) {
                        g_javaCommitCallback(buf.data());
                    }
                }
            }

            // 3. 读取候选词（只在有 preedit 时触发）
            if ((lParam & GCS_COMPSTR) && !g_lastComposition.empty()) {
                readCandidates(himc);
            }

            ImmReleaseContext(hWnd, himc);
            return 0; // 阻止默认处理
        }

        case WM_IME_ENDCOMPOSITION: {
            // 输入结束，清空所有状态
            g_lastComposition.clear();
            g_lastCandidates.clear();
            if (g_javaPreeditCallback) {
                g_javaPreeditCallback(L"", 0, 0, 0);
            }
            return 0;
        }

        case WM_IME_NOTIFY: {
            // 候选词窗口状态变化
            if (wParam == IMN_OPENCANDIDATE || wParam == IMN_CHANGECANDIDATE) {
                HIMC himc = ImmGetContext(hWnd);
                if (himc) {
                    readCandidates(himc);
                    ImmReleaseContext(hWnd, himc);
                }
            }
            break;
        }
    }

    // 转发给原始 WndProc
    return CallWindowProc(g_originalWndProc, hWnd, msg, wParam, lParam);
}
```

### 3. 候选词读取

```cpp
void readCandidates(HIMC himc) {
    DWORD bufSize = ImmGetCandidateListW(himc, 0, nullptr, 0);
    if (bufSize == 0) {
        if (!g_lastCandidates.empty()) {
            g_lastCandidates.clear();
            if (g_javaCandidateCallback) {
                g_javaCandidateCallback(nullptr, 0, 0);
            }
        }
        return;
    }

    std::vector<char> buf(bufSize);
    CANDIDATELIST* candList = reinterpret_cast<CANDIDATELIST*>(buf.data());
    if (!ImmGetCandidateListW(himc, 0, candList, bufSize)) return;

    g_lastCandidates.clear();
    DWORD count = candList->dwCount;
    if (count > 10) count = 10; // 限制数量避免过大

    for (DWORD i = 0; i < count; i++) {
        wchar_t* str = (wchar_t*)(buf.data() + candList->dwOffset[i]);
        g_lastCandidates.push_back(str);
    }
    g_lastSelectedIndex = candList->dwSelection;

    // 准备指针数组回调 Java
    std::vector<const wchar_t*> ptrs;
    for (const auto& c : g_lastCandidates) {
        ptrs.push_back(c.c_str());
    }

    if (g_javaCandidateCallback) {
        g_javaCandidateCallback(ptrs.data(), (int)ptrs.size(), g_lastSelectedIndex);
    }
}
```

### 4. 导出接口

```cpp
extern "C" {
    __declspec(dllexport) void SetEventCallbacks(
        void (*preedit)(const wchar_t*, int, int, int),
        void (*commit)(const wchar_t*),
        void (*candidate)(const wchar_t**, int, int)
    ) {
        g_javaPreeditCallback = preedit;
        g_javaCommitCallback = commit;
        g_javaCandidateCallback = candidate;
    }

    __declspec(dllexport) void HookWindowProc(long hwnd) {
        g_hwnd = (HWND)hwnd;
        g_originalWndProc = (WNDPROC)GetWindowLongPtr(g_hwnd, GWLP_WNDPROC);
        SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)ImeWndProc);
    }

    __declspec(dllexport) void UnhookWindowProc() {
        if (g_hwnd && g_originalWndProc) {
            SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
            g_originalWndProc = nullptr;
        }
    }

    __declspec(dllexport) void RefreshCandidates() {
        // Java 层手动触发候选词刷新（用于翻页后）
        if (g_hwnd) {
            HIMC himc = ImmGetContext(g_hwnd);
            if (himc) {
                readCandidates(himc);
                ImmReleaseContext(g_hwnd, himc);
            }
        }
    }
}
```

---

## Java 层实现

### 1. JNA 接口定义

```java
public interface EventNativeLibrary extends StdCallLibrary {
    void SetEventCallbacks(
        PreeditCallback preedit,
        CommitCallback commit,
        CandidateCallback candidate
    );
    void HookWindowProc(long hwnd);
    void UnhookWindowProc();
    void RefreshCandidates();
}

public interface PreeditCallback extends Callback {
    void invoke(WString text, int cursorPos, int selStart, int selLen);
}

public interface CommitCallback extends Callback {
    void invoke(WString text);
}

public interface CandidateCallback extends Callback {
    void invoke(Pointer candidates, int count, int selectedIndex);
}
```

### 2. 事件桥接类

```java
public class WindowsIMEEventBridge {
    private final CandidateHud candidateHud;
    private String pendingPreedit = "";
    private List<String> pendingCandidates = new ArrayList<>();
    private int pendingSelectedIndex = 0;

    // 回调实例
    private final PreeditCallback preeditCb = (text, cursor, selStart, selLen) -> {
        // 切换到 Minecraft 主线程
        MinecraftClient.getInstance().execute(() -> {
            pendingPreedit = text.toString();
            updateHud();
        });
    };

    private final CommitCallback commitCb = (text) -> {
        MinecraftClient.getInstance().execute(() -> {
            insertTextToMinecraft(text.toString());
            pendingPreedit = "";
            pendingCandidates.clear();
            updateHud();
        });
    };

    private final CandidateCallback candidateCb = (cands, count, selected) -> {
        MinecraftClient.getInstance().execute(() -> {
            pendingCandidates.clear();
            for (int i = 0; i < count; i++) {
                Pointer ptr = cands.getPointer(i * Native.POINTER_SIZE);
                pendingCandidates.add(ptr.getWideString(0));
            }
            pendingSelectedIndex = selected;
            updateHud();
        });
    };

    private void updateHud() {
        if (candidateHud == null) return;

        if (!pendingCandidates.isEmpty()) {
            candidateHud.updateCandidatesKeepSelection(
                pendingCandidates, pendingPreedit, pendingSelectedIndex, 0
            );
        } else if (!pendingPreedit.isEmpty()) {
            // 无候选词时回退到内置词库
            List<String> fallback = PinyinDictionary.getSuggestions(pendingPreedit);
            candidateHud.updateCandidatesKeepSelection(
                fallback, pendingPreedit, 0, 0
            );
        } else {
            candidateHud.clearInput();
        }
    }

    private void insertTextToMinecraft(String text) {
        // 通过 Mixin 或直接操作当前 Screen 的文本字段
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc.currentScreen instanceof ChatScreen chat) {
            // 需要 Accessor Mixin 访问 ChatScreen 的 textField
            // chat.getChatField().insert(text);
        }
    }

    public void initialize() {
        long hwnd = getMinecraftWindowHandle();
        nativeLib.HookWindowProc(hwnd);
        nativeLib.SetEventCallbacks(preeditCb, commitCb, candidateCb);
    }

    public void shutdown() {
        nativeLib.UnhookWindowProc();
    }
}
```

---

## 翻页处理

### 问题

方向键翻页时，系统 IME 内部处理，不产生 `WM_IME_COMPOSITION`，需要手动检测按键并刷新候选词。

### 解决方案

```java
// 在 KeyboardMixin 中拦截方向键
@Mixin(Keyboard.class)
public class KeyboardMixin {
    @Inject(method = "onKey", at = @At("HEAD"), cancellable = true)
    private void onKey(long window, int key, int scancode, int action, int modifiers, CallbackInfo ci) {
        if (action != GLFW.GLFW_PRESS && action != GLFW.GLFW_REPEAT) return;

        CandidateHud hud = ChineseIMEInitializer.getCandidateHud();
        if (hud == null || !hud.isVisible()) return;

        // 翻页键
        if (key == GLFW.GLFW_KEY_UP || key == GLFW.GLFW_KEY_LEFT) {
            hud.prevPage();
            // 触发 C++ 重新读取候选词
            NativeImeBridge.refreshCandidates();
            ci.cancel();
        } else if (key == GLFW.GLFW_KEY_DOWN || key == GLFW.GLFW_KEY_RIGHT) {
            hud.nextPage();
            NativeImeBridge.refreshCandidates();
            ci.cancel();
        }

        // 数字键选择候选词
        if (key >= GLFW.GLFW_KEY_1 && key <= GLFW.GLFW_KEY_9) {
            int index = key - GLFW.GLFW_KEY_1;
            int globalIndex = hud.getPage() * hud.getPerPage() + index;
            if (globalIndex < hud.getCandidates().size()) {
                // 通知系统 IME 选择该候选词
                NativeImeBridge.selectCandidate(globalIndex);
                ci.cancel();
            }
        }
    }
}
```

---

## 非拼音输入法兼容性

### 风险评估

| 输入法 | 兼容性 | 风险点 |
|--------|--------|--------|
| **拼音** | ✅ 完美 | 基准测试 |
| **注音 (Bopomofo)** | 🟡 可能可用 | 空格确认行为、候选词索引语义 |
| **仓颉** | 🟡 可能可用 | preedit 显示字根或汉字 |
| **五笔** | 🟡 可能可用 | 类似拼音，字根编码 |
| **速成** | 🟡 可能可用 | 同仓颉 |

### 关键差异

```
拼音:    preedit="zhong" (ASCII) → 候选=[中, 种, 重...]
注音:    preedit="ㄓㄨㄥ" (Unicode) → 候选=[中, 种, 重...]
仓颉:    preedit="竹竹弓火" (Unicode) 或 preedit="中" (已转换)
```

### 调试日志建议

```cpp
// 在 WM_IME_COMPOSITION 中添加详细日志
char dbg[512];
sprintf_s(dbg, "[ChineseIME] WM_IME_COMPOSITION, lParam=0x%08X
", (DWORD)lParam);
OutputDebugStringA(dbg);

// 记录 preedit 内容和 Unicode 编码
if (lParam & GCS_COMPSTR) {
    sprintf_s(dbg, "[ChineseIME] COMPSTR: len=%d, text='%S', hex=", (int)len, buf.data());
    for (int i = 0; buf[i] && i < 10; i++) {
        sprintf_s(dbg + strlen(dbg), 512 - strlen(dbg), "%04X ", (unsigned short)buf[i]);
    }
    strcat_s(dbg, "
");
    OutputDebugStringA(dbg);
}

// 记录候选词详情
sprintf_s(dbg, "[ChineseIME] Candidates: count=%d, selection=%d, pageSize=%d, pageStart=%d
",
    (int)candList->dwCount, (int)candList->dwSelection,
    (int)candList->dwPageSize, (int)candList->dwPageStart);
OutputDebugStringA(dbg);
```

---

## 降级方案

当事件驱动失效时（如 WndProc 替换失败），自动回退到现有轮询方案：

```java
public class WindowsIMEBridge {
    private final WindowsIMEEventBridge eventBridge;
    private final WindowsIMEBridgeNative pollingBridge; // 现有实现
    private boolean useEventDriven = true;

    public void update() {
        if (useEventDriven) {
            // 事件驱动模式下，update() 只更新状态指示器
            if (statusIndicator != null) {
                statusIndicator.update(...);
            }
            // 检测事件驱动是否正常工作
            if (!eventBridge.isHealthy()) {
                useEventDriven = false;
                pollingBridge.initialize();
            }
        } else {
            // 轮询模式
            pollingBridge.update();
        }
    }
}
```

---

## 实现优先级

| 阶段 | 任务 | 预计时间 |
|------|------|---------|
| 1 | C++ WndProc 替换 + 基本 IME 消息处理 | 1 天 |
| 2 | Java 层 JNA 回调 + HUD 更新 | 0.5 天 |
| 3 | 文本插入 Mixin (ChatScreen textField) | 0.5 天 |
| 4 | 候选词按需读取 + 翻页处理 | 0.5 天 |
| 5 | 调试日志 + 多输入法测试 | 1 天 |
| 6 | 与现有轮询方案整合/降级 | 1 天 |

---

## 核心优势

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
