 # 仓颉/速成竖式候选词 HUD 设计文档

> ⚠️ **设计文档**：本文档描述了竖式候选词 HUD 的设计方案。**部分实现可能与文档描述有差异**，请以实际代码为准。
>
> **当前状态**：
> - `VerticalCandidateHud.java` 已实现 ✅
> - `PlatformIMEManager` 根据输入法类型自动切换布局 ✅
> - Mixin 相关可能需要验证

---

## UI 布局概述

本文档描述如何将现有的横式候选词 HUD 改为竖式，同时保留仓颉/速成输入法的传统竖式候选词界面风格。

---

## 仓颉/速成竖式候选词 UI 设计

### 传统仓颉输入法界面特征

```
┌─────────────────┐
│ 中              │  ← 第1候选 (当前选中，高亮)
│ 种              │  ← 第2候选
│ 重              │  ← 第3候选
│ 众              │  ← 第4候选
│ 仲              │  ← 第5候选
│ 忠              │  ← 第6候选
│ 钟              │  ← 第7候选
│ 终              │  ← 第8候选
│ 肿              │  ← 第9候选
└─────────────────┘
```

**特征**：
- 候选词**竖排**
- 当前选中项**高亮**（蓝色条在左侧）
- 数字序号在**左侧**或**省略**
- 组合字符串（preedit）在**上方或下方**单独显示

---

## 改造方案

### 1. 新增 `VerticalCandidateHud` 类

