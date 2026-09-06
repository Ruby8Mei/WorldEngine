// languages.hpp - the languages a message can be tagged as being in.
//
// This used to hold the diacritic scheme as well: 48 hand written tables,
// one per language, that said what each digit meant in that language.
// They are gone. One universal transformer does that job now, for every
// language at once and without being told which one it is reading, and it
// lives in transform.hpp.
//
// What is left is the list itself, because the three letter tag is still
// written on the end of a transmitted message so the reader knows what
// they are looking at. That was never a folding matter.
#pragma once

#include <string>
#include <vector>

namespace inop {

struct LanguageInfo {
    std::string code;  // 3-letter tag appended to transmitted ciphertext
    std::string name;
};

// The 48 supported languages, alphabetical by display name, which is
// Google Translate own ordering convention.
const std::vector<LanguageInfo>& supported_languages();
bool is_supported_language(const std::string& code);

}  // namespace inop
