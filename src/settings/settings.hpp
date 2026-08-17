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
    std::vector<std::string> notches;
    std::vector<std::string> plugs;
    std::string master_key;
};

bool parse_settings_block(std::istream& in, Settings& out, std::string* error);

bool validate_settings(const Settings& s, std::string* error);

bool load_settings(Settings& s, const std::string& path, std::string* error);

bool save_settings(const Settings& s, const std::string& path);

Machine build_machine(const Settings& s, std::string* note = nullptr);

int count_keysheet_entries(const std::string& path);

bool load_keysheet_entry(const std::string& path, int index, Settings& out, std::string* error);

bool load_keysheet_entry_from_stream(std::istream& in, int index, Settings& out, std::string* error);

}

