#include "languages.hpp"

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

}  // namespace inop
