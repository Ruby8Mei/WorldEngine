#include "gui_config_store.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <system_error>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace inop {
namespace gui {

namespace {

const std::string kDir = "setup";
int g_dir_generation = 0;
int g_dir_cache_gen = -1;
std::vector<SavedConfigInfo> g_dir_cache;

bool ensure_dir(std::string* error = nullptr) {
    std::error_code ec;
    std::filesystem::create_directories(kDir, ec);
    if (!ec) return true;
    if (error) *error = "could not prepare the setup directory";
    return false;
}

std::string lower_extension(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool exact_fields(const nlohmann::json& value, std::initializer_list<const char*> fields,
                  const std::string& context, std::string* error) {
    if (!value.is_object()) {
        if (error) *error = context + " must be an object";
        return false;
    }
    for (const char* field : fields) {
        if (!value.contains(field)) {
            if (error) *error = context + " is missing required field " + field;
            return false;
        }
    }
    for (auto it = value.begin(); it != value.end(); ++it) {
        const bool known = std::any_of(fields.begin(), fields.end(),
                                       [&](const char* field) { return it.key() == field; });
        if (!known) {
            if (error) *error = context + " contains unknown field " + it.key();
            return false;
        }
    }
    return true;
}

nlohmann::json setup_to_json(const PanelState& s) {
    nlohmann::json j;
    j["suite_code"] = s.suite_code;
    j["language_code"] = s.language_code;
    j["rotor_count"] = s.rotor_count;
    nlohmann::json rotors = nlohmann::json::array();
    for (int i = 0; i < s.rotor_count && i < kMaxRotors; ++i) {
        nlohmann::json rotor;
        rotor["name"] = s.rotor_rows[i].rotor_name;
        rotor["ring"] = s.rotor_rows[i].ring_text;
        rotor["notches"] = notch_text(s.rotor_rows[i]);
        rotors.push_back(rotor);
    }
    j["rotors"] = rotors;
    j["reflector"] = s.reflector_name;
    nlohmann::json plugs = nlohmann::json::array();
    for (int i = 0; i < kMaxPlugSlots; ++i) {
        const std::string pair = plug_pair(s, i);
        if (!pair.empty()) plugs.push_back(pair);
    }
    j["plugboard"] = plugs;
    j["double_pass"] = s.double_pass;
    j["padding"] = s.padding;
    j["moving_reflector"] = s.moving_reflector;
    j["master_key"] = s.master_key_text;
    j["marker"] = s.marker_text;
    return j;
}

bool validate_state(const PanelState& candidate, bool allow_missing_marker, std::string* error) {
    if (candidate.rotor_count < 0 || candidate.rotor_count > kMaxRotors) {
        if (error) *error = "rotor_count is outside the supported range";
        return false;
    }
    if (!suites().contains(candidate.suite_code)) {
        if (error) *error = "setup names an unsupported suite";
        return false;
    }
    const Suite& selected_suite = suite(candidate.suite_code);
    for (int i = 0; i < candidate.rotor_count && i < kMaxRotors; ++i) {
        if (!normal_rotor_name_is_eligible(selected_suite, candidate.rotor_rows[i].rotor_name)) {
            if (error) *error = "setup contains a rotor unavailable for normal use";
            return false;
        }
    }
    if (!normal_reflector_name_is_eligible(selected_suite, candidate.reflector_name)) {
        if (error) *error = "setup contains a reflector unavailable for normal use";
        return false;
    }
    const FieldValidity validity = derive_validity(candidate);
    if (!validity.all_mandatory_ok || !master_key_valid(candidate, validity)) {
        if (error) *error = "setup fails semantic validation";
        return false;
    }
    if (!selected_suite.historic_lock &&
        !(allow_missing_marker && candidate.marker_text.empty()) && !validity.marker_ok) {
        if (error) *error = "setup marker must contain exactly 16 suite symbols";
        return false;
    }
    if (selected_suite.historic_lock && !candidate.marker_text.empty()) {
        if (error) *error = "Legacy setup must not contain a marker";
        return false;
    }
    return true;
}

bool parse_setup(const nlohmann::json& j, bool strict, bool marker_required,
                 PanelState& out, std::string* error) {
    if (strict) {
        const bool fields_ok = marker_required
            ? exact_fields(j,
                           {"suite_code", "language_code", "rotor_count", "rotors",
                            "reflector", "plugboard", "double_pass", "padding",
                            "moving_reflector", "master_key", "marker"},
                           "setup data", error)
            : exact_fields(j,
                           {"suite_code", "language_code", "rotor_count", "rotors",
                            "reflector", "plugboard", "double_pass", "padding",
                            "moving_reflector", "master_key"},
                           "setup data", error);
        if (!fields_ok) return false;
    }
    if (!j.is_object()) {
        if (error) *error = "setup data must be an object";
        return false;
    }
    try {
        PanelState candidate;
        candidate.suite_code = j.at("suite_code").get<std::string>();
        candidate.language_code = j.at("language_code").get<std::string>();
        candidate.rotor_count = j.at("rotor_count").get<int>();
        if (candidate.rotor_count < 0 || candidate.rotor_count > kMaxRotors) {
            if (error) *error = "rotor_count is outside the supported range";
            return false;
        }
        const auto& rotors = j.at("rotors");
        if (!rotors.is_array() || static_cast<int>(rotors.size()) != candidate.rotor_count) {
            if (error) *error = "rotors does not match rotor_count";
            return false;
        }
        for (int i = 0; i < candidate.rotor_count; ++i) {
            const auto& rotor = rotors.at(static_cast<size_t>(i));
            if (strict && !exact_fields(rotor, {"name", "ring", "notches"}, "rotor entry", error))
                return false;
            candidate.rotor_rows[i].rotor_name = rotor.at("name").get<std::string>();
            candidate.rotor_rows[i].ring_text = rotor.at("ring").get<std::string>();
            const std::string notches = rotor.at("notches").get<std::string>();
            for (int box = 0; box < kNotchBoxes; ++box)
                candidate.rotor_rows[i].notch_box[box] =
                    box < static_cast<int>(notches.size())
                        ? std::string(1, notches[static_cast<size_t>(box)])
                        : "";
        }
        candidate.reflector_name = j.at("reflector").get<std::string>();
        const auto& plugs = j.at("plugboard");
        if (!plugs.is_array() || plugs.size() > static_cast<size_t>(kMaxPlugSlots)) {
            if (error) *error = "plugboard must be a supported array";
            return false;
        }
        for (size_t i = 0; i < plugs.size(); ++i) {
            const std::string pair = plugs.at(i).get<std::string>();
            candidate.plug_left[i] = pair.size() >= 1 ? std::string(1, pair[0]) : "";
            candidate.plug_right[i] = pair.size() >= 2 ? std::string(1, pair[1]) : "";
        }
        candidate.double_pass = j.at("double_pass").get<bool>();
        candidate.padding = j.at("padding").get<bool>();
        candidate.moving_reflector = j.at("moving_reflector").get<bool>();
        candidate.master_key_text = j.at("master_key").get<std::string>();
        if (j.contains("marker")) candidate.marker_text = j.at("marker").get<std::string>();
        candidate.master_key_prefilled = true;
        if (!validate_state(candidate, !marker_required, error)) return false;
        out = candidate;
        return true;
    } catch (const std::exception&) {
        if (error) *error = "malformed setup data type";
        return false;
    }
}

std::string peek_suite_code(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    try {
        nlohmann::json document;
        file >> document;
        if (lower_extension(path) == kSetupExtension && document.contains("setup") &&
            document["setup"].is_object())
            return document["setup"].value("suite_code", std::string());
        return document.value("suite_code", std::string());
    } catch (...) {
        return "";
    }
}

bool valid_new_filename(const std::string& filename) {
    const std::filesystem::path path(filename);
    return path.filename().string() == filename && !path.stem().string().empty() &&
           lower_extension(filename) == kSetupExtension;
}

bool atomic_write(const std::filesystem::path& target, const std::string& serialized,
                  std::string* error) {
    const std::filesystem::path pending = target.string() + ".pending";
    std::ofstream file(pending, std::ios::binary | std::ios::trunc);
    if (!file) {
        if (error) *error = "could not create the pending setup file";
        return false;
    }
    file << serialized;
    file.flush();
    if (!file) {
        file.close();
        std::error_code ignored;
        std::filesystem::remove(pending, ignored);
        if (error) *error = "setup write did not complete";
        return false;
    }
    file.close();
    std::error_code replace_error;
#if defined(_WIN32)
    if (!MoveFileExW(pending.wstring().c_str(), target.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        replace_error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    std::filesystem::rename(pending, target, replace_error);
#endif
    if (replace_error) {
        std::error_code ignored;
        std::filesystem::remove(pending, ignored);
        if (error) *error = "could not atomically replace the setup file";
        return false;
    }
    return true;
}

}

const std::vector<DeveloperPreset>& developer_presets() {
    static const std::vector<DeveloperPreset> presets = {};
    return presets;
}

std::vector<SavedConfigInfo> list_configs() {
    if (g_dir_cache_gen == g_dir_generation) return g_dir_cache;
    ensure_dir();
    std::vector<SavedConfigInfo> result;
    std::error_code error;
    std::filesystem::directory_iterator iterator(kDir, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const auto& entry = *iterator;
        const std::string extension = lower_extension(entry.path().string());
        if (entry.is_regular_file(error) &&
            (extension == kSetupExtension || extension == ".json")) {
            SavedConfigInfo info;
            info.filename = entry.path().filename().string();
            info.path = entry.path().generic_string();
            info.suite_code = peek_suite_code(info.path);
            info.legacy = extension == ".json";
            result.push_back(info);
        }
        iterator.increment(error);
    }
    std::sort(result.begin(), result.end(),
              [](const SavedConfigInfo& left, const SavedConfigInfo& right) {
                  return left.filename < right.filename;
              });
    g_dir_cache = std::move(result);
    g_dir_cache_gen = g_dir_generation;
    return g_dir_cache;
}

bool config_exists(const std::string& filename) {
    for (const auto& config : list_configs())
        if (config.filename == filename) return true;
    return false;
}

std::string suggest_filename(const PanelState& state) {
    const std::string prefix = state.suite_code == "26" ? "Enigma" : "INOP";
    bool taken[kMaxSavedPerSuite + 1] = {};
    for (const auto& config : list_configs()) {
        if (config.legacy || config.filename.rfind(prefix + "-", 0) != 0) continue;
        const std::string rest = config.filename.substr(prefix.size() + 1);
        const size_t extension = rest.rfind(kSetupExtension);
        if (extension == std::string::npos || extension != rest.size() - 5) continue;
        try {
            const int number = std::stoi(rest.substr(0, extension));
            if (number >= 1 && number <= kMaxSavedPerSuite) taken[number] = true;
        } catch (...) {
        }
    }
    for (int number = 1; number <= kMaxSavedPerSuite; ++number)
        if (!taken[number]) return prefix + "-" + std::to_string(number) + kSetupExtension;
    return "";
}

bool save_config(const PanelState& state, const std::string& filename, std::string* error) {
    if (!valid_new_filename(filename)) {
        if (error) *error = "new setup files must use a plain name ending in .inop";
        return false;
    }
    if (!validate_state(state, false, error)) return false;
    if (!ensure_dir(error)) return false;
    nlohmann::json document;
    document["format"] = kSetupFormatIdentity;
    document["version"] = kSetupFormatVersion;
    document["setup"] = setup_to_json(state);
    if (!atomic_write(std::filesystem::path(kDir) / filename, document.dump(2) + "\n", error))
        return false;
    ++g_dir_generation;
    return true;
}

bool load_config(const std::string& path, PanelState& out, std::string* error,
                 bool* migration_import) {
    if (migration_import) *migration_import = false;
    const std::string extension = lower_extension(path);
    if (extension != kSetupExtension && extension != ".json") {
        if (error) *error = "unsupported setup file extension";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "could not open the setup file";
        return false;
    }
    nlohmann::json document;
    try {
        file >> document;
    } catch (const std::exception&) {
        if (error) *error = "setup file contains malformed JSON";
        return false;
    }
    if (extension == ".json") {
        PanelState candidate;
        if (!parse_setup(document, false, false, candidate, error)) return false;
        out = candidate;
        if (migration_import) *migration_import = true;
        return true;
    }
    if (!document.is_object()) {
        if (error) *error = "preset root must be an object";
        return false;
    }
    if (!document.contains("format") || !document["format"].is_string() ||
        document["format"].get<std::string>() != kSetupFormatIdentity) {
        if (error) *error = "unsupported setup format identity";
        return false;
    }
    if (!document.contains("version") || !document["version"].is_number_integer()) {
        if (error) *error = "setup format version is missing or invalid";
        return false;
    }
    const int version = document["version"].get<int>();
    if (version != 1 && version != kSetupFormatVersion) {
        if (error) *error = "unsupported setup format version " + std::to_string(version);
        return false;
    }
    if (!exact_fields(document, {"format", "version", "setup"}, "preset root", error))
        return false;
    PanelState candidate;
    if (!parse_setup(document["setup"], true, version >= 2, candidate, error)) return false;
    out = candidate;
    if (version == 1 && migration_import) *migration_import = true;
    return true;
}

bool delete_config(const std::string& path, std::string* error) {
    const std::filesystem::path normalized = std::filesystem::path(path).lexically_normal();
    const std::string extension = lower_extension(normalized.string());
    if (normalized.parent_path() != std::filesystem::path(kDir) ||
        (extension != kSetupExtension && extension != ".json")) {
        if (error) *error = "setup file path is outside the setup directory";
        return false;
    }
    std::error_code remove_error;
    if (std::filesystem::remove(normalized, remove_error)) {
        ++g_dir_generation;
        return true;
    }
    if (error) *error = "could not delete the setup file";
    return false;
}

}
}
