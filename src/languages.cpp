#include "languages.hpp"

#include <cctype>
#include <map>
#include <utility>

namespace inop {

const std::vector<LanguageInfo>& supported_languages() {
    static const std::vector<LanguageInfo> v = {
        {"la", "Latin"},        {"en", "English"},    {"es", "Spanish"},
        {"ca", "Catalan"},      {"nl", "Dutch"},       {"pt", "Portuguese"},
        {"fr", "French"},       {"it", "Italian"},     {"de", "German"},
        {"id", "Indonesian"},   {"ms", "Malay"},       {"tl", "Tagalog"},
        {"zh", "Mandarin (Pinyin)"}, {"cs", "Czech"},   {"sk", "Slovak"},
        {"tr", "Turkish"},      {"ro", "Romanian"},    {"sl", "Slovenian"},
        {"mi", "Maori"},
    };
    return v;
}

bool is_supported_language(const std::string& code) {
    for (const auto& l : supported_languages())
        if (l.code == code) return true;
    return false;
}

namespace {

// ── global encode table ────────────────────────────────────────────────
// Every source form (upper and lower) folds to the same lowercase output.
// Longer sequences are matched before shorter ones by trying `multi` first.
struct FoldEntry { const char* src; const char* out; };

const std::vector<FoldEntry>& multi_fold_table() {
    // Catalan geminate l ("l·l", interpunct U+00B7) -> l8, in every case
    // combination an operator might type.
    static const std::vector<FoldEntry> v = {
        {"l\xC2\xB7l", "l8"}, {"L\xC2\xB7L", "l8"},
        {"L\xC2\xB7l", "l8"}, {"l\xC2\xB7L", "l8"},
    };
    return v;
}

const std::vector<FoldEntry>& single_fold_table() {
    static const std::vector<FoldEntry> v = {
        // macron -> 1
        {"ā", "a1"}, {"Ā", "a1"}, {"ē", "e1"}, {"Ē", "e1"}, {"ī", "i1"}, {"Ī", "i1"},
        {"ō", "o1"}, {"Ō", "o1"}, {"ū", "u1"}, {"Ū", "u1"},
        // acute -> 2
        {"á", "a2"}, {"Á", "a2"}, {"é", "e2"}, {"É", "e2"}, {"í", "i2"}, {"Í", "i2"},
        {"ó", "o2"}, {"Ó", "o2"}, {"ú", "u2"}, {"Ú", "u2"}, {"ý", "y2"}, {"Ý", "y2"},
        // caron / breve -> 3
        {"ǎ", "a3"}, {"Ǎ", "a3"}, {"ě", "e3"}, {"Ě", "e3"}, {"ǐ", "i3"}, {"Ǐ", "i3"},
        {"ǒ", "o3"}, {"Ǒ", "o3"}, {"ǔ", "u3"}, {"Ǔ", "u3"},
        {"š", "s3"}, {"Š", "s3"}, {"č", "c3"}, {"Č", "c3"}, {"ž", "z3"}, {"Ž", "z3"},
        {"ř", "r3"}, {"Ř", "r3"}, {"ň", "n3"}, {"Ň", "n3"}, {"ľ", "l3"}, {"Ľ", "l3"},
        {"ť", "t3"}, {"Ť", "t3"}, {"ď", "d3"}, {"Ď", "d3"},
        {"ă", "a3"}, {"Ă", "a3"}, {"ğ", "g3"}, {"Ğ", "g3"},
        // grave -> 4
        {"à", "a4"}, {"À", "a4"}, {"è", "e4"}, {"È", "e4"}, {"ì", "i4"}, {"Ì", "i4"},
        {"ò", "o4"}, {"Ò", "o4"}, {"ù", "u4"}, {"Ù", "u4"},
        // circumflex -> 5
        {"â", "a5"}, {"Â", "a5"}, {"ê", "e5"}, {"Ê", "e5"}, {"î", "i5"}, {"Î", "i5"},
        {"ô", "o5"}, {"Ô", "o5"}, {"û", "u5"}, {"Û", "u5"},
        // umlaut / diaeresis -> 6
        {"ä", "a6"}, {"Ä", "a6"}, {"ë", "e6"}, {"Ë", "e6"}, {"ï", "i6"}, {"Ï", "i6"},
        {"ö", "o6"}, {"Ö", "o6"}, {"ü", "u6"}, {"Ü", "u6"}, {"ÿ", "y6"}, {"Ÿ", "y6"},
        // tilde -> 7
        {"ã", "a7"}, {"Ã", "a7"}, {"ñ", "n7"}, {"Ñ", "n7"}, {"õ", "o7"}, {"Õ", "o7"},
        // genuine distinct letters, unique to one language -> 8
        {"ß", "s8"}, {"\xE1\xBA\x9E", "s8"},  // ß, ẞ
        {"ı", "i8"},                          // Turkish dotless i (İ handled separately)
        // dropped with no encoding — base letter only
        {"ç", "c"}, {"Ç", "c"}, {"ů", "u"}, {"Ů", "u"},
        {"ș", "s"}, {"Ș", "s"}, {"ț", "t"}, {"Ț", "t"},
        {"ş", "s"}, {"Ş", "s"},
    };
    return v;
}

std::string apply_fold_table(const std::string& s) {
    const auto& multi = multi_fold_table();
    const auto& single = single_fold_table();
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        bool matched = false;
        for (const auto& e : multi) {
            size_t n = std::char_traits<char>::length(e.src);
            if (s.compare(i, n, e.src) == 0) { out += e.out; i += n; matched = true; break; }
        }
        if (matched) continue;
        for (const auto& e : single) {
            size_t n = std::char_traits<char>::length(e.src);
            if (s.compare(i, n, e.src) == 0) { out += e.out; i += n; matched = true; break; }
        }
        if (matched) continue;
        unsigned char c = static_cast<unsigned char>(s[i]);
        // Punctuation and anything unrecognised is dropped here, same as
        // preprocess() would drop it later — the worked-examples "encoded"
        // form is already clean ASCII, not just diacritic-folded.
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(std::tolower(c));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
                 c == '#' || c == '/')
            out += s[i];
        ++i;
    }
    return out;
}

