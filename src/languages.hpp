// languages.hpp — numeral-suffix diacritic scheme for the 19 officially
// supported languages.
//
// Prep-layer only: nothing here touches inop.hpp/inop.cpp. Encoding
// (fold_diacritics) is language-independent — a given accented character
// always folds to the same base-letter-plus-digit pair, regardless of the
// declared language — because the same digit is deliberately reused across
// diacritic *classes* that never co-occur in one language's own alphabet
// (caron and breve both land on digit 3, for instance). Decoding
// (resubstitute) is where the language tag actually matters: it's what
// resolves "a3" back to Romanian's ă versus Pinyin's ǎ.
#pragma once

#include <string>
#include <vector>

namespace inop {

struct LanguageInfo {
    std::string code;  // 2-letter tag appended to transmitted ciphertext
    std::string name;
};

// The 19 officially supported languages, in the order given in the spec.
const std::vector<LanguageInfo>& supported_languages();
bool is_supported_language(const std::string& code);

// Raw operator input (UTF-8) -> lowercase INOP-safe ASCII rendering, with
// diacritics folded to base-letter+digit (or base-letter-only for the marks
// that are dropped with no encoding: cedilla, Czech ring-above, Romanian
// comma-below, Turkish comma-below). Turkish gets dotted/dotless-aware
// casing ahead of the generic fold when language == "tr". Does not touch
// digits already present in the raw text — that's mark_literal_digits()'s
// job in pipeline.hpp, and it must run BEFORE this, on the raw text, so
// digits this function introduces are never mistaken for literal ones.
std::string fold_diacritics(const std::string& text, const std::string& language);

// Reverse of fold_diacritics, applied to decrypted (lowercase, space-
// restored) text. A letter immediately followed by a digit is a diacritic
// pair, resolved via `language`'s table. A letter followed by '/' then a
// digit is a literal number — the '/' is stripped and both characters are
// kept as-is. Unknown (letter, digit) pairs for the given language are left
// untouched (best-effort — should not happen in normal operation).
std::string resubstitute(const std::string& text, const std::string& language);

}  // namespace inop
