#include "cli_compat.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

#include "languages.hpp"

namespace inop {

namespace {

std::string lower_cli(std::string value) {
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

}

CliCommand parse_cli_command(const std::string& line) {
    if (line.empty() || line[0] != ':') return CliCommand::Message;
    const std::string command = lower_cli(line);
    if (command == ":q" || command == ":quit" || command == ":exit") return CliCommand::Quit;
    if (command == ":i" || command == ":info") return CliCommand::Info;
    if (command == ":s" || command == ":save") return CliCommand::Save;
    if (command == ":d" || command == ":decrypt") return CliCommand::Decrypt;
    if (command == ":d-old" || command == ":decrypt-old") return CliCommand::DecryptOld;
    if (command == ":b" || command == ":batch") return CliCommand::Batch;
    if (command == ":?" || command == ":h" || command == ":help") return CliCommand::Help;
    return CliCommand::Unknown;
}

std::string take_trailing_language_tag(std::vector<std::string>& tokens, bool historic_lock) {
    if (historic_lock || tokens.empty() || tokens.back().size() != 3) return {};
    const std::string language = lower_cli(tokens.back());
    if (!is_supported_language(language)) return {};
    tokens.pop_back();
    return language;
}

std::vector<std::string> tracked_key_material() {
    static const char* kNames[] = {"inop_rotors.json",  "inop_reflectors.json",
                                   "inop_keysheet.json", "inop_settings.json",
                                   "inop_wheels.txt",    "inop_keysheet.txt",
                                   "inop.settings"};

    std::string dir = ".";
    std::string index;
    for (int up = 0; up < 6; ++up) {
        std::ifstream f(dir + "/.git/index", std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            index = ss.str();
            break;
        }
        dir += "/..";
    }

    std::vector<std::string> found;
    if (index.empty()) return found;
    for (const char* name : kNames)
        if (index.find(name) != std::string::npos) found.push_back(name);
    return found;
}

}
