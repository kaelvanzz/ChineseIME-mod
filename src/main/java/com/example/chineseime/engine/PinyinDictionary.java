package com.example.chineseime.engine;

import java.util.*;

public class PinyinDictionary {
    private static final Map<String, List<String>> DICTIONARY = new HashMap<>();

    static {
        addWords("zhong", "中", "中国", "中文", "中心", "中午", "重大", "重要", "终于", "主动");
        addWords("guo", "国", "中国", "国家", "国外", "国际", "国民", "国会", "国庆");
        addWords("ren", "人", "中国人", "人民", "别人", "大人", "人生", "人格", "人气");
        addWords("wo", "我", "我们", "我的", "我是", "我想", "我爱");
        addWords("ni", "你", "你好", "你的", "你们", "我想你");
        addWords("hao", "好", "你好", "很好", "好的", "好人", "好吃", "好看");
        addWords("shi", "是", "不是", "就是", "也是", "是的", "是不是", "是的");
        addWords("de", "的", "我的", "你的", "好的", "是的");
        addWords("zai", "在", "在这里", "在家", "在线", "在吗", "存在");
        addWords("jian", "见", "再见", "见到", "看见", "再见");
        addWords("dao", "到", "得到", "到达", "到处", "迟到");
        addWords("shi", "时", "时间", "时候", "小时", "有时", "准时");
        addWords("jian", "间", "时间", "中间", "房间", "空间", "人间");
        addWords("you", "有", "没有", "没有", "拥有", "有趣", "有用");
        addWords("mei", "没", "没有", "不是", "没事", "没关系");
        addWords("chu", "出", "出来", "出去", "出现", "出发", "出色");
        addWords("lai", "来", "回来", "起来", "出来", "下来", "未来");
        addWords("zuo", "做", "做事", "做法", "做饭", "做梦", "做梦");
        addWords("xi", "西", "东西", "西方", "西瓜", "西藏", "西欧");
        addWords("dong", "东", "东西", "东方", "东北", "东南", "东京");
        addWords("ma", "吗", "好吗", "是吗", "行吗", "可以", "忙吗");
        addWords("ne", "呢", "你呢", "什么呢", "怎么办");
        addWords("yao", "要", "不要", "需要", "重要", "要求");
        addWords("zhi", "知", "知道", "知识", "通知", "认知");
        addWords("dao", "道", "知道", "道理", "道路", "道德");
        addWords("ke", "可", "可以", "可能", "可是", "可爱", "可乐");
        addWords("yi", "一", "一个", "一样", "第一", "唯一", "万一");
        addWords("zhang", "张", "张开", "张老师", "纸张", "夸张");
        addWords("wang", "王", "王子", "国王", "王老师", "网", "上网");
        addWords("li", "李", "李老师", "李白", "李明", "里", "里面", "这里");
        addWords("liu", "刘", "刘老师", "刘备", "刘先生", "六", "六个", "六月");
        addWords("chen", "陈", "陈老师", "陈先生", "陈旧");
        addWords("he", "和", "和平", "和你", "和好", "合作", "何", "如何");
        addWords("hu", "湖", "湖州", "西湖", "湖南", "糊涂");
        addWords("tian", "天", "今天", "明天", "天气", "天空", "天津");
        addWords("qi", "气", "天气", "生气", "气球", "气愤", "吸气");
        addWords("qi", "其", "其实", "其他", "其中", "其次", "莫名其妙");
        addWords("ta", "他", "他们", "他的", "他人", "它", "它的");
        addWords("men", "们", "他们", "我们", "你们", "它们", "它们");
        addWords("she", "什", "什么", "怎么样", "为什么");
        addWords("me", "么", "什么", "怎么", "多么", "要么");
        addWords("zen", "怎", "怎么", "怎样", "怎么样");
        addWords("yang", "样", "怎么样", "同样", "一样", "样了");
        addWords("neng", "能", "能够", "能力", "能源", "能不能");
        addWords("gou", "够", "能够", "不够", "够了", "够用");
        addWords("jiang", "将", "将军", "将来", "将要", "大将");
        addWords("lai", "来", "起来", "回来", "来到", "来电", "来");
    }

    private static void addWords(String pinyin, String... words) {
        DICTIONARY.computeIfAbsent(pinyin, k -> new ArrayList<>()).addAll(Arrays.asList(words));
    }

    public static List<String> getSuggestions(String pinyin) {
        if (pinyin == null || pinyin.isEmpty()) {
            return Collections.emptyList();
        }

        String normalized = pinyin.toLowerCase().trim();
        List<String> results = DICTIONARY.get(normalized);

        if (results != null && !results.isEmpty()) {
            return new ArrayList<>(results);
        }

        for (Map.Entry<String, List<String>> entry : DICTIONARY.entrySet()) {
            if (entry.getKey().startsWith(normalized)) {
                return new ArrayList<>(entry.getValue());
            }
        }

        return Collections.emptyList();
    }

    public static List<String> getCandidatesForTest() {
        List<String> allWords = new ArrayList<>();
        for (List<String> words : DICTIONARY.values()) {
            allWords.addAll(words);
        }
        return allWords;
    }
}