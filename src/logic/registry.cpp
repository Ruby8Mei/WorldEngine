#include "registry.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace inop {
namespace {

struct Wiring {
    std::string wiring;
    std::string notches;
};

// The wheels below are DEFAULTS: demo and regression material, baked into the
// binary so the program has something to run before anyone generates a real
// batch. They are fixed, public, and shipped in source control — the exact
// opposite of key material. Real traffic must run on wheels generated fresh
// per `generator.hpp` (maintenance menu -> rotor/reflector batch) and loaded
// from inop_wheels.txt, never on these.
const std::map<std::string, Wiring>& rotor_wirings() {
    static const std::map<std::string, Wiring> w = {
        // Legacy — the historic Wehrmacht Enigma wheels, notches and all.
        // Uppercase to match the convention the original wiring tables and
        // traffic were always published in — INOP-38 is the one with the
        // lowercase/numeral-suffix scheme, and it doesn't apply here.
        {"I",    {"EKMFLGDQVZNTOWYHXUSPAIBRCJ", "Q"}},
        {"II",   {"AJDKSIRUXBLHWTMCQGZNPYFVOE", "E"}},
        {"III",  {"BDFHJLCPRTXVZNYEIWGAKMUSQO", "V"}},
        {"IV",   {"ESOVPZJAYQUIRHXLNFTGKDCMWB", "J"}},
        {"V",    {"VZBRGITYUPSDNHLXAWMJQOFECK", "Z"}},
        {"VI",   {"JPGVOUMFYQBENHZRDKASXLICTW", "ZM"}},
        {"VII",  {"NZJHGRCXMYSWBOUFAIVLPEKQDT", "ZM"}},

        // INOP-38 — 38 symbols, notches chosen per message.
        {"R1",   {"bxml2uokh3#46705cyg19etfprid8swqavnzj/", ""}},
        {"R2",   {"1q27#cpzl3rhv6mktjuxfbe5o9n0as4di/ywg8", ""}},
        {"R3",   {"zo5rnuby/k0smtpajwcx23edl8fg9vh4176i#q", ""}},
        {"R4",   {"6v10z/8fhed9s73amrt#kqgjcpluoy5xin2bw4", ""}},
        {"R5",   {"muic7y09e/wz4ohs3t6q82pv#b5xnfd1ajrlgk", ""}},
        {"R6",   {"u85n9qogz6bc4xls70y/vwihf#1mpadk23ejrt", ""}},
        {"R7",   {"5iasenwmjdqk1h38o2#p6t4rzbyx0u9vlcgf7/", ""}},
        {"R8",   {"q6olven1j8fwh3/7cy#kxag4i2stbuzd05mp9r", ""}},
        {"R9",   {"tf/vkhqm1lrpx74wdc0beas89ny52iju6gz#o3", ""}},
        {"R10",  {"boqew5nclhztr748s2f90up6#myvgx/dk1i3aj", ""}},
    };
    return w;
}

// Same warning as rotor_wirings() above: defaults/demo/regression material
// only, never for real traffic.
const std::map<std::string, std::string>& reflector_wirings() {
    static const std::map<std::string, std::string> w = {
        {"A", "EJMZALYXVBWFCRQUONTSPIKHGD"},
        {"B", "YRUHQSLDPXNGOKMIEBFZCWVJAT"},
        {"C", "FVPJIAOYEDRZXWGCTKUQSBNMHL"},

        {"D", "qzn6i4w9ey2v7cuta8/polg#jb10k5f3dmrhxs"},
        {"E", "rcbywiptfu#97x/g1a3hj5end26qzs8v0m4lko"},
        {"F", "v62p9w1#4s3mlyqdo8j7xafun0zgcki/btreh5"},
        {"G", "#yrln3zpuxtd9e4h6c5ki07jbgv21fosqw/ma8"},
        {"H", "sg#l0/b8x4pdt3wk51amvuoi62erznjqy9h7cf"},
    };
    return w;
}

}  // namespace

