package com.example.chineseime.hud;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.font.TextRenderer;
import net.minecraft.client.gui.DrawContext;

public class CandidateHud {
    private List<String> candidates = new ArrayList<>();
    private String composition = "";
    private int selected = 0;
    private int page = 0;
    private int perPage = 9;
    private boolean visible = false;
    private int x, y, width, height;

    private static final int BG = 0x80000000;
    private static final int SEL_BG = 0x66B1B4B6;
    private static final int SEL_BAR = 0xFF4488FF;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int NUM_COLOR = 0xFF929194;
    private static final int ARROW_COLOR = 0xFFAAAAAA;
    private static final int ARROW_HOVER_COLOR = 0xFFFFFFFF;

    private static final int HUD_HEIGHT_PX = 36;
    private static final int ITEM_SLOT_WIDTH_PX = 70;
    private static final int ITEM_CHAR_EXTRA_PX = 20;
    private static final int PAD_PX = 6;
    private static final int BLUE_BAR_W_PX = 3;
    private static final int ARROW_AREA_W_PX = 40;
    private static final int TOTAL_MAX_PX = 960;
    private static final int DEFAULT_COUNT = 9;

    private static final int ARROW_NUM_W_PX = 16;

    private boolean prevArrowHovered = false;
    private boolean nextArrowHovered = false;

    public void updateCandidates(List<String> candidates, String composition) {
        this.candidates = candidates != null ? new ArrayList<>(candidates) : new ArrayList<>();
        this.composition = composition != null ? composition : "";
        this.selected = 0;
        this.page = 0;
        this.visible = !this.candidates.isEmpty() || !this.composition.isEmpty();
    }

    public void updateCandidatesKeepSelection(List<String> candidates, String composition, int selectedIndex, int page) {
        this.candidates = candidates != null ? new ArrayList<>(candidates) : new ArrayList<>();
        this.composition = composition != null ? composition : "";
        int totalPages = this.candidates.isEmpty() ? 1 : (this.candidates.size() + this.perPage - 1) / this.perPage;
        this.page = Math.max(0, Math.min(page, totalPages - 1));
        this.selected = this.page * this.perPage + Math.max(0, Math.min(selectedIndex % this.perPage, this.candidates.size() - 1));
        if (this.selected >= this.candidates.size()) {
            this.selected = Math.max(0, this.candidates.size() - 1);
            int newPage = this.selected / this.perPage;
            if (newPage != this.page) this.page = newPage;
        }
        this.visible = !this.candidates.isEmpty() || !this.composition.isEmpty();
    }

    public void clear() {
        this.candidates.clear();
        this.composition = "";
        this.selected = 0;
        this.page = 0;
        this.visible = false;
    }

    public void selectPrevious() {
        if (this.candidates.isEmpty()) return;
        this.selected--;
        if (this.selected < 0) {
            this.selected = this.candidates.size() - 1;
        }
        this.page = this.selected / this.perPage;
    }

    public void selectNext() {
        if (this.candidates.isEmpty()) return;
        this.selected++;
        if (this.selected >= this.candidates.size()) {
            this.selected = 0;
        }
        this.page = this.selected / this.perPage;
    }

    public void prevPage() {
        if (this.page > 0) {
            this.page--;
            this.selected = this.page * this.perPage;
            if (this.selected >= this.candidates.size()) {
                this.selected = Math.max(0, this.candidates.size() - 1);
            }
        }
    }

    public void nextPage() {
        int totalPages = (this.candidates.size() + this.perPage - 1) / this.perPage;
        if (this.page < totalPages - 1) {
            this.page++;
            this.selected = this.page * this.perPage;
            if (this.selected >= this.candidates.size()) {
                this.selected = Math.max(0, this.candidates.size() - 1);
            }
        }
    }

    public String getSelected() {
        return this.selected >= 0 && this.selected < this.candidates.size() ? this.candidates.get(this.selected) : "";
    }

