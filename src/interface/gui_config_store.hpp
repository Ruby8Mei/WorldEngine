// gui_config_store.hpp — save/load/list PanelState snapshots as JSON under
// machine_settings/.
//
// Deliberately a separate storage structure from the CLI's flat
// inop.settings — this folder holds multiple named configurations, one
// JSON file each, browsable as tiles rather than a single current-session
// file.
#pragma once

#include <string>
#include <vector>

#include "gui_settings_panel.hpp"

namespace inop {
namespace gui {

struct SavedConfigInfo {
    std::string filename;  // e.g. "INOP-7.json" or "Enigma-42.json"
    std::string path;       // machine_settings/<filename>
    // Read from the file's own "suite_code" field (not guessed from the
    // filename — a custom-named save wouldn't follow the INOP-x/Enigma-x
    // convention) — "38", "26", or "" if the file couldn't be read/parsed
    // at all (still listed; clicking it surfaces the usual corruption
    // popup via load_config()).
    std::string suite_code;
};

// One hard-coded, load-only reference configuration — shown in its own
// "Developer settings" section of the Load Settings tile browser,
// never an overwrite/delete target since it isn't a real file.
struct DeveloperPreset {
    std::string name;
    PanelState state;
};

// Currently empty — populate here once specific presets are decided.
const std::vector<DeveloperPreset>& developer_presets();

// Ceiling on the auto-numbered "INOP-x"/"Enigma-x" suggestions, per suite.
// A developer-facing knob, not an operator-facing setting — deliberately
// not exposed anywhere in the UI. Each save is one more JSON file sitting
// in machine_settings/ and one more tile in the Load Settings panel, so
// this keeps suggest_filename() from proposing unbounded numbers.
constexpr int kMaxSavedPerSuite = 360;

// Ensures machine_settings/ exists (creating it if absent) and returns
// every *.json file found there, sorted by filename.
std::vector<SavedConfigInfo> list_configs();

bool config_exists(const std::string& filename);

// The lowest unused "INOP-x" (or "Enigma-x" for Legacy) name, x in
// [1, kMaxSavedPerSuite]. Returns "" if every number in that range is
// already taken for this suite — callers should treat that as "no more
// auto-numbered slots," not silently overwrite or wrap around; the
// operator can still type an arbitrary custom name via the same field.
std::string suggest_filename(const PanelState& state);

// Writes state as JSON to machine_settings/<filename>. `filename` should
// already include ".json". Returns false + *error on I/O failure.
bool save_config(const PanelState& state, const std::string& filename, std::string* error);

// Parses `path`, then re-runs the same validation rules used live
// (derive_validity() + master_key_valid(), against the suite the file
// itself declares). Any parse failure, missing/mis-typed key, or value
// that fails those rules is treated as corrupted: returns false and does
// not modify `out`. This is the sole authority for "is this file
// corrupted" — the Load/Overwrite tile panel shows the corruption popup
// exactly when this returns false.
bool load_config(const std::string& path, PanelState& out, std::string* error);

// Removes the file at `path`. Returns false + *error on failure (e.g. the
// file is already gone, or is locked by another process).
bool delete_config(const std::string& path, std::string* error);

}  // namespace gui
}  // namespace inop
