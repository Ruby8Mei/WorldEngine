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

// One notch-string per rotor, each rotor's count drawn independently in
// [1, max_notches_per_rotor] rather than one shared count for every rotor —
// see the .cpp for why this exists alongside random_settings().
std::vector<std::string> random_variable_notches(const Alphabet& alpha, int rotor_count,
                                                   int max_notches_per_rotor);

// ── wheel batches: built and validated in memory, written only if valid ─
//
// gen_wheels() used to inline all of this, which left the refusal path
// unreachable from a test: the only way to see it was to break the OS
// entropy source. The split is the seam that makes the guards in DESIGN
// section 6 testable, and nothing about the behaviour changed with it.

// One generated wheel. `notches` is empty for a reflector, and for a rotor
// whose notches are left to be set per message.
struct GeneratedWheel {
    std::string name;
    std::string wiring;
    std::string notches;
};

struct WheelBatch {
    // Held as data rather than as ready-to-write lines, which is what the
    // 2.2.x text format let this be: the writer serialises it, so changing
    // the file format does not change what a batch is.
    std::vector<GeneratedWheel> wheels;
    std::vector<std::string> wirings;  // the same wheels, for validation
    bool rotors = true;
};

// Empty if the batch is fit to write, otherwise the reason, phrased for
// the operator. A batch is unfit if two wheels share a wiring (a broken
// entropy source) or if any wiring is a pure rotation of the alphabet (a
// Caesar wheel, which load_wheel_file() would reject the whole file for).
std::string wheel_batch_problem(const WheelBatch& b, const Suite& s);

// Generate `count` wheels into memory. Runs entropy_self_check() first:
// generation is the one moment where a dead entropy source is
// unrecoverable, because its output looks plausible and is not.
WheelBatch build_wheel_batch(const Suite& s, bool rotors, int count,
                             const std::string& prefix, int start, int notch_n);

// Write a batch, or refuse it. A batch with a problem is never written and
// the target is left byte-identical, in BOTH modes. Overwrite is the worse
// case and the reason this function exists: std::ios::trunc empties the
// target at open, so validating after opening destroys the good wheels
// being replaced as well as failing to write the bad ones.
// Returns false and fills *error on refusal or I/O failure.
bool write_wheel_batch(const std::string& path, const WheelBatch& b, const Suite& s,
                       bool append, std::string* error);

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

// A complete, valid, ready-to-use key sheet entry for the given suite, using
// exactly rotor_count rotors. rotor_count must fall within
// [s.min_rotors, s.max_rotors].
GeneratedSettings random_settings(const Suite& s, int rotor_count, int plug_pairs,
                                   int notches_per_rotor);

GeneratedSettings random_setup_settings(const Suite& s);

// Serialise in the same directive format main.cpp reads.
std::string settings_to_text(const GeneratedSettings& g);

// Write `count` key sheet entries for `s` to `path`, overwriting it. Every
// entry uses `fixed_count` rotors unless `random_count`, in which case each
// entry draws its own count from the suite's range. `first_entry`, if given,
// comes back holding entry 1, which is what lets a caller offer to install
// it as inop.settings without generating a second one. Returns false and
// fills *error on an I/O failure or a refused entry.
//
// This used to be inlined in the terminal maintenance menu, where the GUI
// could not reach it. Both front ends go through here now so the sheet they
// produce cannot drift apart.
bool write_key_sheet(const std::string& path, const Suite& s, int count, int plug_pairs,
                     int notches_per_rotor, bool random_count, int fixed_count,
                     std::string* first_entry, std::string* error);

// ── the interactive maintenance menu ────────────────────────────────────
void run_generator();

}  // namespace inop
