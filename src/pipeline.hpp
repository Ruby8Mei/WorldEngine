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
    bool padding = true;          // wrap in random cover traffic
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

// Uppercase, map spaces to '#', drop anything the alphabet cannot carry.
std::string preprocess(const std::string& text, const Alphabet& alpha);

// Group into fixed-width blocks for transmission, as an operator would.
std::string group(const std::string& text, int block);

class Pipeline {
public:
    Pipeline(Machine& machine, PipelineConfig cfg);

    Encrypted encrypt(const std::string& plaintext);
    std::string decrypt(const std::string& ciphertext, const std::string& marker);

    const PipelineConfig& config() const { return cfg_; }

private:
    std::string run_pass(const std::string& text);

    Machine& machine_;
    PipelineConfig cfg_;
};

}  // namespace inop