// ── per-language decode tables ─────────────────────────────────────────
using DecodeKey = std::pair<char, int>;
using DecodeTable = std::map<DecodeKey, std::string>;

DecodeTable make(std::initializer_list<std::pair<DecodeKey, const char*>> items) {
    DecodeTable t;
    for (auto& it : items) t[it.first] = it.second;
    return t;
}

const std::map<std::string, DecodeTable>& decode_tables() {
    static const std::map<std::string, DecodeTable> t = {
        {"la", make({{{'a',1},"ā"}, {{'e',1},"ē"}, {{'i',1},"ī"}, {{'o',1},"ō"}, {{'u',1},"ū"}})},

        {"en", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'a',4},"à"}, {{'e',4},"è"},
                      {{'a',5},"â"}, {{'e',5},"ê"}, {{'i',5},"î"}, {{'o',5},"ô"}, {{'u',5},"û"},
                      {{'a',6},"ä"}, {{'e',6},"ë"}, {{'i',6},"ï"}, {{'o',6},"ö"}, {{'u',6},"ü"},
                      {{'n',7},"ñ"}})},

        {"es", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'n',7},"ñ"}, {{'u',6},"ü"}})},

        {"ca", make({{{'a',4},"à"}, {{'e',4},"è"}, {{'o',4},"ò"},
                      {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'l',8},"l\xC2\xB7l"}})},

        {"nl", make({{{'e',6},"ë"}, {{'i',6},"ï"}, {{'o',6},"ö"}, {{'e',2},"é"}})},

        {"pt", make({{{'a',7},"ã"}, {{'o',7},"õ"},
                      {{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'a',5},"â"}, {{'e',5},"ê"}, {{'o',5},"ô"}, {{'a',4},"à"}})},

        {"fr", make({{{'e',2},"é"},
                      {{'a',4},"à"}, {{'e',4},"è"}, {{'u',4},"ù"},
                      {{'a',5},"â"}, {{'e',5},"ê"}, {{'i',5},"î"}, {{'o',5},"ô"}, {{'u',5},"û"},
                      {{'e',6},"ë"}, {{'i',6},"ï"}, {{'u',6},"ü"}, {{'y',6},"ÿ"}})},

        {"it", make({{{'a',4},"à"}, {{'e',4},"è"}, {{'i',4},"ì"}, {{'o',4},"ò"}, {{'u',4},"ù"},
                      {{'e',2},"é"}})},

        {"de", make({{{'a',6},"ä"}, {{'o',6},"ö"}, {{'u',6},"ü"}, {{'s',8},"ß"}})},

        {"id", make({})},
        {"ms", make({})},

        {"tl", make({{{'n',7},"ñ"}, {{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"},
                      {{'u',2},"ú"}})},

        {"zh", make({{{'a',1},"ā"}, {{'e',1},"ē"}, {{'i',1},"ī"}, {{'o',1},"ō"}, {{'u',1},"ū"},
                      {{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'a',3},"ǎ"}, {{'e',3},"ě"}, {{'i',3},"ǐ"}, {{'o',3},"ǒ"}, {{'u',3},"ǔ"},
                      {{'a',4},"à"}, {{'e',4},"è"}, {{'i',4},"ì"}, {{'o',4},"ò"}, {{'u',4},"ù"},
                      {{'u',6},"ü"}})},

        {"cs", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'y',2},"ý"},
                      {{'e',3},"ě"}, {{'s',3},"š"}, {{'c',3},"č"}, {{'r',3},"ř"}, {{'z',3},"ž"},
                      {{'d',3},"ď"}, {{'t',3},"ť"}, {{'n',3},"ň"}})},

        {"sk", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'y',2},"ý"}, {{'a',6},"ä"}, {{'o',5},"ô"},
                      {{'l',3},"ľ"}, {{'n',3},"ň"}, {{'s',3},"š"}, {{'c',3},"č"}, {{'z',3},"ž"},
                      {{'t',3},"ť"}, {{'d',3},"ď"}})},

        {"tr", make({{{'g',3},"ğ"}, {{'o',6},"ö"}, {{'u',6},"ü"}, {{'i',8},"ı"}})},

        {"ro", make({{{'a',5},"â"}, {{'i',5},"î"}, {{'a',3},"ă"}})},

        {"sl", make({{{'s',3},"š"}, {{'c',3},"č"}, {{'z',3},"ž"}})},

        {"mi", make({{{'a',1},"ā"}, {{'e',1},"ē"}, {{'i',1},"ī"}, {{'o',1},"ō"}, {{'u',1},"ū"}})},
    };
    return t;
}

}  // namespace

