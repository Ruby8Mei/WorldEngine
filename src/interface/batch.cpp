#include "batch.hpp"

#include <fstream>
#include <iterator>
#include <sstream>

namespace inop {

std::vector<std::string> split_batch_messages(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream is(text);
    std::string line, cur;
    bool have = false;
    auto flush = [&]() {
        if (have) { out.push_back(cur); cur.clear(); have = false; }
    };
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        bool blank = line.find_first_not_of(" \t") == std::string::npos;
        if (blank) { flush(); continue; }
        if (have) cur += " ";
        cur += line;
        have = true;
    }
    flush();
    return out;
}

bool read_batch_file(const std::string& path, std::string& out, std::string* error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::streamoff size = f.tellg();
    if (size < 0) { if (error) *error = "cannot determine size of " + path; return false; }
    if (static_cast<size_t>(size) > MAX_BATCH_FILE_BYTES) {
        if (error)
            *error = path + " is " + std::to_string(size) +
                     " bytes, over the 1.44MB batch limit (" +
                     std::to_string(MAX_BATCH_FILE_BYTES) + " bytes) — not read";
        return false;
    }
    f.seekg(0);
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace inop
