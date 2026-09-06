#include "pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

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

// The transposition applied between the two passes. It has to be an
// involution or the double pass stops being self-inverse, and one setup
// sheet would no longer work in both directions.
//
// std::reverse used to fill this role. Reversal is an involution, but it
// fixes the middle index of an odd-length body, and at that index the
// second pass applies the same per-position involution the first one did.
// The two cancel: C[m] == P[m] exactly, on every message, at a position an
// analyst can compute from the length alone. That is the
// no-self-encipherment property the double pass exists to destroy, handed
// straight back at a known index.
//
// The half-swap has no fixed index at all — i + L/2 == i has no solution
// mod L — which is the entire reason it replaced reversal. It is NOT the
// case that reversal additionally degraded the middle of a message by
// pairing it with nearly adjacent rotor states: measured, A_i and A_j
// agree on a symbol at 0.0277 for lag 1 against a 1/37 = 0.0270 baseline,
// indistinguishable from lags out to 1024. Reversal had one defect, the
// fixed index, not a gradient around it.
//
// Requires an even length. encrypt() guarantees one.
void half_swap(std::string& s) {
    const size_t h = s.size() / 2;
    for (size_t i = 0; i < h; ++i) std::swap(s[i], s[i + h]);
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

// encrypt()/decrypt() both run a pass, and — if double_pass is on — swap
// the two halves and run a second one. Was written out identically in both
// places.
std::string Pipeline::run_double_pass(const std::string& text) {
    std::string s = run_pass(text);
    if (cfg_.double_pass) {
        half_swap(s);
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

    // The half-swap between the two passes needs an even body. Padding
    // already delivers one — pad() rounds the body out to a whole number of
    // blocks — but padding can be switched off, so the guarantee is made
    // here rather than assumed from a setting the operator controls. The
    // filler symbol is drawn from the alphabet like any other cover symbol
    // rather than being a fixed one, so it carries no crib. With padding on
    // it lands outside the trailing marker and carve() drops it; with
    // padding off there is no marker to carve against, so it surfaces on
    // the round trip as one extra symbol at the end.
    if (cfg_.double_pass && body.size() % 2 != 0) body += secure_string(alpha, 1);

    result.ciphertext = run_double_pass(body);
    return result;
}

std::string Pipeline::decrypt(const std::string& ciphertext, const std::string& marker) {
    // A blank marker must fail loudly, not silently hand back the raw
    // noise-padded blob as if it were the message.
    if (cfg_.padding && marker.empty())
        throw std::runtime_error("a marker is required to decipher a padded message");

    // encrypt() never emits an odd-length body under the double pass, so an
    // odd one arriving here is a truncated or mistranscribed ciphertext. Say
    // so instead of half-swapping a length the transform is not defined for
    // and handing back plausible-looking garbage.
    if (cfg_.double_pass && ciphertext.size() % 2 != 0)
        throw std::runtime_error(
            "ciphertext length must be even under the double pass — " +
            std::to_string(ciphertext.size()) +
            " symbols received, so at least one symbol is missing");

    std::string s = run_double_pass(ciphertext);
    if (cfg_.padding) s = carve(s, marker);
    std::replace(s.begin(), s.end(), SPACE_SUB, ' ');
    return s;
}

}  // namespace inop
