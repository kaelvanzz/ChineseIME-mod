package com.example.chineseime.hud;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.font.TextRenderer;
import net.minecraft.client.gui.DrawContext;

public class VerticalCandidateHud {
    private List<String> candidates = new ArrayList<>();
    private String composition = "";
    private int selected = 0;
    private int page = 0;
    private int perPage = 9;
    private boolean visible = false;
    private int x, y, width, height;

    private static final int BG = 0xB3000000;
    private static final int SEL_BG = 0x66B1B4B6;
    private static final int SEL_BAR = 0xFF4488FF;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int NUM_COLOR = 0xFF929194;
    private static final int INPUT_COLOR = 0xFFB991FF;
    private static final int SEP_COLOR = 0xFF555555;

    private static final int ITEM_HEIGHT_1080P = 28;
    private static final int WIDTH_1080P = 120;
    private static final int MARGIN_1080P = 8;
    private static final int PAD_1080P = 6;
    private static final int BLUE_BAR_W_1080P = 3;
    private static final int COMPO_HEIGHT_1080P = 32;

    private boolean prevArrowHovered = false;
    private boolean nextArrowHovered = false;

    public void updateCandidates(List<String> candidates, String composition) {
        this.candidates = candidates != null ? new ArrayList<>(candidates) : new ArrayList<>();
        this.composition = composition != null ? composition : "";
        this.selected = 0;
        this.page = 0;
        if (this.composition.isEmpty()) {
            this.visible = false;
        } else {
            this.visible = !this.candidates.isEmpty() || !this.composition.isEmpty();
        }
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
        if (this.composition.isEmpty()) {
            this.visible = false;
        } else {
            this.visible = !this.candidates.isEmpty() || !this.composition.isEmpty();
        }
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
    public int getPerPage() { return this.perPage; }
    public void setSelectedIndex(int selected) { this.selected = selected; }
    public void setPage(int page) { this.page = page; }
    public void setVisible(boolean visible) { this.visible = visible; }

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

        int panelW = (int)(WIDTH_1080P / scale);
        int margin = (int)(MARGIN_1080P / scale);
        int pad = (int)(PAD_1080P / scale);
        int compoH = this.composition.isEmpty() ? 0 : (int)(COMPO_HEIGHT_1080P / scale);
        int itemH = (int)(ITEM_HEIGHT_1080P / scale);
        int blueBarW = (int)(BLUE_BAR_W_1080P / scale);

        MinecraftClient mc = MinecraftClient.getInstance();
        int scaledH = mc.getWindow().getScaledHeight();
        int chatInputTop = scaledH - 22 - 14;
        int panelH = compoH + this.perPage * itemH + pad * 2;

        int panelX = scaledH - panelW - margin;
        int panelY = chatInputTop - 2 - panelH;

        if (mx < panelX || mx > panelX + panelW) return false;
        if (my < panelY || my > panelY + panelH) return false;

        if (compoH > 0 && my < panelY + compoH) {
            return false;
        }

        int relativeY = my - (panelY + compoH + pad);
        int itemIndex = relativeY / itemH;

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        int localIndex = itemIndex;

        if (itemIndex >= 0 && itemIndex < (end - start)) {
            int globalIndex = start + itemIndex;
            if (globalIndex >= 0 && globalIndex < this.candidates.size()) {
                this.selected = globalIndex;
                return true;
            }
        }

        int arrowsY = panelY + compoH + pad + this.perPage * itemH;
        int arrowH = itemH;
        if (my >= arrowsY && my < arrowsY + arrowH) {
            if (localIndex == this.perPage - 2 && this.hasPrevPage()) {
                this.prevPage();
                return true;
            }
            if (localIndex == this.perPage - 1 && this.hasNextPage()) {
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

        MinecraftClient mc = MinecraftClient.getInstance();
        int scaledH = mc.getWindow().getScaledHeight();
        int chatInputTop = scaledH - 22 - 14;
        int panelW = (int)(WIDTH_1080P / scale);
        int margin = (int)(MARGIN_1080P / scale);
        int compoH = this.composition.isEmpty() ? 0 : (int)(COMPO_HEIGHT_1080P / scale);
        int itemH = (int)(ITEM_HEIGHT_1080P / scale);
        int pad = (int)(PAD_1080P / scale);
        int panelH = compoH + this.perPage * itemH + pad * 2;

        int panelX = scaledH - panelW - margin;
        int panelY = chatInputTop - 2 - panelH;

        this.prevArrowHovered = false;
        this.nextArrowHovered = false;

        if (mx < panelX || mx > panelX + panelW) return;
        if (my < panelY || my > panelY + panelH) return;
        if (compoH > 0 && my < panelY + compoH) return;

        int arrowsY = panelY + compoH + pad + this.perPage * itemH;
        int arrowH = itemH;
        if (my >= arrowsY && my < arrowsY + arrowH) {
            int relativeY = my - arrowsY;
            int localIndex = relativeY / itemH;
            if (localIndex == this.perPage - 2 && this.hasPrevPage()) {
                this.prevArrowHovered = true;
            }
            if (localIndex == this.perPage - 1 && this.hasNextPage()) {
                this.nextArrowHovered = true;
            }
        }
    }

    public void render(DrawContext ctx) {
        if (!this.visible) return;

        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;

        int scaledW = ctx.getScaledWindowWidth();
        int scaledH = ctx.getScaledWindowHeight();
        int physicalW = mc.getWindow().getWidth();
        float scale = physicalW > 0 ? (float) physicalW / (float) scaledW : 2.0f;

        int compoH = this.composition.isEmpty() ? 0 : (int)(COMPO_HEIGHT_1080P / scale);
        int itemH = (int)(ITEM_HEIGHT_1080P / scale);
        int panelW = (int)(WIDTH_1080P / scale);
        int pad = (int)(PAD_1080P / scale);
        int blueBarW = (int)(BLUE_BAR_W_1080P / scale);
        int margin = (int)(MARGIN_1080P / scale);

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());

        int panelH = compoH + this.perPage * itemH + pad * 2;

        this.x = scaledW - panelW - margin;
        int chatInputTop = scaledH - 22 - 14;
        this.y = chatInputTop - 2 - panelH;
        this.width = panelW;
        this.height = panelH;

        int px = this.x;
        int py = this.y;

        ctx.fill(px, py, px + panelW, py + panelH, BG);

        if (compoH > 0) {
            int compoY = py + pad;
            int compoTextW = font.getWidth(this.composition);
            int compoX = px + (panelW - compoTextW) / 2;
            ctx.drawText(font, this.composition, compoX, compoY + (compoH - font.fontHeight) / 2, INPUT_COLOR, false);

            int sepY = py + compoH;
            ctx.fill(px + pad, sepY, px + panelW - pad, sepY + 1, SEP_COLOR);
        }

        int listY = py + compoH + pad;
        for (int i = start; i < end; i++) {
            String cand = this.candidates.get(i);
            boolean isSelected = i == this.selected;
            int localIndex = (i - start) + 1;

            int itemY = listY + (i - start) * itemH;

            if (isSelected) {
                ctx.fill(px, itemY, px + panelW, itemY + itemH, SEL_BG);
                ctx.fill(px, itemY, px + blueBarW, itemY + itemH, SEL_BAR);
            }

            String numStr = String.valueOf(localIndex);
            int numW = font.getWidth(numStr);
            int numX = px + pad + blueBarW;
            int textY = itemY + (itemH - font.fontHeight) / 2;
            ctx.drawText(font, numStr, numX, textY, NUM_COLOR, false);

            int candX = numX + numW + pad;
            ctx.drawText(font, cand, candX, textY, TEXT_COLOR, false);
        }

        int arrowsY = listY + this.perPage * itemH;
        int arrowColorUp = this.prevArrowHovered ? 0xFFFFFFFF : 0xFFAAAAAA;
        int arrowColorDown = this.nextArrowHovered ? 0xFFFFFFFF : 0xFFAAAAAA;
        int arrowTextY = arrowsY + (itemH - font.fontHeight) / 2;

        ctx.drawText(font, "<", px + pad, arrowTextY, arrowColorUp, false);
        ctx.drawText(font, ">", px + panelW - pad - font.getWidth(">"), arrowTextY, arrowColorDown, false);
    }

    public void clearInput() {
        this.candidates.clear();
        this.composition = "";
        this.selected = 0;
        this.page = 0;
        this.visible = false;
    }
}