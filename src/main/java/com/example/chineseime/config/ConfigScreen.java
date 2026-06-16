package com.example.chineseime.config;

import com.example.chineseime.engine.InputMode;
import com.example.chineseime.engine.ScriptType;
import com.example.chineseime.platform.PlatformIMEManager;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.text.Text;
import org.lwjgl.glfw.GLFW;

import java.util.ArrayList;
import java.util.List;

public class ConfigScreen extends Screen {
    private final Screen parent;
    private final ModConfig config;
    private final boolean isWindows;

    private static final int BORDER_COLOR = 0xFF4466AA;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int TEXT_DISABLED = 0x88666666;
    private static final int TEXT_HOVER = 0xFFAAAAAA;
    private static final int HOVER_BG = 0x88444444;
    private static final int ITEM_BG = 0xAA000000;
    private static final int SECTION_BG = 0x66000000;

    private int hoveredIndex = -1;
    private int mouseX = 0, mouseY = 0;
    private boolean waitingForKeybind = false;
    private int panelX, panelY, panelW, panelH;

    private static final int ITEM_H = 30;
    private static final int SECTION_H = 24;
    private static final int PADDING = 20;
    private static final int GAP = 4;
    private static final int SLIDER_W = 180;
    private static final int SLIDER_H = 14;
    private static final int CLEAR_BTN_W = 20;

    private List<Item> items = new ArrayList<>();

    private enum ItemType {
        SECTION_TITLE,
        UI_SCALE,
        CANDIDATES,
        INPUT_MODE,
        SCRIPT,
        SHORTCUT_TOGGLE_IME,
        SHORTCUT_TOGGLE_MODE,
        RESET_DEFAULTS
    }

    private static class Item {
        ItemType type;
        int y;
        Item(ItemType type, int y) { this.type = type; this.y = y; }
    }

    private List<Integer> currentCapturedKeys = new ArrayList<>();
    private long captureStartTime = 0;
    private static final long CAPTURE_TIMEOUT_MS = 1000;

    public ConfigScreen(Screen parent, ModConfig config) {
        super(Text.literal("ChineseIME 设置"));
        this.parent = parent;
        this.config = config;
        this.isWindows = PlatformIMEManager.getPlatform() == PlatformIMEManager.OS.WINDOWS;
    }

    @Override
    protected void init() {
        panelW = Math.min(width - 80, 500);
        panelH = calculatePanelHeight();
        panelX = (width - panelW) / 2;
        panelY = (height - panelH) / 2;

        items.clear();
        int y = panelY + PADDING;

        items.add(new Item(ItemType.SECTION_TITLE, y));
        y += SECTION_H;

        items.add(new Item(ItemType.UI_SCALE, y));
        y += ITEM_H + GAP;

        items.add(new Item(ItemType.CANDIDATES, y));
        y += ITEM_H + GAP * 2;

        items.add(new Item(ItemType.SECTION_TITLE, y));
        y += SECTION_H;

        items.add(new Item(ItemType.INPUT_MODE, y));
        y += ITEM_H + GAP;

        items.add(new Item(ItemType.SCRIPT, y));
        y += ITEM_H + GAP;

        items.add(new Item(ItemType.SHORTCUT_TOGGLE_IME, y));
        y += ITEM_H + GAP;

        items.add(new Item(ItemType.SHORTCUT_TOGGLE_MODE, y));
        y += ITEM_H + GAP * 2;

        items.add(new Item(ItemType.RESET_DEFAULTS, y));
    }

    private int calculatePanelHeight() {
        return PADDING * 2 + SECTION_H + (ITEM_H + GAP) * 2 + GAP * 2 + SECTION_H + (ITEM_H + GAP) * 4 + GAP * 2;
    }

