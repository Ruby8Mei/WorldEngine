#include "utf8.hpp"

namespace inop {

Utf8DecodeResult decode_utf8(const std::string& text, std::size_t offset) {
    if (offset >= text.size()) return {};
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) return {first, 1, false};
    std::size_t width = 0;
    std::uint32_t cp = 0;
    if ((first & 0xE0) == 0xC0) {
        width = 2;
        cp = first & 0x1Fu;
    } else if ((first & 0xF0) == 0xE0) {
        width = 3;
        cp = first & 0x0Fu;
    } else if ((first & 0xF8) == 0xF0) {
        width = 4;
        cp = first & 0x07u;
    } else {
        return {0, 1, true};
    }
    if (width > text.size() - offset) return {0, 1, true};
    for (std::size_t i = 1; i < width; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset + i]);
        if ((byte & 0xC0) != 0x80) return {0, 1, true};
        cp = (cp << 6) | (byte & 0x3Fu);
    }
    const std::uint32_t minimum = width == 2 ? 0x80u : width == 3 ? 0x800u : 0x10000u;
    if (cp < minimum || (cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu)
        return {0, 1, true};
    return {cp, width, false};
}

}
