// languages.hpp — numeral-suffix diacritic scheme for the 49 officially
// supported languages.
//
// Digits, not new symbols, carry the marks: INOP-38s alphabet already
// includes 0-9, so reusing them avoids adding anything new to the machine
// itself. Prep-layer only: nothing here touches inop.hpp/inop.cpp. Encoding
// (fold_diacritics) is language-independent — a given accented character
// always folds to the same base-letter-plus-digit pair, regardless of the
// declared language — because the same digit is deliberately reused across
// diacritic *classes* that never co-occur in one language's own alphabet
// (caron and breve both land on digit 3, for instance). Decoding
// (resubstitute) is where the language tag actually matters: it's what
// resolves "a3" back to Romanian's ă versus Pinyin's ǎ. A repeated digit
// ("22", "33", Pinyin's "61"-"64") chains a second mark on the first —
// see the digit table in languages.cpp for the full assignment.
#pragma once

#include <string>
#include <vector>

namespace inop {

struct LanguageInfo {
    std::string code;  // 3-letter tag appended to transmitted ciphertext
    std::string name;
};

// The 49 officially supported languages, alphabetical by display name
// (matching Google Translate's own ordering convention).
const std::vector<LanguageInfo>& supported_languages();
bool is_supported_language(const std::string& code);

// Raw operator input (UTF-8) -> lowercase INOP-safe ASCII rendering, with
// diacritics folded to base-letter+digit (or base-letter-only for Romanian's
// comma-below, the one mark still dropped with no encoding at all — cedilla
// and ring-above used to be dropped here too, but now have real digit
// slots). Turkish gets dotted/dotless-aware casing ahead of the generic
// fold when language == "tur". Does not touch digits already present in
// the raw text — that's mark_literal_digits()'s job in pipeline.hpp, and
// it must run BEFORE this, on the raw text, so digits this function
// introduces are never mistaken for literal ones.
std::string fold_diacritics(const std::string& text, const std::string& language);

// Reverse of fold_diacritics, applied to decrypted (lowercase, space-
// restored) text. A letter immediately followed by a digit is a diacritic
// pair, resolved via `language`'s table. A letter followed by '/' then a
// digit is a literal number — the '/' is stripped and both characters are
// kept as-is. Unknown (letter, digit) pairs for the given language are left
// untouched (best-effort — should not happen in normal operation).
std::string resubstitute(const std::string& text, const std::string& language);

}  // namespace inop
