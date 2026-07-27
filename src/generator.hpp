// generator.hpp — INOP maintenance: fresh wheels and fresh settings
//
// PREP LAYER. Nothing here is part of the cipher; this is the factory that
// stamps out wheels and the clerk who fills in the daily key sheet. All
// randomness comes from the OS entropy source, because both are key material.
#pragma once

#include <string>
#include <vector>

#include "inop.hpp"
#include "registry.hpp"

namespace inop {

// ── wheel generation ────────────────────────────────────────────────────

// A uniformly random permutation of the alphabet.
std::string random_rotor_wiring(const Alphabet& alpha);

// A random fixed-point-free involution: shuffle, then pair off neighbours.
// Requires an even alphabet (both INOP alphabets are).
std::string random_reflector_wiring(const Alphabet& alpha);

// n random notch symbols, distinct, drawn from the alphabet.
// Never returns an empty string — a notchless rotor collapses the machine.
std::string random_notches(const Alphabet& alpha, int count);

// ── settings generation ─────────────────────────────────────────────────

struct GeneratedSettings {
    std::string suite_code;
    std::vector<std::string> rotors;
    std::string reflector;
    std::vector<int> rings;
    std::vector<std::string> notches;
    std::vector<std::string> plugs;
    std::string master_key;
};

// A complete, valid, ready-to-use key sheet entry for the given suite.
GeneratedSettings random_settings(const Suite& s, int plug_pairs, int notches_per_rotor);

// Serialise in the same directive format main.cpp reads.
std::string settings_to_text(const GeneratedSettings& g);

// ── the interactive maintenance menu ────────────────────────────────────
void run_generator();

}  // namespace inop
