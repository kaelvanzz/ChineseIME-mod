package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.sun.jna.Callback;
import com.sun.jna.Memory;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.win32.StdCallLibrary;
import com.sun.jna.win32.W32APIOptions;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

public class NativeImeBridge {

    public static final int IME_TYPE_UNKNOWN = 0;
    public static final int IME_TYPE_ENGLISH = 1;
    public static final int IME_TYPE_PINYIN = 2;
    public static final int IME_TYPE_ZHUYIN = 3;
    public static final int IME_TYPE_CANGJIE = 4;
    public static final int IME_TYPE_WUBI = 5;
    public static final int IME_TYPE_SUCHENG = 6;
    public static final int IME_TYPE_SOGOU = 7;
    public static final int IME_TYPE_OTHER_CHINESE = 99;

    private static NativeLibrary INSTANCE = null;
    private static boolean loaded = false;
    private static boolean loadAttempted = false;
    private static Path cachedDllPath = null;

    public static NativeLibrary getInstance() {
        return INSTANCE;
    }

    private static String cachedComposition = "";
    private static List<String> cachedCandidates = new CopyOnWriteArrayList<>();
    private static int cachedSelectedIndex = 0;
    private static int cachedImeType = IME_TYPE_ENGLISH;
    private static int cachedChineseMode = 0;

    private static ImeCallback listener = null;

    public interface ImeCallback {
        void onPreedit(String composition, int cursor, int selLen);
        void onCommit(String text);
        void onCandidates(List<String> candidates, int selectedIndex);
        void onImeChange(int imeType, int chineseMode);
    }

    public static void setCallback(ImeCallback cb) {
        listener = cb;
    }

    public static String getCachedComposition() {
        return cachedComposition;
    }

    public static List<String> getCachedCandidates() {
        return new ArrayList<>(cachedCandidates);
    }

    public static int getCachedSelectedIndex() {
        return cachedSelectedIndex;
    }

    public static int getCachedImeType() {
        return cachedImeType;
    }

    public static int getCachedChineseMode() {
        return cachedChineseMode;
    }

    public static boolean isAvailable() {
        if (!loadAttempted) {
            loadAttempted = true;
            loadNative();
        }
        return loaded && INSTANCE != null;
    }

    private static synchronized void loadNative() {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Loading native library...");
        try {
            String osArch = System.getProperty("os.arch");
            String nativesPath = "/META-INF/natives/" + osArch + "/chineseime_native.dll";
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Looking for DLL at: {}", nativesPath);

            InputStream dllStream = NativeImeBridge.class.getResourceAsStream(nativesPath);
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] DLL stream: {}", dllStream != null ? "found" : "null");
            if (dllStream == null) {
                ChineseIMEInitializer.LOGGER.warn("[ChineseIME] DLL not found in JAR at {}", nativesPath);
                loaded = false;
                return;
            }

            Path tempDir = Files.createTempDirectory("chineseime_native");
            cachedDllPath = tempDir.resolve("chineseime_native.dll");
            Files.copy(dllStream, cachedDllPath, StandardCopyOption.REPLACE_EXISTING);
            dllStream.close();
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] DLL extracted to: {}", cachedDllPath);

            INSTANCE = Native.load(cachedDllPath.toString(), NativeLibrary.class, W32APIOptions.UNICODE_OPTIONS);
            loaded = true;
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] DLL loaded successfully");

            String version = INSTANCE.GetDllVersion();
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] DLL version: {}", version);
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] DLL load failed: {} - using fallback", e.getMessage());
            e.printStackTrace();
            loaded = false;
        }
    }

    public interface PreeditCallback extends Callback {
        void invoke(Pointer text, int cursor, int selLen);
    }

    public interface CommitCallback extends Callback {
        void invoke(Pointer text);
    }

    public interface CandidatesCallback extends Callback {
        void invoke(Pointer candidates, int count, int selectedIndex);
    }

    public interface ImeChangeCallback extends Callback {
        void invoke(int imeType, int chineseMode);
    }

    public static void hookWindowProc(long hwnd) {
        if (!isAvailable()) return;
        try {
            int result = INSTANCE.HookWindowProcRaw(hwnd);
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] HookWindowProcRaw result: {}", result);
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] HookWindowProc failed: {}", e.getMessage());
        }
    }

    public static void registerCallbacks(PreeditCallback preedit, CommitCallback commit,
            CandidatesCallback candidates, ImeChangeCallback imeChange) {
        if (!isAvailable()) return;
        try {
            INSTANCE.SetEventCallbacks(preedit, commit, candidates, imeChange);
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] IME callbacks registered");
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Failed to register callbacks: {}", e.getMessage());
        }
    }

    public static void unhookWindowProc() {
        if (isAvailable()) {
            INSTANCE.UnhookWindowProc();
        }
    }

    public static boolean isWindowHooked() {
        return isAvailable() && INSTANCE.IsWindowHooked() == 1;
    }

    public static void refreshCandidates() {
        if (isAvailable()) {
            INSTANCE.RefreshCandidates();
        }
    }

    public static String getCompositionString() {
        if (!isAvailable()) return "";
        char[] buffer = new char[256];
        int len = INSTANCE.GetCompositionString(buffer, buffer.length);
        return len <= 0 ? "" : new String(buffer, 0, len);
    }

    public static int getCandidateCount() {
        return isAvailable() ? INSTANCE.GetCandidateCount() : 0;
    }

    public static String getCandidate(int index) {
        if (!isAvailable()) return "";
        char[] buffer = new char[64];
        int len = INSTANCE.GetCandidate(index, buffer, buffer.length);
        return len <= 0 ? "" : new String(buffer, 0, len);
    }

    public static List<String> getCandidates() {
        List<String> result = new ArrayList<>();
        if (!isAvailable()) return result;
        int count = getCandidateCount();
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

    public static boolean isChineseMode() {
        return isAvailable() && INSTANCE.GetChineseMode() == 1;
    }

    public static boolean getShiftMode() {
        return isAvailable() && INSTANCE.GetShiftMode() == 1;
    }

    public static boolean getCapsLockState() {
        return isAvailable() && INSTANCE.GetCapsLockState() == 1;
    }

    public static int getInputMethodType() {
        return isAvailable() ? INSTANCE.GetInputMethodType() : 0;
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
            case IME_TYPE_SOGOU -> InputMode.SOGOU;
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
            case IME_TYPE_SOGOU -> "搜";
            default -> "?";
        };
    }

    public static String getInputMethodTypeString() {
        return getInputMethodTypeString(getInputMethodType());
    }

    public interface NativeLibrary extends StdCallLibrary {
        String GetDllVersion();
        int HookWindowProc(Pointer hwnd);
        int HookWindowProcRaw(long hwnd);
        int InstallMessageHook(long hwnd);
        void UnhookWindowProc();
        int IsWindowHooked();
        int IsWindowValid(long hwnd);
        int GetCompositionString(char[] buffer, int bufferSize);
        int GetCandidateCount();
        int GetCandidate(int index, char[] buffer, int bufferSize);
        int GetSelectedCandidateIndex();
        int GetImeOpenStatus();
        int GetChineseMode();
        int GetShiftMode();
        int GetCapsLockState();
        int GetInputMethodType();
        void RefreshCandidates();
        void SetEventCallbacks(PreeditCallback preedit, CommitCallback commit,
                               CandidatesCallback candidates, ImeChangeCallback imeChange);
    }
}