```java
// src/main/java/com/example/chineseime/hud/VerticalCandidateHud.java
package com.example.chineseime.hud;

import net.minecraft.client.MinecraftClient;
import net.minecraft.client.font.TextRenderer;
import net.minecraft.client.gui.DrawContext;

import java.util.ArrayList;
import java.util.List;

public class VerticalCandidateHud {
    private List<String> candidates = new ArrayList<>();
    private String composition = "";
    private int selected = 0;
    private int page = 0;
    private int perPage = 9;  // 竖式一般显示 9 个
    private boolean visible = false;
    private int x, y, width, height;

    // 颜色常量（保留现有配色）
    private static final int BG = 0xB3000000;
    private static final int SEL_BG = 0x66B1B4B6;
    private static final int SEL_BAR = 0xFF4488FF;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int NUM_COLOR = 0xFF929194;
    private static final int INPUT_COLOR = 0xFFB991FF;

    // 尺寸常量（竖式专用）
    private static final int ITEM_HEIGHT_1080P = 28;   // 每项高度
    private static final int WIDTH_1080P = 120;          // 面板宽度
    private static final int MARGIN_1080P = 8;
    private static final int PAD_1080P = 6;
    private static final int BLUE_BAR_W_1080P = 3;
    private static final int COMPO_HEIGHT_1080P = 32;    // preedit 区域高度

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
        this.selected = Math.max(0, Math.min(selectedIndex, this.candidates.size() - 1));
        this.visible = !this.candidates.isEmpty() || !this.composition.isEmpty();
    }

    public void render(DrawContext ctx) {
        if (!this.visible) return;

        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;

        int scaledW = ctx.getScaledWindowWidth();
        int scaledH = ctx.getScaledWindowHeight();
        int physicalW = mc.getWindow().getWidth();
        float scale = physicalW > 0 ? (float) physicalW / (float) scaledW : 2.0f;

        // 计算尺寸
        int compoH = this.composition.isEmpty() ? 0 : (int)(COMPO_HEIGHT_1080P / scale);
        int itemH = (int)(ITEM_HEIGHT_1080P / scale);
        int panelW = (int)(WIDTH_1080P / scale);
        int pad = (int)(PAD_1080P / scale);
        int blueBarW = (int)(BLUE_BAR_W_1080P / scale);
        int margin = (int)(MARGIN_1080P / scale);

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        int visibleCount = end - start;

        // 总高度 = preedit区 + 候选区 + 内边距
        int panelH = compoH + visibleCount * itemH + pad * 2;

        // 位置：右下角，在聊天框上方
        this.x = scaledW - panelW - margin;
        int chatInputTop = scaledH - 22 - 14;
        this.y = chatInputTop - 2 - panelH;
        this.width = panelW;
        this.height = panelH;

        // 绘制背景
        ctx.fill(this.x, this.y, this.x + panelW, this.y + panelH, BG);

        // 1. 绘制 preedit（组合字符串）区域
        if (compoH > 0) {
            int compoY = this.y + pad;
            int compoTextW = font.getWidth(this.composition);
            int compoX = this.x + (panelW - compoTextW) / 2;
            ctx.drawText(font, this.composition, compoX, compoY + (compoH - font.fontHeight) / 2, INPUT_COLOR, false);
            
            // 分隔线
            int sepY = this.y + compoH;
            ctx.fill(this.x + pad, sepY, this.x + panelW - pad, sepY + 1, 0xFF555555);
        }

        // 2. 绘制候选词（竖排）
        int listY = this.y + compoH + pad;
        for (int i = start; i < end; i++) {
            String cand = this.candidates.get(i);
            boolean isSelected = i == this.selected;
            int localIndex = (i - start) + 1;  // 1-based 序号

            int itemY = listY + (i - start) * itemH;

            // 选中项高亮背景
            if (isSelected) {
                ctx.fill(this.x, itemY, this.x + panelW, itemY + itemH, SEL_BG);
                // 左侧蓝色指示条
                ctx.fill(this.x, itemY, this.x + blueBarW, itemY + itemH, SEL_BAR);
            }

            // 数字序号（左侧）
            String numStr = String.valueOf(localIndex);
            int numW = font.getWidth(numStr);
            int numX = this.x + pad + blueBarW;
            int textY = itemY + (itemH - font.fontHeight) / 2;
            ctx.drawText(font, numStr, numX, textY, NUM_COLOR, false);

            // 候选汉字
            int candX = numX + numW + pad;
            ctx.drawText(font, cand, candX, textY, TEXT_COLOR, false);
        }
    }

    // 翻页方法（同横式）
    public void prevPage() {
        if (this.page > 0) {
            this.page--;
            this.selected = this.page * this.perPage;
        }
    }

    public void nextPage() {
        int totalPages = (this.candidates.size() + this.perPage - 1) / this.perPage;
        if (this.page < totalPages - 1) {
            this.page++;
            this.selected = this.page * this.perPage;
        }
    }

    public void selectPrevious() {
        if (this.candidates.isEmpty()) return;
        this.selected--;
        if (this.selected < 0) this.selected = this.candidates.size() - 1;
        this.page = this.selected / this.perPage;
    }

    public void selectNext() {
        if (this.candidates.isEmpty()) return;
        this.selected++;
        if (this.selected >= this.candidates.size()) this.selected = 0;
        this.page = this.selected / this.perPage;
    }

    public String getSelected() {
        return this.selected >= 0 && this.selected < this.candidates.size() ? this.candidates.get(this.selected) : "";
    }

    public boolean isVisible() { return this.visible; }
    public int getSelectedIndex() { return this.selected; }
    public int getPage() { return this.page; }
    public int getPerPage() { return this.perPage; }
    public List<String> getCandidates() { return this.candidates; }
    public String getInput() { return this.composition; }
    public int getX() { return this.x; }
    public int getY() { return this.y; }
    public int getWidth() { return this.width; }
    public int getHeight() { return this.height; }

    public void clear() {
        this.candidates.clear();
        this.composition = "";
        this.selected = 0;
        this.page = 0;
        this.visible = false;
    }

    public void clearInput() {
        this.candidates.clear();
        this.composition = "";
        this.selected = 0;
        this.page = 0;
        this.visible = false;
    }
}
```

---

### 2. 根据输入法类型自动切换布局

