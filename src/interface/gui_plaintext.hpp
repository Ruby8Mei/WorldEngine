#pragma once

#include <string>

namespace inop::gui {

std::string prepare_gui_plaintext(const std::string& text, bool use_transform,
                                  const std::string& language_code = "");
std::string restore_gui_plaintext(const std::string& text, bool use_transform,
                                  const std::string& language_code = "");

}