const std::map<std::string, Suite>& suites() {
    static const std::map<std::string, Suite> s = {
        {"26", Suite{"26", "Legacy", ALPHA26, 3, 3, 10, 2,
                     {"I", "II", "III", "IV", "V", "VI", "VII"},
                     {"A", "B", "C"},
                     true, true, 5}},
        // max_notches is 5 rather than the 1 that maximises the period.
        // Period is not the scarce resource: c notches on n rotors give
        // 38*(38/c)^(n-1), so even 5 notches across 10 rotors leave roughly
        // 3e9 — orders of magnitude past any message that will ever be sent.
        // Rotor movement WITHIN one message is the scarce resource, and it
        // runs the other way: rotor j steps about once every (38/c)^(j-1)
        // characters. Measured over a 1000-character message, going from 3
        // to 5 takes rotor 3 from 7 of its 38 positions to 18, and rotor 4
        // from roughly half a step to two. Rotors 5 and beyond stay still
        // either way — they are a secret static permutation, not a moving
        // part. Spending an unreachable period on the rotors that can still
        // be woken up is the right trade.
        {"38", Suite{"38", "INOP-38", ALPHA38, 5, 10, 15, 5,
                     {"R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10"},
                     {"D", "E", "F", "G", "H"},
                     false, false, 16}},
    };
    return s;
}

const Suite& suite(const std::string& code) {
    auto it = suites().find(code);
    if (it == suites().end()) throw std::invalid_argument("unknown suite code: " + code);
    return it->second;
}

namespace {
// Wheels loaded from disk. Checked before the built-ins, so a generated
// wheel can shadow a factory one by reusing its name.
std::map<std::string, Wiring>& loaded_rotors() {
    static std::map<std::string, Wiring> m;
    return m;
}
std::map<std::string, std::string>& loaded_reflectors() {
    static std::map<std::string, std::string> m;
    return m;
}

// Bumped every time load_wheel_file() actually commits new wheels — lets
// collect() cache its result instead of rebuilding+re-sorting on every
// call, since the GUI's per-frame validation path calls available_rotors()/
// available_reflectors() several times a frame for a pool that only ever
// actually changes on a suite switch or a wheel-file reload.
int& wheel_generation() {
    static int g = 0;
    return g;
}
}  // namespace

Rotor make_rotor(const std::string& name, const Alphabet& alpha) {
    auto lit = loaded_rotors().find(name);
    if (lit != loaded_rotors().end() && static_cast<int>(lit->second.wiring.size()) == alpha.size())
        return Rotor(name, lit->second.wiring, lit->second.notches, alpha);
    auto it = rotor_wirings().find(name);
    if (it == rotor_wirings().end()) throw std::invalid_argument("unknown rotor: " + name);
    return Rotor(name, it->second.wiring, it->second.notches, alpha);
}

Reflector make_reflector(const std::string& name, const Alphabet& alpha) {
    auto lit = loaded_reflectors().find(name);
    if (lit != loaded_reflectors().end() && static_cast<int>(lit->second.size()) == alpha.size())
        return Reflector(name, lit->second, alpha);
    auto it = reflector_wirings().find(name);
    if (it == reflector_wirings().end()) throw std::invalid_argument("unknown reflector: " + name);
    return Reflector(name, it->second, alpha);
}

namespace {

bool is_permutation(const std::string& wiring, const std::string& alphabet) {
    if (wiring.size() != alphabet.size()) return false;
    std::string sw = wiring, sa = alphabet;
    std::sort(sw.begin(), sw.end());
    std::sort(sa.begin(), sa.end());
    return sw == sa;
}

// The suite whose alphabet this wiring's length matches, or null if none
// does — a wiring of a length no known suite uses can't be validated at all.
const Suite* suite_for_length(size_t len) {
    for (const auto& kv : suites())
        if (kv.second.alphabet.size() == len) return &kv.second;
    return nullptr;
}

void note(std::vector<std::string>* out, const std::string& msg) {
    if (out) out->push_back(msg);
}
}  // namespace

