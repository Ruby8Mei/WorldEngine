#include <iostream>
#include <string>

#include "audio_manager.hpp"

int main() {
    int failures = 0;
    inop::gui::audio_self_test([&](bool ok, const std::string& name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        if (!ok) ++failures;
    });
    return failures == 0 ? 0 : 1;
}
