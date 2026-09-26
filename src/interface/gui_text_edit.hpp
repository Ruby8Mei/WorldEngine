#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace inop::gui {

std::uint32_t read_utf8(const std::string& text, std::size_t& offset);
std::size_t next_utf8(const std::string& text, std::size_t offset);
std::size_t previous_utf8(const std::string& text, std::size_t offset);
std::size_t utf8_boundary(const std::string& text, std::size_t offset);

}
