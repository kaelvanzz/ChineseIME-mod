package com.example.chineseime.mixin;

import com.example.chineseime.ChineseIMEInitializer;
import net.minecraft.client.gui.Click;
import net.minecraft.client.gui.screen.ChatScreen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(ChatScreen.class)
public abstract class ChatScreenMixin {

    @Inject(method = "mouseClicked", at = @At("HEAD"), cancellable = true)
    private void onMouseClicked(Click click, boolean doubled, CallbackInfoReturnable<Boolean> cir) {
        ChineseIMEInitializer instance = ChineseIMEInitializer.getInstance();
        if (instance == null) return;

        if (click.button() == 0) {
            float scale = instance.getCandidateHud().getScaleForClick();
            if (instance.getCandidateHud().handleClick(click.x(), click.y(), scale)) {
                cir.setReturnValue(true);
            }
        }
    }
}
