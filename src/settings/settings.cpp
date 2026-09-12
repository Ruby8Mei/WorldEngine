#include "settings.hpp"

#include <cctype>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "registry.hpp"

namespace inop {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Shared by load_settings()/load_keysheet_entry_from_stream(): parse one
// block from `in`, default an error if parsing found nothing at all,
// validate it, and append `context` to whatever error either step
// produced. Both callers used to hand-roll this exact sequence themselves.
bool parse_and_validate(std::istream& in, Settings& out, const std::string& incomplete_msg,
                         const std::string& context, std::string* error) {
    Settings s;
    if (!parse_settings_block(in, s, error)) {
        if (error && error->empty()) *error = incomplete_msg;
        return false;
    }
    if (!validate_settings(s, error)) {
        if (error) *error += context;
        return false;
    }
    out = s;
    return true;
}
}  // namespace

bool parse_settings_block(std::istream& in, Settings& out, std::string* error) {
    out = Settings();
    std::string line;
    bool saw_rotors = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string key;
        if (!(is >> key)) continue;
        std::string tok;
        if (key == "suite")          is >> out.suite_code;
        else if (key == "reflector") is >> out.reflector;
        else if (key == "key") {
            is >> out.master_key;
            // Fold every alphabet-bound field toward this record's own
            // suite's case (Legacy uppercase, INOP-38 lowercase) now that
            // "suite" is guaranteed already read — both save_settings() and
            // settings_to_text() always write it first, and this "key" line
            // is always last (parsing stops here either way).
            if (suites().count(out.suite_code)) {
                Alphabet fold_alpha(suite(out.suite_code).alphabet);
                out.master_key = fold_alpha.fold_case(out.master_key);
                for (auto& p : out.plugs) p = fold_alpha.fold_case(p);
                for (auto& n : out.notches)
                    if (!n.empty()) n = fold_alpha.fold_case(n);
            } else {
                out.master_key = lower(out.master_key);
            }
            return true;
        }
        else if (key == "rotors")    { while (is >> tok) out.rotors.push_back(upper(tok)); saw_rotors = true; }
        else if (key == "plugs")     while (is >> tok) out.plugs.push_back(tok);
        else if (key == "rings") {
            while (is >> tok) {
                try {
                    out.rings.push_back(std::stoi(tok));
                } catch (const std::exception&) {
                    if (error) *error = "bad 'rings' value '" + tok + "'";
                    return false;
                }
            }
        }
        else if (key == "notches")   while (is >> tok) out.notches.push_back(tok == "-" ? "" : tok);
    }
    if (!saw_rotors && out.master_key.empty()) return false;  // nothing read at all
    return true;  // EOF reached mid-record — validate_settings will catch anything missing
}