```java
// PlatformIMEManager.java
public class PlatformIMEManager {
    private CandidateHud horizontalHud;      // 横式（拼音/五笔）
    private VerticalCandidateHud verticalHud; // 竖式（仓颉/速成/注音）
    private ImeStatusIndicator statusIndicator;
    
    private boolean useVerticalLayout = false;

    public void update() {
        // 获取当前输入法类型
        InputMethodType type = getCurrentInputMethodType();
        
        // 判断使用哪种布局
        useVerticalLayout = (type == InputMethodType.CANGJIE || 
                            type == InputMethodType.SUCHENG ||
                            type == InputMethodType.ZHUYIN);
        
        // 获取候选词数据
        List<String> candidates = getCandidates();
        String composition = getComposition();
        int selectedIndex = getSelectedIndex();
        
        if (useVerticalLayout) {
            // 竖式布局
            if (verticalHud != null) {
                if (!candidates.isEmpty()) {
                    verticalHud.updateCandidatesKeepSelection(candidates, composition, selectedIndex, 0);
                } else if (!composition.isEmpty()) {
                    verticalHud.updateCandidatesKeepSelection(new ArrayList<>(), composition, 0, 0);
                } else {
                    verticalHud.clearInput();
                }
            }
            // 隐藏横式
            if (horizontalHud != null) horizontalHud.clearInput();
        } else {
            // 横式布局（拼音等）
            if (horizontalHud != null) {
                if (!candidates.isEmpty()) {
                    horizontalHud.updateCandidatesKeepSelection(candidates, composition, selectedIndex, 0);
                } else if (!composition.isEmpty()) {
                    List<String> fallback = PinyinDictionary.getSuggestions(composition);
                    horizontalHud.updateCandidatesKeepSelection(fallback, composition, 0, 0);
                } else {
                    horizontalHud.clearInput();
                }
            }
            // 隐藏竖式
            if (verticalHud != null) verticalHud.clearInput();
        }
    }

    public void render(DrawContext ctx) {
        if (useVerticalLayout && verticalHud != null) {
            verticalHud.render(ctx);
        } else if (horizontalHud != null) {
            horizontalHud.render(ctx);
        }
        
        if (statusIndicator != null) {
            statusIndicator.render(ctx);
        }
    }
}
```

---

### 3. 在 Mixin 中根据布局处理点击

```java
// InGameHudMixin.java
@Mixin(InGameHud.class)
public class InGameHudMixin {
    @Inject(method = "render", at = @At("TAIL"))
    private void onRender(DrawContext ctx, RenderTickCounter tickCounter, CallbackInfo ci) {
        PlatformIMEManager mgr = ChineseIMEInitializer.getPlatformManager();
        if (mgr != null) {
            mgr.render(ctx);
        }
    }
}

// MouseMixin.java - 处理鼠标点击选择候选词
@Mixin(Mouse.class)
public class MouseMixin {
    @Inject(method = "onMouseButton", at = @At("HEAD"), cancellable = true)
    private void onMouseButton(long window, int button, int action, int mods, CallbackInfo ci) {
        if (action != GLFW.GLFW_PRESS) return;
        
        PlatformIMEManager mgr = ChineseIMEInitializer.getPlatformManager();
        if (mgr == null) return;
        
        // 获取鼠标位置
        double mouseX = MinecraftClient.getInstance().mouse.getX();
        double mouseY = MinecraftClient.getInstance().mouse.getY();
        float scale = mgr.getHudScale();
        
        // 判断当前布局
        if (mgr.isVerticalLayout()) {
            VerticalCandidateHud hud = mgr.getVerticalHud();
            if (hud != null && hud.isVisible()) {
                // 竖式点击检测
                int mx = (int)(mouseX / scale);
                int my = (int)(mouseY / scale);
                
                // 检查是否在竖式面板内
                if (mx >= hud.getX() && mx <= hud.getX() + hud.getWidth() &&
                    my >= hud.getY() && my <= hud.getY() + hud.getHeight()) {
                    
                    int itemH = (int)(VerticalCandidateHud.ITEM_HEIGHT_1080P / scale);
                    int compoH = hud.getComposition().isEmpty() ? 0 : (int)(VerticalCandidateHud.COMPO_HEIGHT_1080P / scale);
                    int pad = (int)(VerticalCandidateHud.PAD_1080P / scale);
                    
                    int relativeY = my - (hud.getY() + compoH + pad);
                    int index = relativeY / itemH;
                    
                    int start = hud.getPage() * hud.getPerPage();
                    int globalIndex = start + index;
                    
                    if (globalIndex >= 0 && globalIndex < hud.getCandidates().size()) {
                        // 选择该候选词
                        mgr.selectCandidate(globalIndex);
                        ci.cancel();
                    }
                }
            }
        } else {
            // 横式点击检测（现有逻辑）
            CandidateHud hud = mgr.getHorizontalHud();
            if (hud != null && hud.isVisible() && hud.handleClick(mouseX, mouseY, scale)) {
                ci.cancel();
            }
        }
    }
}
```

