package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.engine.PinyinDictionary;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.hud.VerticalCandidateHud;
import com.sun.jna.Memory;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.WString;
import net.minecraft.client.MinecraftClient;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import static com.example.chineseime.platform.win32.NativeImeBridge.PreeditCallback;
import static com.example.chineseime.platform.win32.NativeImeBridge.CommitCallback;
import static com.example.chineseime.platform.win32.NativeImeBridge.CandidateCallback;
import static com.example.chineseime.platform.win32.NativeImeBridge.ImeChangeCallback;
import static com.example.chineseime.platform.win32.NativeImeBridge.KeyboardCallback;

public class WindowsIMEEventBridge {
    private final CandidateHud horizontalHud;
    private final VerticalCandidateHud verticalHud;
    private final ImeStatusIndicator statusIndicator;

    private PreeditCallback preeditCallback;
    private CommitCallback commitCallback;
    private CandidateCallback candidateCallback;
    private ImeChangeCallback imeChangeCallback;
    private KeyboardCallback keyboardCallback;

    private String lastComposition = "";
    private List<String> lastCandidates = new ArrayList<>();
    private int lastSelectedIndex = 0;
    private InputMode currentMode = InputMode.PINYIN;
    private boolean currentChineseMode = false;
    private boolean currentCapsLock = false;
    private boolean currentShiftMode = false;
    private boolean initialized = false;

    public WindowsIMEEventBridge(CandidateHud horizontalHud, VerticalCandidateHud verticalHud, ImeStatusIndicator statusIndicator) {
        this.horizontalHud = horizontalHud;
        this.verticalHud = verticalHud;
        this.statusIndicator = statusIndicator;

        preeditCallback = (text, cursorPos, selStart, selLen) -> {
            MinecraftClient.getInstance().execute(() -> {
                String newComposition = text != null ? text.toString() : "";
                lastComposition = newComposition;

                if (!newComposition.isEmpty()) {
                    if (!lastCandidates.isEmpty()) {
                        updateHudWithCandidates(lastCandidates, newComposition, lastSelectedIndex);
                    } else {
                        updateHudOnlyComposition(newComposition);
                    }
                } else {
                    clearHud();
                }

                ChineseIMEInitializer.LOGGER.info("[ChineseIME] Preedit: '{}', cursor={}", newComposition, cursorPos);
            });
        };

        commitCallback = (text) -> {
            MinecraftClient.getInstance().execute(() -> {
                String commitText = text != null ? text.toString() : "";
                ChineseIMEInitializer.LOGGER.info("[ChineseIME] Commit: '{}'", commitText);

                lastComposition = "";
                lastCandidates.clear();
                lastSelectedIndex = 0;
                clearHud();

                if (!commitText.isEmpty()) {
                    insertTextToChat(commitText);
                }
            });
        };

        candidateCallback = (cands, count, selectedIndex) -> {
            MinecraftClient.getInstance().execute(() -> {
                lastCandidates.clear();
                if (count > 0 && cands != null) {
                    for (int i = 0; i < count; i++) {
                        Pointer ptr = cands.getPointer(i * Native.POINTER_SIZE);
                        String cand = ptr.getWideString(0);
                        lastCandidates.add(cand);
                    }
                }
                lastSelectedIndex = selectedIndex;

                ChineseIMEInitializer.LOGGER.info("[ChineseIME] Candidates: count={}, sel={}", lastCandidates.size(), selectedIndex);

                if (!lastComposition.isEmpty() || !lastCandidates.isEmpty()) {
                    if (!lastCandidates.isEmpty()) {
                        updateHudWithCandidates(lastCandidates, lastComposition, selectedIndex);
                    } else {
                        updateHudOnlyComposition(lastComposition);
                    }
                }
            });
        };

        imeChangeCallback = (inputMethodType, chineseMode) -> {
            MinecraftClient.getInstance().execute(() -> {
                currentMode = NativeImeBridge.getInputMethodTypeAsEnum(inputMethodType);
                currentChineseMode = chineseMode != 0;

                boolean useVertical = (currentMode == InputMode.CANGJIE ||
                                       currentMode == InputMode.ZHUYIN ||
                                       currentMode == InputMode.SUCHENG);

                if (statusIndicator != null) {
                    statusIndicator.update(currentChineseMode, currentMode, currentCapsLock, currentShiftMode);
                }

                ChineseIMEInitializer.LOGGER.info("[ChineseIME] IME changed: type={}({}), chineseMode={}",
                    currentMode, inputMethodType, chineseMode);
            });
        };

        keyboardCallback = (capsLock, shiftMode) -> {
            MinecraftClient.getInstance().execute(() -> {
                boolean changed = (currentCapsLock != (capsLock != 0)) ||
                                  (currentShiftMode != (shiftMode != 0));

                currentCapsLock = capsLock != 0;
                currentShiftMode = shiftMode != 0;

                if (changed && statusIndicator != null) {
                    statusIndicator.update(currentChineseMode, currentMode, currentCapsLock, currentShiftMode);
                }
            });
        };
    }

