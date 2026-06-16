package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.sun.jna.Callback;
import com.sun.jna.Memory;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.WString;
import com.sun.jna.win32.StdCallLibrary;
import com.sun.jna.win32.W32APIOptions;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentLinkedQueue;

public class NativeImeBridge {

    public interface CandidateUpdateCallback extends Callback {
        void invoke(WString composition, Pointer candidates, int count, int selectedIndex);
    }

    public interface LayoutChangeCallback extends Callback {
        void invoke(int inputMethodType);
    }

    public interface ModeChangeCallback extends Callback {
        void invoke(int chineseMode);
    }

    public interface KeyboardStateCallback extends Callback {
        void invoke(int capsLockOn, int inShiftMode);
    }
    public static final int IME_TYPE_UNKNOWN = 0;
    public static final int IME_TYPE_ENGLISH = 1;
    public static final int IME_TYPE_PINYIN = 2;
    public static final int IME_TYPE_ZHUYIN = 3;
    public static final int IME_TYPE_CANGJIE = 4;
    public static final int IME_TYPE_WUBI = 5;
    public static final int IME_TYPE_SUCHENG = 6;
    public static final int IME_TYPE_OTHER_CHINESE = 99;

    private static NativeLibrary INSTANCE = null;
    private static boolean loaded = false;
    private static boolean loadAttempted = false;
    private static final Object LOAD_LOCK = new Object();

    private static CandidateUpdateCallback sCandidateCallback = null;
    private static LayoutChangeCallback sLayoutCallback = null;
    private static ModeChangeCallback sModeCallback = null;
    private static KeyboardStateCallback sKeyboardCallback = null;

    public static boolean isAvailable() {
        if (!loadAttempted) {
            getInstance();
        }
        return loaded && INSTANCE != null;
    }

    public static void registerCallbacks(CandidateUpdateCallback candidateCallback,
                                          LayoutChangeCallback layoutCallback,
                                          ModeChangeCallback modeCallback,
                                          KeyboardStateCallback keyboardCallback) {
        if (!isAvailable()) return;
        sCandidateCallback = candidateCallback;
        sLayoutCallback = layoutCallback;
        sModeCallback = modeCallback;
        sKeyboardCallback = keyboardCallback;
        INSTANCE.SetCallbacks(candidateCallback, layoutCallback, modeCallback, keyboardCallback);
    }

    private static Path cachedDllPath = null;

