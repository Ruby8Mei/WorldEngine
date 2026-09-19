#include "transform.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
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
bool is_ascii_punctuation(char c) {
    return c >= '!' && c <= '~' && !is_ascii_letter(c) && !is_digit(c);
}

TransformValidationResult validation(TransformValidationStatus status, std::size_t offset,
                                     const std::string& reason) {
    TransformValidationResult result;
    result.status = status;
    result.offset = offset;
    result.reason = reason;
    return result;
}

struct GreekRow {
    std::uint32_t lower;
    std::uint32_t upper;
    const char* code;
};

const GreekRow kGreekTable[] = {
    {0x03B1, 0x0391, "a"},  {0x03B2, 0x0392, "b"},  {0x03B3, 0x0393, "g"},
    {0x03B4, 0x0394, "d"},  {0x03B5, 0x0395, "e"},  {0x03B6, 0x0396, "z"},
    {0x03B7, 0x0397, "e2"}, {0x03B8, 0x0398, "th"}, {0x03B9, 0x0399, "i"},
    {0x03BA, 0x039A, "k"},  {0x03BB, 0x039B, "l"},  {0x03BC, 0x039C, "m"},
    {0x03BD, 0x039D, "n"},  {0x03BE, 0x039E, "x"},  {0x03BF, 0x039F, "o"},
    {0x03C0, 0x03A0, "p"},  {0x03C1, 0x03A1, "r"},  {0x03C3, 0x03A3, "s"},
    {0x03C4, 0x03A4, "t"},  {0x03C5, 0x03A5, "u"},  {0x03C6, 0x03A6, "f"},
    {0x03C7, 0x03A7, "c3"}, {0x03C8, 0x03A8, "q"},  {0x03C9, 0x03A9, "o2"},
};

constexpr std::size_t kGreekTableSize = sizeof(kGreekTable) / sizeof(kGreekTable[0]);

const GreekRow* greek_row_for_codepoint(std::uint32_t cp, bool* upper) {
    for (std::size_t i = 0; i < kGreekTableSize; ++i) {
        if (kGreekTable[i].lower == cp) {
            *upper = false;
            return &kGreekTable[i];
        }
        if (kGreekTable[i].upper == cp) {
            *upper = true;
            return &kGreekTable[i];
        }
    }
    if (cp == 0x03C2) {
        *upper = false;
        return &kGreekTable[17];
    }
    return nullptr;
}

struct GreekToken {
    const GreekRow* row = nullptr;
    std::size_t length = 0;
    bool upper = false;
};

bool parse_greek_token(const std::string& text, std::size_t at, GreekToken* token) {
    GreekToken best;
    for (std::size_t i = 0; i < kGreekTableSize; ++i) {
        const std::string code = kGreekTable[i].code;
        if (text.compare(at, code.size(), code) != 0) continue;
        std::size_t length = code.size();
        bool upper = false;
        if (at + length < text.size() && text[at + length] == '0') {
            ++length;
            upper = true;
        }
        if (length > best.length) best = GreekToken{&kGreekTable[i], length, upper};
    }
    if (!best.row) return false;
    *token = best;
    return true;
}

bool greek_letter_follows(const std::string& text, std::size_t at) {
    while (at < text.size() && text[at] != ' ') {
        if (is_digit(text[at])) {
            while (at < text.size() && is_digit(text[at])) ++at;
            continue;
        }
        if (at + 2 < text.size() && text[at] == '/' && text[at + 1] == '/' &&
            is_digit(text[at + 2])) {
            at += 2;
            while (at < text.size() && is_digit(text[at])) ++at;
            continue;
        }
        GreekToken token;
        if (parse_greek_token(text, at, &token)) return true;
        ++at;
    }
    return false;
}

const char* kHangulInitialCodes[] = {
    "g", "gg", "n", "d", "dd", "l", "m", "b", "bb", "s",
    "ss", "q", "j", "jj", "ch", "k", "t", "p", "h",
};

const char* kHangulMedialCodes[] = {
    "a", "ai", "ya", "yai", "eo", "eoi", "yeo", "yeoi", "o", "oa", "oai",
    "oi", "yo", "u", "ueo", "ueoi", "ui", "yu", "eu", "eui", "i",
};

const char* kHangulFinalCodes[] = {
    "", "g", "gg", "gs", "n", "nj", "nh", "d", "l", "lg", "lm", "lb",
    "ls", "lt", "lp", "lh", "m", "b", "bs", "s", "ss", "q", "j", "ch",
    "k", "t", "p", "h",
};

const char* kHangulCompatConsonantCodes[] = {
    "g", "gg", "gs", "n", "nj", "nh", "d", "dd", "l", "lg", "lm", "lb",
    "ls", "lt", "lp", "lh", "m", "b", "bb", "bs", "s", "ss", "q", "j",
    "jj", "ch", "k", "t", "p", "h",
};

