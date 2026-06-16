package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.screen.ChatScreen;
import java.util.ArrayList;
import java.util.List;

public class WindowsIMEBridgeNative {
    private boolean initialized = false;
    private CandidateHud candidateHud;
    private ImeStatusIndicator statusIndicator;

    private int prevInputMethodType = -1;
    private boolean prevChineseMode = false;
    private boolean prevCapsLock = false;
    private boolean prevShiftMode = false;
    private List<String> prevCandidates = new ArrayList<>();
    private String prevComposition = "";
    private int tickCounter = 0;
    private boolean prevHudShown = false;
    private boolean wasInEnglishMode = false;
    private int ticksSinceModeSwitch = 0;

    public WindowsIMEBridgeNative(CandidateHud candidateHud) {
        this.candidateHud = candidateHud;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Initializing WindowsIMEBridgeNative (polling mode)");
    }

    public void setStatusIndicator(ImeStatusIndicator indicator) {
        this.statusIndicator = indicator;
    }

    public boolean initialize() {
        NativeImeBridge.getInstance();

        if (!NativeImeBridge.isAvailable()) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Native library not available");
            return false;
        }

        NativeImeBridge.startListening(0L);
        int tsfResult = NativeImeBridge.startTsfListening();
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] TSF listen result: {}", tsfResult);

        initialized = true;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEBridgeNative initialized (polling mode)");
        return true;
    }

    public void update() {
        if (!initialized) return;

        NativeImeBridge.refreshImeState();

        int inputMethodType = NativeImeBridge.getInputMethodType();
        InputMode currentMode = NativeImeBridge.getInputMethodTypeAsEnum(inputMethodType);
        boolean chineseMode = NativeImeBridge.isChineseMode();
        boolean capsLockOn = NativeImeBridge.getCapsLockState();
        boolean imeOpen = NativeImeBridge.getImeOpenStatus();
        boolean inShiftMode = NativeImeBridge.getShiftMode();

        String composition = NativeImeBridge.getCompositionString();
        List<String> candidates = NativeImeBridge.getCandidates();
        int selectedIndex = NativeImeBridge.getSelectedCandidateIndex();

        boolean stateChanged = prevInputMethodType != inputMethodType
            || prevChineseMode != chineseMode
            || prevCapsLock != capsLockOn
            || prevShiftMode != inShiftMode;

        if (!prevChineseMode && chineseMode) {
            wasInEnglishMode = true;
            ticksSinceModeSwitch = 0;
        }
        if (wasInEnglishMode) {
            ticksSinceModeSwitch++;
        }

        prevInputMethodType = inputMethodType;
        prevChineseMode = chineseMode;
        prevCapsLock = capsLockOn;
        prevShiftMode = inShiftMode;

        boolean inChat = isChatScreenOpen();
        boolean candidatesChanged = !prevCandidates.equals(candidates) || !prevComposition.equals(composition);
        if (candidatesChanged) {
            prevCandidates = new ArrayList<>(candidates);
            prevComposition = composition;

            if (candidateHud != null && inChat) {
                if (!candidates.isEmpty()) {
                    wasInEnglishMode = false;
                    candidateHud.updateCandidatesKeepSelection(
                        candidates, composition, selectedIndex, candidateHud.getPage());
                } else if (!composition.isEmpty()) {
                    if (wasInEnglishMode && ticksSinceModeSwitch < 10) {
                    } else {
                        wasInEnglishMode = false;
                        List<String> fallback = com.example.chineseime.engine.PinyinDictionary.getSuggestions(composition);
                        if (!fallback.isEmpty()) {
                            candidateHud.updateCandidatesKeepSelection(fallback, composition, 0, 0);
                        } else {
                            candidateHud.updateCandidatesKeepSelection(new ArrayList<>(), composition, 0, 0);
                        }
                    }
                } else {
                    candidateHud.clearInput();
                }
            }
        }

        tickCounter++;
        if (tickCounter % 600 == 0 || (tickCounter % 60 == 0 && !candidates.isEmpty())) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Poll: IME={}({}), CMode={}, Caps={}, ShiftM={}, ImeOpen={}, CandCnt={}, Comp='{}'",
                currentMode, inputMethodType, chineseMode, capsLockOn, inShiftMode, imeOpen,
                candidates.size(), composition);
        }

        boolean hudShown = candidateHud != null && candidateHud.isVisible();
        if (hudShown && statusIndicator != null) {
            statusIndicator.hide();
        } else if (statusIndicator != null) {
            if (stateChanged) {
                statusIndicator.update(chineseMode, currentMode, capsLockOn, inShiftMode);
            } else if (prevHudShown) {
                statusIndicator.show();
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
            NativeImeBridge.stopTsfListening();
            NativeImeBridge.stopListening();
            initialized = false;
        }
    }

    public boolean isImeOpen() { return NativeImeBridge.getImeOpenStatus(); }
    public boolean isChineseMode() { return NativeImeBridge.isChineseMode(); }
    public boolean isCapsLockOn() {
        boolean raw = NativeImeBridge.getCapsLockState();
        if (tickCounter % 20 == 0) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] CapsLock raw JNA call = {}", raw);
        }
        return raw;
    }
    public boolean isInShiftMode() { return NativeImeBridge.getShiftMode(); }
    public InputMode getDetectedInputMode() { return NativeImeBridge.getInputMethodTypeAsEnum(); }
    public boolean hasLayoutChanged() { return false; }
}