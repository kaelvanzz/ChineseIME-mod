package com.example.chineseime.engine;

import java.util.*;

public class CangjieDictionary {
    private static final Map<String, List<String>> DICTIONARY = new HashMap<>();

    static {
        addWords("y", "日", "月", "水", "火");
        addWords("t", "木");
        addWords("g", "金");
        addWords("h", "土");
        addWords("k", "口");
        addWords("kv", "田");
        addWords("ka", "男");
        addWords("ki", "里");
        addWords("ku", "固");
        addWords("l", "心", "手");
        addWords("ln", "小");
        addWords("m", "中");
        addWords("n", "山");
        addWords("o", "夕");
        addWords("p", "卜");
        addWords("q", "早");
        addWords("r", "竹");
        addWords("s", "十");
        addWords("sh", "十");
        addWords("sf", "戈");
        addWords("st", "井");
        addWords("su", "辛");
        addWords("tq", "桌");
        addWords("tw", "天");
        addWords("u", "大");
        addWords("w", "人");
        addWords("wa", "介");
        addWords("wf", "合");
        addWords("wg", "全");
        addWords("wu", "共");
        addWords("x", "難");
        addWords("xx", "雙");
        addWords("y", "日", "曰", "月", "水", "火", "yr", "竹", "yrh", "行");
        addWords("yy", "昌", "昊", "明", "林", "森");
        addWords("ye", "艮");
        addWords("yev", "眉");
        addWords("yg", "且");
        addWords("yhh", "亘");
        addWords("yhs", "衛");
        addWords("ym", "曲");
        addWords("ymp", "暢");
        addWords("yp", "足");
        addWords("ypo", "是正");
        addWords("yr", "竹", "毛");
        addWords("ys", "臼");
        addWords("yt", "禹");
        addWords("ytr", "街");
        addWords("yy", "炎", "紋", "紋");
        addWords("yw", "凶");
        addWords("yx", "凵");
        addWords("yy", "刃");
    }

    private static void addWords(String code, String... words) {
        DICTIONARY.computeIfAbsent(code.toLowerCase(), k -> new ArrayList<>()).addAll(Arrays.asList(words));
    }

    public static List<String> getSuggestions(String cangjieCode) {
        if (cangjieCode == null || cangjieCode.isEmpty()) {
            return Collections.emptyList();
        }

        String normalized = cangjieCode.toLowerCase().trim();
        List<String> results = DICTIONARY.get(normalized);

        if (results != null && !results.isEmpty()) {
            return new ArrayList<>(results);
        }

        if (normalized.length() >= 2) {
            String prefix = normalized.substring(0, normalized.length() - 1);
            for (Map.Entry<String, List<String>> entry : DICTIONARY.entrySet()) {
                if (entry.getKey().startsWith(prefix) && !entry.getKey().equals(normalized)) {
                    return new ArrayList<>(entry.getValue());
                }
            }
        }

        return Collections.emptyList();
    }

    public static List<String> getCandidatesForTest() {
        List<String> all = new ArrayList<>();
        for (List<String> words : DICTIONARY.values()) {
            all.addAll(words);
        }
        return all;
    }
}