#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace inop {

struct Utf8DecodeResult {
    std::uint32_t codepoint = 0;
    std::size_t byte_width = 0;
    bool malformed = false;
};

Utf8DecodeResult decode_utf8(const std::string& text, std::size_t offset);

}