    public boolean isVisible() { return this.visible; }
    public int getSelectedIndex() { return this.selected; }
    public List<String> getCandidates() { return this.candidates; }
    public String getInput() { return this.composition; }
    public int getPage() { return this.page; }
    public int getTotalPages() { return (this.candidates.size() + this.perPage - 1) / this.perPage; }
    public boolean hasPrevPage() { return this.page > 0; }
    public boolean hasNextPage() { return this.page < getTotalPages() - 1; }
    public int getX() { return this.x; }
    public int getY() { return this.y; }
    public int getWidth() { return this.width; }
    public int getHeight() { return this.height; }

    public float getScaleForClick() {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null) return 2.0f;
        int physicalW = mc.getWindow().getWidth();
        int scaledW = mc.getWindow().getScaledWidth();
        return physicalW > 0 ? (float) physicalW / (float) scaledW : 2.0f;
    }

    public void onMouseRelease(double mouseX, double mouseY) {
        this.prevArrowHovered = false;
        this.nextArrowHovered = false;
    }

    public boolean handleClick(double mouseX, double mouseY, float scale) {
        if (!this.visible) return false;
        int mx = (int) mouseX;
        int my = (int) mouseY;

        if (my < this.y || my > this.y + this.height) return false;

        int scaleInt = Math.round(scale);
        int itemSlot = Math.round(ITEM_SLOT_WIDTH_PX / scale);
        int pad = Math.round(PAD_PX / scale);
        int blueBar = Math.round(BLUE_BAR_W_PX / scale);

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        int visibleCount = end - start;

        int itemsAreaW = 0;
        for (int i = start; i < end; i++) {
            int itemW = getItemWidth(i, scale);
            itemsAreaW += itemW;
        }
        int arrowsArea = hasPrevPage() || hasNextPage() ? Math.round(ARROW_AREA_W_PX / scale) : 0;

        int totalW = pad + itemsAreaW + arrowsArea + pad;
        if (totalW > Math.round(TOTAL_MAX_PX / scale)) {
            totalW = Math.round(TOTAL_MAX_PX / scale);
        }

        int px = this.x;
        int curX = px + pad;
        for (int i = start; i < end; i++) {
            int itemW = getItemWidth(i, scale);
            if (mx >= curX && mx < curX + itemW) {
                this.selected = i;
                return true;
            }
            curX += itemW;
        }

        if (arrowsArea > 0) {
            int arrowsX = px + totalW - pad - arrowsArea;
            int arrowW = Math.round(ARROW_NUM_W_PX / scale);
            if (mx >= arrowsX && mx < arrowsX + arrowW) {
                this.prevPage();
                return true;
            }
            if (mx >= arrowsX + arrowW && mx < arrowsX + arrowsArea) {
                this.nextPage();
                return true;
            }
        }

        return false;
    }

    public void onMouseMove(double mouseX, double mouseY, float scale) {
        if (!this.visible) {
            this.prevArrowHovered = false;
            this.nextArrowHovered = false;
            return;
        }
        int mx = (int) mouseX;
        int my = (int) mouseY;

        if (my < this.y || my > this.y + this.height) {
            this.prevArrowHovered = false;
            this.nextArrowHovered = false;
            return;
        }

        int pad = Math.round(PAD_PX / scale);
        int arrowsArea = hasPrevPage() || hasNextPage() ? Math.round(ARROW_AREA_W_PX / scale) : 0;

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());

        int itemsAreaW = 0;
        for (int i = start; i < end; i++) {
            itemsAreaW += getItemWidth(i, scale);
        }

        int totalW = pad + itemsAreaW + arrowsArea + pad;
        if (totalW > Math.round(TOTAL_MAX_PX / scale)) {
            totalW = Math.round(TOTAL_MAX_PX / scale);
        }

        int arrowsX = this.x + totalW - pad - arrowsArea;
        int arrowW = Math.round(ARROW_NUM_W_PX / scale);
        this.prevArrowHovered = arrowsArea > 0 && mx >= arrowsX && mx < arrowsX + arrowW;
        this.nextArrowHovered = arrowsArea > 0 && mx >= arrowsX + arrowW && mx < arrowsX + arrowsArea;
    }

    private int getItemWidth(int index, float scale) {
        if (index < 0 || index >= this.candidates.size()) {
            return Math.round(ITEM_SLOT_WIDTH_PX / scale);
        }
        String cand = this.candidates.get(index);
        int charCount = cand != null ? cand.length() : 1;
        int extra = Math.max(0, charCount - 1) * Math.round(ITEM_CHAR_EXTRA_PX / scale);
        return Math.round(ITEM_SLOT_WIDTH_PX / scale) + extra;
    }

    public void render(DrawContext ctx) {
        if (!this.visible) return;

        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;

        int scaledW = ctx.getScaledWindowWidth();
        int scaledH = ctx.getScaledWindowHeight();
        int physicalW = mc.getWindow().getWidth();
        float scale = physicalW > 0 ? (float) physicalW / (float) scaledW : 2.0f;

        int pad = Math.round(PAD_PX / scale);
        int blueBarW = Math.round(BLUE_BAR_W_PX / scale);
        int arrowsArea = hasPrevPage() || hasNextPage() ? Math.round(ARROW_AREA_W_PX / scale) : 0;

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());

        int itemsAreaW = 0;
        for (int i = start; i < end; i++) {
            itemsAreaW += getItemWidth(i, scale);
        }

        int rawW = pad + itemsAreaW + arrowsArea + pad;
        int maxW = Math.round(TOTAL_MAX_PX / scale);
        if (rawW > maxW) rawW = maxW;

        this.height = Math.round(HUD_HEIGHT_PX / scale);
        this.width = rawW;
        this.x = Math.round(8 / scale);
        int chatInputTop = scaledH - 22 - 14;
        this.y = chatInputTop - 2 - this.height;

        int px = this.x;
        int py = this.y;
        int pw = this.width;
        int ph = this.height;

        ctx.fill(px, py, px + pw, py + ph, BG);

        int textY = py + (ph - font.fontHeight + 1) / 2;
        int curX = px + pad;

        for (int i = start; i < end; i++) {
            String cand = this.candidates.get(i);
            int itemW = getItemWidth(i, scale);
            boolean isSelected = i == this.selected;

            if (isSelected) {
                ctx.fill(curX, py, curX + itemW, py + ph, SEL_BG);
                ctx.fill(curX, py, curX + blueBarW, py + ph, SEL_BAR);
            }

            int num = (i - start) + 1;
            String numStr = String.valueOf(num);
            int numW = font.getWidth(numStr);
            int candW = font.getWidth(cand);
            int totalTextW = numW + 2 + candW;
            int textX = curX + (itemW - totalTextW) / 2;

            ctx.drawText(font, numStr, textX, textY, NUM_COLOR, false);
            ctx.drawText(font, cand, textX + numW + 2, textY, TEXT_COLOR, false);

            curX += itemW;
        }

        if (arrowsArea > 0) {
            int arrowX = px + rawW - pad - arrowsArea;
            int arrowW = Math.round(ARROW_NUM_W_PX / scale);
            int textCenterY = textY;

            int leftColor = this.prevArrowHovered ? ARROW_HOVER_COLOR : ARROW_COLOR;
            int rightColor = this.nextArrowHovered ? ARROW_HOVER_COLOR : ARROW_COLOR;

            ctx.drawText(font, "<", arrowX, textCenterY, hasPrevPage() ? leftColor : ARROW_COLOR, false);
            ctx.drawText(font, ">", arrowX + arrowW, textCenterY, hasNextPage() ? rightColor : ARROW_COLOR, false);
        }
    }

    public void clearInput() {
        this.candidates.clear();
        this.composition = "";
        this.selected = 0;
        this.page = 0;
        this.visible = false;
    }
}