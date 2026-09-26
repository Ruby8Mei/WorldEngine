#include <iostream>
#include <string>

#include "bombe_engine.hpp"

int main() {
    int failures = 0;
    inop::bombe::self_test([&](bool ok, const std::string& name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        if (!ok) ++failures;
    });
    return failures == 0 ? 0 : 1;
}
