#include "languages.hpp"

#include <cctype>
#include <map>
#include <utility>

namespace inop {

const std::vector<LanguageInfo>& supported_languages() {
    // Alphabetical by display name (Google Translate's own ordering
    // convention), not by code or family grouping.
    static const std::vector<LanguageInfo> v = {
        {"sqi", "Albanian"},  {"eus", "Basque"},    {"bos", "Bosnian"},
        {"yue", "Cantonese"}, {"cat", "Catalan"},   {"cpf", "Creole"},    {"hrv", "Croatian"},
        {"czr", "Czech"},     {"dan", "Danish"},    {"nld", "Dutch"},
        {"eng", "English"},   {"est", "Estonian"},
        {"fin", "Finnish"},   {"fra", "French"},    {"deu", "German"},
        {"hin", "Hindi (Latin)"},
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

// ── global encode table ────────────────────────────────────────────────
// Every source form (upper and lower) folds to the same lowercase output.
// Longer sequences are matched before shorter ones by trying `multi` first.
//
// Digit assignments (a repeated digit chains a second mark on the first —
// see e.g. the Pinyin ü+tone / Hungarian double-acute / dot-above entries
// below):
//   1 macron            6 umlaut/diaeresis
//   2 acute             7 tilde
//   3 caron / breve     8 cedilla (consonants) / ogonek (vowels) — the two
//   4 grave               never land on the same base letter, so sharing
//   5 circumflex          the digit is unambiguous
//                       9 ring-above
//                       0 one genuinely distinct (non-diacritic) letter —
//                         at most one such letter per language
//   88 dot-below (its own doubled slot, same convention as 22/33 below)
struct FoldEntry { const char* src; const char* out; };

const std::vector<FoldEntry>& multi_fold_table() {
    static const std::vector<FoldEntry> v = {};
    return v;
}

const std::vector<FoldEntry>& single_fold_table() {
    static const std::vector<FoldEntry> v = {
        // macron -> 1
        {"ā", "a1"}, {"Ā", "a1"}, {"ē", "e1"}, {"Ē", "e1"}, {"ī", "i1"}, {"Ī", "i1"},
        {"ō", "o1"}, {"Ō", "o1"}, {"ū", "u1"}, {"Ū", "u1"},
        // acute -> 2 (includes Slovak's long ĺ/ŕ and Polish/Croatian/
        // Serbian's ć ń ś ź — all genuinely acute marks, not caron)
        {"á", "a2"}, {"Á", "a2"}, {"é", "e2"}, {"É", "e2"}, {"í", "i2"}, {"Í", "i2"},
        {"ó", "o2"}, {"Ó", "o2"}, {"ú", "u2"}, {"Ú", "u2"}, {"ý", "y2"}, {"Ý", "y2"},
        {"ć", "c2"}, {"Ć", "c2"}, {"ĺ", "l2"}, {"Ĺ", "l2"}, {"ń", "n2"}, {"Ń", "n2"},
        {"ŕ", "r2"}, {"Ŕ", "r2"}, {"ś", "s2"}, {"Ś", "s2"}, {"ź", "z2"}, {"Ź", "z2"},
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
        // circumflex -> 5 (includes Welsh's ŵ/ŷ alongside the vowels)
        {"â", "a5"}, {"Â", "a5"}, {"ê", "e5"}, {"Ê", "e5"}, {"î", "i5"}, {"Î", "i5"},
        {"ô", "o5"}, {"Ô", "o5"}, {"û", "u5"}, {"Û", "u5"},
        {"ŵ", "w5"}, {"Ŵ", "w5"}, {"ŷ", "y5"}, {"Ŷ", "y5"},
        // umlaut / diaeresis -> 6
        {"ä", "a6"}, {"Ä", "a6"}, {"ë", "e6"}, {"Ë", "e6"}, {"ï", "i6"}, {"Ï", "i6"},
        {"ö", "o6"}, {"Ö", "o6"}, {"ü", "u6"}, {"Ü", "u6"}, {"ÿ", "y6"}, {"Ÿ", "y6"},
        // Pinyin ü + tone mark: ü already carries the umlaut (6); a tone on
        // top of it chains a second digit rather than picking a new base
        // letter, since it's the same base vowel with two stacked marks.
        {"ǖ", "u61"}, {"Ǖ", "u61"}, {"ǘ", "u62"}, {"Ǘ", "u62"},
        {"ǚ", "u63"}, {"Ǚ", "u63"}, {"ǜ", "u64"}, {"Ǜ", "u64"},
        // Hungarian double-acute -> repeated 2 (its own mark, not "acute
        // applied twice" — reuses the acute digit doubled as a distinct slot).
        {"ő", "o22"}, {"Ő", "o22"}, {"ű", "u22"}, {"Ű", "u22"},
        // tilde -> 7 (e/i/u tilde added alongside a/n/o for Cantonese's
        // sixth tone, which reuses this project's existing mark-shape
        // digits — 1/2/3/4/5/7 — across all five vowels rather than
        // inventing a new digit)
        {"ã", "a7"}, {"Ã", "a7"}, {"ñ", "n7"}, {"Ñ", "n7"}, {"õ", "o7"}, {"Õ", "o7"},
        {"ẽ", "e7"}, {"Ẽ", "e7"}, {"ĩ", "i7"}, {"Ĩ", "i7"}, {"ũ", "u7"}, {"Ũ", "u7"},
        // cedilla (consonants) / ogonek (vowels) -> 8 — disjoint by letter
        // type, so sharing the digit is unambiguous.
        {"ç", "c8"}, {"Ç", "c8"}, {"ş", "s8"}, {"Ş", "s8"},
        {"ą", "a8"}, {"Ą", "a8"}, {"ę", "e8"}, {"Ę", "e8"},
        {"į", "i8"}, {"Į", "i8"}, {"ų", "u8"}, {"Ų", "u8"},
        // ring-above -> 9
        {"å", "a9"}, {"Å", "a9"}, {"ů", "u9"}, {"Ů", "u9"},
        // dot-above -> repeated 3 (its own mark, not "caron applied
        // twice" — reuses the caron/breve digit doubled as a distinct slot,
        // shares no base letters with single-3 caron/breve in any
        // supported language).
        {"ė", "e33"}, {"Ė", "e33"}, {"ċ", "c33"}, {"Ċ", "c33"},
        {"ġ", "g33"}, {"Ġ", "g33"}, {"ż", "z33"}, {"Ż", "z33"},
        // dot-below -> repeated 8 (its own doubled slot, same convention as
        // the dot-above/double-acute slots above — single 8 stays cedilla/
        // ogonek, unrelated). Covers Yoruba's open vowels + ṣ, Igbo's open
        // vowels, and Hindi's IAST retroflex/visarga/anusvara/vocalic set.
        {"ẹ", "e88"}, {"Ẹ", "e88"}, {"ọ", "o88"}, {"Ọ", "o88"},
        {"ị", "i88"}, {"Ị", "i88"}, {"ụ", "u88"}, {"Ụ", "u88"},
        {"ṣ", "s88"}, {"Ṣ", "s88"}, {"ṭ", "t88"}, {"Ṭ", "t88"},
        {"ḍ", "d88"}, {"Ḍ", "d88"}, {"ṇ", "n88"}, {"Ṇ", "n88"},
        {"ḥ", "h88"}, {"Ḥ", "h88"}, {"ṃ", "m88"}, {"Ṃ", "m88"},
        {"ṛ", "r88"}, {"Ṛ", "r88"}, {"ḷ", "l88"}, {"Ḷ", "l88"},
        // dot-above n (Hindi's velar nasal ṅ) -> repeated 3, same slot as
        // the other dot-above marks (its base letter 'n' never collides
        // with the existing dot-above set of e/c/g/z).
        {"ṅ", "n33"}, {"Ṅ", "n33"},
        // one genuinely distinct (non-diacritic) letter per language -> 0
        {"ı", "i0"},                          // Turkish dotless i (İ handled separately)
        {"ß", "s0"}, {"\xE1\xBA\x9E", "s0"},  // German ß, ẞ
        {"đ", "d0"}, {"Đ", "d0"},              // Croatian/Serbian d with stroke
        {"ł", "l0"}, {"Ł", "l0"},              // Polish l with stroke
        {"ø", "o0"}, {"Ø", "o0"},              // Danish/Norwegian o with stroke
        {"ħ", "h0"}, {"Ħ", "h0"},              // Maltese h with stroke
        // ligatures decompose to their two plain base letters in sequence —
        // deliberately NOT folded into the special-letter (digit 0) scheme,
        // since they're two ordinary letters historically fused, not a
        // single distinct one.
        {"œ", "oe"}, {"Œ", "oe"}, {"æ", "ae"}, {"Æ", "ae"},
        // dropped with no encoding — Romanian's comma-below is the one mark
        // that stays an accepted information loss (cedilla and ring-above
        // used to be dropped here too, but now have real slots above).
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
using DecodeKey = std::pair<char, int>;  // (base letter, digit or 2-digit chain code)
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

        // A devised-for-this-project diacritic tone scheme, not a claim to
        // match Yale or Jyutping (both tone-number-based) — six tones over
        // the five plain vowels, reusing this project's own mark-shape
        // digits (macron/acute/caron/grave/circumflex/tilde) the same way
        // Mandarin already reuses four of them for its own four tones.
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

        // Haitian + Louisiana Creole share one entry — both use grave to
        // mark vowel quality, and Louisiana Creole's own conventions are a
        // subset of the same repertoire for this scheme's purposes.
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

        // Full academic IAST retroflex/nasal/visarga/anusvara/vocalic set —
        // deliberately inclusive over minimal, per operator preference, so
        // no legitimately-encoded mark gets silently stripped. Excludes only
        // the long vocalic ṝ/ḹ, which have no real attestation outside
        // Sanskrit grammar tables and would need a third chained digit this
        // scheme's decoder doesn't support (resubstitute() only ever looks
        // two digits ahead).
        {"hin", make({{{'a',1},"ā"}, {{'i',1},"ī"}, {{'u',1},"ū"},
                      {{'t',88},"ṭ"}, {{'d',88},"ḍ"}, {{'n',88},"ṇ"}, {{'s',88},"ṣ"},
                      {{'h',88},"ḥ"}, {{'m',88},"ṃ"}, {{'r',88},"ṛ"}, {{'l',88},"ḷ"},
                      {{'n',33},"ṅ"}, {{'n',7},"ñ"}, {{'s',2},"ś"}})},

        {"hun", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"},
                      {{'o',6},"ö"}, {{'u',6},"ü"}, {{'o',22},"ő"}, {{'u',22},"ű"}})},

        // Open vowels + ṣ (dot-below family), the three marks this
        // project's Yoruba orthography needs.
        {"ibo", make({{{'i',88},"ị"}, {{'o',88},"ọ"}, {{'u',88},"ụ"}})},

        {"ind", make({})},

        {"gle", make({{{'a',2},"á"}, {{'e',2},"é"}, {{'i',2},"í"}, {{'o',2},"ó"}, {{'u',2},"ú"}})},

        {"ita", make({{{'a',4},"à"}, {{'e',4},"è"}, {{'i',4},"ì"}, {{'o',4},"ò"}, {{'u',4},"ù"},
                      {{'e',2},"é"}})},

        // Revised Romanization (2000) avoids diacritics by design (digraphs
        // like "eo"/"eu" instead) — same empty-table shape as Indonesian/
        // Somali/Swahili/Zulu.
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

        // Same repertoire as Croatian/Serbian/Bosnian, plus its own ś/ź
        // (2009 orthography) — which reuse the acute (2) slot Polish's ś/ź
        // already occupy, same characters.
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

        // Open vowels + ṣ (dot-below family), the three marks this
        // project's Yoruba orthography needs.
        {"yor", make({{{'e',88},"ẹ"}, {{'o',88},"ọ"}, {{'s',88},"ṣ"}})},

        {"zul", make({})},
    };
    return t;
}

// fold_diacritics() folds every language's diacritics through one shared
// global table, but resubstitute() historically only ever consulted the
// active language's own table — so a diacritic that's legitimate in some
// OTHER supported language (a foreign proper noun, a loanword, a gloss)
// folds fine but can never decode back for a language whose own table
// doesn't happen to define that mark. Built by merging every language's
// decode table; any (letter, digit) key where two languages disagree on
// the mark is a genuine ambiguity (confirmed to exist exactly once today:
// 'a3' means Romanian's ă in ron but Pinyin's ǎ in cmn/yue) and must NOT
// be guessed at, or a message could silently decode to the wrong
// character while looking like a successful round trip. Excluding
// conflicting keys — rather than hand-listing them — keeps this correct
// automatically if a future language's table introduces a new collision.
const DecodeTable& global_decode_table() {
    static const DecodeTable t = [] {
        DecodeTable merged;
        std::map<DecodeKey, bool> conflicting;
        for (const auto& [lang, table] : decode_tables()) {
            for (const auto& [key, value] : table) {
                auto it = merged.find(key);
                if (it == merged.end()) merged[key] = value;
                else if (it->second != value) conflicting[key] = true;
            }
        }
        for (const auto& [key, _] : conflicting) merged.erase(key);
        return merged;
    }();
    return t;
}

}  // namespace