    @Override
    public void render(DrawContext ctx, int mx, int my, float delta) {
        renderBackground(ctx, mx, my, delta);
        this.mouseX = mx;
        this.mouseY = my;
        hoveredIndex = getHoveredItem(mx, my);

        ctx.fill(panelX, panelY, panelX + panelW, panelY + panelH, ITEM_BG);
        ctx.drawBorder(panelX, panelY, panelW, panelH, BORDER_COLOR);

        Text title = Text.literal("ChineseIME 设置");
        int titleW = this.textRenderer.getWidth(title);
        ctx.drawText(this.textRenderer, title, panelX + (panelW - titleW) / 2, panelY + 10, TEXT_COLOR, false);

        for (int i = 0; i < items.size(); i++) {
            Item item = items.get(i);
            boolean hovered = hoveredIndex == i;

            switch (item.type) {
                case SECTION_TITLE -> renderSectionTitle(ctx, item.y, hovered, i == 1 ? "显示" : "键盘");
                case UI_SCALE -> renderUiScale(ctx, item.y, hovered);
                case CANDIDATES -> renderCandidates(ctx, item.y, hovered);
                case INPUT_MODE -> renderInputMode(ctx, item.y, hovered);
                case SCRIPT -> renderScript(ctx, item.y, hovered);
                case SHORTCUT_TOGGLE_IME -> renderShortcutToggleIme(ctx, item.y, hovered);
                case SHORTCUT_TOGGLE_MODE -> renderShortcutToggleMode(ctx, item.y, hovered);
                case RESET_DEFAULTS -> renderResetDefaults(ctx, item.y, hovered);
            }
        }

        ctx.drawText(this.textRenderer, Text.literal("ESC 关闭"), panelX + PADDING, panelY + panelH - 25, TEXT_DISABLED, false);
    }

    private void renderSectionTitle(DrawContext ctx, int y, boolean hovered, String text) {
        int lineY = y + SECTION_H / 2;
        int textX = panelX + panelW / 2 - this.textRenderer.getWidth(text) / 2;
        int textY = y + (SECTION_H - 8) / 2;

        ctx.drawHorizontalLine(panelX + PADDING, panelX + panelW - PADDING, lineY, SECTION_BG);
        ctx.drawText(this.textRenderer, Text.literal(text), textX, textY, TEXT_COLOR, false);
    }

