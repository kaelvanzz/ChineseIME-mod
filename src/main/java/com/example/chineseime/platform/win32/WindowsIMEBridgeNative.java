package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.CangjieDictionary;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.engine.PinyinDictionary;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.hud.VerticalCandidateHud;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.screen.ChatScreen;
import java.util.ArrayList;
import java.util.List;

public class WindowsIMEBridgeNative {
    private boolean initialized = false;
    private boolean hooked = false;
    private CandidateHud candidateHud;
    private VerticalCandidateHud verticalCandidateHud;
    private ImeStatusIndicator statusIndicator;

    private int currentInputMethodType = NativeImeBridge.IME_TYPE_ENGLISH;
    private boolean currentChineseMode = false;
    private String currentComposition = "";
    private List<String> currentCandidates = new ArrayList<>();
    private int currentSelectedIndex = 0;

    private int tickCounter = 0;
    private boolean prevHudShown = false;
    private boolean wasInEnglishMode = false;
    private int ticksSinceModeSwitch = 0;
    private long lastUpdateTime = 0;
    private static final long MIN_UPDATE_INTERVAL = 50; // Minimum 50ms between updates (20 FPS max)
    private static final long INACTIVE_POLL_INTERVAL = 500; // Poll every 500ms when inactive
    private static final long ACTIVE_POLL_INTERVAL = 50;  // Poll every 50ms when active

    private NativeImeBridge.PreeditCallback preeditCallback;
    private NativeImeBridge.CommitCallback commitCallback;
    private NativeImeBridge.CandidatesCallback candidatesCallback;
    private NativeImeBridge.ImeChangeCallback imeChangeCallback;

    public WindowsIMEBridgeNative(CandidateHud candidateHud, VerticalCandidateHud verticalCandidateHud) {
        this.candidateHud = candidateHud;
        this.verticalCandidateHud = verticalCandidateHud;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Initializing WindowsIMEBridgeNative");
    }

    public void setStatusIndicator(ImeStatusIndicator indicator) {
        this.statusIndicator = indicator;
    }

    public boolean initialize() {
        if (!NativeImeBridge.isAvailable()) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Native library not available");
            return false;
        }

