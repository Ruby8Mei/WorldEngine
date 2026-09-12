#include "generator.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "registry.hpp"
#include "rng.hpp"
#include "settings.hpp"

namespace inop {
namespace {

// Fisher-Yates driven by the OS entropy source, not rand().
template <typename T>
void secure_shuffle(std::vector<T>& v) {
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = secure_below(static_cast<uint32_t>(i));
        std::swap(v[i - 1], v[j]);
    }
}

// "Copy the alphabet into a vector and shuffle it" was written out
// separately at every call site below — one shared helper instead.
std::vector<char> shuffled_alphabet(const std::string& alphabet) {
    std::vector<char> v(alphabet.begin(), alphabet.end());
    secure_shuffle(v);
    return v;
}

std::string ask(const std::string& prompt, const std::string& def) {
    std::cout << "  " << prompt;
    if (!def.empty()) std::cout << " [" << def << "]";
    std::cout << ": ";
    std::string line;
    if (!std::getline(std::cin, line)) { std::cout << "\n"; std::exit(0); }
    size_t a = line.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return def;
    size_t b = line.find_last_not_of(" \t\r\n");
    std::string trimmed = line.substr(a, b - a + 1);

    // Leading ':' disambiguates from a legitimate bare "q" answer elsewhere.
    std::string low = trimmed;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (low == ":q" || low == ":quit" || low == ":exit") {
        std::cout << "  closed.\n";
        std::exit(0);
    }
    return trimmed;
}

int ask_int(const std::string& prompt, int def, int lo, int hi) {
    while (true) {
        std::string s = ask(prompt, std::to_string(def));
        try {
            int v = std::stoi(s);
            if (v >= lo && v <= hi) return v;
        } catch (...) {}
        std::cout << "    ! give a number between " << lo << " and " << hi << "\n";
    }
}

// allow_legacy=false is for the rotor/reflector wheel generator specifically
// — Legacy's wheels are the fixed historical set (I-VII / A-B-C), never
// machine-generated, so it must never even be offered there. Keysheet
// generation (gen_settings()) still allows Legacy: randomizing rings/
// plugboard/key against its existing fixed wheels is legitimate.
const Suite& ask_suite(bool allow_legacy = true) {
    while (true) {
        std::string prompt = allow_legacy ? "suite (26 = Legacy, 38 = INOP-38)" : "suite (38 = INOP-38)";
        std::string c = ask(prompt, "38");
        if (!allow_legacy && c == "26") {
            std::cout << "    ! Legacy's wheels are fixed and historical, never machine-generated\n";
            continue;
        }
        if (suites().count(c)) return suite(c);
        std::cout << "    ! unknown suite code\n";
    }
}

}  // namespace

// ── generation primitives ───────────────────────────────────────────────
// A wiring that is a pure rotation of the alphabet is a shift cipher, which
// is what a dead RNG produces. Reject it rather than ship it. Shares the
// rotation check with registry.cpp's load-time validator so the two can
// never diverge again.
std::string random_rotor_wiring(const Alphabet& alpha) {
    std::vector<char> v = shuffled_alphabet(alpha.str());
    std::string w(v.begin(), v.end());
    if (wiring_is_rotation(w, alpha.str()))
        throw std::runtime_error(
            "generated a rotation, not a permutation — the entropy source is broken");
    return w;
}

std::string random_reflector_wiring(const Alphabet& alpha) {
    const int n = alpha.size();
    if (n % 2 != 0)
        throw std::invalid_argument("reflectors need an even alphabet — every symbol must pair");

    std::vector<char> pool = shuffled_alphabet(alpha.str());

    std::string wiring(static_cast<size_t>(n), '?');
    for (size_t i = 0; i + 1 < pool.size(); i += 2) {
        char a = pool[i], b = pool[i + 1];
        wiring[static_cast<size_t>(alpha.index(a))] = b;
        wiring[static_cast<size_t>(alpha.index(b))] = a;
    }
    return wiring;
}

