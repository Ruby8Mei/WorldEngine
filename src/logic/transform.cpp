#include "transform.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace inop {

namespace {

struct Row {
    std::uint32_t cp;
    const char* out;
};

const Row kTable[] = {
#include "transform_table.inc"
};

constexpr std::size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

// The table is generated in codepoint order, so a lookup bisects it
// rather than walking 487 rows per character.
const char* fold_of(std::uint32_t cp) {
    std::size_t lo = 0, hi = kTableSize;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (kTable[mid].cp < cp) lo = mid + 1;
        else hi = mid;
    }
    if (lo < kTableSize && kTable[lo].cp == cp) return kTable[lo].out;
    return nullptr;
}

// The same table read backwards, built once on first use.
//
// The spelled out four are left out of it. Their folds carry a letter
// after the first one, "ae" and "s0s0", and a fold that is more than one
// letter is by definition not one character coming back. Leaving them in
// would have "ae" decode to the ligature and quietly change every English
// word with those two letters in it.
const std::map<std::string, std::uint32_t>& reverse_table() {
    static const std::map<std::string, std::uint32_t> m = [] {
        std::map<std::string, std::uint32_t> out;
        for (std::size_t i = 0; i < kTableSize; ++i) {
            const std::string s = kTable[i].out;
            bool spelled_out = false;
            for (std::size_t j = 1; j < s.size(); ++j)
                if (s[j] >= 'a' && s[j] <= 'z') spelled_out = true;
            if (!spelled_out) out[s] = kTable[i].cp;
        }
        return out;
    }();
    return m;
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// A sequence that is not valid UTF-8 comes back as this and eats exactly
// one byte, so damaged input costs one dropped character rather than
// running off the end of the string.
constexpr std::uint32_t kBadCodepoint = 0xFFFFFFFFu;

// One codepoint out of UTF-8, with `i` left on the byte after it.
std::uint32_t next_codepoint(const std::string& s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t len = 0;
    std::uint32_t cp = 0;
    if (c < 0x80) {
        ++i;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        len = 2;
        cp = c & 0x1Fu;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        cp = c & 0x0Fu;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        cp = c & 0x07u;
    } else {
        ++i;
        return kBadCodepoint;
    }

    if (i + len > s.size()) {
        ++i;
        return kBadCodepoint;
    }
    for (std::size_t k = 1; k < len; ++k) {
        const unsigned char t = static_cast<unsigned char>(s[i + k]);
        if ((t & 0xC0) != 0x80) {
            ++i;
            return kBadCodepoint;
        }
        cp = (cp << 6) | (t & 0x3Fu);
    }
    const std::uint32_t minimum = len == 2 ? 0x80u : (len == 3 ? 0x800u : 0x10000u);
    if (cp < minimum || (cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) {
        ++i;
        return kBadCodepoint;
    }
    i += len;
    return cp;
}

bool is_ascii_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

TransformValidationResult validation(TransformValidationStatus status, std::size_t offset,
                                     const std::string& reason) {
    TransformValidationResult result;
    result.status = status;
    result.offset = offset;
    result.reason = reason;
    return result;
}

}  // namespace

TransformValidationResult validate_transform_input(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t offset = i;
        const std::uint32_t cp = next_codepoint(text, i);
        if (cp == kBadCodepoint)
            return validation(TransformValidationStatus::InvalidUtf8, offset,
                              "invalid UTF-8 sequence");
        if (cp < 0x80) {
            const char c = static_cast<char>(cp);
            if (is_ascii_letter(c) || is_digit(c) || c == ' ' || c == '\t' || c == '\n' ||
                c == '\r' || c == '\f' || c == '\v')
                continue;
            return validation(TransformValidationStatus::UnsupportedInput, offset,
                              "unsupported ASCII character");
        }
        if (!fold_of(cp))
            return validation(TransformValidationStatus::UnsupportedInput, offset,
                              "unsupported Unicode character");
    }
    return {};
}

TransformValidationResult validate_transformed_data(const std::string& text) {
    bool literal = false;
    std::size_t literal_offset = 0;
    std::size_t i = 0;
    const std::map<std::string, std::uint32_t>& rev = reverse_table();
    while (i < text.size()) {
        const unsigned char uc = static_cast<unsigned char>(text[i]);
        if (uc >= 0x80)
            return validation(TransformValidationStatus::MalformedData, i,
                              "transformed data must be ASCII");

        const char c = text[i];
        if (c >= 'a' && c <= 'z') {
            std::string key(1, c);
            std::size_t j = i + 1;
            bool has_code = false;
            while (j < text.size() && is_digit(text[j])) {
                has_code = true;
                while (j < text.size() && is_digit(text[j])) key += text[j++];
                if (j < text.size() && text[j] == '/') {
                    if (j + 1 >= text.size())
                        return validation(TransformValidationStatus::MalformedData, j,
                                          "truncated transform marker");
                    if (text[j + 1] == '/') break;
                    if (!is_digit(text[j + 1]))
                        return validation(TransformValidationStatus::MalformedData, j,
                                          "transform modifier must contain digits");
                    key += '/';
                    ++j;
                    continue;
                }
                break;
            }

            if (has_code && key != std::string(1, c) + "0" && rev.find(key) == rev.end())
                return validation(TransformValidationStatus::MalformedData, i,
                                  "unknown transform code");

            if (j < text.size() && text[j] == '/') {
                if (j + 1 >= text.size())
                    return validation(TransformValidationStatus::MalformedData, j,
                                      "truncated transform marker");
                if (text[j + 1] != '/')
                    return validation(TransformValidationStatus::MalformedData, j,
                                      "modifier marker has no preceding code");
                if (j + 2 >= text.size() || !is_digit(text[j + 2]))
                    return validation(TransformValidationStatus::MalformedData, j,
                                      "literal number marker must be followed by digits");
                j += 2;
                while (j < text.size() && is_digit(text[j])) ++j;
            }
            i = j;
            continue;
        }

        if ((c >= 'A' && c <= 'Z') || c == '/' || c == '#' ||
            (c != ' ' && !is_digit(c))) {
            if (!literal) literal_offset = i;
            literal = true;
        } else if (c == ' ' &&
                   (i == 0 || i + 1 == text.size() || text[i - 1] == ' ')) {
            if (!literal) literal_offset = i;
            literal = true;
        }
        ++i;
    }

    if (literal)
        return validation(TransformValidationStatus::LiteralContent, literal_offset,
                          "literal content is not canonical transformed data");
    return {};
}

