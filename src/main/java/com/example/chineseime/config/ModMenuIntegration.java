package com.example.chineseime.config;

import com.example.chineseime.ChineseIMEInitializer;
import com.terraformersmc.modmenu.api.ConfigScreenFactory;
import com.terraformersmc.modmenu.api.ModMenuApi;
import net.minecraft.client.gui.screen.Screen;

public class ModMenuIntegration implements ModMenuApi {

    @Override
    public ConfigScreenFactory<?> getModConfigScreenFactory() {
        return parent -> {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Opened config from ModMenu");
            return new ConfigScreen(parent, ModConfig.load());
        };
    }
}