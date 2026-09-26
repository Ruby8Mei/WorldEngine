#include "transform.hpp"
#include "transform_internal.hpp"

#include <cstdint>
#include <stdexcept>

namespace inop {
using namespace transform_detail;
namespace {
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

}
