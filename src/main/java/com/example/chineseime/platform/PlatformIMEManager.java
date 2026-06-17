package com.example.chineseime.platform;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.config.ModConfig;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.engine.PinyinDictionary;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.hud.VerticalCandidateHud;
import com.example.chineseime.platform.win32.WindowsIMEEventBridge;
import net.minecraft.client.gui.DrawContext;
import java.util.List;

public class PlatformIMEManager {
    private final ModConfig config;
    private final CandidateHud horizontalHud;
    private final VerticalCandidateHud verticalHud;
    private WindowsIMEEventBridge eventBridge;
    private boolean syncEnabled = false;

    public PlatformIMEManager(ModConfig config, CandidateHud horizontalHud, VerticalCandidateHud verticalHud, ImeStatusIndicator indicator, long windowHandle) {
        this.config = config;
        this.horizontalHud = horizontalHud;
        this.verticalHud = verticalHud;

        if (getPlatform() == OS.WINDOWS) {
            initWindowsIME(indicator, windowHandle);
        }
    }

    private void initWindowsIME(ImeStatusIndicator indicator, long windowHandle) {
        try {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Initializing Windows IME Event Bridge");
            eventBridge = new WindowsIMEEventBridge(horizontalHud, verticalHud, indicator);
            eventBridge.initialize(windowHandle);

            if (eventBridge.isHooked()) {
                syncEnabled = true;
                ChineseIMEInitializer.LOGGER.info("[ChineseIME] Windows IME Event Bridge initialized, syncEnabled=true");
            } else {
                ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Windows IME Event Bridge hook failed, syncEnabled=false");
            }
        } catch (Throwable e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Failed to initialize Windows IME Bridge: {}", e.getMessage());
            e.printStackTrace();
            syncEnabled = false;
        }
    }

    public void tick() {
    }

    public boolean isChineseMode() {
        if (eventBridge != null && syncEnabled) {
            return eventBridge.isChineseMode();
        }
        return config.isChineseMode();
    }

    public void toggleChineseMode() {
        config.toggleChineseMode();
        horizontalHud.clearInput();
        verticalHud.clearInput();
    }

    public InputMode getDetectedInputMode() {
        if (eventBridge != null && syncEnabled) {
            return eventBridge.getDetectedInputMode();
        }
        return config.getInputMode();
    }

    public boolean isImeOpen() {
        if (eventBridge != null && syncEnabled) {
            return eventBridge.isImeOpen();
        }
        return false;
    }

    public boolean isCapsLockOn() {
        if (eventBridge != null && syncEnabled) {
            return eventBridge.isCapsLockOn();
        }
        return false;
    }

    public boolean isInShiftMode() {
        if (eventBridge != null && syncEnabled) {
            return eventBridge.isInShiftMode();
        }
        return false;
    }

    public boolean hasInput() {
        return horizontalHud.isVisible() || verticalHud.isVisible();
    }

    public void clearInput() {
        horizontalHud.clearInput();
        verticalHud.clearInput();
    }

    public void selectPrev() {
        InputMode mode = getDetectedInputMode();
        boolean useVertical = (mode == InputMode.CANGJIE || mode == InputMode.ZHUYIN || mode == InputMode.SUCHENG);

        if (useVertical) {
            verticalHud.selectPrevious();
        } else {
            horizontalHud.selectPrevious();
        }
    }

    public void selectNext() {
        InputMode mode = getDetectedInputMode();
        boolean useVertical = (mode == InputMode.CANGJIE || mode == InputMode.ZHUYIN || mode == InputMode.SUCHENG);

        if (useVertical) {
            verticalHud.selectNext();
        } else {
            horizontalHud.selectNext();
        }
    }

    public void prevPage() {
        InputMode mode = getDetectedInputMode();
        boolean useVertical = (mode == InputMode.CANGJIE || mode == InputMode.ZHUYIN || mode == InputMode.SUCHENG);

        if (useVertical) {
            verticalHud.prevPage();
        } else {
            horizontalHud.prevPage();
        }
    }

    public void nextPage() {
        InputMode mode = getDetectedInputMode();
        boolean useVertical = (mode == InputMode.CANGJIE || mode == InputMode.ZHUYIN || mode == InputMode.SUCHENG);

        if (useVertical) {
            verticalHud.nextPage();
        } else {
            horizontalHud.nextPage();
        }
    }

