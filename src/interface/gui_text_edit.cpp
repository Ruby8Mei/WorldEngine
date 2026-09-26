#include "gui_text_edit.hpp"
#include "utf8.hpp"

#include <algorithm>

namespace inop::gui {

std::uint32_t read_utf8(const std::string& text, std::size_t& offset) {
    const Utf8DecodeResult result = decode_utf8(text, offset);
    offset += result.byte_width;
    return result.malformed ? 0xFFFDu : result.codepoint;
}

std::size_t utf8_boundary(const std::string& text, std::size_t offset) {
    offset = std::min(offset, text.size());
    const std::size_t first = offset > 3 ? offset - 3 : 0;
    for (std::size_t at = first; at < offset; ++at) {
        const Utf8DecodeResult result = decode_utf8(text, at);
        if (!result.malformed && at + result.byte_width > offset) return at;
    }
    return offset;
}

std::size_t next_utf8(const std::string& text, std::size_t offset) {
    const std::size_t at = utf8_boundary(text, offset);
    return at < text.size() ? at + decode_utf8(text, at).byte_width : at;
}

std::size_t previous_utf8(const std::string& text, std::size_t offset) {
    const std::size_t target = utf8_boundary(text, offset);
    if (target < std::min(offset, text.size())) return target;
    if (target == 0) return 0;
    const std::size_t first = target > 4 ? target - 4 : 0;
    for (std::size_t at = first; at < target; ++at) {
        const Utf8DecodeResult result = decode_utf8(text, at);
        if (!result.malformed && at + result.byte_width == target) return at;
    }
    return target - 1;
}

}
