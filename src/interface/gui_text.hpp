#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include "transform.hpp"

namespace inop::gui {

inline std::uint32_t read_utf8(const std::string& text, size_t& at) {
    const auto first = static_cast<unsigned char>(text[at++]);
    if (first < 0x80) return first;
    const size_t count = first >= 0xC2 && first <= 0xDF ? 2 :
                         first >= 0xE0 && first <= 0xEF ? 3 :
                         first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    if (!count || at + count - 1 > text.size()) return 0xFFFD;
    std::uint32_t cp = first & (0x7F >> count);
    for (size_t n = 0; n < count - 1; ++n) {
        const auto byte = static_cast<unsigned char>(text[at + n]);
        if ((byte & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (byte & 0x3F);
    }
    if (cp < (count == 2 ? 0x80u : count == 3 ? 0x800u : 0x10000u) ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) return 0xFFFD;
    at += count - 1;
    return cp;
}

inline size_t next_utf8(const std::string& text, size_t at) {
    if (at < text.size()) read_utf8(text, at);
    return at;
}

inline size_t previous_utf8(const std::string& text, size_t at) {
    if (at) --at;
    while (at && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) --at;
    return at;
}

inline size_t utf8_boundary(const std::string& text, size_t at) {
    if (at > text.size()) at = text.size();
    while (at < text.size() && at &&
           (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) --at;
    return at;
}

inline std::string prepare_gui_plaintext(const std::string& text, bool use_transform) {
    if (use_transform) {
        const auto validation = validate_transform_input(text);
        if (!validation.ok()) {
            size_t position = 1;
            for (size_t at = 0; at < validation.offset && at < text.size(); ++position)
                read_utf8(text, at);
            throw std::runtime_error("Cannot encipher character " + std::to_string(position) +
                ": " + validation.reason +
                ". Punctuation and unsupported text have no encoding. Edit the message to continue.");
        }
        return transform(text);
    }
    size_t position = 0;
    for (size_t at = 0; at < text.size();) {
        ++position;
        const auto cp = read_utf8(text, at);
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') continue;
        if (!((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))) {
            throw std::runtime_error("Cannot encipher character " + std::to_string(position) +
                ": punctuation or unsupported text has no encoding in this suite. Edit the message to continue.");
        }
    }
    return text;
}

}
