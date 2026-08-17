#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace inop {

void secure_bytes(uint8_t* buf, size_t n);

uint32_t secure_below(uint32_t bound);

std::string secure_string(const std::string& alphabet, size_t n);

void entropy_self_check();

}

