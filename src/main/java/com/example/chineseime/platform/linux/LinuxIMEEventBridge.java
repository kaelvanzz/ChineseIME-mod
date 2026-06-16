package com.example.chineseime.platform.linux;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.hud.VerticalCandidateHud;
import net.minecraft.client.MinecraftClient;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class LinuxIMEEventBridge implements FcitxBridgeClient.CandidateListener {
    private final CandidateHud horizontalHud;
    private final VerticalCandidateHud verticalHud;
    private final ImeStatusIndicator statusIndicator;

    private String lastComposition = "";
    private List<String> lastCandidates = new ArrayList<>();
    private int lastHighlighted = -1;
    private InputMode currentMode = InputMode.PINYIN;
    private boolean currentChineseMode = false;
    private boolean currentCapsLock = false;
    private boolean currentShiftMode = false;
    private boolean initialized = false;

    private FcitxBridgeClient fcitxClient;

    public LinuxIMEEventBridge(CandidateHud horizontalHud, VerticalCandidateHud verticalHud, ImeStatusIndicator statusIndicator) {
        this.horizontalHud = horizontalHud;
        this.verticalHud = verticalHud;
        this.statusIndicator = statusIndicator;
    }

    public boolean initialize() {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Initializing Linux IME Event Bridge");

        currentMode = InputMode.PINYIN;
        currentChineseMode = true;

        fcitxClient = new FcitxBridgeClient(this);
        fcitxClient.start();

        initialized = true;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Linux IME Event Bridge initialized with Fcitx bridge client");
        return true;
    }

    public void shutdown() {
        if (fcitxClient != null) {
            fcitxClient.stop();
            fcitxClient = null;
        }
        initialized = false;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] LinuxIMEEventBridge shutdown");
    }

    @Override
    public void onCandidates(String preedit, List<String> candidates, int highlighted) {
        MinecraftClient.getInstance().execute(() -> {
            lastComposition = preedit != null ? preedit : "";
            lastCandidates = candidates != null ? new ArrayList<>(candidates) : new ArrayList<>();
            lastHighlighted = highlighted;

            if (!lastComposition.isEmpty() || !lastCandidates.isEmpty()) {
                if (!lastCandidates.isEmpty()) {
                    updateHudWithCandidates(lastCandidates, lastComposition, highlighted >= 0 ? highlighted : 0);
                } else {
                    updateHudOnlyComposition(lastComposition);
                }
            } else {
                clearHud();
            }
        });
    }

    @Override
    public void onConnected() {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Fcitx bridge connected");
    }

    @Override
    public void onDisconnected() {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Fcitx bridge disconnected");
    }

    @Override
    public void onError(String message) {
        ChineseIMEInitializer.LOGGER.warn("[ChineseIME-Linux] Fcitx bridge error: {}", message);
    }

    public boolean isInitialized() {
        return initialized;
    }

    public boolean isListening() {
        return fcitxClient != null && fcitxClient.isConnected();
    }

    public InputMode getDetectedInputMode() {
        return currentMode;
    }

    public boolean isChineseMode() {
        return currentChineseMode;
    }

    public boolean isCapsLockOn() {
        return currentCapsLock;
    }

    public boolean isInShiftMode() {
        return currentShiftMode;
    }

    public boolean isImeOpen() {
        return !lastComposition.isEmpty() || !lastCandidates.isEmpty();
    }

    public boolean hasInput() {
        return horizontalHud.isVisible() || verticalHud.isVisible();
    }

    private boolean isVerticalLayout() {
        return (currentMode == InputMode.CANGJIE ||
                currentMode == InputMode.ZHUYIN ||
                currentMode == InputMode.SUCHENG);
    }

    private void updateHudWithCandidates(List<String> candidates, String composition, int selectedIndex) {
        if (isVerticalLayout()) {
            verticalHud.updateCandidatesKeepSelection(candidates, composition, selectedIndex, 0);
            horizontalHud.setVisible(false);
            verticalHud.setVisible(true);
        } else {
            horizontalHud.updateCandidatesKeepSelection(candidates, composition, selectedIndex, 0);
            horizontalHud.setVisible(true);
            verticalHud.setVisible(false);
        }
    }

    private void updateHudOnlyComposition(String composition) {
        if (isVerticalLayout()) {
            verticalHud.updateCandidates(Collections.emptyList(), composition);
            horizontalHud.setVisible(false);
            verticalHud.setVisible(true);
        } else {
            horizontalHud.updateCandidates(Collections.emptyList(), composition);
            horizontalHud.setVisible(true);
            verticalHud.setVisible(false);
        }
    }

    private void clearHud() {
        horizontalHud.clearInput();
        verticalHud.clearInput();
    }

    public void setInputMode(InputMode mode) {
        this.currentMode = mode;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Input mode changed to: {}", mode);
    }

    public void setChineseMode(boolean chineseMode) {
        this.currentChineseMode = chineseMode;
        if (statusIndicator != null) {
            statusIndicator.update(currentChineseMode, currentMode, currentCapsLock, currentShiftMode);
        }
    }
}