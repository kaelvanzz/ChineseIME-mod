package com.example.chineseime;

import com.example.chineseime.config.ModConfig;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.hud.VerticalCandidateHud;
import com.example.chineseime.keybind.KeyBindingManager;
import com.example.chineseime.platform.PlatformIMEManager;
import com.example.chineseime.platform.win32.NativeImeBridge;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.api.EnvType;
import net.fabricmc.api.Environment;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import org.lwjgl.glfw.GLFW;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Environment(EnvType.CLIENT)
public class ChineseIMEInitializer implements ClientModInitializer {
    public static final String MOD_ID = "chineseime";
    public static final Logger LOGGER = LoggerFactory.getLogger("chineseime");
    private static ChineseIMEInitializer instance;
    private ModConfig config;
    private CandidateHud candidateHud;
    private VerticalCandidateHud verticalCandidateHud;
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

    @Override
    public void onInitializeClient() {
        instance = this;
        LOGGER.info("[ChineseIME] Initializing...");
        this.config = ModConfig.load();
        this.candidateHud = new CandidateHud();
        this.verticalCandidateHud = new VerticalCandidateHud();
        this.statusIndicator = new ImeStatusIndicator();
        this.imeManager = null;
        this.keyBindingManager = new KeyBindingManager(this.config, null);
        this.keyBindingManager.register();

        ClientTickEvents.START_CLIENT_TICK.register(client -> {
            if (this.imeManager == null && PlatformIMEManager.getPlatform() == PlatformIMEManager.OS.WINDOWS) {
                long windowHandle = client.getWindow().getHandle();
                if (windowHandle != 0) {
                    long nativeHandle = NativeImeBridge.getGlfwWin32Window(windowHandle);
                    LOGGER.info("[ChineseIME] Got native window handle: {}", String.format("0x%X", nativeHandle));

                    this.imeManager = new PlatformIMEManager(this.config, this.candidateHud, this.verticalCandidateHud, this.statusIndicator, nativeHandle);
                    this.keyBindingManager = new KeyBindingManager(this.config, this.imeManager);
                    this.keyBindingManager.register();

                    LOGGER.info("[ChineseIME] IME Manager initialized, syncEnabled={}", this.imeManager.isWindowsSync());
                }
            }
        });

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            if (this.imeManager != null) {
                this.imeManager.tick();
            }

            long windowHandle = client.getWindow().getHandle();
            boolean leftPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_LEFT) == GLFW.GLFW_PRESS;
            boolean rightPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_RIGHT) == GLFW.GLFW_PRESS;
            boolean upPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_UP) == GLFW.GLFW_PRESS;
            boolean downPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_DOWN) == GLFW.GLFW_PRESS;

            if (this.imeManager != null && this.imeManager.hasInput()) {
                if ((leftPressed && !prevLeftPressed) || (upPressed && !prevUpPressed)) {
                    this.imeManager.selectPrev();
                } else if ((rightPressed && !prevRightPressed) || (downPressed && !prevDownPressed)) {
                    this.imeManager.selectNext();
                }
            }

            prevLeftPressed = leftPressed;
            prevRightPressed = rightPressed;
            prevUpPressed = upPressed;
            prevDownPressed = downPressed;

            boolean ctrl = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_LEFT_CONTROL) == GLFW.GLFW_PRESS ||
                    GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_RIGHT_CONTROL) == GLFW.GLFW_PRESS;
            boolean shift = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_LEFT_SHIFT) == GLFW.GLFW_PRESS ||
                    GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_RIGHT_SHIFT) == GLFW.GLFW_PRESS;
            if (ctrl && shift && GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_T) == GLFW.GLFW_PRESS) {
                if (!this.ctrlShiftTPressed) {
                    if (this.imeManager != null) {
                        this.imeManager.showTestCandidates();
                    }
                    this.ctrlShiftTPressed = true;
                }
            } else {
                this.ctrlShiftTPressed = false;
            }

            boolean bracketLeftPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_LEFT_BRACKET) == GLFW.GLFW_PRESS;
            boolean bracketRightPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_RIGHT_BRACKET) == GLFW.GLFW_PRESS;
            if (this.imeManager != null && this.imeManager.hasInput()) {
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
            prevBracketLeftPressed = bracketLeftPressed;
            prevBracketRightPressed = bracketRightPressed;
        });

        LOGGER.info("[ChineseIME] Initialization complete! Platform: {}", PlatformIMEManager.getPlatform());
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

    public VerticalCandidateHud getVerticalCandidateHud() {
        return this.verticalCandidateHud;
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
        if (this.imeManager != null) {
            this.imeManager.renderHud(ctx);
        }
    }
}