std::string random_notches(const Alphabet& alpha, int count) {
    if (count < 1) count = 1;  // a notchless rotor gives the machine a period of 38
    if (count > alpha.size()) count = alpha.size();
    std::vector<char> v = shuffled_alphabet(alpha.str());
    return std::string(v.begin(), v.begin() + count);
}

// Per-rotor notch counts drawn independently in [1, max_notches_per_rotor]
// each — not one shared count applied to every rotor — while still pulling
// every symbol from one shuffled pool so no two rotors can ever land on
// the same notch symbol. Used by callers that want each rotor to look like
// an independent pick (the GUI's single-click "Generate Setup") rather
// than random_settings()'s one-fixed-count-for-everyone contract (what the
// CLI's interactive prompts ask for).
std::vector<std::string> random_variable_notches(const Alphabet& alpha, int rotor_count,
                                                   int max_notches_per_rotor) {
    int cap = max_notches_per_rotor < 1 ? 1 : max_notches_per_rotor;
    std::vector<char> pool = shuffled_alphabet(alpha.str());
    std::vector<std::string> result;
    size_t used = 0;
    for (int i = 0; i < rotor_count; ++i) {
        int rotors_left = rotor_count - i;
        size_t remaining = pool.size() - used;
        // Reserve at least 1 symbol for every rotor still to come after
        // this one, so an early greedy draw can never starve a later rotor
        // of its mandatory minimum.
        size_t max_for_this = remaining - static_cast<size_t>(rotors_left - 1);
        int this_cap = std::min(cap, static_cast<int>(max_for_this));
        if (this_cap < 1)
            throw std::runtime_error("not enough alphabet symbols for every rotor to have distinct notches");
        int count = 1 + static_cast<int>(secure_below(static_cast<uint32_t>(this_cap)));
        result.push_back(std::string(pool.begin() + static_cast<long>(used),
                                      pool.begin() + static_cast<long>(used + static_cast<size_t>(count))));
        used += static_cast<size_t>(count);
    }
    return result;
}

// ── settings generation ─────────────────────────────────────────────────
GeneratedSettings random_settings(const Suite& s, int rotor_count, int plug_pairs,
                                   int notches_per_rotor) {
    if (rotor_count < s.min_rotors || rotor_count > s.max_rotors)
        throw std::invalid_argument("rotor count " + std::to_string(rotor_count) +
                                    " outside " + s.name + "'s range " +
                                    std::to_string(s.min_rotors) + "-" +
                                    std::to_string(s.max_rotors));
    Alphabet alpha(s.alphabet);
    GeneratedSettings g;
    g.suite_code = s.code;

    // rotors: distinct, in a random order
    std::vector<std::string> pool = available_rotors(s);
    if (static_cast<int>(pool.size()) < rotor_count)
        throw std::runtime_error("not enough rotors available for this suite — generate a batch first");
    secure_shuffle(pool);
    g.rotors.assign(pool.begin(), pool.begin() + rotor_count);

    // reflector
    std::vector<std::string> refl = available_reflectors(s);
    if (refl.empty()) throw std::runtime_error("no reflectors available for this suite");
    g.reflector = refl[secure_below(static_cast<uint32_t>(refl.size()))];

    // rings
    for (int i = 0; i < rotor_count; ++i)
        g.rings.push_back(static_cast<int>(secure_below(static_cast<uint32_t>(alpha.size()))) + 1);

    // notches — legacy wheels carry historic ones, so leave those alone.
    // Drawn from one shuffled pool for the whole machine, not one shuffle
    // per rotor, so no two rotors can ever land on the same notch symbol —
    // a notch shared across rotors measurably shrinks keyspace.
    if (s.notches_are_fixed) {
        for (int i = 0; i < rotor_count; ++i) g.notches.push_back(std::string());
    } else {
        int npr = notches_per_rotor < 1 ? 1 : notches_per_rotor;
        // Notch symbols are distinct across the whole machine, not merely
        // within one rotor, so the alphabet is a hard ceiling on
        // rotor_count * npr — 10 rotors cannot carry 5 notches each out of
        // 38 symbols, only 3. Clamp to what the alphabet can actually
        // supply instead of throwing: a caller asking for the most movement
        // available wants the most this rotor count allows, not a refusal.
        // The floor of 1 notch per rotor is the one thing that genuinely
        // cannot be met by shrinking, so that stays a throw.
        const int affordable = static_cast<int>(alpha.str().size()) / rotor_count;
        if (affordable < 1)
            throw std::runtime_error("not enough alphabet symbols for every rotor to have distinct notches");
        if (npr > affordable) npr = affordable;
        std::vector<char> pool = shuffled_alphabet(alpha.str());
        size_t used = 0;
        for (int i = 0; i < rotor_count; ++i) {
            g.notches.push_back(std::string(pool.begin() + static_cast<long>(used),
                                             pool.begin() + static_cast<long>(used + static_cast<size_t>(npr))));
            used += static_cast<size_t>(npr);
        }
    }

    // plugboard: draw distinct symbols, pair them off
    if (plug_pairs > 0) {
        std::vector<char> v = shuffled_alphabet(s.alphabet);
        int usable = std::min(plug_pairs, alpha.size() / 2);
        for (int i = 0; i < usable; ++i)
            g.plugs.push_back(std::string() + v[static_cast<size_t>(i * 2)] +
                              v[static_cast<size_t>(i * 2 + 1)]);
    }

    g.master_key = secure_string(s.alphabet, static_cast<size_t>(rotor_count) + 1);
    return g;
}

