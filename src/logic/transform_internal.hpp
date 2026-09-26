#pragma once

#include "transform.hpp"
#include "utf8.hpp"

#include <cstdint>
#include <string>

namespace inop::transform_detail {

inline void append_utf8(std::string& out, std::uint32_t cp) {
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

inline std::uint32_t next_codepoint(const std::string& text, std::size_t& offset) {
    const Utf8DecodeResult result = decode_utf8(text, offset);
    offset += result.byte_width;
    return result.malformed ? 0xFFFFFFFFu : result.codepoint;
}

constexpr std::uint32_t kBadCodepoint = 0xFFFFFFFFu;

inline bool is_ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

inline bool is_ascii_punctuation(char c) {
    return c >= '!' && c <= '~' && !is_ascii_letter(c) && !is_digit(c);
}

inline TransformValidationResult validation(TransformValidationStatus status,
                                            std::size_t offset, const std::string& reason) {
    TransformValidationResult result;
    result.status = status;
    result.offset = offset;
    result.reason = reason;
    return result;
}

}