constexpr std::size_t kHangulInitialCount =
    sizeof(kHangulInitialCodes) / sizeof(kHangulInitialCodes[0]);
constexpr std::size_t kHangulMedialCount =
    sizeof(kHangulMedialCodes) / sizeof(kHangulMedialCodes[0]);
constexpr std::size_t kHangulFinalCount =
    sizeof(kHangulFinalCodes) / sizeof(kHangulFinalCodes[0]);
constexpr std::size_t kHangulCompatConsonantCount =
    sizeof(kHangulCompatConsonantCodes) / sizeof(kHangulCompatConsonantCodes[0]);

int code_index(const std::string& code, const char* const* codes, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        if (code == codes[i]) return static_cast<int>(i);
    return -1;
}

bool is_modern_hangul_initial(std::uint32_t cp) { return cp >= 0x1100 && cp <= 0x1112; }
bool is_modern_hangul_medial(std::uint32_t cp) { return cp >= 0x1161 && cp <= 0x1175; }
bool is_modern_hangul_final(std::uint32_t cp) { return cp >= 0x11A8 && cp <= 0x11C2; }

const char* hangul_compat_code(std::uint32_t cp) {
    if (cp >= 0x3131 && cp <= 0x314E)
        return kHangulCompatConsonantCodes[cp - 0x3131];
    if (cp >= 0x314F && cp <= 0x3163) return kHangulMedialCodes[cp - 0x314F];
    return nullptr;
}

bool hangul_compat_codepoint(const std::string& code, std::uint32_t* cp) {
    const int consonant = code_index(code, kHangulCompatConsonantCodes,
                                     kHangulCompatConsonantCount);
    if (consonant >= 0) {
        *cp = 0x3131u + static_cast<std::uint32_t>(consonant);
        return true;
    }
    const int vowel = code_index(code, kHangulMedialCodes, kHangulMedialCount);
    if (vowel >= 0) {
        *cp = 0x314Fu + static_cast<std::uint32_t>(vowel);
        return true;
    }
    return false;
}

int hangul_punctuation_code(std::uint32_t cp) {
    if (cp == '.') return 0;
    if (cp == ',') return 1;
    if (cp == '?') return 2;
    if (cp == '!') return 3;
    return -1;
}

char hangul_punctuation(int code) {
    static const char marks[] = {'.', ',', '?', '!'};
    return code >= 0 && code < 4 ? marks[code] : 0;
}

void append_hangul_syllable_frame(std::string& out, char kind, int initial, int medial,
                                  int final) {
    out += '/';
    out += kind;
    out += kHangulInitialCodes[initial];
    out += '0';
    out += kHangulMedialCodes[medial];
    out += '0';
    out += kHangulFinalCodes[final];
    out += '/';
}

struct HangulFrame {
    char kind = 0;
    int initial = -1;
    int medial = -1;
    int final = -1;
    std::uint32_t codepoint = 0;
    std::size_t end = 0;
};