std::string fold_diacritics(const std::string& text, const std::string& language) {
    std::string work = text;
    if (language == "tr") {
        // Turkish-aware casing: dotted capital I (İ, U+0130) lowercases to
        // ordinary ascii 'i'; ordinary ascii 'I' is the CAPITAL of dotless
        // i, so it lowercases to dotless ı (U+0131) instead of 'i'. Must run
        // before the generic table, which would otherwise just lowercase
        // 'I' to 'i' and lose the distinction.
        std::string tmp;
        tmp.reserve(work.size());
        size_t i = 0;
        while (i < work.size()) {
            if (work.compare(i, 2, "\xC4\xB0") == 0) { tmp += 'i'; i += 2; }
            else if (work[i] == 'I') { tmp += "\xC4\xB1"; i += 1; }
            else { tmp += work[i]; i += 1; }
        }
        work = tmp;
    }
    return apply_fold_table(work);
}

std::string resubstitute(const std::string& text, const std::string& language) {
    auto lt = decode_tables().find(language);
    const DecodeTable empty;
    const DecodeTable& table = lt != decode_tables().end() ? lt->second : empty;

    std::string out;
    out.reserve(text.size());
    size_t i = 0, n = text.size();
    while (i < n) {
        char c = text[i];
        bool is_letter = c >= 'a' && c <= 'z';
        if (is_letter && i + 1 < n && std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
            int d = text[i + 1] - '0';
            auto it = table.find({c, d});
            if (it != table.end()) { out += it->second; i += 2; continue; }
        }
        if (is_letter && i + 1 < n && text[i + 1] == '/' && i + 2 < n &&
            std::isdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += c;
            out += text[i + 2];
            i += 3;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

}  // namespace inop