// A wiring that is a fixed shift of the alphabet is a Caesar rotor: it adds
// nothing, and several in series still compose to one. It is also exactly
// what a dead random number generator emits, so it is never legitimate.
bool wiring_is_rotation(const std::string& wiring, const std::string& alphabet) {
    const int n = static_cast<int>(wiring.size());
    if (n != static_cast<int>(alphabet.size()) || n < 2) return n < 2;
    Alphabet alpha(alphabet);  // O(1) indexed lookup instead of rebuilding a std::map every call
    int shift = (alpha.index(wiring[0]) - 0 + n) % n;
    for (int i = 1; i < n; ++i)
        if ((alpha.index(wiring[static_cast<size_t>(i)]) - i + n) % n != shift) return false;
    return true;
}

std::string duplicate_notch_symbols(const std::vector<std::string>& notches_per_rotor) {
    int seen[256] = {};
    for (const std::string& rotor : notches_per_rotor)
        for (unsigned char c : rotor) ++seen[c];
    std::string out;
    for (const std::string& rotor : notches_per_rotor)
        for (unsigned char c : rotor)
            if (seen[c] > 1 && out.find(static_cast<char>(c)) == std::string::npos)
                out += static_cast<char>(c);
    return out;
}

namespace {

// The 2.2.x plain-text wheel file. Kept only so an existing one can be
// converted on first run — nothing writes this format any more.
bool parse_wheel_text(std::istream& f, std::map<std::string, Wiring>& rot,
                      std::map<std::string, std::string>& refl,
                      std::vector<std::string>* problems) {
    std::string line;
    std::size_t line_number = 0;
    bool valid = true;
    while (std::getline(f, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string kind, name, wiring, notches;
        if (!(is >> kind >> name >> wiring)) {
            note(problems, "line " + std::to_string(line_number) + ": incomplete wheel entry");
            valid = false;
            continue;
        }
        is >> notches;
        if (kind == "rotor") {
            if (!rot.emplace(name, Wiring{wiring, notches}).second) {
                note(problems, "line " + std::to_string(line_number) + ": duplicate rotor ID");
                valid = false;
            }
        } else if (kind == "reflector") {
            if (!notches.empty()) {
                note(problems, "line " + std::to_string(line_number) +
                                   ": reflector entry has notch data");
                valid = false;
            }
            if (!refl.emplace(name, wiring).second) {
                note(problems, "line " + std::to_string(line_number) + ": duplicate reflector ID");
                valid = false;
            }
        } else {
            note(problems, "line " + std::to_string(line_number) + ": unknown wheel kind");
            valid = false;
        }
    }
    return valid;
}

// The JSON wheel format. Both arrays are optional, which is what lets the
// two default files (rotors in one, reflectors in the other) and a single
// combined file all go through one reader. An entry missing a name or a
// wiring is skipped rather than fatal: the validation below is what
// decides whether what did parse is fit to use.
bool parse_wheel_json(std::istream& f, std::map<std::string, Wiring>& rot,
                      std::map<std::string, std::string>& refl,
                      std::vector<std::string>* problems) {
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        note(problems, "not readable as JSON");
        return false;
    }
    bool valid = true;
    bool has_catalogue = false;
    if (j.contains("rotors")) {
        has_catalogue = true;
        if (!j["rotors"].is_array()) {
            note(problems, "rotors must be an array");
            valid = false;
        } else for (std::size_t index = 0; index < j["rotors"].size(); ++index) {
            const auto& e = j["rotors"][index];
            const std::string where = "rotor entry " + std::to_string(index + 1);
            if (!e.is_object()) {
                note(problems, where + " is not an object");
                valid = false;
                continue;
            }
            if (!e.contains("name") || !e["name"].is_string() ||
                e["name"].get<std::string>().empty()) {
                note(problems, where + " has no valid ID");
                valid = false;
                continue;
            }
            if (!e.contains("wiring") || !e["wiring"].is_string() ||
                e["wiring"].get<std::string>().empty()) {
                note(problems, where + " has no valid wiring");
                valid = false;
                continue;
            }
            std::string notches;
            if (e.contains("notches")) {
                if (!e["notches"].is_string()) {
                    note(problems, where + " has non-string notch data");
                    valid = false;
                    continue;
                }
                notches = e["notches"].get<std::string>();
            }
            if (!rot.emplace(e["name"].get<std::string>(),
                             Wiring{e["wiring"].get<std::string>(), notches}).second) {
                note(problems, where + " duplicates a rotor ID");
                valid = false;
            }
        }
    }
    if (j.contains("reflectors")) {
        has_catalogue = true;
        if (!j["reflectors"].is_array()) {
            note(problems, "reflectors must be an array");
            valid = false;
        } else for (std::size_t index = 0; index < j["reflectors"].size(); ++index) {
            const auto& e = j["reflectors"][index];
            const std::string where = "reflector entry " + std::to_string(index + 1);
            if (!e.is_object()) {
                note(problems, where + " is not an object");
                valid = false;
                continue;
            }
            if (!e.contains("name") || !e["name"].is_string() ||
                e["name"].get<std::string>().empty()) {
                note(problems, where + " has no valid ID");
                valid = false;
                continue;
            }
            if (!e.contains("wiring") || !e["wiring"].is_string() ||
                e["wiring"].get<std::string>().empty()) {
                note(problems, where + " has no valid wiring");
                valid = false;
                continue;
            }
            if (e.contains("notches")) {
                note(problems, where + " has notch data");
                valid = false;
                continue;
            }
            if (!refl.emplace(e["name"].get<std::string>(),
                              e["wiring"].get<std::string>()).second) {
                note(problems, where + " duplicates a reflector ID");
                valid = false;
            }
        }
    }
    if (!has_catalogue) {
        note(problems, "document contains no wheel catalogue");
        valid = false;
    } else if (rot.empty() && refl.empty()) {
        note(problems, "wheel catalogue is empty");
        valid = false;
    }
    return valid;
}

// Validation and installation, shared by every parser above. Unchanged
// from when it was inline in load_wheel_file: it only ever looked at the
// parsed maps, never at the file, which is why the format could change
// without it moving.
bool wheel_maps_valid(const std::map<std::string, Wiring>& rot,
                      const std::map<std::string, std::string>& refl,
                      std::vector<std::string>* problems) {
    bool bad = false;

    std::map<std::string, std::vector<std::string> > by_wiring;
    for (std::map<std::string, Wiring>::const_iterator it = rot.begin(); it != rot.end(); ++it)
        by_wiring[it->second.wiring].push_back(it->first);
    for (std::map<std::string, std::vector<std::string> >::const_iterator it = by_wiring.begin();
         it != by_wiring.end(); ++it) {
        const std::string& wiring = it->first;
        if (it->second.size() > 1) {
            bad = true;
            note(problems, std::to_string(it->second.size()) +
                 " rotors share one wiring (" + it->second.front() + " ... " +
                 it->second.back() + ")");
        }
        const Suite* s = suite_for_length(wiring.size());
        if (!s) {
            bad = true;
            note(problems, "rotor " + it->second.front() + ": wiring length " +
                 std::to_string(wiring.size()) + " matches no known suite alphabet");
        } else if (!is_permutation(wiring, s->alphabet)) {
            bad = true;
            note(problems, "rotor " + it->second.front() +
                 ": wiring is not a permutation of the " + s->name + " alphabet");
        } else if (wiring_is_rotation(wiring, s->alphabet)) {
            bad = true;
            note(problems, "rotor " + it->second.front() +
                 " is a rotation of the alphabet, not a permutation — a shift cipher");
        } else {
            const std::string& notches = rot.find(it->second.front())->second.notches;
            if (static_cast<int>(notches.size()) > s->max_notches) {
                bad = true;
                note(problems, "rotor " + it->second.front() + ": too many notch symbols");
            }
            std::string seen;
            for (char notch : notches) {
                if (s->alphabet.find(notch) == std::string::npos) {
                    bad = true;
                    note(problems, "rotor " + it->second.front() +
                                       ": notch is outside the suite alphabet");
                    break;
                }
                if (seen.find(notch) != std::string::npos) {
                    bad = true;
                    note(problems, "rotor " + it->second.front() +
                                       ": duplicate notch symbol");
                    break;
                }
                seen += notch;
            }
        }
    }

    std::map<std::string, std::vector<std::string> > refl_by;
    for (std::map<std::string, std::string>::const_iterator it = refl.begin(); it != refl.end(); ++it)
        refl_by[it->second].push_back(it->first);
    for (std::map<std::string, std::vector<std::string> >::const_iterator it = refl_by.begin();
         it != refl_by.end(); ++it) {
        const std::string& wiring = it->first;
        if (it->second.size() > 1) {
            bad = true;
            note(problems, std::to_string(it->second.size()) +
                 " reflectors share one wiring (" + it->second.front() + " ... " +
                 it->second.back() + ")");
        }
        const Suite* s = suite_for_length(wiring.size());
        if (!s) {
            bad = true;
            note(problems, "reflector " + it->second.front() + ": wiring length " +
                 std::to_string(wiring.size()) + " matches no known suite alphabet");
        } else if (!is_permutation(wiring, s->alphabet)) {
            bad = true;
            note(problems, "reflector " + it->second.front() +
                 ": wiring is not a permutation of the " + s->name + " alphabet");
        } else {
            Alphabet alpha(s->alphabet);
            for (size_t i = 0; i < wiring.size(); ++i) {
                const int mapped = alpha.index(wiring[i]);
                if (mapped == static_cast<int>(i)) {
                    bad = true;
                    note(problems, "reflector " + it->second.front() + ": wiring has a fixed point");
                    break;
                }
                if (alpha.index(wiring[static_cast<size_t>(mapped)]) != static_cast<int>(i)) {
                    bad = true;
                    note(problems, "reflector " + it->second.front() + ": wiring is not an involution");
                    break;
                }
            }
        }
    }

    if (bad) note(problems, "file rejected — regenerate it, and discard anything enciphered with it");
    return !bad;
}

int install_wheels(std::map<std::string, Wiring>& rot, std::map<std::string, std::string>& refl,
                   std::vector<std::string>* problems) {
    if (!wheel_maps_valid(rot, refl, problems)) return 0;

    for (std::map<std::string, Wiring>::const_iterator it = rot.begin(); it != rot.end(); ++it)
        loaded_rotors()[it->first] = it->second;
    for (std::map<std::string, std::string>::const_iterator it = refl.begin(); it != refl.end(); ++it)
        loaded_reflectors()[it->first] = it->second;
    ++wheel_generation();
    return static_cast<int>(rot.size() + refl.size());
}

// Write one side of the split. Only ever called by the migration below,
// which is why it takes the already-parsed maps rather than a batch.
bool write_wheel_json(const std::string& path, const std::map<std::string, Wiring>& rot,
                      const std::map<std::string, std::string>& refl) {
    nlohmann::json j;
    if (!rot.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& kv : rot) {
            nlohmann::json e;
            e["name"] = kv.first;
            e["wiring"] = kv.second.wiring;
            if (!kv.second.notches.empty()) e["notches"] = kv.second.notches;
            arr.push_back(e);
        }
        j["rotors"] = arr;
    }
    if (!refl.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& kv : refl) {
            nlohmann::json e;
            e["name"] = kv.first;
            e["wiring"] = kv.second;
            arr.push_back(e);
        }
        j["reflectors"] = arr;
    }
    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2) << "\n";
    return static_cast<bool>(f);
}

}  // namespace

