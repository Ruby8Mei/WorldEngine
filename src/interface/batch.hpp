#pragma once

#include <string>
#include <vector>

namespace inop {

constexpr size_t MAX_BATCH_FILE_BYTES = 1474560;

std::vector<std::string> split_batch_messages(const std::string& text);

bool read_batch_file(const std::string& path, std::string& out, std::string* error);

}

