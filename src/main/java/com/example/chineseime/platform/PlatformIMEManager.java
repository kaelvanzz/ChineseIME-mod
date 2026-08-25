package com.example.chineseime.platform;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.config.ModConfig;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.engine.PinyinDictionary;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.hud.VerticalCandidateHud;
import com.example.chineseime.platform.win32.WindowsIMEBridgeNative;
import java.util.List;

public class PlatformIMEManager {
    private final ModConfig config;
    private final CandidateHud hud;
    private final VerticalCandidateHud verticalHud;
    private WindowsIMEBridgeNative windowsBridge;
    private boolean syncEnabled = false;

    public PlatformIMEManager(ModConfig config, CandidateHud hud, ImeStatusIndicator indicator) {
        this.config = config;
        this.hud = hud;
        this.verticalHud = new VerticalCandidateHud();

        if (getPlatform() == OS.WINDOWS) {
            initWindowsIME(indicator);
        }
    }

private void initWindowsIME(ImeStatusIndicator indicator) {
    try {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Initializing Windows IME Bridge");
        windowsBridge = new WindowsIMEBridgeNative(hud, verticalHud);
        windowsBridge.setStatusIndicator(indicator);
        if (windowsBridge.initialize()) {
            syncEnabled = true;
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Windows IME Bridge initialized, syncEnabled=true");
        } else {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Windows IME Bridge initialize() returned false, syncEnabled=false");
        }
    } catch (Throwable e) {
        ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Failed to initialize Windows IME Bridge: {}", e.getMessage());
        e.printStackTrace();
        syncEnabled = false;
    }
}

public void tick() {
        if (windowsBridge != null && syncEnabled) {
            windowsBridge.update();
        }
    }

    public void renderHud(Object drawContext) {
        if (drawContext instanceof net.minecraft.client.gui.DrawContext ctx) {
            InputMode mode = getDetectedInputMode();
            if (mode == InputMode.CANGJIE || mode == InputMode.SUCHENG || mode == InputMode.ZHUYIN) {
                verticalHud.render(ctx);
            } else {
                hud.render(ctx);
            }
        }
    }

    public boolean isVerticalLayout() {
        InputMode mode = getDetectedInputMode();
        return mode == InputMode.CANGJIE || mode == InputMode.SUCHENG || mode == InputMode.ZHUYIN;
    }

    public VerticalCandidateHud getVerticalHud() {
        return verticalHud;
    }

    @Deprecated
    public boolean hasLayoutChanged() {
        if (windowsBridge != null) {
            return windowsBridge.hasLayoutChanged();
        }
        return false;
    }

    @Deprecated
    public boolean checkAndClearLayoutChanged() {
        return hasLayoutChanged();
    }

    public boolean isChineseMode() {
        if (windowsBridge != null) {
            return windowsBridge.isChineseMode();
        }
        return config.isChineseMode();
    }

    public void toggleChineseMode() {
        config.toggleChineseMode();
        hud.clearInput();
    }

    public InputMode getDetectedInputMode() {
        if (windowsBridge != null) {
            return windowsBridge.getDetectedInputMode();
        }
        return config.getInputMode();
    }

    public boolean isImeOpen() {
        if (windowsBridge != null) {
            return windowsBridge.isImeOpen();
        }
        return false;
    }

    public boolean isCapsLockOn() {
        if (windowsBridge != null) {
            return windowsBridge.isCapsLockOn();
        }
        return false;
    }

    public boolean isInShiftMode() {
        if (windowsBridge != null) {
            return windowsBridge.isInShiftMode();
        }
        return false;
    }

    public boolean hasInput() {
        return hud.isVisible() || verticalHud.isVisible();
    }

    public void clearInput() {
        hud.clearInput();
    }

    public void selectPrev() {
        hud.selectPrevious();
    }

    public void selectNext() {
        hud.selectNext();
    }

    public void prevPage() {
        hud.prevPage();
    }

    public void nextPage() {
        hud.nextPage();
    }

    public String confirmSelection() {
        String selected = hud.getSelected();
        hud.clearInput();
        return selected;
    }

public void backspace() {
    String current = hud.getInput();
    if (current.length() > 1) {
        String newInput = current.substring(0, current.length() - 1);
        List<String> builtIn = PinyinDictionary.getSuggestions(newInput);
        hud.updateCandidates(builtIn, newInput);
    } else {
        hud.clear();
    }
}

public boolean inputChar(char c) {
    if (!config.isChineseMode()) return false;
    if (!Character.isLetter(c)) return false;

    String current = hud.getInput() + Character.toLowerCase(c);
    List<String> builtIn = PinyinDictionary.getSuggestions(current);
    hud.updateCandidates(builtIn, current);
    return true;
}

    public CandidateHud getHud() {
        return hud;
    }

    public Object getWindowsBridge() {
        return windowsBridge;
    }

    public boolean isWindowsSync() {
        return syncEnabled;
    }

    public void toggleScriptType() {
        config.toggleScriptType();
    }

    public void toggleInputMethod() {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] toggleInputMethod called - on Windows, use system shortcuts to switch IME");
    }

    private static final OS CURRENT_PLATFORM = detectPlatform();

    private static OS detectPlatform() {
        String os = System.getProperty("os.name").toLowerCase();
        if (os.contains("win")) {
            return OS.WINDOWS;
        } else if (os.contains("mac")) {
            return OS.MACOS;
        } else if (os.contains("linux")) {
            return OS.LINUX;
        }
        return OS.OTHER;
    }

    public static OS getPlatform() {
        return CURRENT_PLATFORM;
    }

    public String selectCandidate(int index) {
        if (index >= 0 && index < hud.getCandidates().size()) {
            String selected = hud.getCandidates().get(index);
            hud.clearInput();
            return selected;
        }
        return null;
    }

    private boolean testModeActive = false;

    public void showTestCandidates() {
        if (testModeActive) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Hiding test candidates");
            hud.clearInput();
            testModeActive = false;
            return;
        }

        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Showing test candidates");
        List<String> testCands = PinyinDictionary.getCandidatesForTest();
        if (testCands.isEmpty()) {
            testCands = java.util.Arrays.asList("测试词语1", "测试词语2", "测试词语3", "测试词语4", "测试词语5", "测试词语6", "测试词语7", "测试词语8", "测试词语9");
        }
        List<String> displayCands = testCands.subList(0, Math.min(9, testCands.size()));
        hud.updateCandidates(displayCands, "test");
        testModeActive = true;
    }

    public void clearTestCandidates() {
        hud.clearInput();
        testModeActive = false;
    }

    public enum OS {
        WINDOWS, LINUX, MACOS, OTHER
    }
}