    public String confirmSelection() {
        InputMode mode = getDetectedInputMode();
        boolean useVertical = (mode == InputMode.CANGJIE || mode == InputMode.ZHUYIN || mode == InputMode.SUCHENG);

        String selected;
        if (useVertical) {
            selected = verticalHud.getSelected();
            verticalHud.clearInput();
        } else {
            selected = horizontalHud.getSelected();
            horizontalHud.clearInput();
        }
        return selected;
    }

    public void backspace() {
        InputMode mode = getDetectedInputMode();
        boolean useVertical = (mode == InputMode.CANGJIE || mode == InputMode.ZHUYIN || mode == InputMode.SUCHENG);

        String current = useVertical ? verticalHud.getInput() : horizontalHud.getInput();
        if (current.length() > 1) {
            String newInput = current.substring(0, current.length() - 1);
            List<String> builtIn = PinyinDictionary.getSuggestions(newInput);
            horizontalHud.updateCandidates(builtIn, newInput);
            verticalHud.updateCandidates(builtIn, newInput);
        } else {
            horizontalHud.clear();
            verticalHud.clear();
        }
    }

    public boolean inputChar(char c) {
        if (!config.isChineseMode()) return false;
        if (!Character.isLetter(c)) return false;

        InputMode mode = getDetectedInputMode();
        boolean useVertical = (mode == InputMode.CANGJIE || mode == InputMode.ZHUYIN || mode == InputMode.SUCHENG);

        String current = useVertical ? verticalHud.getInput() : horizontalHud.getInput();
        current = current + Character.toLowerCase(c);
        List<String> builtIn = PinyinDictionary.getSuggestions(current);
        horizontalHud.updateCandidates(builtIn, current);
        verticalHud.updateCandidates(builtIn, current);
        return true;
    }

    public CandidateHud getHud() {
        return horizontalHud;
    }

    public VerticalCandidateHud getVerticalHud() {
        return verticalHud;
    }

    public Object getWindowsBridge() {
        return eventBridge;
    }

    public boolean isWindowsSync() {
        return syncEnabled;
    }

    public boolean hasLayoutChanged() {
        return false;
    }

    public boolean checkAndClearLayoutChanged() {
        return false;
    }

    public void toggleScriptType() {
        config.toggleScriptType();
    }

    public void toggleInputMethod() {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] toggleInputMethod called - on Windows, use system shortcuts to switch IME");
    }

    public static OS getPlatform() {
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

    public String selectCandidate(int index) {
        InputMode mode = getDetectedInputMode();
        boolean useVertical = (mode == InputMode.CANGJIE || mode == InputMode.ZHUYIN || mode == InputMode.SUCHENG);

        List<String> candidates = useVertical ? verticalHud.getCandidates() : horizontalHud.getCandidates();
        if (index >= 0 && index < candidates.size()) {
            String selected = candidates.get(index);
            horizontalHud.clearInput();
            verticalHud.clearInput();
            return selected;
        }
        return null;
    }

    private boolean testModeActive = false;

    public void showTestCandidates() {
        if (testModeActive) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Hiding test candidates");
            horizontalHud.clearInput();
            verticalHud.clearInput();
            testModeActive = false;
            return;
        }

        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Showing test candidates");
        List<String> testCands = PinyinDictionary.getCandidatesForTest();
        if (testCands.isEmpty()) {
            testCands = java.util.Arrays.asList("测试词语1", "测试词语2", "测试词语3", "测试词语4", "测试词语5", "测试词语6", "测试词语7", "测试词语8", "测试词语9");
        }
        List<String> displayCands = testCands.subList(0, Math.min(9, testCands.size()));
        horizontalHud.updateCandidates(displayCands, "test");
        verticalHud.updateCandidates(displayCands, "test");
        testModeActive = true;
    }

    public void clearTestCandidates() {
        horizontalHud.clearInput();
        verticalHud.clearInput();
        testModeActive = false;
    }

    public boolean isVerticalLayout() {
        InputMode mode = getDetectedInputMode();
        return (mode == InputMode.CANGJIE || mode == InputMode.ZHUYIN || mode == InputMode.SUCHENG);
    }

    public void renderHud(DrawContext ctx) {
        if (isVerticalLayout()) {
            verticalHud.render(ctx);
        } else {
            horizontalHud.render(ctx);
        }
    }

    public enum OS {
        WINDOWS, LINUX, MACOS, OTHER
    }
}