        createCallbacks();
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEBridgeNative initialized (event-driven)");
        initialized = true;
        return true;
    }

    private void createCallbacks() {
        preeditCallback = new NativeImeBridge.PreeditCallback() {
            @Override
            public void invoke(Pointer text, int cursor, int selLen) {
                String comp = text != null ? text.getWideString(0) : "";
                handlePreedit(comp, cursor, selLen);
            }
        };

        commitCallback = new NativeImeBridge.CommitCallback() {
            @Override
            public void invoke(Pointer text) {
                String committed = text != null ? text.getWideString(0) : "";
                handleCommit(committed);
            }
        };

        candidatesCallback = new NativeImeBridge.CandidatesCallback() {
            @Override
            public void invoke(Pointer candidates, int count, int selectedIndex) {
                List<String> cands = new ArrayList<>();
                if (candidates != null && count > 0) {
                    for (int i = 0; i < count; i++) {
                        Pointer pStr = candidates.getPointer(i * Native.POINTER_SIZE);
                        String s = pStr.getWideString(0);
                        cands.add(s);
                    }
                }
                handleCandidates(cands, selectedIndex);
            }
        };

        imeChangeCallback = new NativeImeBridge.ImeChangeCallback() {
            @Override
            public void invoke(int imeType, int chineseMode) {
                handleImeChange(imeType, chineseMode);
            }
        };
    }

    public boolean hookWindow(long hwnd) {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] hookWindow called: initialized={}, hooked={}, hwnd={}",
            initialized, hooked, hwnd);
        if (!initialized || hooked) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] hookWindow early return: initialized={}, hooked={}", initialized, hooked);
            return hooked;
        }

        // Validate window handle before attempting to hook
        if (!isValidWindow(hwnd)) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Invalid window handle provided: {}", hwnd);
            return false;
        }

        // Try WndProc subclassing first (more efficient, lower latency)
        if (tryHookWindowProc(hwnd)) {
            hooked = true;
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] WndProc hook installed successfully");
        } 
        // If WndProc hook failed, try WH_GETMESSAGE hook as fallback
        else if (tryHookGetMessage(hwnd)) {
            hooked = true;
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] WH_GETMESSAGE hook installed successfully");
        } 
        // If both hooks fail, log error and return false
        else {
            ChineseIMEInitializer.LOGGER.error("[ChineseIME] All hooking methods failed for window: {}", hwnd);
            hooked = false;
        }

        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Hooking result: hooked={}, isWindowHooked={}", hooked, NativeImeBridge.isWindowHooked());

        // Always register callbacks - they may be used by either WndProc hook or WH_* hooks
        NativeImeBridge.registerCallbacks(preeditCallback, commitCallback, candidatesCallback, imeChangeCallback);
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] IME callbacks registered");

        int tsfStarted = NativeImeBridge.getInstance().StartTsfListen();
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] TSF listener started: {}", tsfStarted != 0);

        return hooked;
    }

    private boolean isValidWindow(long hwnd) {
        if (hwnd == 0) {
            return false;
        }
        try {
            return NativeImeBridge.getInstance().IsWindowValid(hwnd) != 0;
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME] Error checking window validity: {}", e.getMessage());
            return false;
        }
    }

    private boolean tryHookWindowProc(long hwnd) {
        try {
            NativeImeBridge.hookWindowProc(hwnd);
            return NativeImeBridge.isWindowHooked();
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME] WndProc hook failed: {}", e.getMessage());
            return false;
        }
    }

    private boolean tryHookGetMessage(long hwnd) {
        try {
            int msgHookResult = NativeImeBridge.getInstance().InstallMessageHook(hwnd);
            return msgHookResult != 0;
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME] WH_GETMESSAGE hook failed: {}", e.getMessage());
            return false;
        }
    }

    public void unhookWindow() {
        if (hooked) {
            NativeImeBridge.getInstance().StopTsfListen();
            NativeImeBridge.unhookWindowProc();
            hooked = false;
        }
    }

    private void handlePreedit(String composition, int cursor, int selLen) {
        currentComposition = composition;
        updateHud();
    }

    private void handleCommit(String text) {
        if (text == null || text.isEmpty()) return;

        boolean containsChinese = false;
        boolean containsOnlyLettersAndSpaces = text.matches("^[a-zA-Z'\\s]+$");
        for (char c : text.toCharArray()) {
            if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF)) {
                containsChinese = true;
                break;
            }
        }

        // Block if text has no Chinese AND:
        // 1. We're showing candidates (user should select one), OR
        // 2. We have an active composition (typing in progress) - IME should not auto-commit
        // 3. We're in Chinese mode (shift to English should not cause pinyin commit)
        if (!containsChinese) {
            if (!currentCandidates.isEmpty()) {
                ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Commit blocked: text='{}' no Chinese but candidates showing", text);
                return;
            }
            if (!currentComposition.isEmpty()) {
                ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Commit blocked: text='{}' no Chinese but composition active='{}'", text, currentComposition);
                return;
            }
            if (currentChineseMode) {
                ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Commit blocked: text='{}' no Chinese but ChineseMode=true", text);
                return;
            }
        }

        // Additional safety: block pure ASCII letter sequences that look like pinyin
        // These are almost certainly accidental commits
        if (!containsChinese && containsOnlyLettersAndSpaces && !text.matches(".*[A-Z].*") && text.length() > 1) {
            // All lowercase letters - likely pinyin
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Commit blocked: text='{}' looks like pinyin", text);
            return;
        }

        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Commit: {}", text);
        insertTextToChat(text);
        currentComposition = "";
        currentCandidates.clear();
        updateHud();
    }

    private void handleCandidates(List<String> candidates, int selectedIndex) {
        boolean changed = !currentCandidates.equals(candidates) || currentSelectedIndex != selectedIndex;
        if (changed) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] handleCandidates: cands={}, selIdx={}", candidates.size(), selectedIndex);
            currentCandidates = new ArrayList<>(candidates);
            currentSelectedIndex = selectedIndex;
            wasInEnglishMode = false;
            updateHud();
        }
    }

    private void handleImeChange(int imeType, int chineseMode) {
        boolean newChineseMode = chineseMode != 0;
        boolean changed = currentInputMethodType != imeType || currentChineseMode != newChineseMode;
        if (changed) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] IME change: type={}, chineseMode={}", imeType, chineseMode);
            int oldInputMethodType = currentInputMethodType;
            currentInputMethodType = imeType;
            currentChineseMode = newChineseMode;

            if (!currentChineseMode) {
                wasInEnglishMode = true;
                ticksSinceModeSwitch = 0;
            }

            if (statusIndicator != null) {
                InputMode mode = NativeImeBridge.getInputMethodTypeAsEnum(imeType);
                boolean shouldShow = currentChineseMode || oldInputMethodType != imeType;
                if (shouldShow) {
                    statusIndicator.update(currentChineseMode, mode,
                        NativeImeBridge.getCapsLockState(), NativeImeBridge.getShiftMode());
                }
            }
        }
    }

    private void updateHud() {
        InputMode mode = NativeImeBridge.getInputMethodTypeAsEnum(currentInputMethodType);
        boolean isVerticalLayout = (mode == InputMode.CANGJIE || mode == InputMode.SUCHENG || mode == InputMode.ZHUYIN);

        boolean inEnglishTransition = wasInEnglishMode && ticksSinceModeSwitch < 10;

        String displayComposition = currentComposition;
        if (displayComposition.contains("'")) {
            int aposIndex = displayComposition.indexOf("'");
            if (aposIndex > 0) {
                displayComposition = displayComposition.substring(0, aposIndex);
            }
        }

        if (isVerticalLayout && verticalCandidateHud != null) {
            if (!currentCandidates.isEmpty()) {
                verticalCandidateHud.updateCandidatesKeepSelection(
                    currentCandidates, currentComposition, currentSelectedIndex, verticalCandidateHud.getPage());
            } else if (!currentComposition.isEmpty()) {
                verticalCandidateHud.updateCandidatesKeepSelection(
                    new ArrayList<>(), currentComposition, 0, 0);
            } else {
                verticalCandidateHud.clearInput();
            }
            if (candidateHud != null) candidateHud.clearInput();
        } else if (candidateHud != null) {
            if (!currentCandidates.isEmpty()) {
                candidateHud.updateCandidatesKeepSelection(
                    currentCandidates, displayComposition, currentSelectedIndex, candidateHud.getPage());
            } else if (!currentComposition.isEmpty()) {
                List<String> fallback = getFallbackCandidates(currentComposition);
                if (!fallback.isEmpty() && !inEnglishTransition) {
                    candidateHud.updateCandidatesKeepSelection(fallback, displayComposition, 0, 0);
                } else {
                    candidateHud.updateCandidatesKeepSelection(
                        new ArrayList<>(), currentComposition, 0, 0);
                }
            } else {
                candidateHud.clearInput();
            }
            if (verticalCandidateHud != null) verticalCandidateHud.clearInput();
        }
    }

    private List<String> getFallbackCandidates(String composition) {
        InputMode mode = NativeImeBridge.getInputMethodTypeAsEnum(currentInputMethodType);
        ChineseIMEInitializer.LOGGER.debug("[ChineseIME] getFallbackCandidates for mode: {}", mode);

        switch (mode) {
            case PINYIN:
                return PinyinDictionary.getSuggestions(composition);
            case CANGJIE:
            case SUCHENG:
                return CangjieDictionary.getSuggestions(composition);
            case ZHUYIN:
            case WUBI:
            case YUEPIN:
            case RIME:
            case OTHER:
            case LATIN:
            default:
                return new ArrayList<>();
        }
    }

    private void insertTextToChat(String text) {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null || !(mc.currentScreen instanceof ChatScreen)) return;

        try {
            var chatScreen = (ChatScreen) mc.currentScreen;
            java.lang.reflect.Field field = ChatScreen.class.getDeclaredField("chatField");
            field.setAccessible(true);
            Object textFieldObj = field.get(chatScreen);
            if (textFieldObj instanceof net.minecraft.client.gui.widget.TextFieldWidget textField) {
                int cursor = textField.getCursor();
                String current = textField.getText();
                String newText = current.substring(0, cursor) + text + current.substring(cursor);
                textField.setText(newText);
                textField.setCursor(cursor + text.length(), false);
            }
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Failed to insert text: {}", e.getMessage());
        }
    }

    private int lastPolledInputMethodType = 0;

    public void update() {
        if (!initialized) return;

        if (tickCounter % 5 == 0) {
            NativeImeBridge.refreshImeState();
        }
        boolean imeOpenCheck = isImeOpen();
        boolean chineseModeCheck = isChineseMode();
        if (wasInEnglishMode) {
            ticksSinceModeSwitch++;
        }

        int inputMethodType = NativeImeBridge.getInputMethodType();
        boolean chineseMode = chineseModeCheck; // Use cached value
        boolean capsLockOn = NativeImeBridge.getCapsLockState();
        boolean imeOpen = imeOpenCheck; // Use cached value
        boolean inShiftMode = NativeImeBridge.getShiftMode();
        InputMode currentMode = NativeImeBridge.getInputMethodTypeAsEnum(inputMethodType);

        // Check for IME type changes via polling as backup when event-driven hook fails
        if (lastPolledInputMethodType == 0) {
            lastPolledInputMethodType = inputMethodType;
        }
        boolean imeTypePolledChanged = (lastPolledInputMethodType != inputMethodType && inputMethodType != 0);
        if (imeTypePolledChanged) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Poll detected IME type change: {} -> {}",
                lastPolledInputMethodType, inputMethodType);
            lastPolledInputMethodType = inputMethodType;
            handleImeChange(inputMethodType, chineseMode ? 1 : 0);
        }

        boolean stateChanged = currentInputMethodType != inputMethodType
            || currentChineseMode != chineseMode;
        if (stateChanged) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Poll detected state change: IME={}({}), CMode={}",
                currentMode, inputMethodType, chineseMode);
            handleImeChange(inputMethodType, chineseMode ? 1 : 0);
        }

        String pollComposition = NativeImeBridge.getCompositionString();
        int nativeCandidateCount = NativeImeBridge.getCandidateCount();
        ChineseIMEInitializer.LOGGER.debug("[ChineseIME] Poll: comp='{}', nativeCandCount={}", pollComposition, nativeCandidateCount);

        List<String> pollCandidates = nativeCandidateCount > 0 ? NativeImeBridge.getCandidates() : new ArrayList<>();

        // Fallback: Always try UI Automation when IMM32 returns no candidates
        // This handles cross-process IME scenarios (like Minecraft with Iris)
        if (nativeCandidateCount == 0) {
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME] IMM32 returned 0 candidates, trying UI Automation...");
            List<String> uiaCandidates = NativeImeBridge.getCandidatesViaUIA();
            if (!uiaCandidates.isEmpty()) {
                ChineseIMEInitializer.LOGGER.debug("[ChineseIME] UI Automation found {} candidates", uiaCandidates.size());
                pollCandidates = uiaCandidates;
            } else {
                ChineseIMEInitializer.LOGGER.debug("[ChineseIME] UI Automation found no candidates");
            }
        }

        if (nativeCandidateCount > 0 && pollCandidates.isEmpty()) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] getCandidateCount={} but getCandidates returned empty", nativeCandidateCount);
        }
        int pollSelectedIndex = NativeImeBridge.getSelectedCandidateIndex();

        boolean contentChanged = !currentComposition.equals(pollComposition) || !currentCandidates.equals(pollCandidates);
        if (contentChanged) {
            ChineseIMEInitializer.LOGGER.debug("[ChineseIME] Content changed: comp='{}', cands={}, imeType={}",
                pollComposition, pollCandidates.size(), currentMode);
            currentComposition = pollComposition;
            currentCandidates = new ArrayList<>(pollCandidates);
            currentSelectedIndex = pollSelectedIndex;
            updateHud();
        }

        tickCounter++;
        // Log status summary every 30 seconds (600 ticks) when IME is active
        if (tickCounter % 600 == 0 && (imeOpen || !pollCandidates.isEmpty() || !pollComposition.isEmpty())) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Status: IME={}({}), CMode={}, CandCnt={}, Comp='{}'",
                currentMode, inputMethodType, chineseMode, pollCandidates.size(), pollComposition);
        }

        boolean hudShown = candidateHud != null && candidateHud.isVisible();
        if (hudShown && statusIndicator != null) {
            statusIndicator.hide();
        }

        if (statusIndicator != null) {
            if (imeOpen && chineseMode) {
                statusIndicator.update(chineseMode, currentMode, capsLockOn, inShiftMode);
                statusIndicator.show();
            } else if (isChatScreenOpen() && !imeOpen) {
                statusIndicator.update(false, currentMode, capsLockOn, inShiftMode);
                statusIndicator.show();
            } else if (stateChanged) {
                statusIndicator.update(chineseMode, currentMode, capsLockOn, inShiftMode);
            }
        }
        prevHudShown = hudShown;
    }

    private static boolean isChatScreenOpen() {
        MinecraftClient mc = MinecraftClient.getInstance();
        return mc != null && mc.currentScreen instanceof ChatScreen;
    }

    public void shutdown() {
        if (initialized) {
            unhookWindow();
            initialized = false;
        }
    }

    public boolean isImeOpen() { return NativeImeBridge.getImeOpenStatus(); }
    public boolean isChineseMode() { return NativeImeBridge.isChineseMode(); }
    public boolean isCapsLockOn() { return NativeImeBridge.getCapsLockState(); }
    public boolean isInShiftMode() { return NativeImeBridge.getShiftMode(); }
    public InputMode getDetectedInputMode() { return NativeImeBridge.getInputMethodTypeAsEnum(); }
    public boolean hasLayoutChanged() { return false; }
    public boolean isHooked() { return hooked; }
}
