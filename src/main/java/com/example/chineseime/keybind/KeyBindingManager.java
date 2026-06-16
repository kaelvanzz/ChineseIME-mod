package com.example.chineseime.keybind;

import com.example.chineseime.config.ConfigScreen;
import com.example.chineseime.config.ModConfig;
import com.example.chineseime.platform.PlatformIMEManager;
import net.fabricmc.fabric.api.client.keybinding.v1.KeyBindingHelper;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.option.KeyBinding;
import net.minecraft.client.util.InputUtil;
import org.lwjgl.glfw.GLFW;

import java.util.List;

public class KeyBindingManager {
    private final ModConfig config;
    private final PlatformIMEManager ime;
    private KeyBinding toggleChinese;
    private KeyBinding toggleScript;
    private KeyBinding openConfig;
    private boolean ctrlShiftFPressed = false;
    private boolean prevToggleImePressed = false;
    private boolean prevToggleModePressed = false;
    private boolean prevOpenConfigPressed = false;

    public KeyBindingManager(ModConfig config, PlatformIMEManager ime) {
        this.config = config;
        this.ime = ime;
    }

    public void register() {
        String cat = "key.categories.chineseime";
        this.toggleChinese = KeyBindingHelper.registerKeyBinding(new KeyBinding(
                "key.chineseime.toggle_chinese",
                InputUtil.Type.KEYSYM,
                GLFW.GLFW_KEY_COMMA,
                cat
        ));
        this.toggleScript = KeyBindingHelper.registerKeyBinding(new KeyBinding(
                "key.chineseime.toggle_script",
                InputUtil.Type.KEYSYM,
                -1,
                cat
        ));
        this.openConfig = KeyBindingHelper.registerKeyBinding(new KeyBinding(
                "key.chineseime.open_config",
                InputUtil.Type.KEYSYM,
                GLFW.GLFW_KEY_G,
                cat
        ));
    }

    public void tick() {
        MinecraftClient mc = MinecraftClient.getInstance();
        long win = mc.getWindow().getHandle();

        if (this.toggleChinese.wasPressed()) {
            this.ime.toggleChineseMode();
        }

        boolean ctrl = isCtrlPressed(win);
        boolean shift = isShiftPressed(win);

        if (ctrl && shift && GLFW.glfwGetKey(win, GLFW.GLFW_KEY_F) == GLFW.GLFW_PRESS) {
            if (!this.ctrlShiftFPressed) {
                this.ime.toggleScriptType();
                this.ctrlShiftFPressed = true;
            }
        } else {
            this.ctrlShiftFPressed = false;
        }

        if (ctrl && GLFW.glfwGetKey(win, GLFW.GLFW_KEY_G) == GLFW.GLFW_PRESS) {
            if (!this.prevOpenConfigPressed) {
                if (mc.currentScreen instanceof ConfigScreen) {
                    mc.currentScreen.close();
                } else {
                    mc.setScreen(new ConfigScreen(mc.currentScreen, this.config));
                }
                this.prevOpenConfigPressed = true;
            }
        } else {
            this.prevOpenConfigPressed = false;
        }

        if (this.openConfig.wasPressed()) {
            if (mc.currentScreen instanceof ConfigScreen) {
                mc.currentScreen.close();
            } else {
                mc.setScreen(new ConfigScreen(mc.currentScreen, this.config));
            }
        }

        List<Integer> toggleImeKeys = this.config.getToggleImeKeys();
        if (!toggleImeKeys.isEmpty()) {
            boolean allPressed = true;
            for (int key : toggleImeKeys) {
                if (GLFW.glfwGetKey(win, key) != GLFW.GLFW_PRESS) {
                    allPressed = false;
                    break;
                }
            }
            if (allPressed && !this.prevToggleImePressed) {
                this.ime.toggleInputMethod();
            }
            this.prevToggleImePressed = allPressed;
        }

        if (!PlatformIMEManager.getPlatform().equals(PlatformIMEManager.OS.WINDOWS)) {
            List<Integer> toggleModeKeys = this.config.getToggleChineseModeKeys();
            if (!toggleModeKeys.isEmpty()) {
                boolean allPressed = true;
                for (int key : toggleModeKeys) {
                    if (GLFW.glfwGetKey(win, key) != GLFW.GLFW_PRESS) {
                        allPressed = false;
                        break;
                    }
                }
                if (allPressed && !this.prevToggleModePressed) {
                    this.ime.toggleChineseMode();
                }
                this.prevToggleModePressed = allPressed;
            }
        }
    }

    private boolean isCtrlPressed(long win) {
        return GLFW.glfwGetKey(win, GLFW.GLFW_KEY_LEFT_CONTROL) == GLFW.GLFW_PRESS ||
                GLFW.glfwGetKey(win, GLFW.GLFW_KEY_RIGHT_CONTROL) == GLFW.GLFW_PRESS;
    }

    private boolean isShiftPressed(long win) {
        return GLFW.glfwGetKey(win, GLFW.GLFW_KEY_LEFT_SHIFT) == GLFW.GLFW_PRESS ||
                GLFW.glfwGetKey(win, GLFW.GLFW_KEY_RIGHT_SHIFT) == GLFW.GLFW_PRESS;
    }
}