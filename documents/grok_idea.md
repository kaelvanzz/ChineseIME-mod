**✅ 已整理完成！**

以下是完整 Markdown 內容，請直接複製全部，存成以下檔案：

**檔案名稱建議**：`ChineseIME-Refactor-Report-20260505.md`

```markdown
# ChineseIME-mod 修復報告 - 2026/05/05

**作者**: Grok  
**本次重點**: ImeStatusIndicator.java 大寫鎖定與輸入法切換偵測失效（顯示 "?"）

---

## 問題診斷

**目前現象**：
- Shift 中英文切換（黃色方塊）正常運作
- Caps Lock 與輸入法切換（中文/英文模式）失效，顯示 "?"
- 切換到中文輸入法時 Indicator 顯示問號

**根本原因**：
- `InputMode` 沒有正確從 C++ 傳到 Java（經常落到 `default`）
- `update()` 方法中缺少 null 防護與強制刷新邏輯
- `visible` 與狀態同步機制不夠強健
- C++ 側雖然有輪詢，但 Java 側接收端處理不完整

---

## 1. ImeStatusIndicator.java 修復版（推薦直接替換）

```java
package com.example.chineseime.hud;

import com.example.chineseime.engine.InputMode;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.font.TextRenderer;
import net.minecraft.client.gui.DrawContext;

public class ImeStatusIndicator {
    private boolean chineseMode = true;
    private InputMode inputMode = InputMode.PINYIN;
    private boolean capsLockOn = false;
    private boolean inShiftMode = false;
    private boolean visible = false;

    private static final int BG_NORMAL = 0x99000000;
    private static final int BG_CAPS = 0x994466AA;
    private static final int BORDER_COLOR = 0xFF4466AA;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int SHIFT_INDICATOR_COLOR = 0xFFFFAA00;

    private static final int SIZE = 22;
    private static final int SHIFT_SIZE = 8;

    /**
     * 核心更新方法 - 已加強防護與 log
     */
    public void update(boolean chineseMode, InputMode inputMode, 
                       boolean capsLockOn, boolean inShiftMode) {
        
        boolean changed = (this.chineseMode != chineseMode) ||
                         (this.inputMode != inputMode) ||
                         (this.capsLockOn != capsLockOn) ||
                         (this.inShiftMode != inShiftMode);

        this.chineseMode = chineseMode;
        this.inputMode = (inputMode != null) ? inputMode : InputMode.OTHER;
        this.capsLockOn = capsLockOn;
        this.inShiftMode = inShiftMode;

        // 強制可見（除非明確要隱藏）
        this.visible = true;

        if (changed) {
            ChineseIMEInitializer.LOGGER.info(
                "[ImeStatusIndicator] UPDATE → Mode: {} | Chinese: {} | Caps: {} | Shift: {}",
                getDisplayText(), chineseMode, capsLockOn, inShiftMode);
        }
    }

    public void setInChatScreen(boolean inChat) {
        if (inChat) this.visible = true;
    }

    public void hide() {
        this.visible = false;
    }

    private String getDisplayText() {
        return switch (inputMode) {
            case LATIN -> "En";
            case PINYIN -> "拼";
            case ZHUYIN -> "注";
            case CANGJIE -> "倉";
            case SUCHENG -> "速";
            case WUBI -> "五";
            case OTHER -> "??";
            default -> "??";
        };
    }

    public void render(DrawContext ctx) {
        if (!visible) return;

        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;

        int x = 4;
        int y = mc.getWindow().getHeight() - SIZE - 48;   // 可依需求調整

        int bg = (capsLockOn && !inShiftMode) ? BG_CAPS : BG_NORMAL;

        ctx.fill(x, y, x + SIZE, y + SIZE, bg);
        ctx.drawBorder(x, y, SIZE, SIZE, BORDER_COLOR);

        String text = getDisplayText();
        int textW = font.getWidth(text);
        ctx.drawText(font, text, x + (SIZE - textW) / 2, y + (SIZE - 10) / 2, TEXT_COLOR, false);

        // Shift 模式黃色小方塊
        if (inShiftMode) {
            int sx = x + SIZE - SHIFT_SIZE - 1;
            int sy = y + 1;
            ctx.fill(sx, sy, sx + SHIFT_SIZE, sy + SHIFT_SIZE, SHIFT_INDICATOR_COLOR);
        }
    }

    // Getter
    public boolean isVisible() { return visible; }
    public boolean isCapsLockOn() { return capsLockOn; }
    public boolean isChineseMode() { return chineseMode; }
    public InputMode getInputMode() { return inputMode; }
}
```

---

## 2. 呼叫端修正建議（ChineseIMEInitializer.java 或 polling 類別）

請找到呼叫 `indicator.update(...)` 的位置，改成以下寫法：

```java
// 推薦呼叫方式
imeStatusIndicator.update(
    NativeImeBridge.isChineseMode(),           // 或 getTsfChineseMode()
    NativeImeBridge.getCurrentInputMode(),     // 確保這個方法回傳正確 enum
    NativeImeBridge.getCapsLockState(),
    NativeImeBridge.isShiftMode()
);
```

**重要**：
- 確保 `NativeImeBridge.getCurrentInputMode()` 有正確實作（不要回傳 null）
- 建議在每個 tick 或狀態變化時都呼叫一次

---

## 3. 後續除錯步驟

1. 替換上面的 `ImeStatusIndicator.java`
2. 確認呼叫端使用正確的 `NativeImeBridge` 方法
3. 進入遊戲後按 `Ctrl + Shift + T` 開啟測試模式
4. 觀察控制台 log（搜尋 `[ImeStatusIndicator] UPDATE`）
5. 測試 Caps Lock、Win+Space（或 Shift）切換中文、切換倉頡/五筆等