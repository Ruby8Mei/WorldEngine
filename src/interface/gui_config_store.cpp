#include "gui_config_store.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

namespace inop {
namespace gui {

namespace {

const std::string kDir = "machine_settings";

void ensure_dir() {
#if defined(_WIN32)
    CreateDirectoryA(kDir.c_str(), nullptr);  // no-op if it already exists
#endif
}

// Bumped by every successful save_config()/delete_config() — lets
// list_configs() skip the FindFirstFileA/FindNextFileA walk + re-sort on
// every call. The Load/Overwrite tile panel calls this once per rendered
// frame for as long as it's open just to browse/scroll it, so without this
// the same directory gets re-read 60x/sec for a listing that only actually
// changes on an explicit Save/Create/Delete.
int g_dir_generation = 0;
int g_dir_cache_gen = -1;
std::vector<SavedConfigInfo> g_dir_cache;

nlohmann::json to_json(const PanelState& s) {
    nlohmann::json j;
    j["suite_code"] = s.suite_code;
    j["language_code"] = s.language_code;
    j["rotor_count"] = s.rotor_count;

    nlohmann::json rotors = nlohmann::json::array();
    for (int i = 0; i < s.rotor_count && i < kMaxRotors; ++i) {
        nlohmann::json r;
        r["name"] = s.rotor_rows[i].rotor_name;
        r["ring"] = s.rotor_rows[i].ring_text;
        r["notches"] = notch_text(s.rotor_rows[i]);
        rotors.push_back(r);
    }
    j["rotors"] = rotors;
    j["reflector"] = s.reflector_name;

    nlohmann::json plugs = nlohmann::json::array();
    for (int i = 0; i < kMaxPlugSlots; ++i) {
        std::string p = plug_pair(s, i);
        if (!p.empty()) plugs.push_back(p);
    }
    j["plugboard"] = plugs;

    j["double_pass"] = s.double_pass;
    j["padding"] = s.padding;
    j["moving_reflector"] = s.moving_reflector;
    j["master_key"] = s.master_key_text;
    return j;
}

// Best-effort peek at just the suite_code field, for categorizing a tile
// without running full load_config() validation — a file that's otherwise
// corrupted should still show up somewhere; clicking it still surfaces the
// usual corruption popup.
std::string peek_suite_code(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    try {
        nlohmann::json j;
        f >> j;
        return j.value("suite_code", std::string());
    } catch (...) {
        return "";
    }
}

}  // namespace

const std::vector<DeveloperPreset>& developer_presets() {
    static const std::vector<DeveloperPreset> v = {};
    return v;
}

std::vector<SavedConfigInfo> list_configs() {
    if (g_dir_cache_gen == g_dir_generation) return g_dir_cache;

    ensure_dir();
    std::vector<SavedConfigInfo> out;
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    std::string pattern = kDir + "\\*.json";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                SavedConfigInfo info;
                info.filename = fd.cFileName;
                info.path = kDir + "\\" + fd.cFileName;
                info.suite_code = peek_suite_code(info.path);
                out.push_back(info);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#endif
    std::sort(out.begin(), out.end(),
              [](const SavedConfigInfo& a, const SavedConfigInfo& b) { return a.filename < b.filename; });
    g_dir_cache = std::move(out);
    g_dir_cache_gen = g_dir_generation;
    return g_dir_cache;
}

bool config_exists(const std::string& filename) {
    for (const auto& c : list_configs())
        if (c.filename == filename) return true;
    return false;
}

std::string suggest_filename(const PanelState& state) {
    std::string prefix = state.suite_code == "26" ? "Enigma" : "INOP";

    bool taken[kMaxSavedPerSuite + 1] = {};
    for (const auto& c : list_configs()) {
        if (c.filename.rfind(prefix + "-", 0) != 0) continue;
        std::string rest = c.filename.substr(prefix.size() + 1);
        size_t dot = rest.rfind(".json");
        if (dot == std::string::npos || dot != rest.size() - 5) continue;
        try {
            int n = std::stoi(rest.substr(0, dot));
            if (n >= 1 && n <= kMaxSavedPerSuite) taken[n] = true;
        } catch (...) {
        }
    }

    for (int n = 1; n <= kMaxSavedPerSuite; ++n)
        if (!taken[n]) return prefix + "-" + std::to_string(n) + ".json";
    return "";  // every INOP-x/Enigma-x slot in [1, kMaxSavedPerSuite] is taken
}

bool save_config(const PanelState& state, const std::string& filename, std::string* error) {
    ensure_dir();
    std::ofstream f(kDir + "\\" + filename);
    if (!f) {
        if (error) *error = "could not open '" + filename + "' for writing";
        return false;
    }
    f << to_json(state).dump(2);
    if (!f) {
        if (error) *error = "write failed for '" + filename + "'";
        return false;
    }
    ++g_dir_generation;
    return true;
}

bool load_config(const std::string& path, PanelState& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "could not open file";
        return false;
    }
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        if (error) *error = std::string("JSON parse error: ") + e.what();
        return false;
    }