GeneratedSettings random_setup_settings(const Suite& s) {
    entropy_self_check();
    const int rotor_count = s.min_rotors + static_cast<int>(secure_below(
                                static_cast<uint32_t>(s.max_rotors - s.min_rotors + 1)));
    const int plug_pairs = static_cast<int>(secure_below(static_cast<uint32_t>(s.max_plug_pairs + 1)));
    GeneratedSettings g = random_settings(s, rotor_count, plug_pairs, 1);
    if (!s.notches_are_fixed)
        g.notches = random_variable_notches(Alphabet(s.alphabet), rotor_count, s.max_notches);
    g.master_key = secure_string(s.alphabet,
                                 static_cast<size_t>(s.historic_lock ? rotor_count : rotor_count + 1));
    return g;
}

std::string settings_to_text(const GeneratedSettings& g) {
    std::ostringstream o;
    o << "suite " << g.suite_code << "\n";
    o << "rotors";    for (const auto& r : g.rotors)  o << " " << r; o << "\n";
    o << "reflector " << g.reflector << "\n";
    o << "rings";     for (int r : g.rings)           o << " " << r; o << "\n";
    o << "notches";   for (const auto& n : g.notches) o << " " << (n.empty() ? "-" : n); o << "\n";
    o << "plugs";     for (const auto& p : g.plugs)   o << " " << p; o << "\n";
    o << "key " << g.master_key << "\n";
    return o.str();
}

