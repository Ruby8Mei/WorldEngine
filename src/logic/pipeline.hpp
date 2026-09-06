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
    bool double_pass = true;      // encipher, swap halves, encipher again
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

// Group into fixed-width blocks for transmission, as an operator would.
std::string group(const std::string& text, int block);

class Pipeline {
public:
    Pipeline(Machine& machine, PipelineConfig cfg);

    // Preprocesses, pads (if cfg.padding), and double-passes (if
    // cfg.double_pass) plaintext into ciphertext. Encrypted::marker is
    // empty when padding is off; otherwise it is the boundary string
    // decrypt() needs back to find the real message inside the padding.
    //
    // Under the double pass the body is rounded up to an even length with
    // one symbol drawn from the alphabet, because the half-swap between the
    // two passes is only defined on an even length. Padding already
    // produced an even body on its own; with padding off, that extra symbol
    // has no marker to hide behind and comes back as one trailing symbol on
    // the round trip.
    Encrypted encrypt(const std::string& plaintext);

    // Reverses encrypt(). Throws if cfg.padding is on and marker is empty —
    // a blank marker must fail loudly rather than hand back the raw
    // noise-padded blob as if it were the message — and, under the double
    // pass, if the ciphertext length is odd, which encrypt() never produces
    // and so means symbols went missing in transit. Returned text has
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
