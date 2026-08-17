#include "languages.hpp"

#include <cctype>
#include <map>
#include <utility>

namespace inop {

const std::vector<LanguageInfo>& supported_languages() {
    static const std::vector<LanguageInfo> v = {
        {"sqi", "Albanian"},  {"eus", "Basque"},    {"bos", "Bosnian"},
        {"yue", "Cantonese"}, {"cat", "Catalan"},   {"cpf", "Creole"},    {"hrv", "Croatian"},
        {"czr", "Czech"},     {"dan", "Danish"},    {"nld", "Dutch"},
        {"eng", "English"},   {"est", "Estonian"},
        {"fin", "Finnish"},   {"fra", "French"},    {"deu", "German"},
        {"heb", "Hebrew (Latin)"}, {"hin", "Hindi (Latin)"},
        {"hun", "Hungarian"}, {"ibo", "Igbo"}, {"ind", "Indonesian"}, {"gle", "Irish"},
        {"ita", "Italian"},   {"kor", "Korean (Latin)"},
        {"kmr", "Kurdish (Kurmanji)"}, {"lat", "Latin"},
        {"lit", "Lithuanian"}, {"ltz", "Luxembourgish"}, {"mly", "Malay"},
        {"mlt", "Maltese"},
        {"cmn", "Mandarin (Pinyin)"}, {"mri", "Maori"}, {"cnr", "Montenegrin"},
        {"nor", "Norwegian"},
        {"pol", "Polish"},    {"por", "Portuguese"}, {"ron", "Romanian"},
        {"gla", "Scottish Gaelic"}, {"srp", "Serbian (Latin)"},
        {"svk", "Slovak"},    {"slv", "Slovenian"}, {"som", "Somali"},
        {"spa", "Spanish"},   {"swa", "Swahili"},   {"swe", "Swedish"},
        {"tgl", "Tagalog"},   {"tur", "Turkish"},   {"cym", "Welsh"},
        {"yor", "Yoruba"},
        {"zul", "Zulu/Xhosa"},
    };
    return v;
}

bool is_supported_language(const std::string& code) {
    for (const auto& l : supported_languages())
        if (l.code == code) return true;
    return false;
}

namespace {

struct FoldEntry { const char* src; const char* out; };

const std::vector<FoldEntry>& multi_fold_table() {
    static const std::vector<FoldEntry> v = {};
    return v;
}

const std::vector<FoldEntry>& single_fold_table() {
    static const std::vector<FoldEntry> v = {
        {"ā", "a1"}, {"Ā", "a1"}, {"ē", "e1"}, {"Ē", "e1"}, {"ī", "i1"}, {"Ī", "i1"},
        {"ō", "o1"}, {"Ō", "o1"}, {"ū", "u1"}, {"Ū", "u1"},
        {"á", "a2"}, {"Á", "a2"}, {"é", "e2"}, {"É", "e2"}, {"í", "i2"}, {"Í", "i2"},
        {"ó", "o2"}, {"Ó", "o2"}, {"ú", "u2"}, {"Ú", "u2"}, {"ý", "y2"}, {"Ý", "y2"},
        {"ć", "c2"}, {"Ć", "c2"}, {"ĺ", "l2"}, {"Ĺ", "l2"}, {"ń", "n2"}, {"Ń", "n2"},
        {"ŕ", "r2"}, {"Ŕ", "r2"}, {"ś", "s2"}, {"Ś", "s2"}, {"ź", "z2"}, {"Ź", "z2"},
        {"ǎ", "a3"}, {"Ǎ", "a3"}, {"ě", "e3"}, {"Ě", "e3"}, {"ǐ", "i3"}, {"Ǐ", "i3"},
        {"ǒ", "o3"}, {"Ǒ", "o3"}, {"ǔ", "u3"}, {"Ǔ", "u3"},
        {"š", "s3"}, {"Š", "s3"}, {"č", "c3"}, {"Č", "c3"}, {"ž", "z3"}, {"Ž", "z3"},
        {"ř", "r3"}, {"Ř", "r3"}, {"ň", "n3"}, {"Ň", "n3"}, {"ľ", "l3"}, {"Ľ", "l3"},
        {"ť", "t3"}, {"Ť", "t3"}, {"ď", "d3"}, {"Ď", "d3"},
        {"ă", "a3"}, {"Ă", "a3"}, {"ğ", "g3"}, {"Ğ", "g3"},
        {"à", "a4"}, {"À", "a4"}, {"è", "e4"}, {"È", "e4"}, {"ì", "i4"}, {"Ì", "i4"},
        {"ò", "o4"}, {"Ò", "o4"}, {"ù", "u4"}, {"Ù", "u4"},
        {"â", "a5"}, {"Â", "a5"}, {"ê", "e5"}, {"Ê", "e5"}, {"î", "i5"}, {"Î", "i5"},
        {"ô", "o5"}, {"Ô", "o5"}, {"û", "u5"}, {"Û", "u5"},
        {"ŵ", "w5"}, {"Ŵ", "w5"}, {"ŷ", "y5"}, {"Ŷ", "y5"},
        {"ä", "a6"}, {"Ä", "a6"}, {"ë", "e6"}, {"Ë", "e6"}, {"ï", "i6"}, {"Ï", "i6"},
        {"ö", "o6"}, {"Ö", "o6"}, {"ü", "u6"}, {"Ü", "u6"}, {"ÿ", "y6"}, {"Ÿ", "y6"},
        {"ǖ", "u61"}, {"Ǖ", "u61"}, {"ǘ", "u62"}, {"Ǘ", "u62"},
        {"ǚ", "u63"}, {"Ǚ", "u63"}, {"ǜ", "u64"}, {"Ǜ", "u64"},
        {"ő", "o22"}, {"Ő", "o22"}, {"ű", "u22"}, {"Ű", "u22"},
        {"ã", "a7"}, {"Ã", "a7"}, {"ñ", "n7"}, {"Ñ", "n7"}, {"õ", "o7"}, {"Õ", "o7"},
        {"ẽ", "e7"}, {"Ẽ", "e7"}, {"ĩ", "i7"}, {"Ĩ", "i7"}, {"ũ", "u7"}, {"Ũ", "u7"},
        {"ç", "c8"}, {"Ç", "c8"}, {"ş", "s8"}, {"Ş", "s8"},
        {"ą", "a8"}, {"Ą", "a8"}, {"ę", "e8"}, {"Ę", "e8"},
        {"į", "i8"}, {"Į", "i8"}, {"ų", "u8"}, {"Ų", "u8"},
        {"å", "a9"}, {"Å", "a9"}, {"ů", "u9"}, {"Ů", "u9"},
        {"ė", "e33"}, {"Ė", "e33"}, {"ċ", "c33"}, {"Ċ", "c33"},
        {"ġ", "g33"}, {"Ġ", "g33"}, {"ż", "z33"}, {"Ż", "z33"},
        {"ẹ", "e88"}, {"Ẹ", "e88"}, {"ọ", "o88"}, {"Ọ", "o88"},
        {"ị", "i88"}, {"Ị", "i88"}, {"ụ", "u88"}, {"Ụ", "u88"},
        {"ṣ", "s88"}, {"Ṣ", "s88"}, {"ṭ", "t88"}, {"Ṭ", "t88"},
        {"ḍ", "d88"}, {"Ḍ", "d88"}, {"ṇ", "n88"}, {"Ṇ", "n88"},
        {"ḥ", "h88"}, {"Ḥ", "h88"}, {"ṃ", "m88"}, {"Ṃ", "m88"},
        {"ṛ", "r88"}, {"Ṛ", "r88"}, {"ḷ", "l88"}, {"Ḷ", "l88"},
        {"ṅ", "n33"}, {"Ṅ", "n33"},
        {"ı", "i0"},
        {"ß", "s0"}, {"\xE1\xBA\x9E", "s0"},
        {"đ", "d0"}, {"Đ", "d0"},
        {"ł", "l0"}, {"Ł", "l0"},
        {"ø", "o0"}, {"Ø", "o0"},
        {"ħ", "h0"}, {"Ħ", "h0"},
        {"œ", "oe"}, {"Œ", "oe"}, {"æ", "ae"}, {"Æ", "ae"},
        {"ș", "s"}, {"Ș", "s"}, {"ț", "t"}, {"Ț", "t"},
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
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(std::tolower(c));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
                 c == '#' || c == '/')
            out += s[i];
        ++i;
    }
    return out;
}

using DecodeKey = std::pair<char, int>;
using DecodeTable = std::map<DecodeKey, std::string>;

DecodeTable make(std::initializer_list<std::pair<DecodeKey, const char*>> items) {
    DecodeTable t;
    for (auto& it : items) t[it.first] = it.second;
    return t;
}

const std::map<std::string, DecodeTable>& decode_tables() {
    static const std::map<std::string, DecodeTable> t = {
        {"sqi", make({{{'c',8},"ç"}, {{'e',6},"ë"}})},

        {"eus", make({})},

        {"bos", make({{{'c',3},"č"}, {{'s',3},"š"}, {{'z',3},"ž"}, {{'c',2},"ć"}, {{'d',0},"đ"}})},

        {"yue", make({{{'a',1},"ā"}, {{'e',1},"ē"}, {{'i',1},"ī"}, {{'o',1},"ō"}, {{'u',1},"ū"},
                      {{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'a',3},"ǎ"}, {{'e',3},"ě"}, {{'i',3},"ǐ"}, {{'o',3},"ǒ"}, {{'u',3},"ǔ"},
                      {{'a',4},"à"}, {{'e',4},"è"}, {{'i',4},"ì"}, {{'o',4},"ò"}, {{'u',4},"ù"},
                      {{'a',5},"â"}, {{'e',5},"ê"}, {{'i',5},"î"}, {{'o',5},"ô"}, {{'u',5},"û"},
                      {{'a',7},"ã"}, {{'e',7},"ẽ"}, {{'i',7},"ĩ"}, {{'o',7},"õ"}, {{'u',7},"ũ"}})},

        {"cat", make({{{'a',4},"à"}, {{'e',4},"è"}, {{'o',4},"ò"},
                      {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'i',6},"ï"}, {{'u',6},"ü"},
                      {{'c',8},"ç"}})},

        {"cpf", make({{{'a',4},"à"}, {{'e',4},"è"}, {{'o',4},"ò"}})},

        {"hrv", make({{{'c',3},"č"}, {{'s',3},"š"}, {{'z',3},"ž"}, {{'c',2},"ć"}, {{'d',0},"đ"}})},

        {"czr", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'y',2},"ý"},
                      {{'e',3},"ě"}, {{'s',3},"š"}, {{'c',3},"č"}, {{'r',3},"ř"}, {{'z',3},"ž"},
                      {{'d',3},"ď"}, {{'t',3},"ť"}, {{'n',3},"ň"}, {{'u',9},"ů"}})},

        {"dan", make({{{'o',0},"ø"}, {{'a',9},"å"}})},

        {"nld", make({{{'e',6},"ë"}, {{'i',6},"ï"}, {{'o',6},"ö"}, {{'u',6},"ü"},
                      {{'e',2},"é"}})},

        {"eng", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'a',4},"à"}, {{'e',4},"è"},
                      {{'a',5},"â"}, {{'e',5},"ê"}, {{'i',5},"î"}, {{'o',5},"ô"}, {{'u',5},"û"},
                      {{'a',6},"ä"}, {{'e',6},"ë"}, {{'i',6},"ï"}, {{'o',6},"ö"}, {{'u',6},"ü"},
                      {{'n',7},"ñ"}})},

        {"est", make({{{'a',6},"ä"}, {{'o',6},"ö"}, {{'u',6},"ü"}, {{'o',7},"õ"},
                      {{'s',3},"š"}, {{'z',3},"ž"}})},

        {"fin", make({{{'a',6},"ä"}, {{'o',6},"ö"}})},

        {"fra", make({{{'e',2},"é"},
                      {{'a',4},"à"}, {{'e',4},"è"}, {{'u',4},"ù"},
                      {{'a',5},"â"}, {{'e',5},"ê"}, {{'i',5},"î"}, {{'o',5},"ô"}, {{'u',5},"û"},
                      {{'e',6},"ë"}, {{'i',6},"ï"}, {{'u',6},"ü"}, {{'y',6},"ÿ"},
                      {{'c',8},"ç"}})},

        {"deu", make({{{'a',6},"ä"}, {{'o',6},"ö"}, {{'u',6},"ü"}, {{'s',0},"ß"}})},

        {"heb", make({{{'h',88},"ḥ"}, {{'t',88},"ṭ"}, {{'s',88},"ṣ"},
                      {{'s',3},"š"}, {{'s',2},"ś"}})},

        {"hin", make({{{'a',1},"ā"}, {{'i',1},"ī"}, {{'u',1},"ū"},
                      {{'t',88},"ṭ"}, {{'d',88},"ḍ"}, {{'n',88},"ṇ"}, {{'s',88},"ṣ"},
                      {{'h',88},"ḥ"}, {{'m',88},"ṃ"}, {{'r',88},"ṛ"}, {{'l',88},"ḷ"},
                      {{'n',33},"ṅ"}, {{'n',7},"ñ"}, {{'s',2},"ś"}})},

        {"hun", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'o',6},"ö"}, {{'u',6},"ü"}, {{'o',22},"ő"}, {{'u',22},"ű"}})},

        {"ibo", make({{{'i',88},"ị"}, {{'o',88},"ọ"}, {{'u',88},"ụ"}})},

        {"ind", make({})},

        {"gle", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"}})},

        {"ita", make({{{'a',4},"à"}, {{'e',4},"è"}, {{'i',4},"ì"}, {{'o',4},"ò"}, {{'u',4},"ù"},
                      {{'e',2},"é"}})},

        {"kor", make({})},

        {"kmr", make({{{'c',8},"ç"}, {{'e',5},"ê"}, {{'i',5},"î"}, {{'u',5},"û"}, {{'s',8},"ş"}})},

        {"lat", make({{{'a',1},"ā"}, {{'e',1},"ē"}, {{'i',1},"ī"}, {{'o',1},"ō"}, {{'u',1},"ū"}})},

        {"lit", make({{{'a',8},"ą"}, {{'e',8},"ę"}, {{'i',8},"į"}, {{'u',8},"ų"},
                      {{'c',3},"č"}, {{'s',3},"š"}, {{'z',3},"ž"},
                      {{'u',1},"ū"}, {{'e',33},"ė"}})},

        {"ltz", make({{{'e',6},"ë"}, {{'e',2},"é"}})},

        {"mly", make({})},

        {"mlt", make({{{'c',33},"ċ"}, {{'g',33},"ġ"}, {{'h',0},"ħ"}, {{'z',33},"ż"}})},

        {"cmn", make({{{'a',1},"ā"}, {{'e',1},"ē"}, {{'i',1},"ī"}, {{'o',1},"ō"}, {{'u',1},"ū"},
                      {{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'a',3},"ǎ"}, {{'e',3},"ě"}, {{'i',3},"ǐ"}, {{'o',3},"ǒ"}, {{'u',3},"ǔ"},
                      {{'a',4},"à"}, {{'e',4},"è"}, {{'i',4},"ì"}, {{'o',4},"ò"}, {{'u',4},"ù"},
                      {{'u',6},"ü"},
                      {{'u',61},"ǖ"}, {{'u',62},"ǘ"}, {{'u',63},"ǚ"}, {{'u',64},"ǜ"}})},

        {"mri", make({{{'a',1},"ā"}, {{'e',1},"ē"}, {{'i',1},"ī"}, {{'o',1},"ō"}, {{'u',1},"ū"}})},

        {"cnr", make({{{'c',3},"č"}, {{'s',3},"š"}, {{'z',3},"ž"}, {{'c',2},"ć"}, {{'d',0},"đ"},
                      {{'s',2},"ś"}, {{'z',2},"ź"}})},

        {"nor", make({{{'o',0},"ø"}, {{'a',9},"å"}})},

        {"pol", make({{{'a',8},"ą"}, {{'e',8},"ę"},
                      {{'c',2},"ć"}, {{'n',2},"ń"}, {{'s',2},"ś"}, {{'z',2},"ź"}, {{'o',2},"ó"},
                      {{'l',0},"ł"}, {{'z',33},"ż"}})},

        {"por", make({{{'a',7},"ã"}, {{'o',7},"õ"},
                      {{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'a',5},"â"}, {{'e',5},"ê"}, {{'o',5},"ô"}, {{'a',4},"à"},
                      {{'c',8},"ç"}})},

        {"ron", make({{{'a',5},"â"}, {{'i',5},"î"}, {{'a',3},"ă"}})},

        {"gla", make({{{'a',4},"à"}, {{'e',4},"è"}, {{'i',4},"ì"}, {{'o',4},"ò"}, {{'u',4},"ù"}})},

        {"srp", make({{{'c',3},"č"}, {{'s',3},"š"}, {{'z',3},"ž"}, {{'c',2},"ć"}, {{'d',0},"đ"}})},

        {"svk", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'y',2},"ý"}, {{'a',6},"ä"}, {{'o',5},"ô"}, {{'l',2},"ĺ"}, {{'r',2},"ŕ"},
                      {{'l',3},"ľ"}, {{'n',3},"ň"}, {{'s',3},"š"}, {{'c',3},"č"}, {{'z',3},"ž"},
                      {{'t',3},"ť"}, {{'d',3},"ď"}})},

        {"slv", make({{{'s',3},"š"}, {{'c',3},"č"}, {{'z',3},"ž"}})},

        {"som", make({})},

        {"spa", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'n',7},"ñ"}, {{'u',6},"ü"}})},

        {"swa", make({})},

        {"swe", make({{{'a',9},"å"}, {{'a',6},"ä"}, {{'o',6},"ö"}})},

        {"tgl", make({{{'n',7},"ñ"}, {{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"},
                      {{'u',2},"ú"}})},

        {"tur", make({{{'g',3},"ğ"}, {{'o',6},"ö"}, {{'u',6},"ü"}, {{'i',0},"ı"}, {{'c',8},"ç"},
                      {{'s',8},"ş"}})},

        {"cym", make({{{'a',5},"â"}, {{'e',5},"ê"}, {{'i',5},"î"}, {{'o',5},"ô"}, {{'u',5},"û"},
                      {{'w',5},"ŵ"}, {{'y',5},"ŷ"}, {{'i',6},"ï"}})},

        {"yor", make({{{'e',88},"ẹ"}, {{'o',88},"ọ"}, {{'s',88},"ṣ"}})},

        {"zul", make({})},
    };
    return t;
}

}

std::string fold_diacritics(const std::string& text, const std::string& language) {
    if (language != "tur") return apply_fold_table(text);

    std::string tmp;
    tmp.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 2, "\xC4\xB0") == 0) { tmp += 'i'; i += 2; }
        else if (text[i] == 'I') { tmp += "\xC4\xB1"; i += 1; }
        else { tmp += text[i]; i += 1; }
    }
    return apply_fold_table(tmp);
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
            if (i + 2 < n && std::isdigit(static_cast<unsigned char>(text[i + 2]))) {
                int two = (text[i + 1] - '0') * 10 + (text[i + 2] - '0');
                auto it2 = table.find({c, two});
                if (it2 != table.end()) { out += it2->second; i += 3; continue; }
            }
            int one = text[i + 1] - '0';
            auto it1 = table.find({c, one});
            if (it1 != table.end()) { out += it1->second; i += 2; continue; }
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

}

