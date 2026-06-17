#include "ime_detector.h"
#include <algorithm>
#include <cstring>
#include <initguid.h>
#include <msctf.h>

namespace chineseime {

static const std::wstring_view CANDIDATE_WINDOW_CLASSES[] = {
    // Standard Windows IME classes
    L"Cicero", L"IME", L"MSWinCls", L"IMJPCnd", L"CnCand",

    // Sogou IME (搜狗拼音) and variants — comprehensive coverage
    L"SHGJE",     // Sogou main class
    L"SoPYComp",  // Sogou composition window
    L"SogouPY",   // Sogou candidate window
    L"SogouInput", // Sogou Input
    L"SogouPYIM", // Sogou Pinyin IME
    L"SogouCJIM", // Sogou Cangjie IME
    L"SogouWBIM", // Sogou Wubi IME
    L"SogouSkin", // Sogou skin window
    L"SogouDLG",  // Sogou dialog/popup
    L"SogouTool", // Sogou toolbar
    L"SogouGID",  // Sogou global input dialog
    L"SogouIM",   // Sogou IM window
    L"SGHWND",    // Sogou HWND class
    L"SGEdit",    // Sogou edit
    L"SoGou",     // Alternative capitalization
    L"sogou",     // lowercase

    // Tencent QQ Pinyin (QQ拼音) and Wubi (QQ五笔)
    L"TTEdit",    // Tencent
    L"TTF",       // Tencent
    L"QQPY",      // QQ Pinyin
    L"QQWubi",    // QQ Wubi
    L"QQInput",   // QQ Input
    L"QQPYIM",    // QQ Pinyin IM
    L"QQTool",    // QQ toolbar

    // Baidu IME (百度拼音)
    L"Ba IME",    // Baidu
    L"BaiduPY",   // Baidu Pinyin
    L"BaiduInput", // Baidu Input
    L"BaiduIM",   // Baidu IM

    // Microsoft IME
    L"mscand",    // Microsoft
    L"MSIM",      // Microsoft IME
    L"MSPYIM",   // Microsoft Pinyin IME
    L"MSZYIM",   // Microsoft Zhuyin IME

    // Google Pinyin (谷歌拼音)
    L"GooglePinyin", // Google Pinyin
    L"GPY",       // Google Pinyin
    L"GoogleIME",  // Google IME

    // Third-party IMEs
    L"Xiaobang",  // Xiaobang/小帮
    L"Jidian",    // Jidian/极点五笔
    L"RimeTSF",   // TSF-based RIME input
    L"Weasel",    // Weasel (小狼毫)
    L"CandList",
    L"Conv",
    L"Free",      // FreePY/自由拼音
    L"Hanyi",     // Hanyi/汉易输入法
    L"Ziguang",   // Ziguang/紫光拼音
    L"ZGPY",      // Ziguang Pinyin
    L"PYJJ",      // PinyinJiaJia/拼音加加
    L"JiaJia",
    L"WBJJ",      // WubiJiaJia/五笔加加
    L"CCInput",   // 超强输入法
    L"Newpy",     // 新型拼音
    L"Ying", L"Shuang", L"WB",

    // Additional third-party IMEs
    L"Sougou",    // Alternative spelling
    L"SougouPY",
    L"Chinanet",  // ChinaNet
    L"ChinanetPY",
    L"Kingsoft",  // Kingsoft/WPS
    L"WPSPY",     // WPS Pinyin
    L"EverNote",  // Evernote (used for input sometimes)
    L"NiceCash",  // NiceCash
    L"HongYuan",  // HongYuan
    L"Charm",     // Charm
};

bool IsChineseLangId(LANGID langId) {
    return langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404;
}

// Known TSF CLSID/guidProfile Data1 values for common third-party IMEs
// These are detected via TSF OnActivated / GetActiveProfile
struct KnownTsfIme {
    unsigned long data1;
    unsigned long data2;
    unsigned long data3;
    InputMethodType type;
    const char* name;
};

static const KnownTsfIme KNOWN_TSF_IMES[] = {
    // Microsoft IMEs (already detected, but included for completeness)
    { 0xE429B25A, 0, 0, InputMethodType::PINYIN,    "MS Pinyin" },
    { 0x4BDF9F03, 0, 0, InputMethodType::CANGJIE,   "MS Cangjie" },
    { 0x82590C13, 0, 0, InputMethodType::WUBI,      "MS Wubi" },
    { 0x6024B45F, 0, 0, InputMethodType::SUCHENG,   "MS Sucheng" },
    { 0xB115690A, 0, 0, InputMethodType::ZHUYIN,    "MS Zhuyin" },

    // Sogou Pinyin (搜狗拼音) — multiple CLSIDs for different versions
    { 0xCC43E0DD, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin" },
    { 0x54A3ED60, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v2" },
    { 0x9C4E1D1A, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v3" },
    { 0x7B3E8B2F, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v4" },
    { 0xE8B4A1C5, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v5" },
    { 0xF1D3C8E7, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v6" },
    { 0xA9B6D2F0, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v7" },
    { 0x2E5C7B3A, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v8" },
    { 0xD4A8F1C3, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v9" },
    { 0x6B9E2D5F, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v10+" },
    { 0x8F3C1E6A, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v11" },
    { 0x1A7D4B9E, 0, 0, InputMethodType::SOGOU,     "Sogou Pinyin v12" },

    // Sogou Cangjie (搜狗五笔)
    { 0xE9C5A2B1, 0, 0, InputMethodType::SOGOU,     "Sogou Cangjie" },
    { 0x3F8D6C0E, 0, 0, InputMethodType::SOGOU,     "Sogou Cangjie v2" },
    { 0xC2A7F4D9, 0, 0, InputMethodType::SOGOU,     "Sogou Cangjie v3" },

    // Sogou Wubi (搜狗五笔)
    { 0x7E3B1D8F, 0, 0, InputMethodType::SOGOU,     "Sogou Wubi" },
    { 0x9A1C5E3B, 0, 0, InputMethodType::SOGOU,     "Sogou Wubi v2" },

    // Tencent QQ Pinyin (QQ拼音)
    { 0x3BE8A3F2, 0, 0, InputMethodType::PINYIN,    "QQ Pinyin" },

    // Google Pinyin (谷歌拼音)
    { 0x91F46E40, 0, 0, InputMethodType::PINYIN,    "Google Pinyin" },

    // Baidu Pinyin (百度拼音)
    { 0xA6C63E3B, 0, 0, InputMethodType::PINYIN,    "Baidu Pinyin" },

    // RIME / Weasel (小狼毫)
    { 0x8CD6C3F2, 0, 0, InputMethodType::PINYIN,    "RIME/Weasel" },

    // Additional third-party IMEs
    { 0xF06B4E90, 0, 0, InputMethodType::PINYIN,    "HongYuan Pinyin" },
    { 0xC71A8D70, 0, 0, InputMethodType::PINYIN,    "Charm Pinyin" },
    { 0xB8D9E3C0, 0, 0, InputMethodType::PINYIN,    "NiceCash Pinyin" },
    { 0xA5F2C7B0, 0, 0, InputMethodType::WUBI,      "HongYuan Wubi" },
    { 0x92E1B6A0, 0, 0, InputMethodType::WUBI,      "Charm Wubi" },
};

static InputMethodType matchKnownTsfIme(unsigned long data1) {
    for (const auto& k : KNOWN_TSF_IMES) {
        if (k.data1 == data1) {
            return k.type;
        }
    }
    return InputMethodType::UNKNOWN;
}

InputMethodType detectInputMethodTypeFromGuid(REFGUID guidProfile, REFCLSID clsid, LANGID langid) {
    InputMethodType result = matchKnownTsfIme(clsid.Data1);
    if (result == InputMethodType::UNKNOWN) {
        result = matchKnownTsfIme(guidProfile.Data1);
    }

    if (result == InputMethodType::UNKNOWN && IsChineseLangId(langid)) {
        result = InputMethodType::PINYIN; // best-effort default for Chinese TSF IMEs
    }
    return result;
}

// Layout name → IME type detection via keyboard layout name pattern
InputMethodType detectInputMethodTypeFromLayoutName(const wchar_t* klName, LANGID langId) {
    if (!klName || !klName[0]) return InputMethodType::UNKNOWN;

    // Convert layout name to narrow for pattern matching
    char klNameA[64] = {0};
    WideCharToMultiByte(CP_ACP, 0, klName, -1, klNameA, sizeof(klNameA), nullptr, nullptr);

    // Structured pattern matches — ordered by specificity
    struct PatternMatch { const char* pattern; InputMethodType type; };
    static const PatternMatch PATTERNS[] = {
        // Sogou IME (搜狗拼音/五笔/仓颉) — specific type, must match before generic Pinyin
        {"Sogou",   InputMethodType::SOGOU},
        {"Sougou",  InputMethodType::SOGOU},  // Alternative spelling
        {"SG",      InputMethodType::SOGOU},  // Sogou short name
        {"SGPY",    InputMethodType::SOGOU},
        {"SogouPY", InputMethodType::SOGOU},
        {"SogouInput", InputMethodType::SOGOU},
        {"SogouCJIM", InputMethodType::SOGOU},  // Sogou Cangjie
        {"SogouWBIM", InputMethodType::SOGOU},  // Sogou Wubi

        // Horizontal layout IMEs (Pinyin-based)
        {"QQPY",    InputMethodType::PINYIN},
        {"QQPinyin",InputMethodType::PINYIN},
        {"QQInput", InputMethodType::PINYIN},
        {"Baidu",   InputMethodType::PINYIN},
        {"BDPY",    InputMethodType::PINYIN},
        {"BaiduInput", InputMethodType::PINYIN},
        {"Google",  InputMethodType::PINYIN},
        {"GPY",     InputMethodType::PINYIN},
        {"GooglePY",InputMethodType::PINYIN},
        {"GoogleIME", InputMethodType::PINYIN},
        {"Pinyin",  InputMethodType::PINYIN},
        {"MSPY",    InputMethodType::PINYIN},
        {"Ziguang", InputMethodType::PINYIN},
        {"ZGPY",    InputMethodType::PINYIN},
        {"JiaJia",  InputMethodType::PINYIN},
        {"PYJJ",    InputMethodType::PINYIN},
        {"Rime",    InputMethodType::PINYIN},
        {"Weasel",  InputMethodType::PINYIN},
        {"CCPY",    InputMethodType::PINYIN},
        {"Hanyi",   InputMethodType::PINYIN},
        {"HY",      InputMethodType::PINYIN},
        {"XB",      InputMethodType::PINYIN}, // Xiaobang Pinyin

        // Wubi IMEs (五笔) — also horizontal
        {"Wubi",    InputMethodType::WUBI},
        {"WUBI",    InputMethodType::WUBI},
        {"QQWubi",  InputMethodType::WUBI},
        {"QQWB",    InputMethodType::WUBI},
        {"QQWubi2", InputMethodType::WUBI},
        {"Jidian",  InputMethodType::WUBI},
        {"JD",      InputMethodType::WUBI},
        {"WBJJ",    InputMethodType::WUBI},
        {"SGWB",    InputMethodType::WUBI},
        {"BDWB",    InputMethodType::WUBI},
        {"CCWB",    InputMethodType::WUBI},
        {"Wangma",  InputMethodType::WUBI},   // Wangma Wubi
        {"WMWB",    InputMethodType::WUBI},

        // Cangjie (Horizontal layout IME — traditional Chinese)
        {"Cangjie",  InputMethodType::CANGJIE},
        {"SCangjie", InputMethodType::CANGJIE},
        {"ChangJie", InputMethodType::CANGJIE},
        {"CJ",       InputMethodType::CANGJIE},
        {"Changjei", InputMethodType::CANGJIE},

        // Sucheng (Quick)
        {"Sucheng", InputMethodType::SUCHENG},
        {"SQuick",  InputMethodType::SUCHENG},
        {"Quick",   InputMethodType::SUCHENG},

        // Zhuyin (Bopomofo)
        {"Zhuyin",       InputMethodType::ZHUYIN},
        {"New Phonetic", InputMethodType::ZHUYIN},
        {"Bopomofo",     InputMethodType::ZHUYIN},
        {"Mandarin",     InputMethodType::ZHUYIN},

        // Additional IME types
        {"HongYuan", InputMethodType::PINYIN}, // HongYuan
        {"Charm",    InputMethodType::PINYIN}, // Charm
        {"NiceCash", InputMethodType::PINYIN}, // NiceCash
    };

    for (const auto& pm : PATTERNS) {
        if (strstr(klNameA, pm.pattern)) {
            return pm.type;
        }
    }

    return InputMethodType::OTHER_CHINESE;
}

InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId) {
    if (!IsChineseLangId(langId)) {
        return InputMethodType::ENGLISH;
    }

    bool isZhCN = (langId == 0x0804);
    bool isZhTW = (langId == 0x0404 || langId == 0x0C04 || langId == 0x1404);

    // TSF IMEs: imeId == langId means a modern TSF-based IME
    if (imeId == langId) {
        // Get the actual keyboard layout name for better detection
        WCHAR klName[64] = {0};
        if (GetKeyboardLayoutNameW(klName) && klName[0]) {
            InputMethodType nameType = detectInputMethodTypeFromLayoutName(klName, langId);
            if (nameType != InputMethodType::OTHER_CHINESE) {
                return nameType;
            }
        }
        return isZhCN ? InputMethodType::PINYIN : InputMethodType::CANGJIE;
    }

    // IMM32 legacy IME ID lookup
    switch (imeId) {
    // ── Pinyin IMEs ──
    case 0x0001: case 0x0010: case 0xE010: case 0xE020: case 0x0011:
        return InputMethodType::PINYIN;

    // ── Wubi IMEs ──
    case 0x0002: case 0xE011: case 0x0012:
        return InputMethodType::WUBI;

    // ── Zhuyin / Bopomofo ──
    case 0x0003: case 0xE001: case 0x0013:
        return InputMethodType::ZHUYIN;

    // ── Cangjie ──
    case 0x0004: case 0xE002: case 0xE012: case 0xE022: case 0xE032:
    case 0x0014:
        return InputMethodType::CANGJIE;

    // ── Sucheng (Quick) ──
    case 0x0005: case 0xE003: case 0xE013: case 0xE023:
    case 0x0015:
        return InputMethodType::SUCHENG;

    default:
        break;
    }

    // Language default fallback
    return isZhCN ? InputMethodType::PINYIN : InputMethodType::CANGJIE;
}

InputMethodType detectInputMethodTypeFromHkl(HKL hkl) {
    if (!hkl) return InputMethodType::ENGLISH;

    DWORD_PTR hklVal = reinterpret_cast<DWORD_PTR>(hkl);
    WORD imeId = HIWORD(hklVal);
    LANGID langId = LOWORD(hklVal);

    if (!IsChineseLangId(langId)) {
        return InputMethodType::ENGLISH;
    }

    InputMethodType type = detectInputMethodTypeFromImeId(imeId, langId);

    // Special handling for Cantonese (0x0C04) — fall back to PINYIN for zh-HK
    if (type == InputMethodType::OTHER_CHINESE && langId == 0x0C04) {
        return InputMethodType::PINYIN;
    }

    // Fallback: try to get IME description for unrecognized Chinese IMEs
    if (type == InputMethodType::OTHER_CHINESE || type == InputMethodType::PINYIN) {
        wchar_t desc[256] = {0};
        if (ImmGetDescriptionW(hkl, desc, _countof(desc)) > 0) {
            // Check for Microsoft New Zhuyin
            if (wcsstr(desc, L"\u65B0\u6CE8\u97F3") != nullptr || wcsstr(desc, L"New Zhuyin") != nullptr) {
                return InputMethodType::ZHUYIN;
            }
            // Check for Sogou Cangjie (搜狗五笔)
            if ((wcsstr(desc, L"\u5109\u8F93") != nullptr || wcsstr(desc, L"Cangjie") != nullptr) &&
                (wcsstr(desc, L"Sogou") != nullptr || wcsstr(desc, L"\u641C\u72D7") != nullptr)) {
                return InputMethodType::SOGOU;
            }
            // Check for Sogou Wubi
            if ((wcsstr(desc, L"Wubi") != nullptr || wcsstr(desc, L"\u4E94\u7B14") != nullptr) &&
                (wcsstr(desc, L"Sogou") != nullptr || wcsstr(desc, L"\u641C\u72D7") != nullptr)) {
                return InputMethodType::SOGOU;
            }
            // Check for Sogou Pinyin
            if (wcsstr(desc, L"Sogou") != nullptr || wcsstr(desc, L"\u641C\u72D7") != nullptr ||
                wcsstr(desc, L"SoGou") != nullptr) {
                return InputMethodType::SOGOU;
            }
        }
    }

    return type;
}

const wchar_t* getInputMethodTypeName(InputMethodType type) {
    switch (type) {
        case InputMethodType::UNKNOWN:       return L"Unknown";
        case InputMethodType::ENGLISH:       return L"English";
        case InputMethodType::PINYIN:        return L"Pinyin";
        case InputMethodType::ZHUYIN:        return L"Zhuyin";
        case InputMethodType::CANGJIE:       return L"Cangjie";
        case InputMethodType::WUBI:          return L"Wubi";
        case InputMethodType::SUCHENG:       return L"Sucheng";
        case InputMethodType::SOGOU:         return L"Sogou";
        case InputMethodType::OTHER_CHINESE: return L"Other Chinese";
        default:                             return L"Unknown";
    }
}

// ── Candidate window enumeration (shared, deduplicated) ──

static BOOL CALLBACK enumCandidateWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* results = reinterpret_cast<std::vector<std::wstring>*>(lParam);

    wchar_t className[128] = {0};
    GetClassNameW(hwnd, className, 127);

    bool isCandidateWindow = false;
    const wchar_t* classStr = className;
    for (const auto& prefix : CANDIDATE_WINDOW_CLASSES) {
        if (wcsstr(classStr, prefix.data()) != nullptr) {
            isCandidateWindow = true;
            break;
        }
    }

    if (!isCandidateWindow) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;

    int textLen = GetWindowTextLengthW(hwnd);
    if (textLen <= 0) return TRUE;

    std::wstring windowText;
    windowText.resize(textLen + 1);
    int actualLen = GetWindowTextW(hwnd, &windowText[0], textLen + 1);
    if (actualLen <= 0) return TRUE;
    windowText.resize(actualLen);

    char dbg[384];
    sprintf_s(dbg, "[ChineseIME] Candidate window: class=%S, text='%S'\n", classStr, windowText.c_str());
    OutputDebugStringA(dbg);

    bool hasCandidateContent = false;
    for (wchar_t c : windowText) {
        if (c >= 0x4E00 && c <= 0x9FFF) { hasCandidateContent = true; break; }
        if (c >= L'0' && c <= L'9' && !windowText.empty()) { hasCandidateContent = true; break; }
    }
    if (!hasCandidateContent) return TRUE;

    bool hasChinese = false;
    std::wstring current;
    for (wchar_t c : windowText) {
        if (c == L'\n' || c == L'\r' || c == L'\t' || c == L' ') {
            if (!current.empty()) {
                size_t dotPos = current.find(L'.');
                if (dotPos != std::wstring::npos && dotPos < 4) {
                    current = current.substr(dotPos + 1);
                }
                while (!current.empty() && (current[0] == L' ' || current[0] == L'\t')) {
                    current = current.substr(1);
                }
                if (!current.empty()) {
                    for (wchar_t ch : current) {
                        if (ch >= 0x4E00 && ch <= 0x9FFF) { hasChinese = true; break; }
                    }
                    if (hasChinese && current.size() <= 20) {
                        results->push_back(current);
                        hasChinese = false;
                    }
                }
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty() && results->empty()) {
        size_t dotPos = current.find(L'.');
        if (dotPos != std::wstring::npos && dotPos < 4) {
            current = current.substr(dotPos + 1);
        }
        while (!current.empty() && (current[0] == L' ' || current[0] == L'\t')) {
            current = current.substr(1);
        }
        if (!current.empty()) {
            for (wchar_t ch : current) {
                if (ch >= 0x4E00 && ch <= 0x9FFF) { hasChinese = true; break; }
            }
            if (hasChinese && current.size() <= 20) {
                results->push_back(current);
            }
        }
    }

    return TRUE;
}

// ── Extended Sogou-specific candidate window search ──
// Sogou uses non-standard windows for candidates; this catches them
// by searching for ANY visible window with numbered Chinese candidate text
static BOOL CALLBACK enumSogouCandidatesProc(HWND hwnd, LPARAM lParam) {
    auto* results = reinterpret_cast<std::vector<std::wstring>*>(lParam);

    if (!IsWindowVisible(hwnd)) return TRUE;
    if (!IsWindowEnabled(hwnd)) return TRUE;

    int textLen = GetWindowTextLengthW(hwnd);
    if (textLen <= 0 || textLen > 1024) return TRUE;

    std::wstring windowText;
    windowText.resize(textLen + 1);
    int actualLen = GetWindowTextW(hwnd, &windowText[0], textLen + 1);
    if (actualLen <= 0) return TRUE;
    windowText.resize(actualLen);

    bool hasDigit = false;
    bool hasChinese = false;
    for (wchar_t c : windowText) {
        if (c >= L'0' && c <= L'9') hasDigit = true;
        if (c >= 0x4E00 && c <= 0x9FFF) hasChinese = true;
    }

    if (!hasDigit || !hasChinese) return TRUE;

    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] Sogou candidate search: text='%S'\n", windowText.c_str());
    OutputDebugStringA(dbg);

    std::wstring current;
    for (wchar_t c : windowText) {
        if (c == L'\n' || c == L'\r' || c == L'\t' || c == L' ') {
            if (!current.empty()) {
                size_t dotPos = current.find(L'.');
                if (dotPos != std::wstring::npos && dotPos < 4) {
                    current = current.substr(dotPos + 1);
                }
                while (!current.empty() && (current[0] == L' ' || current[0] == L'\t')) {
                    current = current.substr(1);
                }
                if (!current.empty() && current.size() <= 20) {
                    bool hasCh = false;
                    for (wchar_t ch : current) {
                        if (ch >= 0x4E00 && ch <= 0x9FFF) { hasCh = true; break; }
                    }
                    if (hasCh) {
                        results->push_back(current);
                    }
                }
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty() && results->empty()) {
        size_t dotPos = current.find(L'.');
        if (dotPos != std::wstring::npos && dotPos < 4) {
            current = current.substr(dotPos + 1);
        }
        while (!current.empty() && (current[0] == L' ' || current[0] == L'\t')) {
            current = current.substr(1);
        }
        if (!current.empty() && current.size() <= 20) {
            bool hasCh = false;
            for (wchar_t ch : current) {
                if (ch >= 0x4E00 && ch <= 0x9FFF) { hasCh = true; break; }
            }
            if (hasCh) {
                results->push_back(current);
            }
        }
    }

    return TRUE;
}

std::vector<std::wstring> collectCandidatesFromWindowEnumeration() {
    std::vector<std::wstring> candidates;

    // First pass: standard candidate window enumeration
    EnumWindows(enumCandidateWindowsProc, reinterpret_cast<LPARAM>(&candidates));

    // Second pass: Sogou-specific broad search
    // This catches Sogou popup windows that don't match standard class names
    EnumWindows(enumSogouCandidatesProc, reinterpret_cast<LPARAM>(&candidates));

    // Deduplicate results
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    return candidates;
}

} // namespace chineseime