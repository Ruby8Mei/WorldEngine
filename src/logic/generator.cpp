#include "generator.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "registry.hpp"
#include "rng.hpp"

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
        size_t total_needed = static_cast<size_t>(rotor_count) * static_cast<size_t>(npr);
        if (total_needed > alpha.str().size())
            throw std::runtime_error("not enough alphabet symbols for every rotor to have distinct notches");
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
namespace {

void gen_wheels(bool rotors) {
    const Suite& s = ask_suite(/*allow_legacy=*/false);
    Alphabet alpha(s.alphabet);
    const char* what = rotors ? "rotors" : "reflectors";

    int count = ask_int(std::string("how many ") + what, rotors ? 50 : 10, 1, 500);
    // 'G' for generated, so a batch does not silently shadow a factory wheel
    std::string prefix = ask("name prefix", rotors ? "G" : "GX");
    int start = ask_int("first number", 1, 0, 100000);

    int notch_n = 0;
    if (rotors && !s.notches_are_fixed)
        notch_n = ask_int("notches per rotor (0 = leave blank, set per message)", 0, 0, s.max_notches);

    std::string path = ask("write to", "inop_wheels.txt");
    std::string mode = ask("(a)ppend or (o)verwrite", "a");
    bool append = !mode.empty() && (mode[0] == 'a' || mode[0] == 'A');

    std::ofstream f(path, append ? std::ios::app : std::ios::trunc);
    if (!f) { std::cout << "  ! cannot write " << path << "\n"; return; }
    if (!append) f << "# INOP wheel file\n# alphabet size decides which suite a wheel belongs to\n";
    f << "# " << count << " " << what << " for " << s.name << "\n";

    std::set<std::string> distinct;
    for (int i = 0; i < count; ++i) {
        std::string name = prefix + std::to_string(start + i);
        std::string w = rotors ? random_rotor_wiring(alpha) : random_reflector_wiring(alpha);
        distinct.insert(w);
        if (rotors) {
            f << "rotor " << name << " " << w;
            if (notch_n > 0) f << " " << random_notches(alpha, notch_n);
            f << "\n";
        } else {
            f << "reflector " << name << " " << w << "\n";
        }
    }
    f.close();
    if (count > 1 && distinct.size() < static_cast<size_t>(count)) {
        std::cout << "  !! only " << distinct.size() << " distinct wirings out of " << count
                  << " — the entropy source is broken, output discarded\n";
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
    if (path != "inop_wheels.txt")
        std::cout << "  note: only inop_wheels.txt is loaded automatically at startup\n";
}

void gen_settings() {
    const Suite& s = ask_suite();
    int count = ask_int("how many key sheet entries", 360, 1, 10000);
    int plugs = ask_int("plugboard pairs per entry", s.max_plug_pairs / 2, 0, s.max_plug_pairs);
    int notch_n = s.notches_are_fixed ? 0 : ask_int("notches per rotor", 1, 1, s.max_notches);
    if (!s.notches_are_fixed && notch_n > 1)
        std::cout << "  note: 1 notch per rotor gives the longest period; more shortens it\n";

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

    std::string path = ask("write to", "inop_keysheet.txt");
    std::ofstream f(path);
    if (!f) { std::cout << "  ! cannot write " << path << "\n"; return; }

    f << "# INOP key sheet — " << count << " entries for " << s.name << "\n";
    f << "# copy one block into inop.settings to use it\n";

    std::string first;
    for (int i = 0; i < count; ++i) {
        try {
            int n = random_count
                        ? s.min_rotors + static_cast<int>(secure_below(
                              static_cast<uint32_t>(s.max_rotors - s.min_rotors + 1)))
                        : fixed_count;
            GeneratedSettings g = random_settings(s, n, plugs, notch_n);
            std::string txt = settings_to_text(g);
            if (i == 0) first = txt;
            f << "\n# --- entry " << (i + 1) << " ---\n" << txt;
        } catch (const std::exception& e) {
            std::cout << "  ! " << e.what() << "\n";
            return;
        }
    }
    std::cout << "  " << count << " entries written to " << path << "\n";

    std::string use = ask("load entry 1 into inop.settings now? (y/n)", "n");
    if (!use.empty() && (use[0] == 'y' || use[0] == 'Y')) {
        std::ofstream s1("inop.settings");
        if (s1) { s1 << first; std::cout << "  entry 1 written to inop.settings\n"; }
        else std::cout << "  ! cannot write inop.settings\n";
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
