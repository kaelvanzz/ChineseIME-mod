package com.example.chineseime.platform.linux;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.sun.jna.Callback;
import com.sun.jna.Library;
import com.sun.jna.Memory;
import com.sun.jna.Native;
import com.sun.jna.Pointer;

import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

public class NativeLinuxBridge {

    public interface PreeditCallback extends Callback {
        void invoke(String text, int cursorPos, int selStart, int selLen);
    }

    public interface CommitCallback extends Callback {
        void invoke(String text);
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

    private static NativeLibrary INSTANCE = null;
    private static boolean loaded = false;
    private static boolean loadAttempted = false;
    private static final Object LOAD_LOCK = new Object();

    public static final int IME_TYPE_UNKNOWN = 0;
    public static final int IME_TYPE_ENGLISH = 1;
    public static final int IME_TYPE_PINYIN = 2;
    public static final int IME_TYPE_ZHUYIN = 3;
    public static final int IME_TYPE_CANGJIE = 4;
    public static final int IME_TYPE_WUBI = 5;
    public static final int IME_TYPE_SUCHENG = 6;
    public static final int IME_TYPE_RIME = 7;
    public static final int IME_TYPE_OTHER_CHINESE = 99;

    public static boolean isAvailable() {
        if (!loadAttempted) {
            getInstance();
        }
        return loaded && INSTANCE != null;
    }

    private static Path cachedLibPath = null;

    public static synchronized NativeLibrary getInstance() {
        if (!loadAttempted) {
            loadAttempted = true;
            loadNative();
        }
        return INSTANCE;
    }

    private static void loadNative() {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Loading native library...");
        try {
            String osArch = System.getProperty("os.arch");
            String libName = System.mapLibraryName("chineseime_native_linux");
            String nativesPath = "/META-INF/natives/" + osArch + "/" + libName;
            ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Looking for library at: {}", nativesPath);

            if (cachedLibPath == null) {
                InputStream libStream = NativeLinuxBridge.class.getResourceAsStream(nativesPath);
                ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Library stream: {}", libStream != null ? "found" : "null");

                if (libStream != null) {
                    Path tempDir = Files.createTempDirectory("chineseime_native");
                    cachedLibPath = tempDir.resolve(libName);
                    Files.copy(libStream, cachedLibPath, StandardCopyOption.REPLACE_EXISTING);
                    libStream.close();

                    File f = cachedLibPath.toFile();
                    f.setExecutable(true, false);

                    ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Library extracted to: {}", cachedLibPath);
                } else {
                    ChineseIMEInitializer.LOGGER.warn("[ChineseIME-Linux] Library not found in JAR at {}", nativesPath);
                    loaded = false;
                    return;
                }
            }

            INSTANCE = Native.load(cachedLibPath.toString(), NativeLibrary.class);
            loaded = true;
            ChineseIMEInitializer.LOGGER.info("[ChineseIME-Linux] Library loaded successfully");
        } catch (Exception e) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME-Linux] Library load failed: {} - using fallback", e.getMessage());
            e.printStackTrace();
            loaded = false;
        }
    }

    public static int startListening() {
        if (!isAvailable()) return 0;
        return INSTANCE.LinuxStartListen();
    }

    public static void stopListening() {
        if (isAvailable()) {
            INSTANCE.LinuxStopListen();
        }
    }

    public static boolean isListening() {
        return isAvailable() && INSTANCE.IsLinuxListening() == 1;
    }

    public static boolean isChineseMode() {
        return isAvailable() && INSTANCE.LinuxIsChineseMode() == 1;
    }

    public static int getInputMethodType() {
        return isAvailable() ? INSTANCE.LinuxGetInputMethodType() : 0;
    }

    public static boolean getImeOpenStatus() {
        return isAvailable() && INSTANCE.LinuxGetImeOpenStatus() == 1;
    }

    public static boolean getCapsLockState() {
        return isAvailable() && INSTANCE.LinuxGetCapsLockState() == 1;
    }

    public static boolean getShiftMode() {
        return isAvailable() && INSTANCE.LinuxGetShiftMode() == 1;
    }

    public static String getCompositionString() {
        if (!isAvailable()) return "";
        int bufChars = 256;
        Memory buffer = new Memory(bufChars * 2L);
        int len = INSTANCE.LinuxGetCompositionString(buffer, bufChars);
        return len <= 0 ? "" : buffer.getWideString(0);
    }

    public static int getCandidateCount() {
        return isAvailable() ? INSTANCE.LinuxGetCandidateCount() : 0;
    }

    public static String getCandidate(int index) {
        if (!isAvailable()) return "";
        int bufChars = 64;
        Memory buffer = new Memory(bufChars * 2L);
        int len = INSTANCE.LinuxGetCandidate(index, buffer, bufChars);
        return len <= 0 ? "" : buffer.getWideString(0);
    }

    public static List<String> getCandidates() {
        List<String> result = new ArrayList<>();
        if (!isAvailable()) return result;

        int count = INSTANCE.LinuxGetCandidateCount();
        for (int i = 0; i < count && i < 20; i++) {
            String cand = getCandidate(i);
            if (!cand.isEmpty()) {
                result.add(cand);
            }
        }
        return result;
    }

    public static int getSelectedCandidateIndex() {
        return isAvailable() ? INSTANCE.LinuxGetSelectedCandidateIndex() : 0;
    }

    public static void setEventCallbacks(
            PreeditCallback preedit,
            CommitCallback commit,
            CandidateCallback candidate,
            ImeChangeCallback imeChange,
            KeyboardCallback keyboard) {
        if (!isAvailable()) return;
        INSTANCE.LinuxSetEventCallbacks(preedit, commit, candidate, imeChange, keyboard);
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
            case IME_TYPE_RIME -> InputMode.RIME;
            case IME_TYPE_OTHER_CHINESE -> InputMode.OTHER;
            default -> InputMode.OTHER;
        };
    }

    public interface NativeLibrary extends Library {
        int LinuxStartListen();
        void LinuxStopListen();
        int IsLinuxListening();

        int LinuxIsChineseMode();
        int LinuxGetInputMethodType();
        int LinuxGetImeOpenStatus();
        int LinuxGetCapsLockState();
        int LinuxGetShiftMode();

        int LinuxGetCompositionString(Pointer buffer, int bufferSize);
        int LinuxGetCandidateCount();
        int LinuxGetCandidate(int index, Pointer buffer, int bufferSize);
        int LinuxGetSelectedCandidateIndex();

        void LinuxSetEventCallbacks(
            PreeditCallback preedit,
            CommitCallback commit,
            CandidateCallback candidate,
            ImeChangeCallback imeChange,
            KeyboardCallback keyboard);
    }
}