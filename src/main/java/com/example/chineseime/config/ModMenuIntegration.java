package com.example.chineseime.config;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.terraformersmc.modmenu.api.ConfigScreenFactory;
import com.terraformersmc.modmenu.api.ModMenuApi;
import net.minecraft.client.MinecraftClient;

public class ModMenuIntegration implements ModMenuApi {

    @Override
    public ConfigScreenFactory<net.minecraft.client.gui.screen.Screen> getModMenuConfigScreenFactory() {
        return parent -> {
            MinecraftClient mc = MinecraftClient.getInstance();
            ModConfig config = ModConfig.load();
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Opened config from ModMenu");
            return new ConfigScreen(parent, config);
        };
    }
}