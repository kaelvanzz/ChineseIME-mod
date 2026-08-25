package com.example.chineseime;

import com.example.chineseime.config.ModConfig;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.keybind.KeyBindingManager;
import com.example.chineseime.platform.PlatformIMEManager;
import com.example.chineseime.platform.win32.WindowsIMEBridgeNative;
import com.sun.jna.Function;
import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.platform.win32.WinDef;
import com.sun.jna.platform.win32.WinDef.HWND;
import com.sun.jna.platform.win32.WinUser;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.api.EnvType;
import net.fabricmc.api.Environment;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.ChatScreen;
import org.lwjgl.glfw.GLFW;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;

@Environment(EnvType.CLIENT)
public class ChineseIMEInitializer implements ClientModInitializer {
    public static final String MOD_ID = "chineseime";
    public static final Logger LOGGER = LoggerFactory.getLogger("chineseime");
    private static ChineseIMEInitializer instance;
    private ModConfig config;
    private CandidateHud candidateHud;
    private ImeStatusIndicator statusIndicator;
    private PlatformIMEManager imeManager;
    private KeyBindingManager keyBindingManager;
    private boolean ctrlShiftTPressed = false;
    private boolean prevLeftPressed = false;
    private boolean prevRightPressed = false;
    private boolean prevUpPressed = false;
    private boolean prevDownPressed = false;
    private boolean prevBracketLeftPressed = false;
    private boolean prevBracketRightPressed = false;
    private boolean windowHookAttempted = false;
    private boolean windowHooked = false;
    private long lastKnownHwnd = 0;
    private int tickCounter = 0;

    public interface WINDOWS_API extends Library {
        WINDOWS_API INSTANCE = Native.load("user32", WINDOWS_API.class);

        interface WNDENUMPROC extends com.sun.jna.Callback {
            boolean callback(HWND hWnd, Pointer userData);
        }

        boolean EnumWindows(WNDENUMPROC lpEnumFunc, Pointer userData);
        int GetWindowTextA(HWND hWnd, char[] lpString, int nMaxCount);
        int GetWindowTextLength(HWND hWnd);
        void GetWindowText(HWND hWnd, char[] lpString, int nMaxCount);
        com.sun.jna.platform.win32.WinDef.DWORD GetWindowThreadProcessId(HWND hWnd, com.sun.jna.platform.win32.WinDef.DWORDByReference lpdwProcessId);
        HWND GetForegroundWindow();
        HWND GetActiveWindow();
    }

    @Override
    public void onInitializeClient() {
        instance = this;
        LOGGER.info("[ChineseIME] Initializing...");
        this.config = ModConfig.load();
        this.candidateHud = new CandidateHud();
        this.statusIndicator = new ImeStatusIndicator();
        this.imeManager = new PlatformIMEManager(this.config, this.candidateHud, this.statusIndicator);
        this.keyBindingManager = new KeyBindingManager(this.config, this.imeManager);
        this.keyBindingManager.register();

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            tickCounter++;
            if (tickCounter % 400 == 0) {
                config.flush();
            }

            if (!windowHookAttempted && client.isRunning()) {
                windowHookAttempted = true;
                if (PlatformIMEManager.getPlatform() == PlatformIMEManager.OS.WINDOWS) {
                    long glfwWindow = client.getWindow().getHandle();
                    LOGGER.info("[ChineseIME] GLFW window handle: {}", glfwWindow);

                    long hwnd = findMinecraftWindow();
                    LOGGER.info("[ChineseIME] Found Win32 HWND via EnumWindows: {}", hwnd);

                    lastKnownHwnd = hwnd;
                    if (hwnd != 0) {
                        Object bridge = imeManager.getWindowsBridge();
                        if (bridge instanceof WindowsIMEBridgeNative) {
                            ChineseIMEInitializer.LOGGER.info("[ChineseIME] About to call hookWindow with hwnd={}", hwnd);
                            windowHooked = ((WindowsIMEBridgeNative) bridge).hookWindow(hwnd);
                            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Window hook result: {}", windowHooked);
                        } else {
                            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] bridge is not WindowsIMEBridgeNative");
                        }
                    } else {
                        ChineseIMEInitializer.LOGGER.warn("[ChineseIME] HWND is 0, cannot hook window");
                    }
                }
            }

            this.keyBindingManager.tick();
            this.imeManager.tick();

