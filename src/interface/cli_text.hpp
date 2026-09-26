#pragma once

#include <string>
#include <vector>

namespace inop {

enum class CliTextProfile { Raw, Latin };

std::string prepare_cli_text(const std::string& text, CliTextProfile profile);
std::string restore_cli_text(const std::string& text, CliTextProfile profile);

struct CliTextLine {
    std::string label;
    std::string value;
};

std::vector<CliTextLine> cli_encipher_lines(const std::string& raw,
                                            CliTextProfile profile);
std::vector<CliTextLine> cli_decrypt_lines(const std::string& raw,
                                           CliTextProfile profile);

}