    try {
        PanelState cand;
        cand.suite_code = j.at("suite_code").get<std::string>();
        cand.language_code = j.at("language_code").get<std::string>();
        cand.rotor_count = j.at("rotor_count").get<int>();
        if (cand.rotor_count < 0 || cand.rotor_count > kMaxRotors) {
            if (error) *error = "rotor_count out of range";
            return false;
        }

        const auto& rotors = j.at("rotors");
        if (!rotors.is_array() || static_cast<int>(rotors.size()) != cand.rotor_count) {
            if (error) *error = "rotors array does not match rotor_count";
            return false;
        }
        for (int i = 0; i < cand.rotor_count; ++i) {
            const auto& rj = rotors.at(static_cast<size_t>(i));
            cand.rotor_rows[i].rotor_name = rj.at("name").get<std::string>();
            cand.rotor_rows[i].ring_text = rj.at("ring").get<std::string>();
            std::string notches = rj.at("notches").get<std::string>();
            for (int b = 0; b < kNotchBoxes; ++b)
                cand.rotor_rows[i].notch_box[b] =
                    b < static_cast<int>(notches.size()) ? std::string(1, notches[static_cast<size_t>(b)]) : "";
        }

        cand.reflector_name = j.at("reflector").get<std::string>();

        const auto& plugs = j.at("plugboard");
        if (!plugs.is_array() || plugs.size() > static_cast<size_t>(kMaxPlugSlots)) {
            if (error) *error = "plugboard array invalid";
            return false;
        }
        for (size_t i = 0; i < plugs.size(); ++i) {
            std::string p = plugs.at(i).get<std::string>();
            cand.plug_left[i] = p.size() >= 1 ? std::string(1, p[0]) : "";
            cand.plug_right[i] = p.size() >= 2 ? std::string(1, p[1]) : "";
        }

        cand.double_pass = j.at("double_pass").get<bool>();
        cand.padding = j.at("padding").get<bool>();
        cand.moving_reflector = j.at("moving_reflector").get<bool>();
        cand.master_key_text = j.at("master_key").get<std::string>();
        cand.master_key_prefilled = true;

        FieldValidity v = derive_validity(cand);
        if (!v.all_mandatory_ok || !master_key_valid(cand, v)) {
            if (error) *error = "configuration fails validation";
            return false;
        }
        out = cand;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("malformed settings file: ") + e.what();
        return false;
    }
}

bool delete_config(const std::string& path, std::string* error) {
#if defined(_WIN32)
    if (DeleteFileA(path.c_str())) {
        ++g_dir_generation;
        return true;
    }
    if (error) *error = "could not delete '" + path + "'";
    return false;
#else
    if (error) *error = "delete not supported on this platform";
    return false;
#endif
}

}  // namespace gui
}  // namespace inop
