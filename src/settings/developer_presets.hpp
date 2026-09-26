#pragma once

#include <string>
#include <utility>
#include <vector>

#include "machine_config.hpp"

namespace inop {

struct DeveloperSetupPreset {
    std::string name;
    std::string purpose;
    MachineConfig settings;
    bool double_pass;
    bool padding;
    bool moving_reflector;
};

inline const std::vector<DeveloperSetupPreset>& developer_setup_presets() {
    static const std::vector<DeveloperSetupPreset> presets = [] {
        std::vector<DeveloperSetupPreset> result;
        auto add = [&](const std::string& name, const std::string& purpose,
                       int rotor_count, int notch_count, int plug_count,
                       const std::string& reflector, bool double_pass,
                       bool padding, bool moving_reflector) {
            MachineConfig settings;
            settings.suite_code = "38";
            settings.public_builtin_preset = true;
            const std::string alphabet = "abcdefghijklmnopqrstuvwxyz0123456789#/";
            for (int i = 0; i < rotor_count; ++i) {
                settings.rotors.push_back("R" + std::to_string(i + 1));
                settings.rings.push_back(1 + ((i * 7 + 3) % 38));
                settings.notches.push_back(alphabet.substr(
                    static_cast<size_t>(i * notch_count), static_cast<size_t>(notch_count)));
                settings.master_key += alphabet[static_cast<size_t>((i * 11 + 5) % 38)];
            }
            settings.master_key += alphabet[static_cast<size_t>((rotor_count * 11 + 5) % 38)];
            settings.reflector = reflector;
            settings.marker = "abcdefghijklmnop";
            for (int i = 0; i < plug_count; ++i)
                settings.plugs.push_back(alphabet.substr(static_cast<size_t>(i * 2), 2));
            result.push_back({name, purpose, std::move(settings), double_pass,
                              padding, moving_reflector});
        };
        add("Public Fast 5", "Rotor throughput with five wheels and no plug pairs", 5, 1, 0,
            "D", false, false, false);
        add("Public Pipeline 5", "Full pipeline with five wheels and fifteen plug pairs", 5, 5, 15,
            "E", true, true, true);
        add("Public Stress 10", "Ten wheel stepping with thirty distinct notch symbols", 10, 3, 15,
            "F", true, true, true);
        return result;
    }();
    return presets;
}

}