const char* const kRotorsPath = "inop_rotors.json";
const char* const kReflectorsPath = "inop_reflectors.json";

int load_wheel_file(const std::string& path, std::vector<std::string>* problems) {
    std::ifstream f(path);
    if (!f) return 0;
    std::map<std::string, Wiring> rot;
    std::map<std::string, std::string> refl;
    if (!parse_wheel_json(f, rot, refl, problems)) return 0;
    return install_wheels(rot, refl, problems);
}

bool validate_wheel_file(const std::string& path, std::vector<std::string>* problems) {
    std::ifstream f(path);
    if (!f) {
        note(problems, "cannot read wheel file");
        return false;
    }
    std::map<std::string, Wiring> rot;
    std::map<std::string, std::string> refl;
    if (!parse_wheel_json(f, rot, refl, problems)) return false;
    return wheel_maps_valid(rot, refl, problems);
}

bool validate_wheel_document(const std::string& json, std::vector<std::string>* problems) {
    std::istringstream input(json);
    std::map<std::string, Wiring> rot;
    std::map<std::string, std::string> refl;
    if (!parse_wheel_json(input, rot, refl, problems)) return false;
    return wheel_maps_valid(rot, refl, problems);
}

int migrate_wheels_from_text(const std::string& txt_path, const std::string& rotors_path,
                             const std::string& reflectors_path,
                             std::vector<std::string>* problems) {
    // Nothing to convert, or the conversion already happened. Deliberately
    // refuses to run if either target exists: overwriting a JSON wheel file
    // with the contents of a stale text one would be destroying current key
    // material with old key material, which is the worst outcome available.
    std::ifstream src(txt_path);
    if (!src) return 0;
    { std::ifstream a(rotors_path), b(reflectors_path); if (a || b) return 0; }

    std::map<std::string, Wiring> rot;
    std::map<std::string, std::string> refl;
    if (!parse_wheel_text(src, rot, refl, problems)) return 0;
    if (rot.empty() && refl.empty()) return 0;
    if (!wheel_maps_valid(rot, refl, problems)) return 0;

    // The originals are never deleted. They are key material, and a
    // conversion that eats the only copy of the wheels a message was
    // enciphered under is not a conversion, it is a loss.
    std::map<std::string, std::string> no_refl;
    std::map<std::string, Wiring> no_rot;
    if (!rot.empty() && !write_wheel_json(rotors_path, rot, no_refl)) {
        note(problems, "cannot write " + rotors_path);
        return 0;
    }
    if (!refl.empty() && !write_wheel_json(reflectors_path, no_rot, refl)) {
        note(problems, "cannot write " + reflectors_path);
        return 0;
    }
    return static_cast<int>(rot.size() + refl.size());
}