// ── menu actions ────────────────────────────────────────────────────────
std::string wheel_batch_problem(const WheelBatch& b, const Suite& s) {
    if (b.wheels.empty()) return "wheel batch is empty";
    if (b.wheels.size() != b.wirings.size())
        return "wheel batch wiring index is inconsistent";

    std::set<std::string> names;
    for (size_t i = 0; i < b.wheels.size(); ++i) {
        const GeneratedWheel& wheel = b.wheels[i];
        if (wheel.name.empty()) return "wheel " + std::to_string(i + 1) + " has no ID";
        if (!names.insert(wheel.name).second)
            return "wheel batch contains duplicate ID " + wheel.name;
        if (wheel.wiring != b.wirings[i])
            return "wheel batch wiring index disagrees with wheel " + wheel.name;
        if (wheel.wiring.size() != s.alphabet.size())
            return "wheel " + wheel.name + " has the wrong wiring length";
        std::string sorted_wiring = wheel.wiring;
        std::string sorted_alphabet = s.alphabet;
        std::sort(sorted_wiring.begin(), sorted_wiring.end());
        std::sort(sorted_alphabet.begin(), sorted_alphabet.end());
        if (sorted_wiring != sorted_alphabet)
            return "wheel " + wheel.name + " wiring is not a suite permutation";

        if (b.rotors) {
            if (static_cast<int>(wheel.notches.size()) > s.max_notches)
                return "rotor " + wheel.name + " has too many notch symbols";
            std::set<char> notches;
            for (char notch : wheel.notches) {
                if (s.alphabet.find(notch) == std::string::npos)
                    return "rotor " + wheel.name + " has a notch outside the suite alphabet";
                if (!notches.insert(notch).second)
                    return "rotor " + wheel.name + " has a duplicate notch symbol";
            }
        } else {
            if (!wheel.notches.empty()) return "reflector " + wheel.name + " has notch data";
            Alphabet alpha(s.alphabet);
            for (size_t position = 0; position < wheel.wiring.size(); ++position) {
                const int mapped = alpha.index(wheel.wiring[position]);
                if (mapped == static_cast<int>(position))
                    return "reflector " + wheel.name + " has a fixed point";
                if (alpha.index(wheel.wiring[static_cast<size_t>(mapped)]) !=
                    static_cast<int>(position))
                    return "reflector " + wheel.name + " is not an involution";
            }
        }
    }

    std::set<std::string> distinct(b.wirings.begin(), b.wirings.end());
    if (b.wirings.size() > 1 && distinct.size() < b.wirings.size())
        return "only " + std::to_string(distinct.size()) + " distinct wirings out of " +
               std::to_string(b.wirings.size()) + " - the entropy source is broken";
    for (size_t i = 0; i < b.wirings.size(); ++i) {
        if (wiring_is_rotation(b.wirings[i], s.alphabet)) {
            return "wiring " + std::to_string(i + 1) +
                   " came out a pure rotation of the alphabet, which is a Caesar wheel and "
                   "never legitimate - the entropy source is suspect";
        }
    }
    return "";
}

WheelBatch build_wheel_batch(const Suite& s, bool rotors, int count,
                             const std::string& prefix, int start, int notch_n) {
    // Before generation, not after. A batch drawn from a dead source looks
    // exactly like a good one and would be discovered only by whoever
    // tried to use it.
    entropy_self_check();

    Alphabet alpha(s.alphabet);
    WheelBatch b;
    b.rotors = rotors;
    b.wheels.reserve(static_cast<size_t>(count));
    b.wirings.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        GeneratedWheel g;
        g.name = prefix + std::to_string(start + i);
        g.wiring = rotors ? random_rotor_wiring(alpha) : random_reflector_wiring(alpha);
        if (rotors && notch_n > 0) g.notches = random_notches(alpha, notch_n);
        b.wirings.push_back(g.wiring);
        b.wheels.push_back(g);
    }
    return b;
}