bool parse_hangul_frame(const std::string& text, std::size_t at, HangulFrame* frame,
                        std::string* reason) {
    const std::size_t close = text.find('/', at + 1);
    if (close == std::string::npos) {
        *reason = "truncated Hangul frame";
        return false;
    }
    const std::string body = text.substr(at + 1, close - at - 1);
    if (body.empty()) {
        *reason = "empty Hangul frame";
        return false;
    }

    HangulFrame parsed;
    parsed.kind = body[0];
    parsed.end = close + 1;
    if (parsed.kind == '1' || parsed.kind == '2') {
        const std::size_t first = body.find('0', 1);
        const std::size_t second =
            first == std::string::npos ? std::string::npos : body.find('0', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
            body.find('0', second + 1) != std::string::npos) {
            *reason = "Hangul syllable frame needs three components";
            return false;
        }
        parsed.initial = code_index(body.substr(1, first - 1), kHangulInitialCodes,
                                    kHangulInitialCount);
        parsed.medial = code_index(body.substr(first + 1, second - first - 1),
                                   kHangulMedialCodes, kHangulMedialCount);
        parsed.final = code_index(body.substr(second + 1), kHangulFinalCodes,
                                  kHangulFinalCount);
        if (parsed.initial < 0 || parsed.medial < 0 || parsed.final < 0) {
            *reason = "unknown Hangul syllable component";
            return false;
        }
    } else if (parsed.kind == '3') {
        if (!hangul_compat_codepoint(body.substr(1), &parsed.codepoint)) {
            *reason = "unknown Hangul compatibility jamo code";
            return false;
        }
    } else if (parsed.kind == '4') {
        if (body.size() != 2 || body[1] < '0' || body[1] > '3') {
            *reason = "unknown Hangul punctuation code";
            return false;
        }
        parsed.codepoint = static_cast<std::uint32_t>(hangul_punctuation(body[1] - '0'));
    } else {
        *reason = "unknown Hangul frame type";
        return false;
    }
    *frame = parsed;
    return true;
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
            if (is_ascii_letter(c) || is_digit(c) || is_ascii_punctuation(c) || c == ' ' ||
                c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
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

TransformValidationResult validate_greek_input(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t offset = i;
        const std::uint32_t cp = next_codepoint(text, i);
        if (cp == kBadCodepoint)
            return validation(TransformValidationStatus::InvalidUtf8, offset,
                              "invalid UTF-8 sequence");
        if (cp < 0x80) {
            const char c = static_cast<char>(cp);
            if (is_digit(c) || c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                c == '\f' || c == '\v')
                continue;
            return validation(TransformValidationStatus::UnsupportedInput, offset,
                              "character is not supported in Greek mode");
        }
        bool upper = false;
        if (!greek_row_for_codepoint(cp, &upper))
            return validation(TransformValidationStatus::UnsupportedInput, offset,
                              "Greek diacritic or unsupported character has no reversible encoding");
    }
    return {};
}

TransformValidationResult validate_greek_transformed_data(const std::string& text) {
    bool after_letter = false;
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        if (byte >= 0x80)
            return validation(TransformValidationStatus::MalformedData, i,
                              "Greek transformed data must be ASCII");
        if (text[i] == ' ') {
            if (i == 0 || i + 1 == text.size() || text[i - 1] == ' ')
                return validation(TransformValidationStatus::MalformedData, i,
                                  "Greek transformed spacing is not canonical");
            after_letter = false;
            ++i;
            continue;
        }
        if (is_digit(text[i])) {
            if (after_letter)
                return validation(TransformValidationStatus::MalformedData, i,
                                  "literal number after Greek code needs a marker");
            while (i < text.size() && is_digit(text[i])) ++i;
            continue;
        }
        GreekToken token;
        if (!parse_greek_token(text, i, &token))
            return validation(TransformValidationStatus::MalformedData, i,
                              "unknown Greek transform code");
        i += token.length;
        after_letter = true;
        if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
            if (i + 2 >= text.size() || !is_digit(text[i + 2]))
                return validation(TransformValidationStatus::MalformedData, i,
                                  "Greek literal number marker needs digits");
            i += 2;
            while (i < text.size() && is_digit(text[i])) ++i;
            after_letter = false;
        }
    }
    return {};
}

std::string transform_greek(const std::string& text) {
    const TransformValidationResult valid = validate_greek_input(text);
    if (!valid.ok()) throw std::invalid_argument(valid.reason);

    std::string out;
    out.reserve(text.size());
    bool digit_would_read_as_code = false;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint32_t cp = next_codepoint(text, i);
        if (cp < 0x80) {
            const char c = static_cast<char>(cp);
            if (is_digit(c)) {
                if (digit_would_read_as_code) {
                    out += "//";
                    digit_would_read_as_code = false;
                }
                out += c;
                continue;
            }
            if (!out.empty() && out.back() != ' ') out += ' ';
            digit_would_read_as_code = false;
            continue;
        }
        bool upper = false;
        const GreekRow* row = greek_row_for_codepoint(cp, &upper);
        out += row->code;
        if (upper) out += '0';
        digit_would_read_as_code = true;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string untransform_greek(const std::string& text) {
    const TransformValidationResult valid = validate_greek_transformed_data(text);
    if (!valid.ok()) throw std::invalid_argument(valid.reason);

    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ' || is_digit(text[i])) {
            out += text[i++];
            continue;
        }
        GreekToken token;
        parse_greek_token(text, i, &token);
        i += token.length;
        std::uint32_t cp = token.upper ? token.row->upper : token.row->lower;
        if (!token.upper && token.row->lower == 0x03C3 && !greek_letter_follows(text, i))
            cp = 0x03C2;
        append_utf8(out, cp);
        if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
            i += 2;
            while (i < text.size() && is_digit(text[i])) out += text[i++];
        }
    }
    return out;
}