bool validate_settings(const Settings& s, std::string* error) {
    auto fail = [&](const std::string& msg) { if (error) *error = msg; return false; };

    const size_t count = s.rotors.size();
    if (count == 0) return fail("no rotors listed");
    if (!suites().count(s.suite_code)) return fail("unknown suite '" + s.suite_code + "'");
    const Suite& su = suite(s.suite_code);
    if (static_cast<int>(count) < su.min_rotors || static_cast<int>(count) > su.max_rotors)
        return fail("rotor count " + std::to_string(count) + " is outside " + su.name +
                    "'s range " + std::to_string(su.min_rotors) + "-" + std::to_string(su.max_rotors));
    const std::vector<std::string> rotor_pool = available_rotors(su);
    std::set<std::string> rotor_names;
    for (const std::string& name : s.rotors) {
        if (std::find(rotor_pool.begin(), rotor_pool.end(), name) == rotor_pool.end())
            return fail("rotor " + name + " is not available for " + su.name);
        if (!rotor_names.insert(name).second)
            return fail("rotor " + name + " is used more than once");
    }
    const std::vector<std::string> reflector_pool = available_reflectors(su);
    if (std::find(reflector_pool.begin(), reflector_pool.end(), s.reflector) == reflector_pool.end())
        return fail("reflector " + s.reflector + " is not available for " + su.name);
    if (s.rings.size() != count)
        return fail("rings count (" + std::to_string(s.rings.size()) +
                    ") does not match rotor count (" + std::to_string(count) + ")");
    Alphabet alpha(su.alphabet);
    for (int ring : s.rings)
        if (ring < 1 || ring > alpha.size())
            return fail("ring value " + std::to_string(ring) + " is outside 1-" +
                        std::to_string(alpha.size()));
    if (!su.notches_are_fixed && s.notches.size() != count)
        return fail("notches count (" + std::to_string(s.notches.size()) +
                    ") does not match rotor count (" + std::to_string(count) + ")");
    if (!su.notches_are_fixed) {
        for (const std::string& notches : s.notches) {
            if (notches.empty() || static_cast<int>(notches.size()) > su.max_notches)
                return fail("each rotor needs 1-" + std::to_string(su.max_notches) + " notch symbols");
            for (char symbol : notches)
                if (!alpha.contains(symbol))
                    return fail("notch symbol is outside the " + su.name + " alphabet");
        }
        const std::string duplicates = duplicate_notch_symbols(s.notches);
        if (!duplicates.empty()) return fail("notch symbols are repeated across rotors");
    }
    if (static_cast<int>(s.plugs.size()) > su.max_plug_pairs)
        return fail("plugboard pair count exceeds " + std::to_string(su.max_plug_pairs));
    try {
        Plugboard probe(s.plugs, alpha);
        (void)probe;
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
    const size_t need_key = su.historic_lock ? count : count + 1;
    if (s.master_key.size() != need_key && !(su.historic_lock && s.master_key.size() == need_key + 1))
        return fail("key length (" + std::to_string(s.master_key.size()) +
                    ") does not match rotor count (" + std::to_string(count) + ")");
    for (char symbol : s.master_key)
        if (!alpha.contains(symbol)) return fail("master key symbol is outside the " + su.name + " alphabet");
    return true;
}

namespace {

// The schema is the one the GUI already writes into setup/*.json, so a
// configuration saved by either interface opens in the other. The fields
// below are the ones a Settings has; the pipeline options the GUI also
// stores (double_pass, padding, moving_reflector, language_code) have no
// home in this struct and are deliberately not invented here — see
// save_settings for how they survive a round trip anyway.
bool settings_from_json(const nlohmann::json& j, Settings& out, std::string* error) {
    if (!j.is_object()) {
        if (error) *error = "not a JSON object";
        return false;
    }
    auto fail = [&](const std::string& msg) { if (error) *error = msg; return false; };
    if (!j.contains("suite_code") || !j["suite_code"].is_string())
        return fail("suite_code must be a string");
    if (!j.contains("reflector") || !j["reflector"].is_string())
        return fail("reflector must be a string");
    if (!j.contains("master_key") || !j["master_key"].is_string())
        return fail("master_key must be a string");
    if (!j.contains("rotors") || !j["rotors"].is_array())
        return fail("rotors must be an array");

    Settings parsed;
    parsed.suite_code = j["suite_code"].get<std::string>();
    parsed.reflector = j["reflector"].get<std::string>();
    parsed.master_key = j["master_key"].get<std::string>();

    for (const auto& r : j["rotors"]) {
            if (!r.is_object()) return fail("each rotor must be an object");
            if (!r.contains("name") || !r["name"].is_string())
                return fail("rotor name must be a string");
            if (!r.contains("ring")) return fail("rotor ring is missing");
            if (r.contains("notches") && !r["notches"].is_string())
                return fail("rotor notches must be a string");
            parsed.rotors.push_back(upper(r["name"].get<std::string>()));
            // The GUI writes ring as a string because the control behind it
            // is a text field; anything thinking in integers writes a
            // number. Both are accepted rather than making one interface
            // wrong.
            int ring = 0;
            if (r["ring"].is_string()) {
                const std::string t = r["ring"].get<std::string>();
                size_t consumed = 0;
                try {
                    ring = std::stoi(t, &consumed);
                } catch (const std::exception&) {
                    return fail("rotor ring must be an integer");
                }
                if (consumed != t.size()) return fail("rotor ring must be an integer");
            } else if (r["ring"].is_number_integer()) {
                try {
                    ring = r["ring"].get<int>();
                } catch (const std::exception&) {
                    return fail("rotor ring is outside the integer range");
                }
            } else {
                return fail("rotor ring must be an integer or integer string");
            }
            parsed.rings.push_back(ring);
            parsed.notches.push_back(r.contains("notches") ? r["notches"].get<std::string>() : std::string());
    }
    if (j.contains("plugboard")) {
        if (!j["plugboard"].is_array()) return fail("plugboard must be an array");
        for (const auto& p : j["plugboard"]) {
            if (!p.is_string()) return fail("each plugboard pair must be a string");
            parsed.plugs.push_back(p.get<std::string>());
        }
    }
    out = std::move(parsed);
    return true;
}

nlohmann::json settings_to_json(const Settings& s) {
    nlohmann::json j;
    j["suite_code"] = s.suite_code;
    j["reflector"] = s.reflector;
    j["master_key"] = s.master_key;
    j["rotor_count"] = static_cast<int>(s.rotors.size());

    nlohmann::json rotors = nlohmann::json::array();
    for (size_t i = 0; i < s.rotors.size(); ++i) {
        nlohmann::json r;
        r["name"] = s.rotors[i];
        r["ring"] = std::to_string(i < s.rings.size() ? s.rings[i] : 1);
        r["notches"] = i < s.notches.size() ? s.notches[i] : std::string();
        rotors.push_back(r);
    }
    j["rotors"] = rotors;

    nlohmann::json plugs = nlohmann::json::array();
    for (const std::string& p : s.plugs) plugs.push_back(p);
    j["plugboard"] = plugs;
    return j;
}

}  // namespace

bool load_settings(Settings& s, const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (error) *error = path + " is not readable as JSON";
        return false;
    }
    Settings out;
    if (!settings_from_json(j, out, error)) return false;
    std::string err;
    if (!validate_settings(out, &err)) {
        if (error) *error = err + " in " + path;
        return false;
    }
    s = out;
    return true;
}

