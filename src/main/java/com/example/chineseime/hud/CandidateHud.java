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

    private int[] cachedItemWidths = null;
    private int cachedItemWidthsStart = -1;
    private int cachedItemWidthsEnd = -1;
    private float cachedScale = -1f;

    private static final int BG = 0xB3000000;
    private static final int SEL_BG = 0x66B1B4B6;
    private static final int SEL_BAR = 0xFF4488FF;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int NUM_COLOR = 0xFF929194;
    private static final int INPUT_COLOR = 0xFFB991FF;
    private static final int ARROW_COLOR = 0xFFAAAAAA;
    private static final int ARROW_HOVER_COLOR = 0xFFFFFFFF;

    private static final int HUD_HEIGHT_1080P = 36;
    private static final int ITEM_WIDTH_1080P = 60;
    private static final int ITEM_GAP_1080P = 0;
    private static final int MARGIN_1080P = 8;
    private static final int ARROW_W_1080P = 20;
    private static final int ARROW_GAP_1080P = 6;
    private static final int BLUE_BAR_W_1080P = 3;
    private static final int HIGHLIGHT_MARGIN_1080P = 2;
    private static final int COMPO_MIN_GAP_1080P = 10;
    private static final int COMPO_PAD_1080P = 8;
    private static final int CAND_END_PAD_1080P = 6;
    private static final int NUM_PAD_1080P = 2;

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

        if (my < this.y || my > this.y + this.height) return false;

        int gap = (int)(ITEM_GAP_1080P / scale);
        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        int compoW = computeCompositionWidth(scale);
        int arrowW = (int)(ARROW_W_1080P / scale);
        int arrowGap = (int)(ARROW_GAP_1080P / scale);

        int[] itemWidths = computeItemWidths(start, end, scale);
        int compoMinGap = computeCompositionMinGap(scale);
        int compoCx = this.x + computePad(scale) + (compoW > 0 ? compoW + compoMinGap : 0);
        int cx = compoCx;
        for (int i = start; i < end; i++) {
            int itemW = itemWidths[i - start];
            if (mx >= cx && mx < cx + itemW) {
                this.selected = i;
                return true;
            }
            cx += itemW + gap;
        }

        int candidatesTotalW = 0;
        for (int w : itemWidths) candidatesTotalW += w;
        candidatesTotalW += Math.max(0, end - start - 1) * gap;
        int arrowsX = compoCx + candidatesTotalW + arrowGap;
        if (end > start) {
            if (mx >= arrowsX && mx < arrowsX + arrowW) {
                this.prevPage();
                return true;
            }
            if (mx >= arrowsX + arrowW && mx < arrowsX + arrowW * 2) {
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

        int gap = (int)(ITEM_GAP_1080P / scale);
        int compoW = computeCompositionWidth(scale);
        int arrowW = (int)(ARROW_W_1080P / scale);
        int arrowGap = (int)(ARROW_GAP_1080P / scale);

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        int visibleCount = end - start;
        int[] itemWidths = computeItemWidths(start, end, scale);

        int compoMinGap = computeCompositionMinGap(scale);
        int compoCx = this.x + computePad(scale) + (compoW > 0 ? compoW + compoMinGap : 0);
        int candidatesTotalW = 0;
        for (int w : itemWidths) candidatesTotalW += w;
        candidatesTotalW += Math.max(0, visibleCount - 1) * gap;
        int arrowsX = compoCx + candidatesTotalW + arrowGap;
        this.prevArrowHovered = visibleCount > 0 && mx >= arrowsX && mx < arrowsX + arrowW;
        this.nextArrowHovered = visibleCount > 0 && mx >= arrowsX + arrowW && mx < arrowsX + arrowW * 2;
    }

    private int computePad(float scale) {
        return (int)(8 / scale);
    }

    private int computeCompositionWidth(float scale) {
        if (this.composition.isEmpty()) return 0;
        TextRenderer font = MinecraftClient.getInstance().textRenderer;
        int pad = (int)(COMPO_PAD_1080P / scale);
        return font.getWidth(this.composition) + pad * 2;
    }

    private int computeCompositionMinGap(float scale) {
        return (int)(COMPO_MIN_GAP_1080P / scale);
    }

    private int[] computeItemWidths(int start, int end, float scale) {
        int visibleCount = end - start;
        int[] widths = new int[visibleCount];
        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;
        int minItemW = (int)(ITEM_WIDTH_1080P / scale);
        int candEndPad = (int)(CAND_END_PAD_1080P / scale);
        int numPad = (int)(NUM_PAD_1080P / scale);
        int blueBarW = (int)(BLUE_BAR_W_1080P / scale);
        for (int i = start; i < end; i++) {
            String cand = this.candidates.get(i);
            int localIndex = (i - start) + 1;
            String numStr = String.valueOf(localIndex);
            int numW = font.getWidth(numStr);
            int candW = font.getWidth(cand);
            int totalTextW = numW + numPad + candW;
            widths[i - start] = Math.max(minItemW, totalTextW + blueBarW + candEndPad);
        }
        return widths;
    }

    public void render(DrawContext ctx) {
        if (!this.visible) return;

        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;

        int scaledW = ctx.getScaledWindowWidth();
        int scaledH = ctx.getScaledWindowHeight();
        int physicalW = mc.getWindow().getWidth();
        float scale = physicalW > 0 ? (float) physicalW / (float) scaledW : 2.0f;

        int h = (int)(HUD_HEIGHT_1080P / scale);
        int gap = (int)(ITEM_GAP_1080P / scale);
        int numPad = (int)(NUM_PAD_1080P / scale);
        int pad = computePad(scale);
        int rightPad = (int)(8 / scale);
        int highlightMargin = (int)(HIGHLIGHT_MARGIN_1080P / scale);
        int blueBarW = (int)(BLUE_BAR_W_1080P / scale);
        int arrowW = (int)(ARROW_W_1080P / scale);
        int arrowGap = (int)(ARROW_GAP_1080P / scale);

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        int visibleCount = Math.max(end - start, 0);

        int compoW = computeCompositionWidth(scale);
        int compoMinGap = computeCompositionMinGap(scale);

        int[] itemWidths = computeItemWidths(start, end, scale);
        int candidatesW = 0;
        for (int w : itemWidths) candidatesW += w;
        candidatesW += Math.max(0, visibleCount - 1) * gap;

        int arrowsW = visibleCount > 0 ? arrowW * 2 + arrowGap * 2 : 0;
        int compoAreaW = compoW > 0 ? compoW + compoMinGap : 0;
        int rawW = pad + compoAreaW + candidatesW + arrowsW + rightPad;

        this.width = rawW;
        this.x = (int)(MARGIN_1080P / scale);
        int chatInputTop = scaledH - 22 - 14;
        this.y = chatInputTop - 2 - h;
        this.height = h;

        int px = this.x;
        int py = this.y;
        int pw = this.width;
        int ph = this.height;

        ctx.fill(px, py, px + pw, py + ph, BG);

        int cx = px + pad;
        int textY = py + (ph - font.fontHeight + 1) / 2;

        if (compoW > 0) {
            int compoTextW = font.getWidth(this.composition);
            int compoTextX = cx + (compoW - compoTextW) / 2;
            ctx.drawText(font, this.composition, compoTextX, textY, INPUT_COLOR, false);
            cx += compoW + compoMinGap;
        }

        for (int i = start; i < end; i++) {
            String cand = this.candidates.get(i);
            boolean isSelected = i == this.selected;
            int localIndex = (i - start) + 1;
            int localIdx = i - start;
            int itemW = itemWidths[localIdx];

            int itemX = cx;
            for (int j = 0; j < localIdx; j++) {
                itemX += itemWidths[j] + gap;
            }

            if (isSelected) {
                ctx.fill(itemX, py + highlightMargin,
                    itemX + itemW, py + ph - highlightMargin, SEL_BG);
                ctx.fill(itemX, py + highlightMargin,
                    itemX + blueBarW, py + ph - highlightMargin, SEL_BAR);
            }

            String numStr = String.valueOf(localIndex);
            int numW = font.getWidth(numStr);
            int candW = font.getWidth(cand);
            int totalTextW = numW + numPad + candW;
            int textX = itemX + blueBarW + (itemW - blueBarW - totalTextW) / 2;

            ctx.drawText(font, numStr, textX, textY, NUM_COLOR, false);
            ctx.drawText(font, cand, textX + numW + numPad, textY, TEXT_COLOR, false);
        }

        if (visibleCount > 0) {
            int arrowsX = cx + candidatesW + arrowGap;
            int arrowColorLeft = this.prevArrowHovered ? ARROW_HOVER_COLOR : ARROW_COLOR;
            int arrowColorRight = this.nextArrowHovered ? ARROW_HOVER_COLOR : ARROW_COLOR;
            ctx.drawText(font, "<", arrowsX, textY, arrowColorLeft, false);
            ctx.drawText(font, ">", arrowsX + arrowW, textY, arrowColorRight, false);
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