bool write_wheel_batch(const std::string& path, const WheelBatch& b, const Suite& s,
                       bool append, std::string* error) {
    // Validate BEFORE the stream is opened. Not before it is written to -
    // before it is opened, because opening for overwrite is itself
    // destructive.
    std::string problem = wheel_batch_problem(b, s);
    if (!problem.empty()) {
        if (error) *error = problem;
        return false;
    }
    const char* key = b.rotors ? "rotors" : "reflectors";

    // Appending to JSON is a read-modify-write, not a seek to the end, so
    // the existing document is parsed first. A target that is present but
    // unreadable is refused rather than replaced: it may be the only copy
    // of the wheels some traffic was enciphered under.
    nlohmann::json doc = nlohmann::json::object();
    if (append) {
        std::ifstream in(path);
        if (in) {
            nlohmann::json existing = nlohmann::json::parse(in, nullptr, false);
            if (existing.is_discarded() || !existing.is_object()) {
                if (error)
                    *error = path + " is not readable as JSON, so there is nothing to append to";
                return false;
            }
            std::vector<std::string> existing_problems;
            if (!validate_wheel_document(existing.dump(), &existing_problems)) {
                if (error)
                    *error = path + " does not pass validation, so there is nothing safe to append to";
                return false;
            }
            doc = existing;
        }
    }

    nlohmann::json arr =
        (doc.contains(key) && doc[key].is_array()) ? doc[key] : nlohmann::json::array();
    for (const GeneratedWheel& g : b.wheels) {
        nlohmann::json e;
        e["name"] = g.name;
        e["wiring"] = g.wiring;
        if (!g.notches.empty()) e["notches"] = g.notches;
        arr.push_back(e);
    }
    doc[key] = arr;
    doc["suite"] = s.name;

    const std::string serialized = doc.dump(2) + "\n";
    std::vector<std::string> proposed_problems;
    if (!validate_wheel_document(serialized, &proposed_problems)) {
        if (error)
            *error = proposed_problems.empty() ? "proposed wheel catalogue failed validation"
                                               : proposed_problems.front();
        return false;
    }

    {
        std::ifstream current(path, std::ios::binary);
        if (current) {
            std::ostringstream contents;
            contents << current.rdbuf();
            if (contents.str() == serialized) return true;
        }
    }

    // The whole document is assembled in memory and only then opened for
    // writing, for the same reason validation happens before the stream is
    // opened at all: opening for overwrite is itself destructive.
    const std::filesystem::path target(path);
    const std::filesystem::path pending = target.string() + ".pending";
    std::ofstream f(pending, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    f << serialized;
    f.flush();
    if (!f) {
        f.close();
        std::error_code ignored;
        std::filesystem::remove(pending, ignored);
        if (error) *error = "failed while writing " + path;
        return false;
    }
    f.close();

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
        if (error) *error = "cannot replace " + path + ": " + replace_error.message();
        return false;
    }
    return true;
}

namespace {

// The same object shape settings.cpp reads back, so a key sheet entry and
// a settings file are the same thing and one reader understands both.
// Deliberately duplicated rather than shared: GeneratedSettings lives here
// and Settings lives in src/settings, and giving the logic layer a
// dependency on the settings layer to save eighteen lines would be the
// wrong trade. If a third writer ever appears, that is the moment to make
// one of them the definition.
nlohmann::json generated_settings_to_json(const GeneratedSettings& g) {
    nlohmann::json j;
    j["suite_code"] = g.suite_code;
    j["reflector"] = g.reflector;
    j["master_key"] = g.master_key;
    j["rotor_count"] = static_cast<int>(g.rotors.size());

    nlohmann::json rotors = nlohmann::json::array();
    for (size_t i = 0; i < g.rotors.size(); ++i) {
        nlohmann::json r;
        r["name"] = g.rotors[i];
        r["ring"] = std::to_string(i < g.rings.size() ? g.rings[i] : 1);
        r["notches"] = i < g.notches.size() ? g.notches[i] : std::string();
        rotors.push_back(r);
    }
    j["rotors"] = rotors;

    nlohmann::json plugs = nlohmann::json::array();
    for (const std::string& p : g.plugs) plugs.push_back(p);
    j["plugboard"] = plugs;
    return j;
}

}  // namespace