            long window = client.getWindow().getHandle();
            boolean leftPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_LEFT) == GLFW.GLFW_PRESS;
            boolean rightPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_RIGHT) == GLFW.GLFW_PRESS;
            boolean upPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_UP) == GLFW.GLFW_PRESS;
            boolean downPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_DOWN) == GLFW.GLFW_PRESS;
            boolean bracketLeftPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_LEFT_BRACKET) == GLFW.GLFW_PRESS;
            boolean bracketRightPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_RIGHT_BRACKET) == GLFW.GLFW_PRESS;

            if (this.imeManager.hasInput()) {
                if ((leftPressed && !prevLeftPressed) || (upPressed && !prevUpPressed)) {
                    this.imeManager.selectPrev();
                } else if ((rightPressed && !prevRightPressed) || (downPressed && !prevDownPressed)) {
                    this.imeManager.selectNext();
                }
                if (bracketLeftPressed && !prevBracketLeftPressed) {
                    this.imeManager.prevPage();
                } else if (bracketRightPressed && !prevBracketRightPressed) {
                    this.imeManager.nextPage();
                }
            }

            prevLeftPressed = leftPressed;
            prevRightPressed = rightPressed;
            prevUpPressed = upPressed;
            prevDownPressed = downPressed;
            prevBracketLeftPressed = bracketLeftPressed;
            prevBracketRightPressed = bracketRightPressed;

            boolean ctrl = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_LEFT_CONTROL) == GLFW.GLFW_PRESS ||
                    GLFW.glfwGetKey(window, GLFW.GLFW_KEY_RIGHT_CONTROL) == GLFW.GLFW_PRESS;
            boolean shift = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_LEFT_SHIFT) == GLFW.GLFW_PRESS ||
                    GLFW.glfwGetKey(window, GLFW.GLFW_KEY_RIGHT_SHIFT) == GLFW.GLFW_PRESS;
            if (ctrl && shift && GLFW.glfwGetKey(window, GLFW.GLFW_KEY_T) == GLFW.GLFW_PRESS) {
                if (!this.ctrlShiftTPressed) {
                    this.imeManager.showTestCandidates();
                    this.ctrlShiftTPressed = true;
                }
            } else {
                this.ctrlShiftTPressed = false;
            }
        });

        LOGGER.info("[ChineseIME] Initialization complete! Platform: {}, isWindowsSync: {}", PlatformIMEManager.getPlatform(), this.imeManager.isWindowsSync());
    }

    private long findMinecraftWindow() {
        try {
            int currentPid = (int) ProcessHandle.current().pid();
            LOGGER.info("[ChineseIME] findMinecraftWindow: current PID = {}", currentPid);

            final int targetPid = currentPid;
            final List<Long> foundHwnds = new ArrayList<>();
            final long[] minecraftWindowHwnd = {0};

            WINDOWS_API.WNDENUMPROC callback = new WINDOWS_API.WNDENUMPROC() {
                public boolean callback(HWND hWnd, Pointer userData) {
                    try {
                        WinDef.DWORDByReference processIdRef = new WinDef.DWORDByReference();
                        WINDOWS_API.INSTANCE.GetWindowThreadProcessId(hWnd, processIdRef);
                        int processId = (int)processIdRef.getValue().longValue();
                        long hwndVal = Pointer.nativeValue(hWnd.getPointer());
                        int length = WINDOWS_API.INSTANCE.GetWindowTextLength(hWnd);
                        String titleStr = "";
                        if (length > 0) {
                            char[] title = new char[length + 1];
                            WINDOWS_API.INSTANCE.GetWindowText(hWnd, title, length + 1);
                            titleStr = new String(title, 0, length);
                        }
                        LOGGER.debug("[ChineseIME] Enum callback: title='{}', hwnd={}, procId={}, targetPid={}, match={}",
                            titleStr, hwndVal, processId, targetPid, processId == targetPid);

                        if (processId == targetPid) {
                            if (titleStr.contains("Minecraft")) {
                                minecraftWindowHwnd[0] = hwndVal;
                                LOGGER.info("[ChineseIME] Found Minecraft window: hwnd={}", hwndVal);
                            }
                            foundHwnds.add(hwndVal);
                        }
                    } catch (Exception e) {
                        LOGGER.warn("[ChineseIME] Enum callback exception: {}", e.getMessage());
                    }
                    return true;
                }
            };

            LOGGER.info("[ChineseIME] Starting EnumWindows...");
            WINDOWS_API.INSTANCE.EnumWindows(callback, null);
            LOGGER.info("[ChineseIME] EnumWindows complete. Found {} windows in target process", foundHwnds.size());

            if (minecraftWindowHwnd[0] != 0) {
                LOGGER.info("[ChineseIME] Using Minecraft window HWND: {}", minecraftWindowHwnd[0]);
                return minecraftWindowHwnd[0];
            }

            if (!foundHwnds.isEmpty()) {
                long hwnd = foundHwnds.get(foundHwnds.size() - 1);
                LOGGER.info("[ChineseIME] Using last HWND from enum: {} (of {} windows)", hwnd, foundHwnds.size());
                return hwnd;
            }

            HWND foreground = WINDOWS_API.INSTANCE.GetForegroundWindow();
            if (foreground != null) {
                long hwnd = Pointer.nativeValue(foreground.getPointer());
                LOGGER.info("[ChineseIME] Using foreground window HWND: {}", hwnd);
                return hwnd;
            }

            HWND active = WINDOWS_API.INSTANCE.GetActiveWindow();
            if (active != null) {
                long hwnd = Pointer.nativeValue(active.getPointer());
                LOGGER.info("[ChineseIME] Using active window HWND: {}", hwnd);
                return hwnd;
            }

        } catch (Exception e) {
            LOGGER.warn("[ChineseIME] findMinecraftWindow failed: {}", e.getMessage());
            e.printStackTrace();
        }
        return 0;
    }

    public static ChineseIMEInitializer getInstance() {
        return instance;
    }

    public ModConfig getConfig() {
        return this.config;
    }

    public CandidateHud getCandidateHud() {
        return this.candidateHud;
    }

    public ImeStatusIndicator getStatusIndicator() {
        return this.statusIndicator;
    }

    public PlatformIMEManager getImeManager() {
        return this.imeManager;
    }

    public void renderHud(DrawContext ctx) {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null) return;

        this.statusIndicator.render(ctx);
        this.imeManager.renderHud(ctx);
    }
}