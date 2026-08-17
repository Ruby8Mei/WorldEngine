#pragma once

#include <map>
#include <string>
#include <vector>

#include "inop.hpp"

namespace inop {

struct Suite {
    std::string code;
    std::string name;
    std::string alphabet;
    int min_rotors;
    int max_rotors;
    int max_plug_pairs;
    int max_notches;
    std::vector<std::string> rotor_names;
    std::vector<std::string> reflector_names;
    bool notches_are_fixed;
    bool historic_lock;
    int  block;
};

const std::map<std::string, Suite>& suites();
const Suite& suite(const std::string& code);

Rotor make_rotor(const std::string& name, const Alphabet& alpha);
Reflector make_reflector(const std::string& name, const Alphabet& alpha);

bool wiring_is_rotation(const std::string& wiring, const std::string& alphabet);

int load_wheel_file(const std::string& path, std::vector<std::string>* problems = 0);

std::vector<std::string> available_rotors(const Suite& s);
std::vector<std::string> available_reflectors(const Suite& s);

bool rotor_exists(const std::string& name, const Suite& s);
bool reflector_exists(const std::string& name, const Suite& s);

}