bool write_key_sheet(const std::string& path, const Suite& s, int count, int plug_pairs,
                     int notches_per_rotor, bool random_count, int fixed_count,
                     std::string* first_entry, std::string* error) {
    try {
        entropy_self_check();
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
    // Built whole in memory before the target is opened, so a failure part
    // way through a long sheet cannot leave a truncated one behind.
    nlohmann::json entries = nlohmann::json::array();
    std::string first;
    for (int i = 0; i < count; ++i) {
        try {
            int n = random_count
                        ? s.min_rotors + static_cast<int>(secure_below(
                              static_cast<uint32_t>(s.max_rotors - s.min_rotors + 1)))
                        : fixed_count;
            GeneratedSettings g = random_settings(s, n, plug_pairs, notches_per_rotor);
            nlohmann::json e = generated_settings_to_json(g);
            if (i == 0) first = e.dump(2);
            entries.push_back(e);
        } catch (const std::exception& ex) {
            if (error) *error = ex.what();
            return false;
        }
    }

    nlohmann::json doc;
    doc["suite_code"] = s.code;
    doc["entries"] = entries;

    std::ofstream f(path);
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    f << doc.dump(2) << "\n";
    if (!f) {
        if (error) *error = "failed while writing " + path;
        return false;
    }
    if (first_entry) *first_entry = first;
    return true;
}

namespace {

void gen_wheels(bool rotors) {
    const Suite& s = ask_suite(/*allow_legacy=*/false);
    Alphabet alpha(s.alphabet);
    const char* what = rotors ? "rotors" : "reflectors";

    int count = ask_int(std::string("how many ") + what, rotors ? 50 : 10, 1, 500);
    // 'U' for a generated rotor, 'K' for a generated reflector, both
    // numbered from 1. Neither can shadow a factory wheel by reusing its
    // name: the built-in INOP-38 rotors are R1-R10 and its reflectors are
    // D-H (A-C on Legacy), so no generated name collides with one. That
    // matters because make_rotor()/make_reflector() look in the loaded pool
    // first, so a name clash would silently replace a factory wheel rather
    // than being reported.
    std::string prefix = ask("name prefix", rotors ? "U" : "K");
    int start = ask_int("first number", 1, 0, 100000);

    int notch_n = 0;
    if (rotors && !s.notches_are_fixed)
        notch_n = ask_int("notches per rotor (0 = leave blank, set per message)", 0, 0, s.max_notches);

    // Rotors and reflectors default to their own files, which is the whole
    // point of the split: a bad reflector cannot take the rotors down with
    // it if they are not in the same document.
    std::string path = ask("write to", rotors ? kRotorsPath : kReflectorsPath);
    std::string mode = ask("(a)ppend or (o)verwrite", "a");
    bool append = !mode.empty() && (mode[0] == 'a' || mode[0] == 'A');

    // Appending to a file that is already rejected as a whole would bury
    // good wheels behind bad ones: load_wheel_file() throws out an entire
    // file on a single duplicate or rotation, so one degenerate batch
    // already sitting in there invalidates everything appended after it
    // too. Checked before anything is generated, so a refusal costs
    // nothing. A file that does not exist yet reports no problems.
    if (append) {
        std::vector<std::string> problems;
        load_wheel_file(path, &problems);
        if (!problems.empty()) {
            std::cout << "  !! " << path << " does not pass validation as it stands:\n";
            for (size_t i = 0; i < problems.size(); ++i)
                std::cout << "     " << problems[i] << "\n";
            std::cout << "  !! appending cannot fix that — every wheel in the file, old and\n"
                         "  !! new, is rejected together on load. Overwrite it, or write to a\n"
                         "  !! fresh path instead.\n";
            return;
        }
    }

    // Generation and validation both live in build_wheel_batch() /
    // write_wheel_batch() now, so the refusal path is reachable from the
    // self-test instead of only from a broken entropy source.
    WheelBatch batch = build_wheel_batch(s, rotors, count, prefix, start, notch_n);
    std::string err;
    if (!write_wheel_batch(path, batch, s, append, &err)) {
        std::cout << "  !! " << err << ".\n"
                  << "  !! Nothing was written; " << path << " is untouched.\n";
        return;
    }
    std::cout << "  " << count << " " << what << " " << (append ? "appended to " : "written to ")
              << path << "\n";

    // Pull them into the live pool now, so a settings batch generated in this
    // same session can actually draw on them.
    int loaded = load_wheel_file(path);
    std::cout << "  " << loaded << " wheels now in the pool ("
              << available_rotors(s).size() << " rotors, "
              << available_reflectors(s).size() << " reflectors for " << s.name << ")\n";
    if (path != kRotorsPath && path != kReflectorsPath)
        std::cout << "  note: only " << kRotorsPath << " and " << kReflectorsPath
                  << " are loaded automatically at startup\n";
}

void gen_settings() {
    const Suite& s = ask_suite();
    int count = ask_int("how many key sheet entries", 360, 1, 10000);
    int plugs = ask_int("plugboard pairs per entry", s.max_plug_pairs / 2, 0, s.max_plug_pairs);
    int notch_n =
        s.notches_are_fixed ? 0 : ask_int("notches per rotor", s.max_notches, 1, s.max_notches);
    // Notches are distinct across the whole machine, so a high rotor count
    // buys fewer of them per rotor. Say so before the sheet is written
    // rather than letting the entries quietly carry fewer than asked.
    if (!s.notches_are_fixed) {
        const int affordable = static_cast<int>(s.alphabet.size()) / s.max_rotors;
        if (notch_n > affordable)
            std::cout << "  note: entries using more than "
                      << (static_cast<int>(s.alphabet.size()) / notch_n)
                      << " rotors will carry fewer notches than that — every notch symbol\n"
                         "  in a machine is distinct, and the alphabet runs out first.\n";
    }
    if (!s.notches_are_fixed && notch_n < s.max_notches)
        std::cout << "  note: fewer notches lengthen the period, but they also move\n"
                     "  fewer rotors inside a single message. The period is already far\n"
                     "  longer than any message will ever be, so the maximum is usually\n"
                     "  the better pick.\n";

    // rotor count: fixed suites (Legacy) have nothing to ask; a ranged suite
    // (INOP-38) lets the operator pin one count or draw a fresh one per entry.
    bool random_count = false;
    int fixed_count = s.min_rotors;
    if (s.min_rotors == s.max_rotors) {
        fixed_count = s.min_rotors;
    } else {
        std::string mode = ask("rotor count: (f)ixed or (r)andom per entry", "f");
        if (!mode.empty() && (mode[0] == 'r' || mode[0] == 'R')) {
            random_count = true;
        } else {
            fixed_count = ask_int("rotor count", s.min_rotors, s.min_rotors, s.max_rotors);
        }
    }

    std::string path = ask("write to", "inop_keysheet.json");
    std::string first, err;
    if (!write_key_sheet(path, s, count, plugs, notch_n, random_count, fixed_count, &first, &err)) {
        std::cout << "  ! " << err << "\n";
        return;
    }
    std::cout << "  " << count << " entries written to " << path << "\n";

    std::string use = ask("load entry 1 into inop_settings.json now? (y/n)", "n");
    if (!use.empty() && (use[0] == 'y' || use[0] == 'Y')) {
        Settings entry;
        std::string load_error;
        if (!load_keysheet_entry(path, 1, entry, &load_error)) {
            std::cout << "  ! cannot install entry 1: " << load_error << "\n";
        } else if (!save_settings(entry, "inop_settings.json")) {
            std::cout << "  ! cannot install entry 1: could not write inop_settings.json\n";
        } else {
            std::cout << "  entry 1 written to inop_settings.json\n";
        }
    }
}

}  // namespace

void run_generator() {
    // Never generate key material without proving the RNG is alive first.
    try {
        entropy_self_check();
    } catch (const std::exception& e) {
        std::cout << "\n  !! " << e.what() << "\n"
                  << "  !! refusing to generate anything. Any wheels or key sheets\n"
                  << "  !! produced by an earlier run must be regenerated and discarded.\n";
        return;
    }
    while (true) {
        std::cout << "\n-- maintenance ---------------------------------------------\n"
                  << "  1  rotor batch       fresh wiring, as many as you like\n"
                  << "  2  reflector batch   fresh involutions\n"
                  << "  3  settings batch    a key sheet you did not have to invent\n"
                  << "  4  back\n";
        std::string c = ask("choice", "4");
        if (c == "1")      gen_wheels(true);
        else if (c == "2") gen_wheels(false);
        else if (c == "3") gen_settings();
        else break;
    }
}

}  // namespace inop
