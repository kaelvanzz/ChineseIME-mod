# ChineseIME-mod Phase 1 修复报告 (历史)

> ⚠️ **历史文档**：本文档记录 2026-05-05 的实现修复建议。部分内容可能已过时。

**日期**：2026-05-05
**目標**：修復「拼音輸入吞字母 + 莫名 Commit」問題
**優先級**：最高（最影響使用者體驗）

---

## 問題原因總結 (當時分析)

- `CommitCallback` 在 composition 還沒真正結束時就過早觸發
- `insertTextToFocusedField` 直接用反射修改 `chatField`，容易與 Windows IME 本身的 composition 機制衝突
- 狀態清除不徹底，導致舊 composition 殘留
- Native 與 Java 側同步時機不一致

---

## Phase 1 修復 Patch（當時建議）

> ⚠️ 以下修復建議可能已在後續版本中整合，請參考當前代碼驗證。

### 1. ChineseIMEInitializer.java

當時建議加強 `CommitCallback` 的保護邏輯：

```java
private void registerCallbacks() {
    NativeImeBridge.CommitCallback commitCB = text -> {
        if (text == null || text.length() == 0) return;

        String committed = text.toString();
        LOGGER.info("[ChineseIME] CommitCallback triggered: '{}'", committed);

        // === 關鍵防護：只有沒有活躍 composition 時才真正 commit ===
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
                this.imeManager.clearInput();
            }
            LOGGER.info("[ChineseIME] Commit accepted and inserted: '{}'", committed);
        } else {
            LOGGER.warn("[ChineseIME] Commit BLOCKED - Still has active composition: {}", committed);
        }
    };

    NativeImeBridge.setEventCallbacks(null, commitCB, candidateCB, imeChangeCB, keyboardCB);
    LOGGER.info("[ChineseIME] Event callbacks registered with strengthened commit protection");
}
```

### 2. insertTextToFocusedField（当时建议版本）

當時建議更安全的實現：

```java
public void insertTextToFocusedField(String text) {
    if (text == null || text.isEmpty()) return;

    MinecraftClient mc = MinecraftClient.getInstance();
    if (mc == null || !(mc.currentScreen instanceof ChatScreen chatScreen)) return;

    try {
        java.lang.reflect.Field field = ChatScreen.class.getDeclaredField("chatField");
        field.setAccessible(true);
        TextFieldWidget chatField = (TextFieldWidget) field.get(chatScreen);

        if (chatField != null) {
            String current = chatField.getText();
            int cursor = chatField.getCursor();

            String newText = current.substring(0, cursor) + text + current.substring(cursor);
            chatField.setText(newText);
            chatField.setCursor(cursor + text.length(), false);

            LOGGER.info("[ChineseIME] Inserted '{}' at cursor {}, new text: '{}'",
                       text, cursor + text.length(), newText);
        }
    } catch (Exception e) {
        LOGGER.error("[ChineseIME] Failed to insert text to chat field", e);
    }
}
```

### 3. PlatformIMEManager.tick() 当时建议加入的逻辑

```java
public void tick() {
    // 強制同步最新狀態
    NativeImeBridge.RefreshImeState();

    // 強制清除已結束的 composition
    if (candidateHud != null && candidateHud.getInput().isEmpty()) {
        candidateHud.clear();
    }
    if (verticalCandidateHud != null && verticalCandidateHud.getInput().isEmpty()) {
        verticalCandidateHud.clear();
    }

    // ... 其他原有 tick 邏輯
}
```

---

## 測試步驟 (当时建议)

1. 替換以上程式碼
2. 重新編譯 JAR + DLL
3. 使用**微軟拼音**測試以下輸入：
   - `nihaoma`
   - `woxihuanni`
   - `zhonghuarenmin`
   - 較長的句子
4. 觀察是否還有吞字母或突然 Commit 的情況

---

## 預期效果

- Commit 只會在真正完成輸入時觸發
- 大幅減少吞字母現象
- 輸入過程更穩定

---

## 后续更新

本文档记录的修复建议可能已整合到后续版本中。当前代码中的相关逻辑可能有所不同，请参考：

- `WindowsIMEBridgeNative.java` - `handleCommit()` 方法
- `ChineseIMEInitializer.java` - `registerCallbacks()` 方法
- `PlatformIMEManager.java` - `tick()` 方法