TransformValidationResult validate_hangul_input(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t offset = i;
        const std::uint32_t cp = next_codepoint(text, i);
        if (cp == kBadCodepoint)
            return validation(TransformValidationStatus::InvalidUtf8, offset,
                              "invalid UTF-8 sequence");
        if (cp >= 0xAC00 && cp <= 0xD7A3) continue;
        if (hangul_compat_code(cp)) continue;
        if (is_modern_hangul_initial(cp)) {
            if (i >= text.size())
                return validation(TransformValidationStatus::UnsupportedInput, offset,
                                  "incomplete decomposed Hangul syllable");
            const std::size_t medial_offset = i;
            const std::uint32_t medial = next_codepoint(text, i);
            if (medial == kBadCodepoint)
                return validation(TransformValidationStatus::InvalidUtf8, medial_offset,
                                  "invalid UTF-8 sequence");
            if (!is_modern_hangul_medial(medial))
                return validation(TransformValidationStatus::UnsupportedInput, medial_offset,
                                  "decomposed Hangul initial needs a modern medial jamo");
            if (i < text.size()) {
                std::size_t after_final = i;
                const std::uint32_t possible_final = next_codepoint(text, after_final);
                if (possible_final == kBadCodepoint)
                    return validation(TransformValidationStatus::InvalidUtf8, i,
                                      "invalid UTF-8 sequence");
                if (is_modern_hangul_final(possible_final)) i = after_final;
            }
            continue;
        }
        if (is_modern_hangul_medial(cp) || is_modern_hangul_final(cp))
            return validation(TransformValidationStatus::UnsupportedInput, offset,
                              "incomplete decomposed Hangul syllable");
        if (cp < 0x80) {
            const char c = static_cast<char>(cp);
            if (c == ' ' || is_digit(c) || hangul_punctuation_code(cp) >= 0) continue;
            return validation(TransformValidationStatus::UnsupportedInput, offset,
                              "character is not supported in Hangul mode");
        }
        return validation(TransformValidationStatus::UnsupportedInput, offset,
                          "character is not supported in Hangul mode");
    }
    return {};
}

TransformValidationResult validate_hangul_transformed_data(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        if (byte >= 0x80)
            return validation(TransformValidationStatus::MalformedData, i,
                              "Hangul transformed data must be ASCII");
        if (text[i] == ' ' || is_digit(text[i])) {
            ++i;
            continue;
        }
        if (text[i] != '/')
            return validation(TransformValidationStatus::MalformedData, i,
                              "text outside a Hangul frame");
        HangulFrame frame;
        std::string reason;
        if (!parse_hangul_frame(text, i, &frame, &reason))
            return validation(TransformValidationStatus::MalformedData, i, reason);
        i = frame.end;
    }
    return {};
}

std::string transform_hangul(const std::string& text) {
    const TransformValidationResult valid = validate_hangul_input(text);
    if (!valid.ok()) throw std::invalid_argument(valid.reason);

    std::string out;
    out.reserve(text.size() * 2);
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint32_t cp = next_codepoint(text, i);
        if (cp >= 0xAC00 && cp <= 0xD7A3) {
            const std::uint32_t value = cp - 0xAC00;
            const int initial = static_cast<int>(value / (21 * 28));
            const int medial = static_cast<int>((value / 28) % 21);
            const int final = static_cast<int>(value % 28);
            append_hangul_syllable_frame(out, '1', initial, medial, final);
            continue;
        }
        if (is_modern_hangul_initial(cp)) {
            const int initial = static_cast<int>(cp - 0x1100);
            const std::uint32_t medial_cp = next_codepoint(text, i);
            const int medial = static_cast<int>(medial_cp - 0x1161);
            int final = 0;
            if (i < text.size()) {
                std::size_t after_final = i;
                const std::uint32_t possible_final = next_codepoint(text, after_final);
                if (is_modern_hangul_final(possible_final)) {
                    final = static_cast<int>(possible_final - 0x11A7);
                    i = after_final;
                }
            }
            append_hangul_syllable_frame(out, '2', initial, medial, final);
            continue;
        }
        if (const char* code = hangul_compat_code(cp)) {
            out += "/3";
            out += code;
            out += '/';
            continue;
        }
        if (cp == ' ' || is_digit(static_cast<char>(cp))) {
            out += static_cast<char>(cp);
            continue;
        }
        out += "/4";
        out += static_cast<char>('0' + hangul_punctuation_code(cp));
        out += '/';
    }
    return out;
}

std::string untransform_hangul(const std::string& text) {
    const TransformValidationResult valid = validate_hangul_transformed_data(text);
    if (!valid.ok()) throw std::invalid_argument(valid.reason);

    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ' || is_digit(text[i])) {
            out += text[i++];
            continue;
        }
        HangulFrame frame;
        std::string reason;
        parse_hangul_frame(text, i, &frame, &reason);
        i = frame.end;
        if (frame.kind == '1') {
            const std::uint32_t cp = 0xAC00u +
                static_cast<std::uint32_t>((frame.initial * 21 + frame.medial) * 28 +
                                           frame.final);
            append_utf8(out, cp);
        } else if (frame.kind == '2') {
            append_utf8(out, 0x1100u + static_cast<std::uint32_t>(frame.initial));
            append_utf8(out, 0x1161u + static_cast<std::uint32_t>(frame.medial));
            if (frame.final > 0)
                append_utf8(out, 0x11A7u + static_cast<std::uint32_t>(frame.final));
        } else {
            append_utf8(out, frame.codepoint);
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