std::string transform(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 4);

    // Whether a literal digit written here could be read as a mark, which
    // is true exactly when a letter or a mark digit was the last thing
    // written. Once the double slash has been put down the rest of the
    // number needs nothing, so this goes false with it.
    bool digit_would_read_as_mark = false;

    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint32_t cp = next_codepoint(text, i);
        if (cp == kBadCodepoint) continue;

        if (cp < 0x80) {
            const char c = static_cast<char>(cp);
            if (is_ascii_letter(c)) {
                const bool upper = c >= 'A' && c <= 'Z';
                out += static_cast<char>(upper ? c - 'A' + 'a' : c);
                if (upper) out += '0';
                digit_would_read_as_mark = true;
                continue;
            }
            if (is_digit(c)) {
                if (digit_would_read_as_mark) {
                    out += "//";
                    digit_would_read_as_mark = false;
                }
                out += c;
                continue;
            }
            // Every kind of space reads as one space, which is what the
            // rotors are handed. Everything else ASCII goes: punctuation,
            // and the slash and hash the machine alphabet keeps for its
            // own purposes.
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
                if (!out.empty() && out.back() != ' ') out += ' ';
                digit_would_read_as_mark = false;
            }
            continue;
        }

        const char* fold = fold_of(cp);
        if (!fold) continue;  // the sanitizer, and it says nothing
        out += fold;
        digit_would_read_as_mark = true;
    }

    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string untransform(const std::string& text) {
    if (validate_transformed_data(text).status == TransformValidationStatus::MalformedData)
        return text;

    std::string out;
    out.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (!is_ascii_letter(c)) {
            out += c;
            ++i;
            continue;
        }

        // A letter, and after it the codes that belong to it. They are
        // collected as one string in exactly the form the table is keyed
        // on, so the lookup is that string and nothing has to be assembled.
        std::string key(1, c);
        std::size_t j = i + 1;
        while (j < text.size()) {
            // A double slash ends the codes: what follows is a number.
            if (text[j] == '/' && j + 1 < text.size() && text[j + 1] == '/') break;
            if (!is_digit(text[j])) break;
            while (j < text.size() && is_digit(text[j])) key += text[j++];
            // A single slash with a digit behind it is another mark on the
            // same letter. Anything else ends the letter.
            if (j + 1 < text.size() && text[j] == '/' && is_digit(text[j + 1])) {
                key += '/';
                ++j;
                continue;
            }
            break;
        }

        if (key.size() == 1) {
            out += c;
            i = j;
        } else if (c >= 'a' && c <= 'z' && key == std::string(1, c) + "0") {
            // No mark, only the case code.
            out += static_cast<char>(c - 'a' + 'A');
            i = j;
        } else {
            const std::map<std::string, std::uint32_t>& rev = reverse_table();
            const std::map<std::string, std::uint32_t>::const_iterator it = rev.find(key);
            if (it == rev.end()) {
                // Not a code this scheme knows. Left exactly as it was
                // found rather than guessed at.
                out += c;
                ++i;
                continue;
            }
            append_utf8(out, it->second);
            i = j;
        }

        // The double slash is the marker and not part of the number, so it
        // goes and the digits behind it stay.
        if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
            i += 2;
            while (i < text.size() && is_digit(text[i])) out += text[i++];
        }
    }
    return out;
}

std::vector<std::pair<char, std::string>> declared_codes() {
    std::vector<std::pair<char, std::string>> out;
    for (std::size_t i = 0; i < kTableSize; ++i) {
        const std::string s = kTable[i].out;
        // The spelled out four declare no code at all: they are two
        // letters, not a letter and a mark.
        bool spelled_out = false;
        for (std::size_t j = 1; j < s.size(); ++j)
            if (s[j] >= 'a' && s[j] <= 'z') spelled_out = true;
        if (spelled_out || s.size() < 2) continue;
        const std::pair<char, std::string> row(s[0], s.substr(1));
        bool seen = false;
        for (const std::pair<char, std::string>& e : out)
            if (e == row) seen = true;
        if (!seen) out.push_back(row);
    }
    return out;
}

}  // namespace inop
