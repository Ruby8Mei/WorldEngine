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

struct KeySheetEntry {
    Settings settings;
    std::string error;
    bool valid = false;
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

// How many entries a key sheet holds (0 if absent, unreadable or empty).
// A key sheet is JSON as of 2.3.0: an object with an "entries" array, each
// entry the same shape as a settings file.
int count_keysheet_entries(const std::string& path);

bool load_keysheet(const std::string& path, std::vector<KeySheetEntry>& entries,
                   std::string* error);

// Parses entry `index` (1-based) out of a key sheet and validates it.
//
// The stream-scanning variant this used to sit beside is gone: it existed
// so a batch caller reading entries in order paid O(1) per entry instead of
// rescanning from the top, and a JSON document has to be parsed whole
// regardless, so there was nothing left for it to save.
bool load_keysheet_entry(const std::string& path, int index, Settings& out, std::string* error);

// One-time conversion of a 2.2.x plain-text settings file into JSON. Does
// nothing and returns false if the text file is absent or the JSON one
// already exists; the original is never deleted.
bool migrate_settings_from_text(const std::string& txt_path, const std::string& json_path);

}  // namespace inop
