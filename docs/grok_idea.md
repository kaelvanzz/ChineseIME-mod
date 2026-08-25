**✅ 已重新詳細查看 `cangjie/hud` 分支最新狀態**

我把目前分支的整體情況整理如下：

### **總體評價**
這個分支目前**屎山程度中等偏高**。核心功能有一定基礎，但**輸入體驗不穩定**，特別是**拼音**和**候選詞顯示**兩個關鍵問題依然突出。

---

### **目前主要問題（優先級排序）**

**1. 最高優先 - 拼音輸入體驗差（吞字母 + 提前 Commit）**
- 輸入較長拼音時容易吞掉字母，或 composition 突然整個被 commit 送出。
- `CommitCallback` 防護不足，與系統 IME 衝突嚴重。

**2. 候選詞完全不顯示（極嚴重）**
- `PollIMEState()` 有拉取 candidates。
- 但 Java 側（`PlatformIMEManager`）幾乎沒有把資料推給 `CandidateHud` / `VerticalCandidateHud`。
- 目前只有 composition 有時能顯示，candidates 永遠是空的。

**3. 架構混亂**
- 同時存在 `CandidateHud`（橫版）和 `VerticalCandidateHud`（直版），但切換邏輯不明確。
- `InputMethodType` 辨識雖然有改，但倉頡還是容易被誤判為拼音。
- Polling + Callback + WinEventBridge 多頭馬車，容易 race condition。

**4. 其他問題**
- `PlatformIMEManager.java` 可能是目前最大黑盒（我還沒看到完整內容）。
- HUD 渲染代碼重複度高。
- Clear 機制不夠徹底，切換輸入法後容易殘留舊狀態。
- Logging 過於密集。

---

### **正面部分（已經做得好的）**

- `CandidateHud` 和 `VerticalCandidateHud` 的渲染品質不錯（1080p 基準、hover 效果、數字選擇）。
- `ImeStatusIndicator` 基礎架構存在。
- 有 Config 系統和 KeyBindingManager。
- Native 層有 TSF + IMM32 雙備援。

---

### **下一步強烈建議**

我建議你**不要急著融合 CocoaInput**，先把目前分支的核心問題打通再說。

**推薦優先順序**：

1. **Phase 1**：修復拼音吞字母（Commit 防護 + 狀態清除）
2. **Phase 2**：打通候選詞顯示通道
3. **Phase 3**：統一 HUD 邏輯 + InputMethodType 強化

**✅ 收到！** 我會同時為你處理 **Phase 1（拼音吞字母）** 和 **Phase 2（候選詞不顯示）** 的修復。

---

### **ChineseIME-mod cangjie/hud 分支 - Phase 1 & 2 聯合修復報告**

**目標**：同時解決「拼音吞字母 / 提前 Commit」 + 「候選詞完全不顯示」兩個核心問題。

---

### **Phase 1：修復拼音吞字母 + 莫名 Commit**

#### **1. ChineseIMEInitializer.java**（替換這兩個方法）

```java
private void registerCallbacks() {
    NativeImeBridge.CommitCallback commitCB = text -> {
        if (text == null || text.length() == 0) return;

        String committed = text.toString();
        LOGGER.info("[ChineseIME] CommitCallback: '{}'", committed);

        // === Phase 1 關鍵防護 ===
        boolean hasActiveComposition = false;
        if (this.imeManager != null) {
            CandidateHud hud = this.imeManager.getHud();
            VerticalCandidateHud vhud = this.imeManager.getVerticalHud();
            
            hasActiveComposition = 
                (hud != null && !hud.getInput().isEmpty()) ||
                (vhud != null && !vhud.getInput().isEmpty());
        }

        if (!hasActiveComposition) {
            insertTextToFocusedField(committed);
            if (this.imeManager != null) {
                this.imeManager.clearInput();   // 徹底清除
            }
            LOGGER.info("[ChineseIME] Commit accepted");
        } else {
            LOGGER.warn("[ChineseIME] Commit BLOCKED - composition still active");
        }
    };

    NativeImeBridge.setEventCallbacks(null, commitCB, candidateCB, imeChangeCB, keyboardCB);
}

public void insertTextToFocusedField(String text) {
    if (text == null || text.isEmpty()) return;

    MinecraftClient mc = MinecraftClient.getInstance();
    if (mc == null || !(mc.currentScreen instanceof ChatScreen chatScreen)) return;

    try {
        var field = ChatScreen.class.getDeclaredField("chatField");
        field.setAccessible(true);
        TextFieldWidget chatField = (TextFieldWidget) field.get(chatScreen);

        if (chatField != null) {
            String current = chatField.getText();
            int cursor = chatField.getCursor();
            String newText = current.substring(0, cursor) + text + current.substring(cursor);
            chatField.setText(newText);
            chatField.setCursor(cursor + text.length(), false);
        }
    } catch (Exception e) {
        LOGGER.error("[ChineseIME] Insert failed", e);
    }
}
```

---

### **Phase 2：修復候選詞不顯示**

#### **在 ChineseIMEInitializer.java 的 END_CLIENT_TICK 中加入強制更新**

```java
ClientTickEvents.END_CLIENT_TICK.register(client -> {
    if (this.imeManager != null) {
        this.imeManager.tick();

        // === Phase 2 關鍵：強制同步候選詞 ===
        if (client.currentScreen instanceof ChatScreen) {
            this.imeManager.updateHudFromNativeState();   // ← 需要在 PlatformIMEManager 實作這個方法
        }
    }

    // ... 其他按鍵處理保持不變
});
```

#### **PlatformIMEManager.java 需要加入以下方法**（強烈建議）

