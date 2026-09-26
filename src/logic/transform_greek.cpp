#include "transform.hpp"
#include "transform_internal.hpp"

#include <cstdint>
#include <stdexcept>

namespace inop {
using namespace transform_detail;
namespace {
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

}