std::string fold_diacritics(const std::string& text, const std::string& language) {
    // Most languages never touch `text` before folding — building a full
    // copy just to hand it unchanged to apply_fold_table() was pure waste
    // on every one of those calls. Only Turkish needs a transformed buffer
    // first.
    if (language != "tur") return apply_fold_table(text);

    // Turkish-aware casing: dotted capital I (İ, U+0130) lowercases to
    // ordinary ascii 'i'; ordinary ascii 'I' is the CAPITAL of dotless
    // i, so it lowercases to dotless ı (U+0131) instead of 'i'. Must run
    // before the generic table, which would otherwise just lowercase
    // 'I' to 'i' and lose the distinction.
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
    const DecodeTable& global = global_decode_table();

    // Language-specific table first (it's the authority on that language's
    // own orthography); the global table only steps in for a mark that
    // isn't one of this language's own — see global_decode_table() above.
    // Returns nullptr on a miss in both — never compare the result against
    // one specific table's end() iterator, since it may come from either.
    auto find_mark = [&](const DecodeKey& key) -> const std::string* {
        auto it = table.find(key);
        if (it != table.end()) return &it->second;
        auto git = global.find(key);
        return git != global.end() ? &git->second : nullptr;
    };

    std::string out;
    out.reserve(text.size());
    size_t i = 0, n = text.size();
    while (i < n) {
        char c = text[i];
        bool is_letter = c >= 'a' && c <= 'z';

        if (is_letter && i + 1 < n && std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
            // A chained mark (Pinyin's umlaut+tone, Hungarian's
            // double-acute, dot-above sharing the caron/breve slot doubled)
            // encodes as two digit characters — tried first so it isn't
            // mistaken for its own first digit alone with a stray second
            // digit left dangling. Falls back to the single-digit form —
            // the overwhelming majority of marks — if no two-digit entry
            // matches for this language.
            if (i + 2 < n && std::isdigit(static_cast<unsigned char>(text[i + 2]))) {
                int two = (text[i + 1] - '0') * 10 + (text[i + 2] - '0');
                if (const std::string* mark = find_mark({c, two})) {
                    out += *mark; i += 3; continue;
                }
            }
            int one = text[i + 1] - '0';
            if (const std::string* mark = find_mark({c, one})) {
                out += *mark; i += 2; continue;
            }
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
