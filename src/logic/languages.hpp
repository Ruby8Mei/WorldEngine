#pragma once

#include <string>
#include <vector>

namespace inop {

struct LanguageInfo {
    std::string code;
    std::string name;
};

const std::vector<LanguageInfo>& supported_languages();
bool is_supported_language(const std::string& code);

std::string fold_diacritics(const std::string& text, const std::string& language);

std::string resubstitute(const std::string& text, const std::string& language);

}

