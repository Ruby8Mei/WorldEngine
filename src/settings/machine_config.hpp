#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace inop {

struct MachineConfig {
    std::string suite_code = "38";
    bool public_builtin_preset = false;
    std::vector<std::string> rotors;
    std::string reflector;
    std::vector<int> rings;
    std::vector<std::string> notches;
    std::vector<std::string> plugs;
    std::string master_key;
    std::string marker;
};

enum class MachineField {
    Suite,
    RotorCount,
    Rotor,
    Reflector,
    Ring,
    Notch,
    Plugboard,
    MasterKey,
    Marker
};

struct MachineDiagnostic {
    MachineField field;
    std::size_t index;
    std::string message;
};

struct MachineValidation {
    bool valid = false;
    std::vector<MachineDiagnostic> diagnostics;
};

MachineValidation validate_machine_config(const MachineConfig& config);

}
