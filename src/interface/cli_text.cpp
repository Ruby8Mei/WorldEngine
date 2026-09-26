#include "cli_text.hpp"
#include "transform.hpp"

namespace inop {

std::string prepare_cli_text(const std::string& text, CliTextProfile profile) {
    return profile == CliTextProfile::Latin ? transform(text) : text;
}

std::string restore_cli_text(const std::string& text, CliTextProfile profile) {
    return profile == CliTextProfile::Latin ? untransform(text) : text;
}

std::vector<CliTextLine> cli_encipher_lines(const std::string& raw,
                                            CliTextProfile profile) {
    std::vector<CliTextLine> lines{{"check  ", raw}};
    if (profile == CliTextProfile::Latin)
        lines.push_back({"human  ", restore_cli_text(raw, profile)});
    return lines;
}

std::vector<CliTextLine> cli_decrypt_lines(const std::string& raw,
                                           CliTextProfile profile) {
    std::vector<CliTextLine> lines{{"plain  ", raw}};
    if (profile == CliTextProfile::Latin)
        lines.push_back({"human  ", restore_cli_text(raw, profile)});
    return lines;
}

}
