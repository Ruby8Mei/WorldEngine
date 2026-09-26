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

}
