#pragma once

#include <string>

#include "inop.hpp"

namespace inop {

struct PipelineConfig {
    bool double_pass = true;
    bool padding = true;
    bool moving_reflector = true;
    int block = 16;
    int base_noise = 64;
    int marker_len = 16;
};

struct Encrypted {
    std::string ciphertext;
    std::string marker;
};

bool apply_suite_lock(PipelineConfig& cfg, bool historic_lock, int block);

std::string preprocess(const std::string& text, const Alphabet& alpha);

std::string mark_literal_digits(const std::string& text);

std::string group(const std::string& text, int block);

class Pipeline {
public:
    Pipeline(Machine& machine, PipelineConfig cfg);

    Encrypted encrypt(const std::string& plaintext);

    std::string decrypt(const std::string& ciphertext, const std::string& marker);

    const PipelineConfig& config() const { return cfg_; }

private:
    std::string run_pass(const std::string& text);
    std::string run_double_pass(const std::string& text);

    Machine& machine_;
    PipelineConfig cfg_;
};

}

