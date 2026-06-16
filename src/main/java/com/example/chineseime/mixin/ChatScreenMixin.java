package com.example.chineseime.mixin;

import com.example.chineseime.ChineseIMEInitializer;
import net.minecraft.client.gui.screen.ChatScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(ChatScreen.class)
public abstract class ChatScreenMixin {

    @Inject(method = "mouseClicked", at = @At("HEAD"), cancellable = true)
    private void onMouseClicked(double mouseX, double mouseY, int button, CallbackInfoReturnable<Boolean> cir) {
        ChineseIMEInitializer instance = ChineseIMEInitializer.getInstance();
        if (instance == null) return;

        if (button == 0) {
            if (instance.getImeManager().isVerticalLayout()) {
                float scale = instance.getVerticalCandidateHud().getScaleForClick();
                if (instance.getVerticalCandidateHud().handleClick(mouseX, mouseY, scale)) {
                    cir.setReturnValue(true);
                }
            } else {
                float scale = instance.getCandidateHud().getScaleForClick();
                if (instance.getCandidateHud().handleClick(mouseX, mouseY, scale)) {
                    cir.setReturnValue(true);
                }
            }
        }
    }
}