namespace {
// Natural-ish ordering for names with a numeric suffix: R2 before R10.
// Names without one (the Roman-numeral Legacy wheels) fall through to
// plain string comparison below, which happens to sort I-VII correctly by
// coincidence, not because Roman numerals are handled specially.
bool name_less(const std::string& a, const std::string& b) {
    size_t ia = a.find_first_of("0123456789");
    size_t ib = b.find_first_of("0123456789");
    if (ia != std::string::npos && ib != std::string::npos &&
        a.substr(0, ia) == b.substr(0, ib)) {
        long na = std::strtol(a.c_str() + ia, nullptr, 10);
        long nb = std::strtol(b.c_str() + ib, nullptr, 10);
        if (na != nb) return na < nb;
    }
    return a < b;
}

struct WheelPoolCache {
    int generation = -1;
    std::vector<std::string> rotors, reflectors;
};

std::vector<std::string> collect(const Suite& s, bool rotors) {
    static std::map<std::string, WheelPoolCache> cache;
    WheelPoolCache& entry = cache[s.code];
    if (entry.generation == wheel_generation()) return rotors ? entry.rotors : entry.reflectors;

    const size_t want = s.alphabet.size();
    std::vector<std::string> rot_out, refl_out;
    for (const auto& kv : rotor_wirings())
        if (kv.second.wiring.size() == want) rot_out.push_back(kv.first);
    for (const auto& kv : loaded_rotors())
        if (kv.second.wiring.size() == want &&
            std::find(rot_out.begin(), rot_out.end(), kv.first) == rot_out.end())
            rot_out.push_back(kv.first);
    std::sort(rot_out.begin(), rot_out.end(), name_less);

    for (const auto& kv : reflector_wirings())
        if (kv.second.size() == want) refl_out.push_back(kv.first);
    for (const auto& kv : loaded_reflectors())
        if (kv.second.size() == want &&
            std::find(refl_out.begin(), refl_out.end(), kv.first) == refl_out.end())
            refl_out.push_back(kv.first);
    std::sort(refl_out.begin(), refl_out.end(), name_less);

    entry.generation = wheel_generation();
    entry.rotors = std::move(rot_out);
    entry.reflectors = std::move(refl_out);
    return rotors ? entry.rotors : entry.reflectors;
}
}  // namespace

std::vector<std::string> available_rotors(const Suite& s)     { return collect(s, true); }
std::vector<std::string> available_reflectors(const Suite& s) { return collect(s, false); }

}  // namespace inop
