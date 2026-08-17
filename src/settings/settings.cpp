#include "settings.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

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
}

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
    if (!saw_rotors && out.master_key.empty()) return false;
    return true;
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
    if (s.rings.size() != count)
        return fail("rings count (" + std::to_string(s.rings.size()) +
                    ") does not match rotor count (" + std::to_string(count) + ")");
    if (!su.notches_are_fixed && s.notches.size() != count)
        return fail("notches count (" + std::to_string(s.notches.size()) +
                    ") does not match rotor count (" + std::to_string(count) + ")");
    const size_t need_key = su.historic_lock ? count : count + 1;
    if (s.master_key.size() != need_key && !(su.historic_lock && s.master_key.size() == need_key + 1))
        return fail("key length (" + std::to_string(s.master_key.size()) +
                    ") does not match rotor count (" + std::to_string(count) + ")");
    return true;
}

bool load_settings(Settings& s, const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    return parse_and_validate(f, s, "no settings found in " + path, " in " + path, error);
}

bool save_settings(const Settings& s, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "suite " << s.suite_code << "\n";
    f << "rotors";     for (auto& r : s.rotors)   f << " " << r; f << "\n";
    f << "reflector "  << s.reflector << "\n";
    f << "rings";      for (int r : s.rings)      f << " " << r; f << "\n";
    f << "notches";    for (auto& n : s.notches)  f << " " << (n.empty() ? "-" : n); f << "\n";
    f << "plugs";      for (auto& p : s.plugs)    f << " " << p; f << "\n";
    f << "key " << s.master_key << "\n";
    return true;
}

Machine build_machine(const Settings& s, std::string* note) {
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

    std::string key = s.master_key;
    if (su.historic_lock) {
        const size_t want = s.rotors.size();
        if (key.size() == want + 1) {
            if (note) *note = std::string("Legacy reflector is fixed — key symbol '") +
                               key.back() + "' ignored";
            key = key.substr(0, want);
        }
        key += alpha.at(0);
    }

    return Machine(alpha, std::move(rotors), make_reflector(s.reflector, alpha),
                   Plugboard(s.plugs, alpha), s.rings, key, su.historic_lock);
}

int count_keysheet_entries(const std::string& path) {
    std::ifstream f(path);
    if (!f) return 0;
    int n = 0;
    std::string line;
    while (std::getline(f, line))
        if (line.compare(0, 12, "# --- entry ") == 0) ++n;
    return n;
}

bool load_keysheet_entry_from_stream(std::istream& in, int index, Settings& out, std::string* error) {
    const std::string marker = "# --- entry " + std::to_string(index) + " ---";
    std::string line;
    bool found = false;
    while (std::getline(in, line)) {
        if (line == marker) { found = true; break; }
    }
    if (!found) { if (error) *error = "no entry " + std::to_string(index); return false; }

    return parse_and_validate(in, out, "entry " + std::to_string(index) + " is incomplete",
                               " (entry " + std::to_string(index) + ")", error);
}

bool load_keysheet_entry(const std::string& path, int index, Settings& out, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::string err;
    if (!load_keysheet_entry_from_stream(f, index, out, &err)) {
        if (error) *error = err + " in " + path;
        return false;
    }
    return true;
}

}