```java
public void updateHudFromNativeState() {
    // 從 Native 拉取最新狀態
    NativeImeBridge.RefreshImeState();

    String composition = NativeImeBridge.getCompositionString();
    int count = NativeImeBridge.getCandidateCount();
    int selected = NativeImeBridge.getSelectedCandidateIndex();

    List<String> candidates = new ArrayList<>();
    for (int i = 0; i < count; i++) {
        String cand = NativeImeBridge.getCandidate(i);
        if (cand != null) candidates.add(cand);
    }

    InputMode currentMode = /* 從 Native 取得 InputMethodType */;

    // 更新 HUD
    if (candidateHud != null) {
        candidateHud.update(candidates, composition, selected, currentMode);
    }
    if (verticalCandidateHud != null) {
        verticalCandidateHud.updateKeepSelection(candidates, composition, selected, 0, currentMode);
    }
}
```

---

### **共同需要做的清理**

1. **在 CandidateHud 和 VerticalCandidateHud 的 `updateCandidates` 方法最前面加上**：

```java
this.clear();   // 先徹底清除舊狀態
```

2. **在 PlatformIMEManager 的 `clearInput()` 方法中**：

```java
candidateHud.clear();
verticalCandidateHud.clear();
```

---

**現在請你做以下事情：**

1. 先套用上面的 `ChineseIMEInitializer.java` 修改
2. 把 **`PlatformIMEManager.java`** 完整內容貼給我（這是目前最大的黑盒）
3. 重新 build 後測試拼音輸入 + 候選詞

---

測試後告訴我：
- 吞字母問題是否改善？
- 候選詞是否開始出現？
- Console 有沒有出現 `Commit BLOCKED` 或其他關鍵 log？

準備好了就把 `PlatformIMEManager.java` 貼上來，我們繼續下一輪精準打怪！

**✅ 收到 `PlatformIMEManager.java`，我已完整分析完目前 `cangjie/hud` 分支的核心檔案。**

### 快速診斷總結

這個 `PlatformIMEManager` 是目前問題的**主要瓶頸**之一：
- 幾乎沒有把 Native 的 candidates 推給 HUD（只有 fallback 的 test mode）
- `tick()` 只呼叫 `windowsBridge.update()`，但沒有把結果更新到 HUD
- Commit 與 composition 狀態同步很弱
- 橫版/直版 HUD 切換邏輯存在，但實際更新路徑斷掉

---

### **Phase 1 + Phase 2 聯合修復 Patch**

#### **1. PlatformIMEManager.java 完整修改建議**

請**替換以下關鍵方法**：

```java
public void tick() {
    if (windowsBridge != null && syncEnabled) {
        windowsBridge.update();

        // === Phase 2 關鍵：強制同步 Native 狀態到 HUD ===
        updateHudFromNativeState();
    }
}

/**
 * Phase 1 & 2 核心方法：從 Native 同步最新狀態到 HUD
 */
public void updateHudFromNativeState() {
    if (windowsBridge == null) return;

    String composition = windowsBridge.getComposition();
    List<String> candidates = windowsBridge.getCandidates();
    int selectedIndex = windowsBridge.getSelectedIndex();
    InputMode mode = getDetectedInputMode();

    boolean isVertical = isVerticalLayout();

    if (isVertical) {
        verticalHud.updateKeepSelection(candidates, composition, selectedIndex, 0, mode);
    } else {
        hud.update(candidates, composition, selectedIndex, mode);
    }
}

public void clearInput() {
    hud.clearInput();
    verticalHud.clearInput();
    if (windowsBridge != null) {
        windowsBridge.clearInput();
    }
}
```

#### **2. WindowsIMEBridgeNative.java（建議增加這些方法）**

如果還沒有，請確保有以下方法：

```java
public String getComposition() {
    // 呼叫 Native GetCompositionString
    return /* ... */;
}

public List<String> getCandidates() {
    List<String> list = new ArrayList<>();
    int count = getCandidateCount();
    for (int i = 0; i < count; i++) {
        list.add(getCandidate(i));
    }
    return list;
}

public int getSelectedIndex() {
    return /* Native GetSelectedCandidateIndex */;
}

public void clearInput() {
    // 通知 Native 清除
}
```

---

#### **3. ChineseIMEInitializer.java - CommitCallback 加強版**

```java
private void registerCallbacks() {
    NativeImeBridge.CommitCallback commitCB = text -> {
        if (text == null || text.length() == 0) return;

        String committed = text.toString();
        LOGGER.info("[ChineseIME] CommitCallback: '{}'", committed);

        // Phase 1 防護
        if (this.imeManager != null) {
            boolean hasComposition = this.imeManager.hasInput();   // 使用 hasInput() 方法

            if (!hasComposition) {
                insertTextToFocusedField(committed);
                this.imeManager.clearInput();
            } else {
                LOGGER.warn("[ChineseIME] Commit BLOCKED due to active composition");
            }
        }
    };

    NativeImeBridge.setEventCallbacks(null, commitCB, candidateCB, imeChangeCB, keyboardCB);
}
```

---

**立即執行步驟**：

1. 修改 `PlatformIMEManager.java` 的 `tick()` 和新增 `updateHudFromNativeState()`
2. 修改 `ChineseIMEInitializer.java` 的 `registerCallbacks()`
3. 在兩個 HUD 類的 `updateCandidates` 方法最前面加上 `this.clear();`
4. 重新 build 測試

---

**測試重點**：
- 微軟拼音：輸入較長拼音，觀察是否還會吞字母
- 倉頡 / 速成：是否能顯示候選詞
- 切換輸入法後狀態是否正確清除

---

**下一步**：

測試完後告訴我結果，我會根據反饋繼續優化（例如加強 `WindowsIMEBridgeNative` 的同步機制）。

需要我把以上修改打包成一個完整的 `.md` 檔案嗎？還是你想先試試看？