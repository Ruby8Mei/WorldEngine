// pipeline.hpp — everything that happens AROUND the cipher
//
// This layer is deliberately unconstrained. Padding, cover traffic, markers
// and the double pass are operator procedure, not cryptographic machinery,
// so they can evolve freely without touching inop.hpp.
#pragma once

#include <string>

#include "inop.hpp"

namespace inop {

struct PipelineConfig {
    bool double_pass = true;      // encipher, reverse, encipher again
    bool padding = true;          // wrap in random cover traffic — this
                                   // hides message boundaries, not length:
                                   // padding scales with message size, so
                                   // ciphertext length still tracks it
    bool moving_reflector = true; // advance the reflector each keypress
    int block = 16;               // grouping width in the printed output
    int base_noise = 64;          // minimum cover-traffic length
    int marker_len = 16;          // hidden boundary length
};

struct Encrypted {
    std::string ciphertext;
    std::string marker;  // empty when padding is off
};

// Strip a config back to what the suite historically allowed. Legacy is a
// 1939 machine: no padding, no double pass, no reflector motion, and output
// in 5-letter groups the way it went out over the wire. Returns true if
// anything was actually locked.
bool apply_suite_lock(PipelineConfig& cfg, bool historic_lock, int block);

// Lowercase, map spaces to '#', drop anything the alphabet cannot carry.
std::string preprocess(const std::string& text, const Alphabet& alpha);

// Insert '/' between a letter and an immediately-following literal digit in
// RAW operator input — "Room A2" -> "Room A/2" — so it survives folding and
// preprocessing distinguishable from a digit the diacritic-numeral scheme
// introduces (which never gets a separator). A space is not used for this:
// spaces get folded to SPACE_SUB same as any other space in the message, so
// "Room A2" would silently become two separate tokens instead of staying
// one recognizably-marked word. Must run before any diacritic folding, on
// the raw text, or it cant tell literal digits from folded ones anymore.
std::string mark_literal_digits(const std::string& text);

// Group into fixed-width blocks for transmission, as an operator would.
std::string group(const std::string& text, int block);

// The sign-off phrase, matched against the lowercase-preprocessed text —
// must be trailing content, not just present anywhere. INOP-38 only; the
// caller is responsible for exempting Legacy.
extern const std::string SIGNOFF_PHRASE;
bool ends_with_signoff(const std::string& text, const Alphabet& alpha);

class Pipeline {
public:
    Pipeline(Machine& machine, PipelineConfig cfg);

    // Preprocesses, pads (if cfg.padding), and double-passes (if
    // cfg.double_pass) plaintext into ciphertext. Encrypted::marker is
    // empty when padding is off; otherwise it is the boundary string
    // decrypt() needs back to find the real message inside the padding.
    Encrypted encrypt(const std::string& plaintext);

    // Reverses encrypt(). Throws if cfg.padding is on and marker is empty —
    // a blank marker must fail loudly rather than hand back the raw
    // noise-padded blob as if it were the message. Returned text has
    // SPACE_SUB already mapped back to a literal space.
    std::string decrypt(const std::string& ciphertext, const std::string& marker);

    const PipelineConfig& config() const { return cfg_; }

private:
    std::string run_pass(const std::string& text);
    std::string run_double_pass(const std::string& text);

    Machine& machine_;
    PipelineConfig cfg_;
};

}  // namespace inop
