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
            // A literal '#' is pruned rather than carried through: decrypt()
            // maps every SPACE_SUB back to a space, so a literal one would
            // be indistinguishable from a substituted space either way.
        } else if (alpha.contains(c)) {
            out += c;
        }
        // anything else is silently dropped — the machine has no key for it
    }
    return out;
}

std::string mark_literal_digits(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    // Tracks the last character that will still be there after
    // fold_diacritics() strips punctuation it doesn't carry (parens, %,
    // commas, ...) — not just the literal previous byte. Punctuation sitting
    // between a letter and a digit ("neomneun(50.74%)") disappears during
    // folding, so if marking only looked at the immediate previous byte, the
    // digit would end up touching the letter in the folded text with no '/'
    // to say it doesn't belong to a diacritic-numeral pair — exactly the gap
    // that broke the human-readable round trip on real corpus text with
    // parenthetical numbers.
    unsigned char prev_surviving = 0;
    for (unsigned char c : text) {
        bool is_digit = c >= '0' && c <= '9';
        bool prev_is_letter = (prev_surviving >= 'A' && prev_surviving <= 'Z') ||
                               (prev_surviving >= 'a' && prev_surviving <= 'z') ||
                               prev_surviving >= 0x80;  // any byte of a multi-byte UTF-8 letter
        if (is_digit && prev_is_letter) out += '/';
        out += static_cast<char>(c);

        // Mirrors apply_fold_table()'s keep-set (letters, digits, space,
        // '#', '/') plus multi-byte UTF-8 bytes, which survive as
        // recognized diacritics get converted rather than dropped.
        bool survives = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') || c == ' ' || c == '#' ||
                         c == '/' || c >= 0x80;
        if (survives) prev_surviving = c;
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

}  // namespace

Pipeline::Pipeline(Machine& machine, PipelineConfig cfg) : machine_(machine), cfg_(cfg) {
    machine_.set_moving_reflector(cfg_.moving_reflector);
}

// Every pass starts from the same rewound state, which is what makes the
// double pass reversible.
std::string Pipeline::run_pass(const std::string& text) {
    machine_.rewind();
    return machine_.encipher(text);
}

// encrypt()/decrypt() both run a pass, and — if double_pass is on — reverse
// and run a second one. Was written out identically in both places.
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
    // A blank marker must fail loudly, not silently hand back the raw
    // noise-padded blob as if it were the message.
    if (cfg_.padding && marker.empty())
        throw std::runtime_error("a marker is required to decipher a padded message");

    std::string s = run_double_pass(ciphertext);
    if (cfg_.padding) s = carve(s, marker);
    std::replace(s.begin(), s.end(), SPACE_SUB, ' ');
    return s;
}

}  // namespace inop
