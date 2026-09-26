#pragma once

#include <string>
#include <vector>

namespace inop {

enum class CliCommand { Message, Quit, Info, Save, Decrypt, DecryptOld, Batch, Help, Unknown };

CliCommand parse_cli_command(const std::string& line);
std::string take_trailing_language_tag(std::vector<std::string>& tokens, bool historic_lock);
std::vector<std::string> tracked_key_material();

}