    public void initialize(long hwnd) {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEEventBridge.initialize() called with hwnd={}", String.format("0x%X", hwnd));

        if (hwnd == 0) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] HWND is 0, cannot initialize");
            return;
        }

        if (!NativeImeBridge.isAvailable()) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Native library not available");
            return;
        }

        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Native library available, proceeding with initialization");

        try {
            NativeImeBridge.setEventCallbacks(
                preeditCallback,
                commitCallback,
                candidateCallback,
                imeChangeCallback,
                keyboardCallback
            );
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Event callbacks set successfully");
        } catch (Throwable e) {
            ChineseIMEInitializer.LOGGER.error("[ChineseIME] Failed to set event callbacks", e);
            return;
        }

        try {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Calling hookWindowProc with hwnd={}", String.format("0x%X", hwnd));
            NativeImeBridge.hookWindowProc(hwnd);
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] hookWindowProc returned");

            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Starting TSF listening...");
            int tsfResult = NativeImeBridge.startTsfListening();
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] TSF listening started: {}", tsfResult);
        } catch (Throwable e) {
            ChineseIMEInitializer.LOGGER.error("[ChineseIME] hookWindowProc threw exception", e);
            return;
        }

        initialized = NativeImeBridge.isWindowHooked();
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEEventBridge initialize complete, hooked={}", initialized);
    }

    public void shutdown() {
        if (NativeImeBridge.isAvailable()) {
            NativeImeBridge.unhookWindowProc();
        }
        initialized = false;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEEventBridge shutdown");
    }

    public void refreshCandidates() {
        if (NativeImeBridge.isAvailable()) {
            NativeImeBridge.refreshCandidates();
        }
    }

    public boolean isInitialized() {
        return initialized;
    }

    public boolean isHooked() {
        return NativeImeBridge.isWindowHooked();
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
        if (NativeImeBridge.isAvailable()) {
            return NativeImeBridge.getImeOpenStatus();
        }
        return false;
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

    private void insertTextToChat(String text) {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null || mc.currentScreen == null) return;

        try {
            var screen = mc.currentScreen;

            Field textFieldField = null;
            for (Field field : screen.getClass().getDeclaredFields()) {
                if (field.getType().getSimpleName().contains("TextField") ||
                    field.getType().getSimpleName().equals("ChatTextField")) {
                    textFieldField = field;
                    break;
                }
            }

            if (textFieldField != null) {
                textFieldField.setAccessible(true);
                Object textField = textFieldField.get(screen);
                if (textField != null) {
                    Object currentText = textField.getClass().getMethod("getText").invoke(textField);
                    String newText = (currentText != null ? currentText : "") + text;
                    textField.getClass().getMethod("setText", String.class).invoke(textField, newText);
                    ChineseIMEInitializer.LOGGER.info("[ChineseIME] Inserted text to chat: {}", text);
                }
            }
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Failed to insert text: {}", e.getMessage());
        }
    }
}