    private void renderUiScale(DrawContext ctx, int y, boolean hovered) {
        ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, SECTION_BG);
        if (hovered) ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, HOVER_BG);

        Text label = Text.literal("界面缩放");
        ctx.drawText(this.textRenderer, label, panelX + PADDING + 10, y + 8, TEXT_COLOR, false);

        Text value = Text.literal(config.getHudScale() + "%");
        ctx.drawText(this.textRenderer, value, panelX + panelW - PADDING - 10 - this.textRenderer.getWidth(value), y + 8, TEXT_COLOR, false);

        int sliderX = panelX + panelW - PADDING - SLIDER_W - 60;
        int sliderY = y + (ITEM_H - SLIDER_H) / 2;
        renderSlider(ctx, sliderX, sliderY, SLIDER_W, config.getHudScale(), 50, 150);
    }

    private void renderCandidates(DrawContext ctx, int y, boolean hovered) {
        ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, SECTION_BG);
        if (hovered && !isWindows) ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, HOVER_BG);

        int textColor = isWindows ? TEXT_DISABLED : TEXT_COLOR;
        Text label = Text.literal("候选词数量");
        ctx.drawText(this.textRenderer, label, panelX + PADDING + 10, y + 8, textColor, false);

        Text value = Text.literal(String.valueOf(config.getMaxCandidates()));
        ctx.drawText(this.textRenderer, value, panelX + panelW - PADDING - 10 - this.textRenderer.getWidth(value), y + 8, textColor, false);

        int sliderX = panelX + panelW - PADDING - SLIDER_W - 60;
        int sliderY = y + (ITEM_H - SLIDER_H) / 2;
        int sliderColor = isWindows ? 0x66666666 : BORDER_COLOR;
        if (isWindows) {
            ctx.drawBorder(sliderX, sliderY, SLIDER_W, SLIDER_H, 0x66666666);
            ctx.fill(sliderX + 2, sliderY + 2, sliderX + SLIDER_W - 2, sliderY + SLIDER_H - 2, 0x66000000);
        } else {
            renderSlider(ctx, sliderX, sliderY, SLIDER_W, config.getMaxCandidates(), 5, 9);
        }
    }

    private void renderInputMode(DrawContext ctx, int y, boolean hovered) {
        ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, SECTION_BG);
        if (hovered) ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, HOVER_BG);

        String mode = config.getInputMode() == InputMode.LATIN ? "英语" :
                config.getInputMode() == InputMode.RIME ? "中州韵" : "系统";
        Text label = Text.literal("输入模式");
        ctx.drawText(this.textRenderer, label, panelX + PADDING + 10, y + 8, isWindows ? TEXT_DISABLED : TEXT_COLOR, false);

        Text value = Text.literal(mode);
        ctx.drawText(this.textRenderer, value, panelX + panelW - PADDING - 10 - this.textRenderer.getWidth(value), y + 8, isWindows ? TEXT_DISABLED : TEXT_COLOR, false);
    }

    private void renderScript(DrawContext ctx, int y, boolean hovered) {
        ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, SECTION_BG);
        if (hovered) ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, HOVER_BG);

        String script = config.getScriptType() == ScriptType.SIMPLIFIED ? "简体" : "繁体";
        Text label = Text.literal("字体样式");
        ctx.drawText(this.textRenderer, label, panelX + PADDING + 10, y + 8, isWindows ? TEXT_DISABLED : TEXT_COLOR, false);

        Text value = Text.literal(script);
        ctx.drawText(this.textRenderer, value, panelX + panelW - PADDING - 10 - this.textRenderer.getWidth(value), y + 8, isWindows ? TEXT_DISABLED : TEXT_COLOR, false);
    }

    private void renderShortcutToggleIme(DrawContext ctx, int y, boolean hovered) {
        ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, SECTION_BG);
        if (hovered && !waitingForKeybind) ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, HOVER_BG);

        String shortcut;
        if (waitingForKeybind) {
            if (currentCapturedKeys.isEmpty()) {
                shortcut = "按下按键...";
            } else {
                shortcut = getKeybindDisplay(currentCapturedKeys) + "...";
            }
        } else {
            shortcut = getKeybindDisplay(config.getToggleImeKeys());
        }
        Text label = Text.literal("切换输入法");
        ctx.drawText(this.textRenderer, label, panelX + PADDING + 10, y + 8, waitingForKeybind ? TEXT_HOVER : TEXT_COLOR, false);

        Text value = Text.literal(shortcut);
        int valueX = panelX + panelW - PADDING - CLEAR_BTN_W - 10 - this.textRenderer.getWidth(value);
        ctx.drawText(this.textRenderer, value, valueX, y + 8, TEXT_COLOR, false);

        int clearBtnX = panelX + panelW - PADDING - CLEAR_BTN_W;
        boolean clearHovered = mouseX >= clearBtnX && mouseX <= clearBtnX + CLEAR_BTN_W && mouseY >= y && mouseY <= y + ITEM_H;
        ctx.drawText(this.textRenderer, Text.literal("x"), clearBtnX + 6, y + 10, clearHovered ? 0xFFFF5555 : 0xFFCC4444, false);
    }

    private void renderShortcutToggleMode(DrawContext ctx, int y, boolean hovered) {
        ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, SECTION_BG);
        if (hovered && !waitingForKeybind) ctx.fill(panelX + PADDING, y, panelX + panelW - PADDING, y + ITEM_H, HOVER_BG);

        String shortcut;
        if (waitingForKeybind) {
            if (currentCapturedKeys.isEmpty()) {
                shortcut = "按下按键...";
            } else {
                shortcut = getKeybindDisplay(currentCapturedKeys) + "...";
            }
        } else {
            shortcut = getKeybindDisplay(config.getToggleChineseModeKeys());
        }
        Text label = Text.literal("中英文切换");
        ctx.drawText(this.textRenderer, label, panelX + PADDING + 10, y + 8, isWindows ? TEXT_DISABLED : TEXT_COLOR, false);

        if (isWindows) {
            Text disabled = Text.literal("(Windows不可用)");
            ctx.drawText(this.textRenderer, disabled, panelX + panelW - PADDING - 10 - this.textRenderer.getWidth(disabled), y + 8, TEXT_DISABLED, false);
        } else {
            Text value = Text.literal(shortcut);
            int valueX = panelX + panelW - PADDING - CLEAR_BTN_W - 10 - this.textRenderer.getWidth(value);
            ctx.drawText(this.textRenderer, value, valueX, y + 8, TEXT_COLOR, false);

            int clearBtnX = panelX + panelW - PADDING - CLEAR_BTN_W;
            boolean clearHovered = mouseX >= clearBtnX && mouseX <= clearBtnX + CLEAR_BTN_W && mouseY >= y && mouseY <= y + ITEM_H;
            ctx.drawText(this.textRenderer, Text.literal("x"), clearBtnX + 6, y + 10, clearHovered ? 0xFFFF5555 : 0xFFCC4444, false);
        }
    }

    private void renderResetDefaults(DrawContext ctx, int y, boolean hovered) {
        int btnW = 120;
        int btnH = 24;
        int btnX = panelX + (panelW - btnW) / 2;
        int btnY = y + (ITEM_H - btnH) / 2;

        ctx.fill(btnX, btnY, btnX + btnW, btnY + btnH, HOVER_BG);
        if (hovered) ctx.fill(btnX, btnY, btnX + btnW, btnY + btnH, 0x88555555);
        ctx.drawBorder(btnX, btnY, btnW, btnH, BORDER_COLOR);

        Text text = Text.literal("恢复默认设置");
        int textW = this.textRenderer.getWidth(text);
        ctx.drawText(this.textRenderer, text, btnX + (btnW - textW) / 2, btnY + 7, TEXT_COLOR, false);
    }

    private void renderSlider(DrawContext ctx, int x, int y, int w, int value, int min, int max) {
        float ratio = (float)(value - min) / (max - min);
        int fillW = (int)(ratio * (w - 4));
        ctx.drawBorder(x, y, w, SLIDER_H, BORDER_COLOR);
        ctx.fill(x + 2, y + 2, x + 2 + fillW, y + SLIDER_H - 2, BORDER_COLOR);
        ctx.fill(x + 2 + fillW, y + 2, x + w - 2, y + SLIDER_H - 2, 0x88000000);
    }

    private String getKeybindDisplay(int keyCode) {
        if (keyCode == -1) return "无";
        if (keyCode == GLFW.GLFW_KEY_LEFT_SHIFT || keyCode == GLFW.GLFW_KEY_RIGHT_SHIFT) return "Shift";
        if (keyCode == GLFW.GLFW_KEY_LEFT_CONTROL || keyCode == GLFW.GLFW_KEY_RIGHT_CONTROL) return "Ctrl";
        if (keyCode == GLFW.GLFW_KEY_LEFT_ALT || keyCode == GLFW.GLFW_KEY_RIGHT_ALT) return "Alt";
        if (keyCode == GLFW.GLFW_KEY_LEFT_SUPER || keyCode == GLFW.GLFW_KEY_RIGHT_SUPER) return "Win";
        if (keyCode == GLFW.GLFW_KEY_TAB) return "Tab";
        if (keyCode == GLFW.GLFW_KEY_ENTER) return "Enter";
        if (keyCode == GLFW.GLFW_KEY_BACKSPACE) return "Backspace";
        if (keyCode == GLFW.GLFW_KEY_SPACE) return "Space";
        if (keyCode >= GLFW.GLFW_KEY_F1 && keyCode <= GLFW.GLFW_KEY_F12) return "F" + (keyCode - GLFW.GLFW_KEY_F1 + 1);
        if (keyCode == GLFW.GLFW_KEY_ESCAPE) return "Esc";
        if (keyCode == GLFW.GLFW_KEY_CAPS_LOCK) return "Caps Lock";
        if (keyCode == GLFW.GLFW_KEY_SCROLL_LOCK) return "Scroll Lock";
        if (keyCode == GLFW.GLFW_KEY_NUM_LOCK) return "Num Lock";
        if (keyCode == GLFW.GLFW_KEY_PRINT_SCREEN) return "Print Screen";
        if (keyCode == GLFW.GLFW_KEY_PAUSE) return "Pause";
        if (keyCode == GLFW.GLFW_KEY_INSERT) return "Insert";
        if (keyCode == GLFW.GLFW_KEY_DELETE) return "Delete";
        if (keyCode == GLFW.GLFW_KEY_HOME) return "Home";
        if (keyCode == GLFW.GLFW_KEY_END) return "End";
        if (keyCode == GLFW.GLFW_KEY_PAGE_UP) return "PgUp";
        if (keyCode == GLFW.GLFW_KEY_PAGE_DOWN) return "PgDn";
        if (keyCode == GLFW.GLFW_KEY_UP) return "Up";
        if (keyCode == GLFW.GLFW_KEY_DOWN) return "Down";
        if (keyCode == GLFW.GLFW_KEY_LEFT) return "Left";
        if (keyCode == GLFW.GLFW_KEY_RIGHT) return "Right";
        if (keyCode >= GLFW.GLFW_KEY_KP_0 && keyCode <= GLFW.GLFW_KEY_KP_9) return "Numpad " + (keyCode - GLFW.GLFW_KEY_KP_0);
        if (keyCode == GLFW.GLFW_KEY_KP_DECIMAL) return "Numpad .";
        if (keyCode == GLFW.GLFW_KEY_KP_DIVIDE) return "Numpad /";
        if (keyCode == GLFW.GLFW_KEY_KP_MULTIPLY) return "Numpad *";
        if (keyCode == GLFW.GLFW_KEY_KP_SUBTRACT) return "Numpad -";
        if (keyCode == GLFW.GLFW_KEY_KP_ADD) return "Numpad +";
        if (keyCode == GLFW.GLFW_KEY_KP_ENTER) return "Numpad Enter";
        if (keyCode == GLFW.GLFW_KEY_MENU) return "Menu";
        String name = GLFW.glfwGetKeyName(keyCode, 0);
        return name != null ? name : "Key" + keyCode;
    }

    private boolean isModifierKey(int keyCode) {
        return keyCode == GLFW.GLFW_KEY_LEFT_SHIFT || keyCode == GLFW.GLFW_KEY_RIGHT_SHIFT ||
               keyCode == GLFW.GLFW_KEY_LEFT_CONTROL || keyCode == GLFW.GLFW_KEY_RIGHT_CONTROL ||
               keyCode == GLFW.GLFW_KEY_LEFT_ALT || keyCode == GLFW.GLFW_KEY_RIGHT_ALT ||
               keyCode == GLFW.GLFW_KEY_LEFT_SUPER || keyCode == GLFW.GLFW_KEY_RIGHT_SUPER ||
               keyCode == GLFW.GLFW_KEY_CAPS_LOCK || keyCode == GLFW.GLFW_KEY_SCROLL_LOCK ||
               keyCode == GLFW.GLFW_KEY_NUM_LOCK;
    }

    private boolean isSystemKey(int keyCode) {
        return keyCode == GLFW.GLFW_KEY_LEFT_SUPER || keyCode == GLFW.GLFW_KEY_RIGHT_SUPER ||
               keyCode == GLFW.GLFW_KEY_MENU;
    }

    private int getHoveredItem(int mx, int my) {
        for (int i = 0; i < items.size(); i++) {
            Item item = items.get(i);
            int h = item.type == ItemType.SECTION_TITLE ? SECTION_H : ITEM_H;
            if (mx >= panelX + PADDING && mx <= panelX + panelW - PADDING &&
                    my >= item.y && my <= item.y + h) {
                return i;
            }
        }
        return -1;
    }

    private int getSliderValue(int mx, int sliderX) {
        int relX = mx - sliderX;
        if (relX < 0) relX = 0;
        if (relX > SLIDER_W) relX = SLIDER_W;
        return relX;
    }

    private int getHoveredClearBtn(int mx, int my, int itemIndex) {
        if (itemIndex < 0 || itemIndex >= items.size()) return -1;
        Item item = items.get(itemIndex);
        if (item.type != ItemType.SHORTCUT_TOGGLE_IME && item.type != ItemType.SHORTCUT_TOGGLE_MODE) {
            return -1;
        }
        if (item.type == ItemType.SHORTCUT_TOGGLE_MODE && isWindows) {
            return -1;
        }
        int clearBtnX = panelX + panelW - PADDING - CLEAR_BTN_W;
        if (mx >= clearBtnX && mx <= clearBtnX + CLEAR_BTN_W && my >= item.y && my <= item.y + ITEM_H) {
            return item.type == ItemType.SHORTCUT_TOGGLE_IME ? 0 : 1;
        }
        return -1;
    }

    @Override
    public boolean mouseClicked(double mouseX, double mouseY, int button) {
        int mx = (int) mouseX;
        int my = (int) mouseY;

        if (waitingForKeybind) {
            waitingForKeybind = false;
            return true;
        }

        int clearBtn = getHoveredClearBtn(mx, my, hoveredIndex);
        if (clearBtn == 0) {
            config.setToggleImeKeys(new ArrayList<>());
            return true;
        }
        if (clearBtn == 1) {
            config.setToggleChineseModeKeys(new ArrayList<>());
            return true;
        }

        int idx = getHoveredItem(mx, my);
        if (idx == -1) return false;

        Item item = items.get(idx);
        switch (item.type) {
            case UI_SCALE -> {
                int sliderX = panelX + panelW - PADDING - SLIDER_W - 60;
                int sliderY = item.y + (ITEM_H - SLIDER_H) / 2;
                if (mx >= sliderX && mx <= sliderX + SLIDER_W && my >= sliderY && my <= sliderY + SLIDER_H) {
                    float ratio = getSliderValue(mx, sliderX) / (float) SLIDER_W;
                    int value = Math.round(50 + ratio * 100);
                    value = Math.round(value / 10.0f) * 10;
                    value = Math.max(50, Math.min(150, value));
                    config.setHudScale(value);
                }
                break;
            }
            case CANDIDATES -> {
                if (isWindows) break;
                int sliderX = panelX + panelW - PADDING - SLIDER_W - 60;
                int sliderY = item.y + (ITEM_H - SLIDER_H) / 2;
                if (mx >= sliderX && mx <= sliderX + SLIDER_W && my >= sliderY && my <= sliderY + SLIDER_H) {
                    float ratio = getSliderValue(mx, sliderX) / (float) SLIDER_W;
                    int value = Math.round(5 + ratio * 4);
                    config.setMaxCandidates(value);
                }
                break;
            }
            case INPUT_MODE -> {
                if (!isWindows) {
                    InputMode[] modes = {InputMode.LATIN, InputMode.RIME};
                    config.setInputMode(config.getInputMode() == InputMode.LATIN ? modes[1] : modes[0]);
                }
                break;
            }
            case SCRIPT -> {
                if (!isWindows) config.toggleScriptType();
                break;
            }
            case SHORTCUT_TOGGLE_IME -> {
                int sliderX = panelX + panelW - PADDING - SLIDER_W - 60;
                int sliderY = item.y + (ITEM_H - SLIDER_H) / 2;
                if (mx >= sliderX && mx <= sliderX + SLIDER_W && my >= sliderY && my <= sliderY + SLIDER_H) {
                    return false;
                }
                waitingForKeybind = true;
                break;
            }
            case SHORTCUT_TOGGLE_MODE -> {
                if (isWindows) return false;
                int sliderX = panelX + panelW - PADDING - SLIDER_W - 60;
                int sliderY = item.y + (ITEM_H - SLIDER_H) / 2;
                if (mx >= sliderX && mx <= sliderX + SLIDER_W && my >= sliderY && my <= sliderY + SLIDER_H) {
                    return false;
                }
                waitingForKeybind = true;
                break;
            }
            case RESET_DEFAULTS -> {
                int btnW = 120;
                int btnH = 24;
                int btnX = panelX + (panelW - btnW) / 2;
                int btnY = item.y + (ITEM_H - btnH) / 2;
                if (mx >= btnX && mx <= btnX + btnW && my >= btnY && my <= btnY + btnH) {
                    config.resetToDefaults();
                }
            }
        }
        return true;
    }

    @Override
    public boolean mouseDragged(double mouseX, double mouseY, int button, double deltaX, double deltaY) {
        int mx = (int) mouseX;
        int my = (int) mouseY;

        int idx = getHoveredItem(mx, my);
        if (idx == -1 || button != 0) return false;

        Item item = items.get(idx);
        switch (item.type) {
            case UI_SCALE -> {
                int sliderX = panelX + panelW - PADDING - SLIDER_W - 60;
                int sliderY = item.y + (ITEM_H - SLIDER_H) / 2;
                if (mx >= sliderX && mx <= sliderX + SLIDER_W && my >= sliderY && my <= sliderY + SLIDER_H) {
                    float ratio = getSliderValue(mx, sliderX) / (float) SLIDER_W;
                    int value = Math.round(50 + ratio * 100);
                    value = Math.round(value / 10.0f) * 10;
                    value = Math.max(50, Math.min(150, value));
                    config.setHudScale(value);
                    return true;
                }
            }
            case CANDIDATES -> {
                if (isWindows) return false;
                int sliderX = panelX + panelW - PADDING - SLIDER_W - 60;
                int sliderY = item.y + (ITEM_H - SLIDER_H) / 2;
                if (mx >= sliderX && mx <= sliderX + SLIDER_W && my >= sliderY && my <= sliderY + SLIDER_H) {
                    float ratio = getSliderValue(mx, sliderX) / (float) SLIDER_W;
                    int value = Math.round(5 + ratio * 4);
                    config.setMaxCandidates(value);
                    return true;
                }
            }
        }
        return false;
    }

    @Override
    public boolean keyPressed(int keyCode, int scanCode, int modifiers) {
        if (waitingForKeybind) {
            if (keyCode == GLFW.GLFW_KEY_ESCAPE) {
                waitingForKeybind = false;
                currentCapturedKeys.clear();
                return true;
            }
            if (isSystemKey(keyCode)) {
                return true;
            }
            if (isModifierKey(keyCode)) {
                if (!currentCapturedKeys.contains(keyCode)) {
                    currentCapturedKeys.add(keyCode);
                    captureStartTime = System.currentTimeMillis();
                }
                return true;
            }
            if (!currentCapturedKeys.contains(keyCode)) {
                currentCapturedKeys.add(keyCode);
            }
            int idx = getHoveredItem(mouseX, mouseY);
            if (idx >= 0) {
                Item item = items.get(idx);
                if (item.type == ItemType.SHORTCUT_TOGGLE_IME) {
                    config.setToggleImeKeys(currentCapturedKeys);
                } else if (item.type == ItemType.SHORTCUT_TOGGLE_MODE) {
                    config.setToggleChineseModeKeys(currentCapturedKeys);
                }
            }
            waitingForKeybind = false;
            currentCapturedKeys.clear();
            return true;
        }
        if (keyCode == GLFW.GLFW_KEY_ESCAPE) {
            close();
            return true;
        }
        return super.keyPressed(keyCode, scanCode, modifiers);
    }

    private String getKeybindDisplay(List<Integer> keyCodes) {
        if (keyCodes == null || keyCodes.isEmpty()) return "无";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < keyCodes.size(); i++) {
            if (i > 0) sb.append("+");
            sb.append(getKeybindDisplay(keyCodes.get(i)));
        }
        return sb.toString();
    }

    @Override
    public void close() {
        client.setScreen(parent);
    }
}