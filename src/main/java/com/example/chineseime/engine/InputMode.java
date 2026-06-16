package com.example.chineseime.engine;

public enum InputMode {
    LATIN("英文", "Latin"),
    PINYIN("拼音", "Pinyin"),
    ZHUYIN("注音", "Zhuyin"),
    CANGJIE("仓颉", "Cangjie"),
    SUCHENG("速成", "Cangjie Quick"),
    WUBI("五笔", "Wubi"),
    YUEPIN("粤拼", "Cantonese Pinyin"),
    RIME("中州韵", "RIME"),
    OTHER("其他", "Other");

    private final String chineseName;
    private final String englishName;

    private InputMode(String chineseName, String englishName) {
        this.chineseName = chineseName;
        this.englishName = englishName;
    }

    public String getChineseName() {
        return this.chineseName;
    }

    public String getEnglishName() {
        return this.englishName;
    }

    public String getDisplayName() {
        return this.chineseName + " (" + this.englishName + ")";
    }
}
