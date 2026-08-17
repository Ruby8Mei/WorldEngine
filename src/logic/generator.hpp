#pragma once

#include <string>
#include <vector>

#include "inop.hpp"
#include "registry.hpp"

namespace inop {


std::string random_rotor_wiring(const Alphabet& alpha);

std::string random_reflector_wiring(const Alphabet& alpha);

std::string random_notches(const Alphabet& alpha, int count);

std::vector<std::string> random_variable_notches(const Alphabet& alpha, int rotor_count,
                                                   int max_notches_per_rotor);


struct GeneratedSettings {
    std::string suite_code;
    std::vector<std::string> rotors;
    std::string reflector;
    std::vector<int> rings;
    std::vector<std::string> notches;
    std::vector<std::string> plugs;
    std::string master_key;
};

GeneratedSettings random_settings(const Suite& s, int rotor_count, int plug_pairs,
                                   int notches_per_rotor);

std::string settings_to_text(const GeneratedSettings& g);

void run_generator();

}

