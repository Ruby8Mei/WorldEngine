// settings.hpp — a machine configuration, independent of how it was
// obtained (typed interactively, loaded from inop.settings, or pulled as
// one indexed entry out of a keysheet file). No I/O side effects beyond
// reading/writing the files it's explicitly asked to — errors come back as
// strings rather than being printed, so both the interactive UI and batch
// processing can present them however they like.
#pragma once

#include <string>
#include <vector>

#include "inop.hpp"

namespace inop {

struct Settings {
    std::string suite_code = "38";
    std::vector<std::string> rotors;
    std::string reflector;
    std::vector<int> rings;
    std::vector<std::string> notches;  // parallel to rotors
    std::vector<std::string> plugs;
    std::string master_key;
};

// Reads directive lines (suite/rotors/reflector/rings/notches/plugs/key)
// from `in`, skipping blank lines and '#' comments, stopping once a "key"
// line has been read (every record ends with one) or at EOF. Leaves `in`
// positioned right after the key line, so a keysheet's next entry can be
// parsed with another call against the same stream.
bool parse_settings_block(std::istream& in, Settings& out, std::string* error);

// Cross-checks a parsed Settings against its declared suite: rotor count in
// range, rings/notches/key lengths matching. Independent of where the
// Settings came from.
bool validate_settings(const Settings& s, std::string* error);

// Fails if the file is missing, empty, or fails validate_settings() against
// its own declared suite — does not modify `s` on failure.
bool load_settings(Settings& s, const std::string& path, std::string* error);

// Overwrites `path` unconditionally if it can be opened for writing.
bool save_settings(const Settings& s, const std::string& path);

// (rotor_count + 1)-symbol key wanted by Machine's constructor, synthesising
// the missing reflector-orientation symbol for historic-lock suites. If
// `note` is given and Legacy's key got a symbol dropped for compatibility,
// the message is written there instead of being printed.
Machine build_machine(const Settings& s, std::string* note = nullptr);

// How many "# --- entry N ---" records a keysheet file has (0 if absent or
// empty). Used to drive "iterate sequentially through every entry" batch
// mode without hardcoding a count.
int count_keysheet_entries(const std::string& path);

// Parses just entry `index` (1-based) out of a keysheet file and validates
// it. Used both for "one indexed entry for every message" and for
// sequential iteration.
bool load_keysheet_entry(const std::string& path, int index, Settings& out, std::string* error);

// Same as load_keysheet_entry(), but scans forward from wherever `in` is
// currently positioned instead of reopening the file and rescanning from
// the top — for batch callers reading entries 1, 2, 3, ... in order from
// one already-open stream, where reopening per entry would cost O(entries)
// per call instead of O(1) amortized.
bool load_keysheet_entry_from_stream(std::istream& in, int index, Settings& out, std::string* error);

}  // namespace inop
