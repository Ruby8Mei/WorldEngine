#include "transform.hpp"
#include "transform_internal.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace inop {
using namespace transform_detail;
namespace {
struct Row {
    std::uint32_t cp;
    const char* out;
};

const Row kTable[] = {
#include "transform_table.inc"
};

constexpr std::size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

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


}

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
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
                if (!out.empty() && out.back() != ' ') out += ' ';
                digit_would_read_as_mark = false;
            }
            continue;
        }

        const char* fold = fold_of(cp);
        if (!fold) continue;
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

        std::string key(1, c);
        std::size_t j = i + 1;
        while (j < text.size()) {
            if (text[j] == '/' && j + 1 < text.size() && text[j + 1] == '/') break;
            if (!is_digit(text[j])) break;
            while (j < text.size() && is_digit(text[j])) key += text[j++];
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
            out += static_cast<char>(c - 'a' + 'A');
            i = j;
        } else {
            const std::map<std::string, std::uint32_t>& rev = reverse_table();
            const std::map<std::string, std::uint32_t>::const_iterator it = rev.find(key);
            if (it == rev.end()) {
                out += c;
                ++i;
                continue;
            }
            append_utf8(out, it->second);
            i = j;
        }

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

}