---

### 4. 键盘翻页处理（竖式）

```java
// KeyboardMixin.java
@Mixin(Keyboard.class)
public class KeyboardMixin {
    @Inject(method = "onKey", at = @At("HEAD"), cancellable = true)
    private void onKey(long window, int key, int scancode, int action, int mods, CallbackInfo ci) {
        if (action != GLFW.GLFW_PRESS && action != GLFW.GLFW_REPEAT) return;
        
        PlatformIMEManager mgr = ChineseIMEInitializer.getPlatformManager();
        if (mgr == null) return;
        
        // 竖式布局的翻页：上下键翻页，左右键选择
        if (mgr.isVerticalLayout()) {
            VerticalCandidateHud hud = mgr.getVerticalHud();
            if (hud == null || !hud.isVisible()) return;
            
            if (key == GLFW.GLFW_KEY_UP) {
                hud.selectPrevious();  // 向上选择
                mgr.onCandidateSelectionChanged(hud.getSelectedIndex());
                ci.cancel();
            } else if (key == GLFW.GLFW_KEY_DOWN) {
                hud.selectNext();  // 向下选择
                mgr.onCandidateSelectionChanged(hud.getSelectedIndex());
                ci.cancel();
            } else if (key == GLFW.GLFW_KEY_LEFT) {
                hud.prevPage();  // 上一页
                mgr.refreshCandidates();
                ci.cancel();
            } else if (key == GLFW.GLFW_KEY_RIGHT) {
                hud.nextPage();  // 下一页
                mgr.refreshCandidates();
                ci.cancel();
            } else if (key >= GLFW.GLFW_KEY_1 && key <= GLFW.GLFW_KEY_9) {
                // 数字键直接选择
                int index = key - GLFW.GLFW_KEY_1;
                int globalIndex = hud.getPage() * hud.getPerPage() + index;
                if (globalIndex < hud.getCandidates().size()) {
                    mgr.selectCandidate(globalIndex);
                    ci.cancel();
                }
            }
        } else {
            // 横式布局（现有逻辑）
            CandidateHud hud = mgr.getHorizontalHud();
            if (hud == null || !hud.isVisible()) return;
            
            if (key == GLFW.GLFW_KEY_LEFT || key == GLFW.GLFW_KEY_UP) {
                hud.prevPage();
                mgr.refreshCandidates();
                ci.cancel();
            } else if (key == GLFW.GLFW_KEY_RIGHT || key == GLFW.GLFW_KEY_DOWN) {
                hud.nextPage();
                mgr.refreshCandidates();
                ci.cancel();
            }
        }
    }
}
```

---

## 最终效果对比

| 输入法 | 布局 | 位置 | 翻页键 |
|--------|------|------|--------|
| **拼音** | 横式 | 左下角 | ← → |
| **五笔** | 横式 | 左下角 | ← → |
| **注音** | 竖式 | 右下角 | ↑ ↓ 选择，← → 翻页 |
| **仓颉** | 竖式 | 右下角 | ↑ ↓ 选择，← → 翻页 |
| **速成** | 竖式 | 右下角 | ↑ ↓ 选择，← → 翻页 |

---

## 需要修改的文件清单

| 文件 | 修改内容 |
|------|----------|
| `VerticalCandidateHud.java` | 新增竖式 HUD 类 |
| `PlatformIMEManager.java` | 添加布局切换逻辑 |
| `InGameHudMixin.java` | 渲染时判断布局 |
| `MouseMixin.java` | 鼠标点击检测竖式 |
| `KeyboardMixin.java` | 键盘翻页适配竖式 |
| `ime_state_manager.cpp` | 输入法类型检测 |
