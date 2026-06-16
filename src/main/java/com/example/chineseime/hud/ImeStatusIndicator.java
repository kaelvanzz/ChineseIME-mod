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
    private static final int SHIFT_INDICATOR = 0xFFFFAA00;

    private static final int SIZE_1080P = 40;
    private static final int SHIFT_INDICATOR_SIZE_1080P = 8;

    public ImeStatusIndicator() {
        this.inputMode = InputMode.PINYIN;
        this.capsLockOn = false;
        this.inShiftMode = false;
        this.visible = false;
    }

    public void update(boolean chineseMode, InputMode inputMode, boolean capsLockOn, boolean inShiftMode) {
        InputMode safeInputMode = (inputMode != null) ? inputMode : InputMode.OTHER;

        boolean changed = this.chineseMode != chineseMode
            || this.inputMode != safeInputMode
            || this.capsLockOn != capsLockOn
            || this.inShiftMode != inShiftMode;

        this.chineseMode = chineseMode;
        this.inputMode = safeInputMode;
        this.capsLockOn = capsLockOn;
        this.inShiftMode = inShiftMode;
        this.visible = true;

        if (changed) {
            com.example.chineseime.ChineseIMEInitializer.LOGGER.info(
                "[ChineseIME] Indicator: IME={}, CapsLock={}, ShiftMode={}, ChineseMode={}",
                getDisplayText(), capsLockOn, inShiftMode, chineseMode);
        }
    }

    public void setInChatScreen(boolean inChat) {
        if (inChat) this.visible = true;
    }

    public void hide() {
        this.visible = false;
    }

    public void show() {
        this.visible = true;
    }

    public void forceUpdate(boolean chineseMode, InputMode inputMode, boolean capsLockOn, boolean inShiftMode) {
        InputMode safeInputMode = (inputMode != null) ? inputMode : InputMode.OTHER;
        this.chineseMode = chineseMode;
        this.inputMode = safeInputMode;
        this.capsLockOn = capsLockOn;
        this.inShiftMode = inShiftMode;
        this.visible = true;
    }

    private String getDisplayText() {
        return switch (this.inputMode) {
            case LATIN -> "En";
            case PINYIN -> "拼";
            case ZHUYIN -> "注";
            case CANGJIE -> "倉";
            case SUCHENG -> "速";
            case WUBI -> "五";
            case OTHER -> "中";
            case YUEPIN -> "粤";
            default -> "?";
        };
    }

    public void render(DrawContext ctx) {
        if (!this.visible) return;

        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null) return;

        TextRenderer font = mc.textRenderer;
        int scaledW = ctx.getScaledWindowWidth();
        int scaledH = ctx.getScaledWindowHeight();
        int physicalW = mc.getWindow().getWidth();
        float scale = physicalW > 0 ? (float) physicalW / (float) scaledW : 2.0f;

        int size = (int)(SIZE_1080P / scale);
        int shiftSize = (int)(SHIFT_INDICATOR_SIZE_1080P / scale);
        int margin = (int)(8 / scale);
        int chatInputTop = scaledH - 22 - 14;
        int x = margin;
        int y = chatInputTop - 2 - size;

        int bgColor = this.capsLockOn ? BG_CAPS : BG_NORMAL;

        ctx.fill(x, y, x + size, y + size, bgColor);
        ctx.drawBorder(x, y, size, size, BORDER_COLOR);

        String text = this.getDisplayText();
        int textWidth = font.getWidth(text);
        float textX = x + (size - textWidth) / 2f;
        float textY = y + (size - font.fontHeight) / 2f;
        ctx.getMatrices().push();
        ctx.getMatrices().translate(textX, textY, 0);
        ctx.drawText(font, text, 0, 0, TEXT_COLOR, false);
        ctx.getMatrices().pop();

        if (this.inShiftMode) {
            int shiftX = x + size - shiftSize - 1;
            int shiftY = y + 1;
            ctx.fill(shiftX, shiftY, shiftX + shiftSize, shiftY + shiftSize, SHIFT_INDICATOR);
        }
    }

    public boolean isVisible() { return this.visible; }
    public boolean isChineseMode() { return this.chineseMode; }
    public InputMode getInputMode() { return this.inputMode; }
    public boolean isInShiftMode() { return this.inShiftMode; }
    public boolean isCapsLockOn() { return this.capsLockOn; }

    public static int getHeight(float scale) {
        return (int)(SIZE_1080P / scale);
    }
}