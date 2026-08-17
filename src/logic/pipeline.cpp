#include "pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "rng.hpp"

namespace inop {

bool apply_suite_lock(PipelineConfig& cfg, bool historic_lock, int block) {
    cfg.block = block;
    if (!historic_lock) return false;
    cfg.double_pass      = false;
    cfg.padding          = false;
    cfg.moving_reflector = false;
    return true;
}

std::string preprocess(const std::string& text, const Alphabet& alpha) {
    const bool has_space_sub = alpha.contains(SPACE_SUB);
    std::string out;
    out.reserve(text.size());
    for (char raw : text) {
        char c = alpha.fold_case(raw);
        if (c == ' ') {
            if (has_space_sub) out += SPACE_SUB;
        } else if (c == SPACE_SUB) {
        } else if (alpha.contains(c)) {
            out += c;
        }
    }
    return out;
}

std::string mark_literal_digits(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    unsigned char prev = 0;
    for (unsigned char c : text) {
        bool is_digit = c >= '0' && c <= '9';
        bool prev_is_letter = (prev >= 'A' && prev <= 'Z') || (prev >= 'a' && prev <= 'z') ||
                               prev >= 0x80;
        if (is_digit && prev_is_letter) out += '/';
        out += static_cast<char>(c);
        prev = c;
    }
    return out;
}

std::string group(const std::string& text, int block) {
    if (block <= 0) return text;
    std::string out;
    out.reserve(text.size() + text.size() / static_cast<size_t>(block) * 2);
    for (size_t i = 0; i < text.size(); i += static_cast<size_t>(block)) {
        if (i) out += "  ";
        out += text.substr(i, static_cast<size_t>(block));
    }
    return out;
}

namespace {

std::string pad(const std::string& msg, const std::string& alpha, int base_noise, int block) {
    int scaled = std::max(base_noise, static_cast<int>(msg.size() * 35 / 100));
    int residue = (static_cast<int>(msg.size()) + scaled) % block;
    int extra = (block - residue) % block;
    int n = scaled + extra;
    int front = static_cast<int>(secure_below(static_cast<uint32_t>(n) + 1));
    int back = n - front;
    return secure_string(alpha, static_cast<size_t>(front)) + msg +
           secure_string(alpha, static_cast<size_t>(back));
}

std::string carve(const std::string& full, const std::string& marker) {
    size_t i = full.find(marker);
    size_t j = full.rfind(marker);
    if (i == std::string::npos || i == j)
        throw std::runtime_error(
            "markers not found — wrong settings, wrong key, or corrupted ciphertext");
    return full.substr(i + marker.size(), j - i - marker.size());
}

}

Pipeline::Pipeline(Machine& machine, PipelineConfig cfg) : machine_(machine), cfg_(cfg) {
    machine_.set_moving_reflector(cfg_.moving_reflector);
}

std::string Pipeline::run_pass(const std::string& text) {
    machine_.rewind();
    return machine_.encipher(text);
}

std::string Pipeline::run_double_pass(const std::string& text) {
    std::string s = run_pass(text);
    if (cfg_.double_pass) {
        std::reverse(s.begin(), s.end());
        s = run_pass(s);
    }
    return s;
}

Encrypted Pipeline::encrypt(const std::string& plaintext) {
    const std::string& alpha = machine_.alphabet().str();
    Encrypted result;

    std::string body;
    if (cfg_.padding) {
        result.marker = secure_string(alpha, static_cast<size_t>(cfg_.marker_len));
        body = pad(result.marker + preprocess(plaintext, machine_.alphabet()) + result.marker,
                   alpha, cfg_.base_noise, cfg_.block);
    } else {
        body = preprocess(plaintext, machine_.alphabet());
    }

    result.ciphertext = run_double_pass(body);
    return result;
}

std::string Pipeline::decrypt(const std::string& ciphertext, const std::string& marker) {
    if (cfg_.padding && marker.empty())
        throw std::runtime_error("a marker is required to decipher a padded message");

    std::string s = run_double_pass(ciphertext);
    if (cfg_.padding) s = carve(s, marker);
    std::replace(s.begin(), s.end(), SPACE_SUB, ' ');
    return s;
}

}