    public static synchronized NativeLibrary getInstance() {
        if (!loadAttempted) {
            loadAttempted = true;
            loadNative();
        }
        return INSTANCE;
    }

private static void loadNative() {
    ChineseIMEInitializer.LOGGER.info("[ChineseIME] Loading native library...");
    try {
        String osArch = System.getProperty("os.arch");
        String nativesPath = "/META-INF/natives/" + osArch + "/chineseime_native.dll";
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Looking for DLL at: {}", nativesPath);

        if (cachedDllPath == null) {
            InputStream dllStream = NativeImeBridge.class.getResourceAsStream(nativesPath);
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] DLL stream: {}", dllStream != null ? "found" : "null");
            if (dllStream != null) {
                Path tempDir = Files.createTempDirectory("chineseime_native");
                cachedDllPath = tempDir.resolve("chineseime_native.dll");
                Files.copy(dllStream, cachedDllPath, StandardCopyOption.REPLACE_EXISTING);
                dllStream.close();
                ChineseIMEInitializer.LOGGER.info("[ChineseIME] DLL extracted to: {}", cachedDllPath);
            } else {
                ChineseIMEInitializer.LOGGER.warn("[ChineseIME] DLL not found in JAR at {}, using fallback", nativesPath);
                loaded = false;
                return;
            }
        } else {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Using cached DLL: {}", cachedDllPath);
        }

        INSTANCE = Native.load(cachedDllPath.toString(), NativeLibrary.class, W32APIOptions.UNICODE_OPTIONS);
        loaded = true;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] DLL loaded successfully");
    } catch (Exception e) {
        ChineseIMEInitializer.LOGGER.warn("[ChineseIME] DLL load failed: {} - using fallback", e.getMessage());
        e.printStackTrace();
        loaded = false;
    }
}

    public static boolean startListening(long hwnd) {
        if (!isAvailable()) return false;

        Pointer hwndPtr = hwnd != 0L ? Pointer.createConstant(hwnd) : null;
        int result = INSTANCE.StartListen(hwndPtr);
        return result == 1;
    }

    public static void stopListening() {
        if (isAvailable()) {
            INSTANCE.StopListen();
        }
    }

    public static int startTsfListening() {
        if (!isAvailable()) return 0;
        return INSTANCE.StartTsfListen();
    }

    public static void stopTsfListening() {
        if (isAvailable()) {
            INSTANCE.StopTsfListen();
        }
    }

    public static boolean isTsfListening() {
        return isAvailable() && INSTANCE.IsTsfListening() == 1;
    }

    public static boolean isChineseMode() {
        return isAvailable() && INSTANCE.IsChineseMode() == 1;
    }

    public static boolean hasLayoutChanged() {
        return isAvailable() && INSTANCE.HasLayoutChanged() == 1;
    }

    public static boolean hasTsfLayoutChanged() {
        return isAvailable() && INSTANCE.HasTsfLayoutChanged() == 1;
    }

    public static String getCompositionString() {
        if (!isAvailable()) return "";
        int bufChars = 256;
        Memory buffer = new Memory(bufChars * 2L);
        int len = INSTANCE.GetCompositionString(buffer, bufChars);
        return len <= 0 ? "" : buffer.getWideString(0);
    }

    public static int getCandidateCount() {
        return isAvailable() ? INSTANCE.GetCandidateCount() : 0;
    }

    public static String getCandidate(int index) {
        if (!isAvailable()) return "";
        int bufChars = 64;
        Memory buffer = new Memory(bufChars * 2L);
        int len = INSTANCE.GetCandidate(index, buffer, bufChars);
        return len <= 0 ? "" : buffer.getWideString(0);
    }

    public static List<String> getCandidates() {
        List<String> result = new ArrayList<>();
        if (!isAvailable()) return result;

        int count = INSTANCE.GetCandidateCount();
        for (int i = 0; i < count && i < 20; i++) {
            String cand = getCandidate(i);
            if (!cand.isEmpty()) {
                result.add(cand);
            }
        }
        return result;
    }

    public static int getSelectedCandidateIndex() {
        return isAvailable() ? INSTANCE.GetSelectedCandidateIndex() : 0;
    }

    public static boolean getImeOpenStatus() {
        return isAvailable() && INSTANCE.GetImeOpenStatus() == 1;
    }

    public static boolean getTsfChineseMode() {
        return isAvailable() && INSTANCE.GetTsfChineseMode() == 1;
    }

    public static void refreshImeState() {
        if (isAvailable()) {
            INSTANCE.RefreshImeState();
        }
    }

    public static boolean getShiftMode() {
        return isAvailable() && INSTANCE.GetShiftMode() == 1;
    }

    public static boolean getCapsLockState() {
        return isAvailable() && INSTANCE.GetCapsLockState() == 1;
    }

    public static boolean isKeyPressed(int vKey) {
        return isAvailable() && INSTANCE.GetKeyboardStateForPolling(vKey) == 1;
    }

    public static int getInputMethodType() {
        return isAvailable() ? INSTANCE.GetInputMethodType() : 0;
    }

    public static void setEventCallbacks(
            PreeditCallback preedit,
            CommitCallback commit,
            CandidateCallback candidate,
            ImeChangeCallback imeChange,
            KeyboardCallback keyboard) {
        if (!isAvailable()) return;
        INSTANCE.SetEventCallbacks(preedit, commit, candidate, imeChange, keyboard);
    }

    public static void hookWindowProc(long hwnd) {
        if (!isAvailable()) return;
        Pointer hwndPtr = hwnd != 0L ? Pointer.createConstant(hwnd) : null;
        INSTANCE.HookWindowProc(hwndPtr);
    }

    public static void unhookWindowProc() {
        if (isAvailable()) {
            INSTANCE.UnhookWindowProc();
        }
    }

    public static void refreshCandidates() {
        if (isAvailable()) {
            INSTANCE.RefreshCandidates();
        }
    }

    public static boolean isWindowHooked() {
        return isAvailable() && INSTANCE.IsWindowHooked() == 1;
    }

    public static InputMode getInputMethodTypeAsEnum() {
        return getInputMethodTypeAsEnum(getInputMethodType());
    }

    public static InputMode getInputMethodTypeAsEnum(int type) {
        return switch (type) {
            case IME_TYPE_ENGLISH -> InputMode.LATIN;
            case IME_TYPE_PINYIN -> InputMode.PINYIN;
            case IME_TYPE_ZHUYIN -> InputMode.ZHUYIN;
            case IME_TYPE_CANGJIE -> InputMode.CANGJIE;
            case IME_TYPE_WUBI -> InputMode.WUBI;
            case IME_TYPE_SUCHENG -> InputMode.SUCHENG;
            case IME_TYPE_OTHER_CHINESE -> InputMode.OTHER;
            default -> InputMode.OTHER;
        };
    }

    public static String getInputMethodTypeString(int type) {
        return switch (type) {
            case IME_TYPE_ENGLISH -> "En";
            case IME_TYPE_PINYIN -> "拼";
            case IME_TYPE_ZHUYIN -> "注";
            case IME_TYPE_CANGJIE -> "倉";
            case IME_TYPE_WUBI -> "五";
            case IME_TYPE_SUCHENG -> "速";
            case IME_TYPE_OTHER_CHINESE -> "中";
            default -> "?";
        };
    }

    public static String getInputMethodTypeString() {
        return getInputMethodTypeString(getInputMethodType());
    }

    public static long getGlfwWin32Window(long glfwWindow) {
        if (glfwWindow == 0) return 0;
        try {
            com.sun.jna.NativeLibrary glfw = com.sun.jna.NativeLibrary.getInstance("glfw");
            com.sun.jna.Function func = glfw.getFunction("glfwGetWin32Window");
            if (func != null) {
                Object result = func.invoke(long.class, new Object[]{glfwWindow});
                if (result != null) {
                    return (Long) result;
                }
            }
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] glfwGetWin32Window not available: {}", e.getMessage());
        }
        return glfwWindow;
    }

    public interface NativeLibrary extends StdCallLibrary {
        String GetDllVersion();
        int StartListen(Pointer hwnd);
        void StopListen();
        int IsListening();
        int IsChineseMode();
        int HasLayoutChanged();
        int StartTsfListen();
        void StopTsfListen();
        int IsTsfListening();
        int GetCompositionString(Pointer buffer, int bufferSize);
        int GetCandidateCount();
        int GetCandidate(int index, Pointer buffer, int bufferSize);
        int GetSelectedCandidateIndex();
        int GetImeOpenStatus();
        int GetTsfChineseMode();
        int HasTsfLayoutChanged();
        int GetInputMethodType();
        int GetShiftMode();
        int GetCapsLockState();
        int GetKeyboardStateForPolling(int vKey);
        void RefreshImeState();
        void FreeBuffer(Pointer ptr);
        long GetKeyboardLayoutHKL();
        void SetCallbacks(CandidateUpdateCallback candidateUpdate,
                          LayoutChangeCallback layoutChange,
                          ModeChangeCallback modeChange,
                          KeyboardStateCallback keyboardState);

        void SetEventCallbacks(
            PreeditCallback preedit,
            CommitCallback commit,
            CandidateCallback candidate,
            ImeChangeCallback imeChange,
            KeyboardCallback keyboard);
        void HookWindowProc(Pointer hwnd);
        void UnhookWindowProc();
        void RefreshCandidates();
        int IsWindowHooked();
    }

    public interface PreeditCallback extends Callback {
        void invoke(WString text, int cursorPos, int selStart, int selLen);
    }

    public interface CommitCallback extends Callback {
        void invoke(WString text);
    }

    public interface CandidateCallback extends Callback {
        void invoke(Pointer candidates, int count, int selectedIndex);
    }

    public interface ImeChangeCallback extends Callback {
        void invoke(int inputMethodType, int chineseMode);
    }

    public interface KeyboardCallback extends Callback {
        void invoke(int capsLock, int shiftMode);
    }
}