bool save_settings(const Settings& s, const std::string& path) {
    // Merge into whatever is already there rather than replacing it. A file
    // written by the GUI carries pipeline options this struct has no field
    // for, and rewriting from scratch would silently drop them — which is
    // exactly the "two sets of settings" problem the JSON move was meant to
    // end.
    nlohmann::json doc = nlohmann::json::object();
    {
        std::ifstream in(path);
        if (in) {
            nlohmann::json existing = nlohmann::json::parse(in, nullptr, false);
            if (!existing.is_discarded() && existing.is_object()) doc = existing;
        }
    }
    nlohmann::json mine = settings_to_json(s);
    for (auto it = mine.begin(); it != mine.end(); ++it) doc[it.key()] = it.value();

    std::ofstream f(path);
    if (!f) return false;
    f << doc.dump(2) << "\n";
    return static_cast<bool>(f);
}

bool migrate_settings_from_text(const std::string& txt_path, const std::string& json_path) {
    // Same rule as the wheel conversion: never overwrite an existing JSON
    // file with the contents of a stale text one, and never delete the
    // original.
    std::ifstream src(txt_path);
    if (!src) return false;
    { std::ifstream probe(json_path); if (probe) return false; }

    Settings s;
    if (!parse_and_validate(src, s, "no settings found in " + txt_path, " in " + txt_path, nullptr))
        return false;
    return save_settings(s, json_path);
}

Machine build_machine(const Settings& s, std::string* note) {
    std::string error;
    if (!validate_settings(s, &error)) throw std::invalid_argument(error);
    const Suite& su = suite(s.suite_code);
    Alphabet alpha(su.alphabet);

    std::vector<Rotor> rotors;
    rotors.reserve(s.rotors.size());
    for (size_t i = 0; i < s.rotors.size(); ++i) {
        Rotor r = make_rotor(s.rotors[i], alpha);
        if (!su.notches_are_fixed) {
            std::string n = i < s.notches.size() ? s.notches[i] : std::string();
            r.set_notches(n, alpha);
        }
        rotors.push_back(std::move(r));
    }

    // The Machine core always wants (rotor count + 1) key symbols — one
    // window letter per rotor, plus a reflector orientation letter. A
    // historic-lock suite's reflector is fixed at position 0 and its key
    // sheet carries no orientation symbol, so that symbol is synthesised
    // here rather than by relaxing Machine's own contract.
    std::string key = s.master_key;
    if (su.historic_lock) {
        const size_t want = s.rotors.size();
        if (key.size() == want + 1) {
            if (note) *note = std::string("Legacy reflector is fixed — key symbol '") +
                               key.back() + "' ignored";
            key = key.substr(0, want);
        }
        key += alpha.at(0);  // reflector fixed at position 0
    }

    return Machine(alpha, std::move(rotors), make_reflector(s.reflector, alpha),
                   Plugboard(s.plugs, alpha), s.rings, key, su.historic_lock);
}

namespace {

// One parse of the whole document, shared by the two functions below. A key
// sheet is an object with an "entries" array, each entry the same shape as
// a settings file, so one reader understands both.
bool read_keysheet_json(const std::string& path, nlohmann::json& out) {
    std::ifstream f(path);
    if (!f) return false;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    if (!j.contains("entries") || !j["entries"].is_array()) return false;
    out = j;
    return true;
}

}  // namespace

int count_keysheet_entries(const std::string& path) {
    nlohmann::json j;
    if (!read_keysheet_json(path, j)) return 0;
    return static_cast<int>(j["entries"].size());
}

bool load_keysheet(const std::string& path, std::vector<KeySheetEntry>& entries,
                   std::string* error) {
    nlohmann::json j;
    if (!read_keysheet_json(path, j)) {
        if (error) *error = path + " is not a readable key sheet";
        return false;
    }
    std::vector<KeySheetEntry> parsed;
    const nlohmann::json& arr = j["entries"];
    parsed.reserve(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
        KeySheetEntry entry;
        std::string entry_error;
        if (settings_from_json(arr[i], entry.settings, &entry_error) &&
            validate_settings(entry.settings, &entry_error)) {
            entry.valid = true;
        } else {
            entry.error = entry_error + " (entry " + std::to_string(i + 1) + " in " + path + ")";
        }
        parsed.push_back(std::move(entry));
    }
    entries = std::move(parsed);
    return true;
}

bool load_keysheet_entry(const std::string& path, int index, Settings& out, std::string* error) {
    std::vector<KeySheetEntry> entries;
    if (!load_keysheet(path, entries, error)) return false;
    if (index < 1 || index > static_cast<int>(entries.size())) {
        if (error) *error = "no entry " + std::to_string(index) + " in " + path;
        return false;
    }
    const KeySheetEntry& entry = entries[static_cast<size_t>(index - 1)];
    if (!entry.valid) {
        if (error) *error = entry.error;
        return false;
    }
    out = entry.settings;
    return true;
}

}  // namespace inop
