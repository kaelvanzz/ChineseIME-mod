package com.example.chineseime.config;

import com.example.chineseime.engine.InputMode;
import com.example.chineseime.engine.ScriptType;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import java.io.Reader;
import java.io.Writer;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import net.fabricmc.loader.api.FabricLoader;
import org.lwjgl.glfw.GLFW;

public class ModConfig {
    private static final Gson GSON = (new GsonBuilder()).setPrettyPrinting().create();
    private static final Path CONFIG_PATH = FabricLoader.getInstance().getConfigDir().resolve("chineseime.json");

    private InputMode inputMode;
    private ScriptType scriptType;
    private boolean chineseMode;
    private int hudScale;
    private int maxCandidates;
    private boolean preferRime;
    private String rimeSchema;
    private transient boolean windowsLocked;
    private int toggleImeKey;
    private int toggleChineseModeKey;
    private int toggleScriptKey;
    private List<Integer> toggleImeKeys;
    private List<Integer> toggleChineseModeKeys;

    public ModConfig() {
        this.inputMode = InputMode.PINYIN;
        this.scriptType = ScriptType.SIMPLIFIED;
        this.chineseMode = true;
        this.hudScale = 100;
        this.maxCandidates = 9;
        this.preferRime = false;
        this.rimeSchema = "luna_pinyin";
        this.windowsLocked = false;
        this.toggleImeKey = -1;
        this.toggleChineseModeKey = -1;
        this.toggleScriptKey = -1;
        this.toggleImeKeys = new ArrayList<>();
        this.toggleChineseModeKeys = new ArrayList<>();
    }

    public void lockForWindows() {
        this.windowsLocked = true;
    }

    public boolean isWindowsLocked() {
        return this.windowsLocked;
    }

    public static ModConfig load() {
        if (Files.exists(CONFIG_PATH, new LinkOption[0])) {
            try {
                Reader reader = Files.newBufferedReader(CONFIG_PATH);
                try {
                    ModConfig config = (ModConfig) GSON.fromJson(reader, ModConfig.class);
                    if (config != null) {
                        return config;
                    }
                } finally {
                    reader.close();
                }
            } catch (Exception e) {
                System.err.println("[ChineseIME] 加载配置失败: " + e.getMessage());
            }
        }

        ModConfig config = new ModConfig();
        config.save();
        return config;
    }

    public void save() {
        try {
            Files.createDirectories(CONFIG_PATH.getParent());
            Writer writer = Files.newBufferedWriter(CONFIG_PATH);
            try {
                GSON.toJson(this, writer);
            } finally {
                writer.close();
            }
        } catch (Exception e) {
            System.err.println("[ChineseIME] 保存配置失败: " + e.getMessage());
        }
    }

    public InputMode getInputMode() { return this.inputMode; }

    public void setInputMode(InputMode mode) {
        this.inputMode = mode;
        this.save();
    }

    public ScriptType getScriptType() { return this.scriptType; }

    public void setScriptType(ScriptType type) {
        if (!this.windowsLocked) {
            this.scriptType = type;
            this.save();
        }
    }

    public boolean isChineseMode() { return this.chineseMode; }

    public void setChineseMode(boolean mode) {
        if (!this.windowsLocked) {
            this.chineseMode = mode;
            this.save();
        }
    }

    public void setChineseModeInternal(boolean mode) { this.chineseMode = mode; }
    public void setScriptTypeInternal(ScriptType type) { this.scriptType = type; }

    public int getHudScale() { return this.hudScale; }

    public void setHudScale(int scale) {
        this.hudScale = scale;
        this.save();
    }

    public int getMaxCandidates() { return this.maxCandidates; }

    public void setMaxCandidates(int max) {
        this.maxCandidates = max;
        this.save();
    }

    public boolean isPreferRime() { return this.preferRime; }

    public void setPreferRime(boolean prefer) {
        this.preferRime = prefer;
        this.save();
    }

    public String getRimeSchema() { return this.rimeSchema; }

    public void setRimeSchema(String schema) {
        this.rimeSchema = schema;
        this.save();
    }

    public void toggleChineseMode() {
        if (!this.windowsLocked) {
            this.chineseMode = !this.chineseMode;
            this.save();
        }
    }

    public void toggleScriptType() {
        if (!this.windowsLocked) {
            this.scriptType = this.scriptType == ScriptType.SIMPLIFIED ? ScriptType.TRADITIONAL : ScriptType.SIMPLIFIED;
            this.save();
        }
    }

    public void cycleInputMode() {
        if (this.windowsLocked) {
            if (this.inputMode == InputMode.PINYIN) {
                this.inputMode = InputMode.RIME;
            } else {
                this.inputMode = InputMode.PINYIN;
            }
        } else {
            InputMode[] modes = InputMode.values();
            this.inputMode = modes[(this.inputMode.ordinal() + 1) % modes.length];
        }
        this.save();
    }

    public int getToggleImeKey() { return this.toggleImeKey; }

    public void setToggleImeKey(int key) {
        this.toggleImeKey = key;
        this.save();
    }

    public void clearToggleImeKey() {
        this.toggleImeKey = -1;
        this.save();
    }

    public int getToggleChineseModeKey() { return this.toggleChineseModeKey; }

    public void setToggleChineseModeKey(int key) {
        this.toggleChineseModeKey = key;
        this.save();
    }

    public void clearToggleChineseModeKey() {
        this.toggleChineseModeKey = -1;
        this.save();
    }

    public List<Integer> getToggleImeKeys() {
        return this.toggleImeKeys != null ? this.toggleImeKeys : new ArrayList<>();
    }

    public void setToggleImeKeys(List<Integer> keys) {
        this.toggleImeKeys = keys != null ? new ArrayList<>(keys) : new ArrayList<>();
        this.save();
    }

    public List<Integer> getToggleChineseModeKeys() {
        return this.toggleChineseModeKeys != null ? this.toggleChineseModeKeys : new ArrayList<>();
    }

    public void setToggleChineseModeKeys(List<Integer> keys) {
        this.toggleChineseModeKeys = keys != null ? new ArrayList<>(keys) : new ArrayList<>();
        this.save();
    }

    public int getToggleScriptKey() { return this.toggleScriptKey; }

    public void setToggleScriptKey(int key) {
        this.toggleScriptKey = key;
        this.save();
    }

    public void clearToggleScriptKey() {
        this.toggleScriptKey = -1;
        this.save();
    }

    public void resetAllKeybinds() {
        this.toggleImeKey = -1;
        this.toggleChineseModeKey = -1;
        this.toggleScriptKey = -1;
        this.save();
    }

    public void resetToDefaults() {
        this.inputMode = InputMode.PINYIN;
        this.scriptType = ScriptType.SIMPLIFIED;
        this.chineseMode = true;
        this.hudScale = 100;
        this.maxCandidates = 9;
        this.preferRime = false;
        this.rimeSchema = "luna_pinyin";
        this.toggleImeKey = -1;
        this.toggleChineseModeKey = -1;
        this.toggleScriptKey = -1;
        this.save();
    }

    public String getShortcutKey() {
        if (this.toggleImeKey == -1) return "";
        return GLFW.glfwGetKeyName(this.toggleImeKey, 0);
    }

    public void setShortcutKey(String key) {
        this.toggleImeKey = -1;
        this.save();
    }
}