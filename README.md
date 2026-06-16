

<p align="center">
     <img src="pictures/logo.png" alt="the_penguin" width="256px">
</p>

<h3 align="center">ChineseIME-mod</h3>

<p align="center">Minecraft Fabric 模組，為 Windows 上的中文輸入法提供狀態指示與候選詞顯示。</p>

## 功能

- **輸入法狀態指示器** — 在聊天框旁顯示當前輸入法類型（拼/注/倉/速/五）
- **候選詞 HUD** — 即時顯示輸入法候選詞列表
- **中英文模式指示** — Shift 切換時顯示黃色方塊，Caps Lock 時藍色背景


## 環境需求

| 項目 | 版本 |
|------|------|
| Minecraft | 1.21.4 |
| Fabric Loader | 0.16.9+ |
| Fabric API | 0.110.5+1.21.4 |
| Java | 21 |
| Windows | 10/11 |
| CMake | 3.15+ |
| MSVC | C++17 支援 |

## 支援的輸入法(Windows)

| 輸入法 | 架構 | 候選詞取得方式 |
|--------|------|--------------|
| 微軟拼音 | TSF | TSF API (`GUID_PROP_CANDIDATE`) |
| 微軟五筆 | TSF | TSF API (`GUID_PROP_CANDIDATE`) |
| 微軟注音 | TSF | TSF API (`GUID_PROP_CANDIDATE`) |
| 微軟倉頡 | IMM32 | `ImmGetCandidateList` |
| 微軟速成 | IMM32 | `ImmGetCandidateList` |

## 編譯

### 1. 編譯 C++ DLL

```bash
cd native
cmake -B build
cmake --build build --config Release
```

產出檔案：`natives/Release/chineseime_native.dll`

### 2. 編譯 Java 模組

```bash
./gradlew.bat clean build
```

產出檔案：`build/libs/chineseime-1.0.0.jar`

DLL 會自動嵌入 JAR 的 `META-INF/natives/` 中，啟動時解壓至 temp 目錄載入。

## 部署

將 JAR 和 DLL 複製到 Minecraft mods 資料夾：

```powershell
copy build\libs\chineseime-1.0.0.jar "<多啟動器實例路徑>\minecraft\mods\"
copy natives\Release\chineseime_native.dll "<多啟動器實例路徑>\minecraft\mods\"
```

## 除錯

- **C++ 端**：使用 [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) 檢視 `OutputDebugStringA` 輸出
- **Java 端**：搜尋 Minecraft 日誌中 `[ChineseIME]` 前綴
- **測試候選詞**：遊戲內按 `Ctrl+Shift+T` 顯示測試資料

## 快捷鍵

| 快捷鍵 | 功能 |
|--------|------|
| ↑/↓ 或 ←/→ | 選擇候選詞 |
| [ / ] | 翻頁 |
| Ctrl+Shift+T | 顯示測試候選詞（開發用） |



## 目錄結構

```
native/src/                    # C++ DLL 原始碼
├── ime_bridge.cpp             # 主入口、輪詢執行緒、匯出函式
├── win_event_bridge.cpp       # WndProc Hook (IMM32 通道)
├── tsf_monitor.cpp            # TSF 監控、候選詞讀取
├── imm32_monitor.cpp          # IMM32 備用監控
├── ime_state_manager.cpp      # 狀態管理、變化偵測
├── jni_callback.cpp           # JNA 回呼
└── sta_thread.cpp             # STA 執行緒管理

src/main/java/com/example/chineseime/
├── ChineseIMEInitializer.java # 主初始化器
├── platform/win32/
│   ├── NativeImeBridge.java   # JNA 介面定義
│   ├── WindowsIMEEventBridge.java  # 事件回呼處理
│   └── WindowsIMEBridgeNative.java # IME 橋接
├── hud/
│   ├── CandidateHud.java      # 橫式候選詞 HUD
│   ├── ImeStatusIndicator.java # 輸入法狀態指示器
│   └── VerticalCandidateHud.java # 豎式候選詞（倉頡/注音/速成）
└── engine/
    └── PinyinDictionary.java  # 內建拼音引擎

documents/                     # 開發文件
```

