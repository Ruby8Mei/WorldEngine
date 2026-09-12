// main.cpp — INOP terminal interface
#include <algorithm>
#include <cctype>    // std::toupper, std::isspace
#include <chrono>
#include <cstdlib>   // std::exit
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "batch.hpp"
#include "generator.hpp"
#include "gui.hpp"
#include "inop.hpp"
#include "languages.hpp"
#include "pipeline.hpp"
#include "registry.hpp"
#include "rng.hpp"
#include "settings.hpp"
#include "transform.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX               // MinGW's os_defines.h already defines this
#define NOMINMAX               // stop windows.h defining min/max as macros
#endif
#include <windows.h>
// Older MinGW and pre-Win10 SDK headers lack this constant.
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif

using namespace inop;

// ── ANSI helpers ────────────────────────────────────────────────────────
namespace {

bool g_color = true;

const char* C(const char* code) { return g_color ? code : ""; }
#define DIM   C("\033[2m")
#define BOLD  C("\033[1m")
#define CYAN  C("\033[36m")
#define GREEN C("\033[32m")
#define YELL  C("\033[33m")
#define RED   C("\033[31m")
#define RST   C("\033[0m")

void enable_vt() {
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
    // SetConsoleOutputCP alone only affects what the console WRITES. Typed
    // or pasted accented characters are decoded on the way IN using the
    // console's separate input codepage, which defaults to the system
    // legacy codepage, not UTF-8 — without this, é/è/â/... arrive already
    // mangled or dropped before transform() ever sees them.
    SetConsoleCP(CP_UTF8);
#endif
}

void rule(const std::string& title = "") {
    std::cout << DIM << "-- " << RST;
    if (!title.empty()) std::cout << BOLD << title << RST << " ";
    std::cout << DIM << std::string(title.empty() ? 60 : 56 - title.size(), '-') << RST << "\n";
}

void fail(const std::string& msg) { std::cout << RED << "  ! " << msg << RST << "\n"; }

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// The cipher alphabet is lowercase (ALPHA26/ALPHA38 in inop.hpp), so any
// value that gets fed into it — plugboard pairs, notch symbols, the master
// key, ciphertext, markers — needs this, not upper(). Rotor/reflector/suite
// NAMES are identifiers, not alphabet symbols, and stay upper().
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Every symbol handed to the machine has to be a member of its alphabet.
// The encrypt path guarantees that by running everything through
// preprocess(); the decrypt path takes a ciphertext straight from the
// operator and has no equivalent, so it checks here instead.
// Machine::encipher() resolves symbols with Alphabet::index_unchecked(),
// which answers -1 for a stranger and then indexes the plugboard and rotor
// tables with it — reading outside both. Returns a description of the first
// offending character, or an empty string if the text is clean.
//
// Rejecting is deliberate, and dropping the character would be worse than
// useless: a ciphertext is positional, so one symbol removed shifts every
// symbol after it and turns the rest of the message into noise the operator
// has no way to diagnose. A hyphen picked up from a wrapped line is enough
// to trigger it, and under Legacy so is any digit at all.
std::string foreign_symbol(const std::string& text, const Alphabet& alpha) {
    static const char* HEX = "0123456789abcdef";
    for (size_t i = 0; i < text.size(); ++i) {
        char raw = text[i];
        if (alpha.contains(raw)) continue;
        unsigned char c = static_cast<unsigned char>(raw);
        std::string shown = c >= 0x20 && c < 0x7f
                                ? std::string("\"") + raw + "\""
                                : std::string("byte 0x") + HEX[c >> 4] + HEX[c & 0x0f];
        return shown + " at position " + std::to_string(i + 1);
    }
    return std::string();
}

std::string ask(const std::string& prompt) {
    std::cout << CYAN << prompt << RST << " ";
    std::string line;
    if (!std::getline(std::cin, line)) { std::cout << "\n"; std::exit(0); }
    // trim
    size_t a = line.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = line.find_last_not_of(" \t\r\n");
    std::string trimmed = line.substr(a, b - a + 1);

    // The leading ':' is what makes this unambiguous — a bare "q" or "quit"
    // is a legitimate answer at several prompts (notch symbols, master key
    // symbols, plugboard pairs), since both alphabets contain Q.
    std::string low = trimmed;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (low == ":q" || low == ":quit" || low == ":exit") {
        std::cout << DIM << "  closed.\n" << RST;
        std::exit(0);
    }
    return trimmed;
}

std::vector<std::string> split(const std::string& s) {
    std::istringstream is(s);
    std::vector<std::string> out;
    std::string tok;
    while (is >> tok) out.push_back(tok);
    return out;
}

bool ask_toggle(const std::string& prompt, bool def) {
    std::string a = upper(ask(prompt + (def ? " [ON/off]" : " [on/OFF]")));
    if (a.empty()) return def;
    return a == "ON" || a == "Y" || a == "YES" || a == "1";
}

// Language tag for the numeral-suffix diacritic scheme — INOP-38 only.
// Asked once per message, defaulting to whatever was chosen last time.
std::string ask_language(const std::string& def) {
    while (true) {
        std::string c = lower(ask("language [" + def + "]"));
        if (c.empty()) return def;
        if (is_supported_language(c)) return c;
        fail("unknown language code — see the list in the README");
    }
}

// ── settings ────────────────────────────────────────────────────────────
void verify_legacy_integrity();  // defined below; forward-declared for collect_settings()

// ── interactive setup ───────────────────────────────────────────────────
Settings collect_settings() {
    Settings s;
    rule("suite");
    for (const auto& kv : suites()) {
        std::string rotors_desc = kv.second.min_rotors == kv.second.max_rotors
            ? std::to_string(kv.second.min_rotors)
            : std::to_string(kv.second.min_rotors) + "-" + std::to_string(kv.second.max_rotors);
        std::cout << "  " << BOLD << kv.second.code << RST << "  " << kv.second.name
                  << DIM << "  (" << kv.second.alphabet.size() << " symbols, "
                  << rotors_desc << " rotors)" << RST << "\n";
    }
    while (true) {
        std::string c = ask("suite [38]");
        if (c.empty()) c = "38";
        if (suites().count(c)) { s.suite_code = c; break; }
        fail("unknown suite code");
    }
    const Suite& su = suite(s.suite_code);
    if (su.code == "26") verify_legacy_integrity();
    Alphabet alpha(su.alphabet);

    rule("rotor count");
    int rotor_count = su.min_rotors;
    if (su.min_rotors == su.max_rotors) {
        std::cout << DIM << "  " << su.name << " always uses " << su.min_rotors << " rotors" << RST << "\n";
    } else {
        while (true) {
            std::string c = ask("how many rotors (" + std::to_string(su.min_rotors) + "-" +
                                std::to_string(su.max_rotors) + ")");
            try {
                int v = std::stoi(c);
                if (v >= su.min_rotors && v <= su.max_rotors) { rotor_count = v; break; }
            } catch (...) {}
            fail("need a number " + std::to_string(su.min_rotors) + "-" + std::to_string(su.max_rotors));
        }
    }

    rule("rotors");
    std::vector<std::string> pool = available_rotors(su);
    std::cout << "  available:";
    for (auto& n : pool) std::cout << " " << n;
    std::cout << DIM << "   (" << pool.size() << " wheels)" << RST << "\n";
    while (true) {
        auto picks = split(upper(ask("choose " + std::to_string(rotor_count) +
                                     " rotors, left to right")));
        if (static_cast<int>(picks.size()) != rotor_count) {
            fail("need exactly " + std::to_string(rotor_count));
            continue;
        }
        bool ok = true, dup = false;
        for (size_t i = 0; i < picks.size(); ++i) {
            bool known = false;
            for (auto& n : pool) if (n == picks[i]) known = true;
            if (!known) { ok = false; break; }
            for (size_t j = 0; j < i; ++j) if (picks[j] == picks[i]) dup = true;
        }
        if (!ok)  { fail("unknown rotor for this suite"); continue; }
        if (dup)  { fail("the same wheel cannot sit in two slots"); continue; }
        s.rotors = picks;
        break;
    }

    rule("reflector");
    std::vector<std::string> rpool = available_reflectors(su);
    std::cout << "  available:";
    for (auto& n : rpool) std::cout << " " << n;
    std::cout << "\n";
    while (true) {
        std::string r = upper(ask("reflector"));
        bool known = false;
        for (auto& n : rpool) if (n == r) known = true;
        if (known) { s.reflector = r; break; }
        fail("not a reflector for this suite");
    }

    rule("plugboard");
    std::cout << DIM << "  up to " << su.max_plug_pairs
              << " pairs, e.g. AB CD 3X — blank for none" << RST << "\n";
    while (true) {
        auto pairs = split(alpha.fold_case(ask("pairs")));
        if (pairs.empty()) { s.plugs.clear(); break; }
        if (static_cast<int>(pairs.size()) > su.max_plug_pairs) {
            fail("too many pairs (max " + std::to_string(su.max_plug_pairs) + ")");
            continue;
        }
        try {
            Plugboard probe(pairs, alpha);  // validates fully
            s.plugs = pairs;
            break;
        } catch (const std::exception& e) { fail(e.what()); }
    }

    rule("ring settings");
    while (true) {
        auto toks = split(ask(std::to_string(rotor_count) + " values 1-" +
                              std::to_string(alpha.size())));
        if (static_cast<int>(toks.size()) != rotor_count) {
            fail("need " + std::to_string(rotor_count) + " numbers");
            continue;
        }
        std::vector<int> vals;
        bool ok = true;
        for (auto& t : toks) {
            try {
                int v = std::stoi(t);
                if (v < 1 || v > alpha.size()) { ok = false; break; }
                vals.push_back(v);
            } catch (...) { ok = false; break; }
        }
        if (!ok) { fail("values must be integers in 1.." + std::to_string(alpha.size())); continue; }
        s.rings = vals;
        break;
    }

    rule("notches");
    s.notches.assign(s.rotors.size(), "");
    if (su.notches_are_fixed) {
        std::cout << DIM << "  legacy wheels carry their historic notches" << RST << "\n";
    } else {
        while (true) {
            for (size_t i = 0; i < s.rotors.size(); ++i) {
                while (true) {
                    std::string raw = ask("notches for " + s.rotors[i] + " (1-" +
                                          std::to_string(su.max_notches) + " symbols)");
                    if (raw.empty() || raw == "-") {
                        fail("at least one notch is required — a notch-less rotor never advances "
                             "the next rotor, which collapses the machine period");
                        continue;
                    }
                    std::string n = alpha.fold_case(raw);
                    if (static_cast<int>(n.size()) > su.max_notches) {
                        fail("at most " + std::to_string(su.max_notches)); continue;
                    }
                    bool ok = true;
                    for (char c : n) if (!alpha.contains(c)) ok = false;
                    if (!ok) { fail("symbols must come from the alphabet"); continue; }
                    s.notches[i] = n;
                    break;
                }
            }
            if (duplicate_notch_symbols(s.notches).empty()) break;
            fail("notch symbols cannot repeat within or across rotors; enter all notches again");
        }
    }

    rule("master key");
    // The historic reflector does not rotate, so a Legacy key carries no
    // orientation symbol — just one window letter per rotor. A 4-symbol key
    // from an older sheet is still accepted for compatibility; build_machine()
    // drops the extra symbol with a notice rather than rejecting it.
    const size_t need = su.historic_lock ? s.rotors.size() : s.rotors.size() + 1;
    std::cout << DIM << "  " << need << " symbols: one window position per rotor"
              << (su.historic_lock ? "" : ", plus the reflector orientation") << RST << "\n";
    if (su.historic_lock)
        std::cout << DIM << "  (the historic reflector is fixed and does not rotate; a "
                  << (need + 1) << "-symbol key from an older sheet still loads, with the "
                  << "last symbol ignored)" << RST << "\n";
    try {
        entropy_self_check();
        std::cout << DIM << "  suggestion (freshly drawn): " << RST << BOLD
                  << secure_string(su.alphabet, need) << RST << "\n";
    } catch (const std::exception& e) {
        std::cout << DIM << "  no key suggestion: " << e.what() << RST << "\n";
    }
    while (true) {
        std::string k = alpha.fold_case(ask("key"));
        if (k.size() != need && !(su.historic_lock && k.size() == need + 1)) {
            fail("need exactly " + std::to_string(need) + " symbols" +
                 (su.historic_lock ? " (or " + std::to_string(need + 1) + " for compatibility)" : ""));
            continue;
        }
        bool ok = true;
        for (char c : k) if (!alpha.contains(c)) ok = false;
        if (!ok) { fail("symbols must come from the alphabet"); continue; }
        s.master_key = k;
        break;
    }
    return s;
}

// Notches, rings and rotors are read back from the MACHINE rather than from
// what was typed, so this shows what is actually loaded — including the
// historic notches on legacy wheels, which the operator never enters.
void show_settings(const Settings& s, const Machine& m) {
    const Suite& su = suite(s.suite_code);
    rule("active settings");
    std::cout << "  suite     " << su.name << "\n";

    const size_t n = s.rotors.size();
    std::vector<std::string> notch(n, "-"), ring(n);
    for (size_t i = 0; i < n && i < m.rotors().size(); ++i) {
        std::string t = m.rotors()[i].notch_str(m.alphabet());
        if (!t.empty()) notch[i] = t;
    }
    for (size_t i = 0; i < n; ++i)
        ring[i] = i < s.rings.size() ? std::to_string(s.rings[i]) : "?";

    // one column per rotor, wide enough for whichever field is longest
    std::vector<size_t> w(n);
    for (size_t i = 0; i < n; ++i) {
        w[i] = s.rotors[i].size();
        if (notch[i].size() > w[i]) w[i] = notch[i].size();
        if (ring[i].size()  > w[i]) w[i] = ring[i].size();
    }
    struct Row { const char* label; const std::vector<std::string>* v; };
    std::vector<std::string> rotors(s.rotors.begin(), s.rotors.end());
    Row rows[3] = { {"  rotors    ", &rotors}, {"  rings     ", &ring}, {"  notches   ", &notch} };
    for (int r = 0; r < 3; ++r) {
        std::cout << rows[r].label;
        for (size_t i = 0; i < n; ++i)
            std::cout << (*rows[r].v)[i] << std::string(w[i] - (*rows[r].v)[i].size() + 2, ' ');
        std::cout << "\n";
    }

    std::cout << "  reflector " << s.reflector;
    if (su.historic_lock)
        std::cout << DIM << "  (fixed — historic reflectors do not rotate)" << RST;
    else
        std::cout << DIM << "  (starts at '" << s.master_key[s.master_key.size() - 1] << "')" << RST;
    std::cout << "\n  plugs     ";
    if (s.plugs.empty()) std::cout << DIM << "(none)" << RST;
    else for (auto& p : s.plugs) std::cout << p << " ";
    std::cout << "\n  key       " << s.master_key << "\n";
    if (su.notches_are_fixed)
        std::cout << DIM << "  notches shown are the historic ones carried by the wheels" << RST << "\n";
    rule();
}

// ── Legacy integrity guard ──────────────────────────────────────────────
//
// The Legacy suite is a museum exhibit and a correctness anchor at the same
// time: if it silently drifted from the historic machine it claims to be,
// nothing would notice except a cryptanalyst. Run before the main menu and
// again whenever Legacy is actually selected, so a regression is caught at
// the moment it matters rather than only under --self-test.
void verify_legacy_integrity() {
    auto abort_check = [](const std::string& what) {
        std::cout << RED << "  !! legacy integrity check failed: " << what << RST << "\n";
        std::exit(1);
    };

    // (a) the historic Enigma vector: rotors I II III, reflector B, rings
    // 1 1 1, key AAAA, twelve presses of A.
    {
        Alphabet a(ALPHA26);
        std::vector<Rotor> rs{make_rotor("I", a), make_rotor("II", a), make_rotor("III", a)};
        Machine m(a, std::move(rs), make_reflector("B", a), Plugboard({}, a), {1, 1, 1}, "AAAA",
                  /*legacy_stepping=*/true);
        m.set_moving_reflector(false);
        std::string got = m.encipher("AAAAAAAAAAAA");
        if (got != "BDZGOWCXLTKS")
            abort_check("historic Enigma I-II-III/B vector produced '" + got + "', expected BDZGOWCXLTKS");
    }

    // (b) the Legacy suite descriptor itself.
    {
        const Suite& su = suite("26");
        if (su.alphabet.size() != 26) abort_check("Legacy alphabet is not 26 symbols");
        if (su.min_rotors != 3 || su.max_rotors != 3) abort_check("Legacy rotor count is not fixed at 3");
        if (su.block != 5) abort_check("Legacy block width is not 5");
        if (!su.historic_lock) abort_check("Legacy suite is not historic_lock");
        if (!su.notches_are_fixed) abort_check("Legacy suite notches are not fixed");
    }

    // (c) apply_suite_lock forces double pass, padding and moving reflector off.
    {
        PipelineConfig cfg;
        cfg.double_pass = cfg.padding = cfg.moving_reflector = true;
        bool locked = apply_suite_lock(cfg, true, 5);
        if (!locked || cfg.double_pass || cfg.padding || cfg.moving_reflector)
            abort_check("apply_suite_lock did not force Legacy's double pass, padding and "
                        "reflector motion off");
    }
}

// ── self-test ───────────────────────────────────────────────────────────
// ── key material must never be tracked by git ───────────────────────────
//
// A ratchet, not a remedy. Nothing is tracked today; this is what makes a
// future `git add -f inop_keysheet.txt` loud instead of silent.
//
// Implemented by reading .git/index directly rather than shelling out to
// git: no process spawn on a hot startup path, and no dependency on git
// being installed. The index is the list of tracked paths, so a filename
// appearing in it means exactly the thing being tested for.
//
// Two limits, stated rather than discovered later. A path stored under
// index version 4 is prefix-compressed and could hide a name from this
// scan, which fails open; version 4 is opt-in and rare. And a tracked file
// whose path merely contains one of these names as a substring will trip
// it, which fails closed. Of the two directions, that is the right one.
std::vector<std::string> tracked_key_material() {
    // Both the 2.3.0 JSON names and the 2.2.x plain-text ones: an operator
    // mid-migration has both on disk, and either committed is the same
    // permanent compromise.
    static const char* kNames[] = {"inop_rotors.json",  "inop_reflectors.json",
                                   "inop_keysheet.json", "inop_settings.json",
                                   "inop_wheels.txt",    "inop_keysheet.txt",
                                   "inop.settings"};

    std::string dir = ".";
    std::string index;
    for (int up = 0; up < 6; ++up) {
        std::ifstream f(dir + "/.git/index", std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            index = ss.str();
            break;
        }
        dir += "/..";
    }

    std::vector<std::string> found;
    if (index.empty()) return found;  // not a checkout, or no index yet
    for (const char* name : kNames)
        if (index.find(name) != std::string::npos) found.push_back(name);
    return found;
}

int self_test() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::cout << (ok ? GREEN : RED) << (ok ? "  ok   " : "  FAIL ") << RST << what << "\n";
        // Flushed rather than buffered, because a later check can crash.
        // Removing the floor from random_notches() makes a negative count
        // index off the front of a vector, and the buffered FAIL from the
        // check before it died with the process — which reads as "the
        // guard is not covered" when the truth is the opposite.
        if (!ok) { std::cout.flush(); ++failures; }
    };

    // 1. Historic Enigma vector: rotors I II III, reflector B, all rings 01,
    //    key AAA. Pressing A twelve times gives a known ciphertext.
    {
        Alphabet a(ALPHA26);
        std::vector<Rotor> rs{make_rotor("I", a), make_rotor("II", a), make_rotor("III", a)};
        Machine m(a, std::move(rs), make_reflector("B", a), Plugboard({}, a), {1, 1, 1}, "AAAA", true);
        m.set_moving_reflector(false);
        std::string got = m.encipher("AAAAAAAAAAAA");
        check(got == "BDZGOWCXLTKS", "historic Enigma I-II-III/B vector -> " + got);
    }

    // 2. The machine is its own inverse when rewound.
    {
        Alphabet a(ALPHA38);
        std::vector<Rotor> rs;
        for (auto n : {"R1", "R4", "R7", "R2", "R9"}) {
            Rotor r = make_rotor(n, a);
            r.set_notches("q7#", a);
            rs.push_back(std::move(r));
        }
        Machine m(a, std::move(rs), make_reflector("E", a),
                  Plugboard({"ab", "3x", "#/"}, a), {5, 12, 30, 1, 22}, "k3m9qz", false);
        std::string plain = preprocess("THE QUICK BROWN FOX 0123456789 / END", a);
        m.rewind();
        std::string ct = m.encipher(plain);
        m.rewind();
        std::string back = m.encipher(ct);
        check(back == plain, "reciprocity across 5 rotors + moving reflector");
        check(ct != plain, "ciphertext differs from plaintext");
    }

    // 3. Full pipeline round trip, padding and double pass on.
    {
        Settings s;
        s.suite_code = "38";
        s.rotors = {"R3", "R1", "R8", "R5", "R10"};
        s.reflector = "G";
        s.rings = {7, 19, 2, 33, 11};
        s.notches = {"a", "5", "#", "z", "/"};
        s.plugs = {"qw", "12"};
        s.master_key = "h4t#0p";
        Machine m = build_machine(s);
        Pipeline p(m, PipelineConfig{});

        std::string msg = "ATTACK AT DAWN / HOLD THE LINE 0800";
        Encrypted e = p.encrypt(msg);
        std::string back = p.decrypt(e.ciphertext, e.marker);
        check(back == "attack at dawn / hold the line 0800", "pipeline round trip -> " + back);
        check(e.ciphertext.size() % 16 == 0, "ciphertext is block aligned");
    }

    // 4. The double pass removes Enigma's fatal no-self-encipherment property.
    //    The body length here is ODD on purpose. An even length cannot show
    //    the failure this section exists to catch: the transposition applied
    //    between the two passes has to be free of fixed indices, and the old
    //    std::reverse had exactly one whenever the length was odd.
    {
        Settings s;
        s.suite_code = "38";
        s.rotors = {"R1", "R2", "R3", "R4", "R5"};
        s.reflector = "D";
        s.rings = {1, 1, 1, 1, 1};
        s.notches = {"a", "b", "c", "d", "e"};
        s.master_key = "aaaaaa";
        const size_t odd_len = 4001;

        auto self_hits = [&](bool double_pass) {
            Machine m = build_machine(s);
            PipelineConfig c;
            c.double_pass = double_pass;
            c.padding = false;
            Pipeline p(m, c);
            std::string plain(odd_len, 'a');
            std::string ct = p.encrypt(plain).ciphertext;
            int hits = 0;
            for (size_t i = 0; i < plain.size(); ++i) if (ct[i] == plain[i]) ++hits;
            return hits;
        };
        int single = self_hits(false);
        int doubled = self_hits(true);
        check(single == 0, "single pass: letter never maps to itself (Enigma's flaw), hits=" +
                               std::to_string(single));
        check(doubled > 0, "double pass: self-mapping restored, hits=" + std::to_string(doubled));

        // A chance self-hit is expected and welcome — that is the whole point
        // of the double pass. A STRUCTURAL one is not: an index that
        // self-enciphers under every plaintext is a crib handle at a known
        // position, exactly what Enigma handed Bletchley. Intersecting the
        // self-hit index sets of several unrelated plaintexts separates the
        // two: a 1-in-38 coincidence does not survive sixteen intersections,
        // a structural fixed point survives all of them.
        //
        // The draw alphabet deliberately excludes SPACE_SUB — preprocess()
        // prunes a literal one, which would shorten the body and slide every
        // index after it out of alignment with the plaintext being compared.
        {
            Machine m = build_machine(s);
            PipelineConfig c;
            c.double_pass = true;
            c.padding = false;
            Pipeline p(m, c);
            const std::string draw = "abcdefghijklmnopqrstuvwxyz0123456789/";
            std::vector<bool> universal(odd_len, true);
            for (int trial = 0; trial < 16; ++trial) {
                std::string plain = secure_string(draw, odd_len);
                std::string ct = p.encrypt(plain).ciphertext;
                for (size_t i = 0; i < odd_len; ++i)
                    if (i >= ct.size() || ct[i] != plain[i]) universal[i] = false;
            }
            int structural = 0;
            std::string where;
            for (size_t i = 0; i < odd_len; ++i)
                if (universal[i]) { ++structural; where += " " + std::to_string(i); }
            check(structural == 0,
                  "double pass: no index self-enciphers under every plaintext at an odd length, "
                  "structural fixed points=" + std::to_string(structural) +
                      (where.empty() ? "" : " at index" + where));
        }
    }

    // 5. The Legacy lock: a 1939 machine cannot be given INOP features.
    {
        PipelineConfig c;
        c.double_pass = c.padding = c.moving_reflector = true;
        bool locked = apply_suite_lock(c, suite("26").historic_lock, suite("26").block);
        check(locked && !c.double_pass && !c.padding && !c.moving_reflector && c.block == 5,
              "Legacy locks off double pass, padding and reflector motion; 5-letter blocks");

        PipelineConfig d;
        apply_suite_lock(d, suite("38").historic_lock, suite("38").block);
        check(d.double_pass && d.padding && d.block == 16,
              "INOP-38 keeps its features, 16-symbol blocks");
    }

    // 5b. Stepping rule follows the SUITE, not rotors_.size(). Two 3-rotor
    //     machines with identical wirings, notches, reflector, rings and key
    //     must diverge once one is built as Legacy-style and the other as
    //     INOP-38-style — nothing about "3 rotors" may pick that for them.
    {
        Alphabet a(ALPHA38);
        auto build = [&](bool legacy_stepping) {
            std::vector<Rotor> rs;
            for (auto n : {"R1", "R2", "R3"}) {
                Rotor r = make_rotor(n, a);
                r.set_notches("am", a);
                rs.push_back(std::move(r));
            }
            return Machine(a, std::move(rs), make_reflector("D", a), Plugboard({}, a),
                           {1, 2, 3}, "abcd", legacy_stepping);
        };
        Machine legacy_style = build(true);
        Machine inop38_style = build(false);
        std::string msg(50, 'a');
        std::string ct_legacy = legacy_style.encipher(msg);
        std::string ct_inop38 = inop38_style.encipher(msg);
        check(ct_legacy != ct_inop38,
              "identical 3-rotor wirings/settings diverge between Legacy-style and "
              "INOP-38-style stepping — the rule is chosen by the caller, not inferred");
    }

    // 6. The entropy source must be provably alive.
    {
        bool ok = true;
        std::string why;
        try { entropy_self_check(); } catch (const std::exception& e) { ok = false; why = e.what(); }
        check(ok, ok ? "entropy source is alive and uniform" : why);
    }

    // 7. wiring_is_rotation must rank by the declared alphabet, not ASCII —
    //    ASCII sorts digits/#// before letters, ALPHA38 puts them after.
    {
        auto shift_by_one = [](const std::string& alpha) {
            std::string w;
            w.reserve(alpha.size());
            for (size_t i = 1; i <= alpha.size(); ++i) w += alpha[i % alpha.size()];
            return w;
        };
        check(wiring_is_rotation(shift_by_one(ALPHA38), ALPHA38),
              "shift-by-1 wiring of ALPHA38 is caught as a rotation");
        check(wiring_is_rotation(shift_by_one(ALPHA26), ALPHA26),
              "shift-by-1 wiring of ALPHA26 is caught as a rotation");
        // R1's factory wiring, copied from registry.cpp — must NOT be
        // flagged as a rotation.
        const std::string r1 = "bxml2uokh3#46705cyg19etfprid8swqavnzj/";
        check(!wiring_is_rotation(r1, ALPHA38), "built-in R1 wiring is accepted, not a rotation");
    }

    // 8. Throughput.
    {
        Alphabet a(ALPHA38);
        std::vector<Rotor> rs;
        for (auto n : {"R1", "R2", "R3", "R4", "R5"}) rs.push_back(make_rotor(n, a));
        for (auto& r : rs) r.set_notches("am", a);
        Machine m(a, std::move(rs), make_reflector("D", a), Plugboard({}, a), {1, 2, 3, 4, 5}, "abcdef", false);
        std::string text(200000, 'a');
        auto t0 = std::chrono::steady_clock::now();
        volatile size_t sink = m.encipher(text).size();
        (void)sink;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << DIM << "  --   " << RST << "throughput: "
                  << static_cast<long>(text.size() / ms / 1000.0) << "M symbols/s\n";
    }

    // 9. One real sentence per language, through the transformer and back.
    //
    //    These are the worked examples the old per-language scheme was
    //    tested against, kept because they are real text in 48 real
    //    languages and no set of invented cases covers as much. What is
    //    checked is different, though. The old test compared against a
    //    hand written expected folding, one per language, which the new
    //    scheme changes and which nobody could maintain by hand anyway.
    //
    //    What is checked here instead is stability: fold a sentence,
    //    unfold it, fold it again, and the two foldings must be the same
    //    string. That is the property an operator actually depends on --
    //    decode a message, encode it again, get the same ciphertext -- and
    //    it fails loudly on any disagreement between the two directions.
    //    It is not circular: nothing here is generated from the
    //    transformer, and a decoder that guessed wrong would produce a
    //    different second folding.
    //
    //    The character level correctness is section 10s job, where every
    //    one of the 480 carried characters is compared against the real
    //    unicode character rather than against the transformer.
    {
        struct Row { const char* lang; const char* plain; };
        static const Row rows[] = {
            {"sqi", "\x55\x6e\xc3\xab\x20\x66\x6c\x61\x73\x20\x73\x68\x71\x69\x70\x20\x64\x68\x65\x20\x70\x69\x20\xc3\xa7\x61\x6a\x2e"},
            {"eus", "\x4b\x61\x69\x78\x6f\x2c\x20\x7a\x65\x72\x20\x6d\x6f\x64\x75\x7a\x20\x7a\x61\x75\x64\x65\x3f"},
            {"bos", "\xc4\x86\x61\x6f\x2c\x20\xc4\x90\x6f\x72\xc4\x91\x65\x20\x76\x6f\x6c\x69\x20\xc4\x8d\x6f\x6b\x6f\x6c\x61\x64\x75\x20\x69\x20\xc4\x8d\x61\x6a\x2e"},
            {"yue", "\x4e\xc3\xa9\x69\x68\x20\x68\xc3\xb3\x75\x2c\x20\x6e\x67\xc3\xb3\x68\x20\x64\xc5\x8d\x75\x20\x68\xc3\xb3\x75\x2e"},
            {"cat", "\x45\x6c\x20\x70\x61\x72\x61\x6c\x6c\x65\x6c\x20\xc3\xa9\x73\x20\x63\x6c\x61\x72\x2e"},
            {"cpf", "\x4c\x69\x20\x66\xc3\xb2\x2c\x20\x6c\x69\x20\x67\x65\x6e\x20\x6b\xc3\xa8\x20\x6b\x6f\x6e\x74\x61\x6e\x2c\x20\x65\x20\x6c\x69\x20\x72\x65\x74\x65\x20\x62\xc3\xb2\x20\x6c\x61\x6e\x6d\xc3\xa8\x20\x61\x2e"},
            {"hrv", "\xc4\x86\x61\x6f\x2c\x20\xc4\x90\x6f\x72\xc4\x91\x65\x20\x76\x6f\x6c\x69\x20\xc4\x8d\x6f\x6b\x6f\x6c\x61\x64\x75\x20\x69\x20\xc4\x8d\x61\x6a\x2e"},
            {"czr", "\x44\xc4\x9b\x6b\x75\x6a\x69\x2c\x20\x6d\xc5\xaf\x6a\x20\x70\xc5\x99\xc3\xad\x74\x65\x6c\x20\x6d\xc3\xa1\x20\x6e\x6f\x76\xc3\xbd\x20\x64\xc5\xaf\x6d\x2e"},
            {"dan", "\x48\xc3\xa5\x70\x65\x72\x20\x64\x75\x20\x66\xc3\xa5\x72\x20\x65\x6e\x20\x66\x69\x6e\x20\x64\x61\x67\x20\x70\xc3\xa5\x20\xc3\xb8\x79\x61\x2c\x20\x6b\x6a\xc3\xa6\x72\x65\x20\x76\x65\x6e\x6e\x2e"},
            {"nld", "\x44\x65\x20\x63\x6f\xc3\xb6\x72\x64\x69\x6e\x61\x74\x69\x65\x20\x77\x61\x73\x20\x69\x64\x65\x65\xc3\xab\x6e\x20\x77\x61\x61\x72\x64\x2e"},
            {"eng", "\x54\x68\x65\x20\x6e\x61\xc3\xaf\x76\x65\x20\x63\x61\x66\xc3\xa9\x20\x6f\x77\x6e\x65\x72\x20\x73\x6d\x69\x6c\x65\x64\x2e"},
            {"est", "\x53\xc3\xb6\xc3\xb6\x64\x61\x76\x20\xc3\xb5\x75\x6e\x61\x70\x75\x75\x20\x6f\x6e\x20\x68\x65\x61\x2e"},
            {"fin", "\x48\xc3\xa4\x6e\x20\x6f\x6e\x20\x74\xc3\xa4\xc3\xa4\x6c\x6c\xc3\xa4\x2e"},
            {"fra", "\x4c\x65\x20\x63\x61\x66\xc3\xa9\x20\x65\x73\x74\x20\x74\x72\xc3\xa8\x73\x20\x63\x68\x65\x72\x2e"},
            {"deu", "\x4d\xc3\xb6\x63\x68\x74\x65\x6e\x20\x53\x69\x65\x20\x65\x69\x6e\x20\x67\x72\x6f\xc3\x9f\x65\x73\x20\x4b\xc3\xa4\x73\x65\x62\x72\xc3\xb6\x74\x63\x68\x65\x6e\x3f"},
            {"hin", "\x4d\x61\x69\xe1\xb9\x83\x20\x4b\xe1\xb9\x9b\xe1\xb9\xa3\xe1\xb9\x87\x61\x20\x6b\xc4\xab\x20\x67\xc4\xab\x74\xc4\x81\x20\x70\x61\xe1\xb9\x9b\x68\x74\xc4\x81\x20\x68\xc5\xab\xe1\xb9\x83\x2e"},
            {"hun", "\xc5\x90\x20\x73\x7a\x65\x72\x65\x74\x69\x20\x61\x20\x67\x79\xc3\xbc\x6d\xc3\xb6\x6c\x63\x73\xc3\xb6\x74\x20\xc3\xa9\x73\x20\x61\x20\x74\xc5\xb1\x7a\x68\x65\x6c\x79\x65\x74\x2e"},
            {"ibo", "\xe1\xbb\x8a\x20\x62\xe1\xbb\xa5\x20\x65\x7a\x69\x67\x62\x6f\x20\xe1\xbb\xa5\x6d\xe1\xbb\xa5\x20\x6e\x77\x6f\x6b\x65\x2e"},
            {"ind", "\x53\x65\x6c\x61\x6d\x61\x74\x20\x70\x61\x67\x69\x2c\x20\x61\x70\x61\x20\x6b\x61\x62\x61\x72\x3f"},
            {"gle", "\x54\xc3\xa1\x20\x6d\x6f\x20\x6d\x68\xc3\xa1\x74\x68\x61\x69\x72\x20\x61\x67\x20\x69\x74\x68\x65\x20\xc3\xba\x6c\x6c\x20\x73\x61\x20\x67\x68\x61\x69\x72\x64\xc3\xad\x6e\x2e"},
            {"ita", "\x50\x65\x72\x63\x68\xc3\xa9\x20\xc3\xa8\x20\x63\x6f\x73\xc3\xac\x20\x63\x69\x74\x74\xc3\xa0\x3f"},
            {"kor", "\x41\x6e\x6e\x79\x65\x6f\x6e\x67\x68\x61\x73\x65\x79\x6f\x2c\x20\x6a\x61\x6c\x20\x6a\x69\x6e\x61\x65\x73\x65\x79\x6f\x3f"},
            {"kmr", "\x45\x7a\x20\x6b\x75\x72\x64\xc3\xae\x20\x6d\x65\x20\xc3\xbb\x20\x6a\x69\x20\xc3\xa7\x61\x79\xc3\xaa\x20\x68\x65\x7a\x20\x64\x69\x6b\x69\x6d\x2c\x20\x6e\x65\x20\x6a\x69\x20\xc5\x9f\x65\x72\xc3\xae\x2e"},
            {"lat", "\x56\xc4\x93\x6e\xc4\xab\x2c\x20\x76\xc4\xab\x64\xc4\xab\x2c\x20\x76\xc4\xab\x63\xc4\xab"},
            {"lit", "\xc4\x96\x6a\x61\x75\x20\x70\x72\x69\x65\x20\xc4\x85\xc5\xbe\x75\x6f\x6c\x6f\x20\x73\x75\x20\xc5\xab\x6b\x69\x6e\x69\x6e\x6b\x75\x2e"},
            {"ltz", "\x4c\xc3\xab\x74\x7a\x65\x62\x75\x65\x72\x67\x20\x61\x73\x73\x20\x65\x20\x73\x63\x68\xc3\xa9\x69\x6e\x74\x20\x4c\x61\x6e\x64\x2e"},
            {"mly", "\x53\x65\x6c\x61\x6d\x61\x74\x20\x70\x61\x67\x69\x2c\x20\x61\x70\x61\x20\x6b\x68\x61\x62\x61\x72\x3f"},
            {"mlt", "\xc4\xa0\x6f\x72\xc4\xa1\x20\x6a\x69\x65\x6b\x6f\x6c\x20\xc4\x8b\x65\x72\x61\x73\x61\x2c\x20\x75\x20\xc5\xbc\x6d\x69\x65\x6e\x20\x68\x75\x77\x61\x20\x73\x61\x62\x69\xc4\xa7\x2e"},
            {"cmn", "\x57\xc7\x92\x20\x68\xc4\x9b\x6e\x20\x78\xc7\x90\x68\x75\xc4\x81\x6e\x20\x7a\x68\xc3\xa8\x67\x65\x20\x64\xc3\xac\x66\xc4\x81\x6e\x67\x2e"},
            {"mri", "\x4b\x65\x69\x20\x74\x65\x20\x70\x61\x69\x20\x74\x65\x20\x72\xc4\x81\x2c\x20\x65\x20\x68\x6f\x61\x20\x6d\xc4\x81\x2e"},
            {"cnr", "\xc5\x9a\x65\x76\x65\x72\x20\x69\x20\xc5\xba\x65\x6e\x69\x63\x61\x20\x73\x75\x20\xc5\x9b\x75\x74\x72\x61\x2e"},
            {"nor", "\x48\xc3\xa5\x70\x65\x72\x20\x64\x75\x20\x66\xc3\xa5\x72\x20\x65\x6e\x20\x66\x69\x6e\x20\x64\x61\x67\x20\x70\xc3\xa5\x20\xc3\xb8\x79\x61\x2c\x20\x6b\x6a\xc3\xa6\x72\x65\x20\x76\x65\x6e\x6e\x2e"},
            {"pol", "\x44\x7a\x69\xc4\x99\x6b\x75\x6a\xc4\x99\x2c\x20\x6d\xc3\xb3\x6a\x20\x77\x75\x6a\x65\x6b\x20\x6d\x61\x20\xc5\x82\x61\x64\x6e\x79\x20\x64\x6f\x6d\x2e\x20\xc4\x86\x6d\x61\x20\x69\x20\xc5\xba\x72\x65\x62\x69\xc4\x99\x20\xc5\x9b\x70\x69\xc4\x85\x2c\x20\x61\x20\xc5\x82\xc4\x85\x6b\x61\x20\x70\x61\x63\x68\x6e\x69\x65\x20\x72\xc3\xb3\xc5\xbc\xc4\x85\x2e"},
            {"por", "\x4f\x20\x69\x72\x6d\xc3\xa3\x6f\x20\x63\x6f\x6d\x65\x75\x20\x70\xc3\xa3\x6f\x20\x63\x6f\x6d\x20\x6d\x61\xc3\xa7\xc3\xa3\x2e"},
            {"ron", "\x43\xc3\xa2\x69\x6e\x65\x6c\x65\x20\x6d\x65\x75\x20\x61\x6c\x65\x61\x72\x67\xc4\x83\x20\xc3\xae\x6e\x20\x67\x72\xc4\x83\x64\x69\x6e\xc4\x83\x2e"},
            {"gla", "\x43\x68\xc3\xac\x20\x6d\x69\x20\x62\xc3\xa0\x74\x61\x20\xc3\xb9\x72\x20\x61\x67\x75\x73\x20\x74\x68\x61\x20\x65\x20\x6d\x61\x74\x68\x2e"},
            {"srp", "\xc4\x86\x61\x6f\x2c\x20\xc4\x90\x6f\x72\xc4\x91\x65\x20\x76\x6f\x6c\x69\x20\xc4\x8d\x6f\x6b\x6f\x6c\x61\x64\x75\x20\x69\x20\xc4\x8d\x61\x6a\x2e"},
            {"svk", "\x4d\xc3\xb4\x6a\x20\x70\x72\x69\x61\x74\x65\xc4\xbe\x20\x6d\xc3\xa1\x20\x6e\x6f\x76\xc3\xbd\x20\x64\x6f\x6d\x20\x76\x20\x6d\x65\x73\x74\x65\x2e"},
            {"slv", "\xc5\xa0\x6c\x61\x20\x73\x65\x6d\x20\x76\x20\x4c\x6a\x75\x62\x6c\x6a\x61\x6e\x6f\x20\x76\x69\x64\x65\x74\x69\x20\xc4\x8d\x75\x64\x6f\x76\x69\x74\x6f\x20\x72\x65\x6b\x6f\x2e"},
            {"som", "\x4e\x61\x62\x61\x64\x2c\x20\x73\x69\x64\x65\x65\x20\x74\x61\x68\x61\x79\x3f"},
            {"spa", "\x45\x6c\x20\x6e\x69\xc3\xb1\x6f\x20\x63\x6f\x6d\x69\xc3\xb3\x20\x70\x69\xc3\xb1\x61\x20\x65\x6e\x20\x45\x73\x70\x61\xc3\xb1\x61\x2e"},
            {"swa", "\x48\x61\x62\x61\x72\x69\x2c\x20\x75\x6e\x61\x65\x6e\x64\x65\x6c\x65\x61\x6a\x65\x3f"},
            {"swe", "\xc3\x85\x73\x61\x20\xc3\xa4\x74\x65\x72\x20\xc3\xa4\x70\x70\x6c\x65\x6e\x20\x6f\x63\x68\x20\x64\x72\x69\x63\x6b\x65\x72\x20\xc3\xb6\x6c\x2e"},
            {"tgl", "\x50\x69\x6e\x75\x6e\x74\x61\x68\x61\x6e\x20\x6e\x61\x6d\x69\x6e\x20\x61\x6e\x67\x20\x50\x65\xc3\xb1\x61\x66\x72\x61\x6e\x63\x69\x61\x2e"},
            {"tur", "\x47\xc3\xbc\x7a\x65\x6c\x20\x62\x69\x72\x20\x67\xc3\xbc\x6e\x2c\x20\x64\x65\xc4\x9f\x69\x6c\x20\x6d\x69\x3f\x20\x49\xc5\x9f\xc4\xb1\x6b\x20\xc3\xa7\x6f\x6b\x20\x70\x61\x72\x6c\x61\x6b\x2e"},
            {"cym", "\x4d\x61\x65\x27\x72\x20\x74\xc5\xb7\x27\x6e\x20\x68\x61\x72\x64\x64\x20\x61\x27\x72\x20\x63\xc5\xb5\x6e\x20\x79\x6e\x20\x68\x61\x70\x75\x73\x2e"},
            {"yor", "\xe1\xba\xb8\x20\xe1\xb9\xa3\x65\x75\x6e\x2c\x20\xe1\xbb\x8d\x6d\xe1\xbb\x8d\x20\x6d\x69\x20\x64\xc3\xa1\x72\x61\x2e"},
            {"zul", "\x53\x61\x77\x75\x62\x6f\x6e\x61\x2c\x20\x75\x6e\x6a\x61\x6e\x69\x3f"},
        };
        int unstable = 0, leftover = 0;
        for (const Row& r : rows) {
            const std::string folded = transform(r.plain);
            const std::string human = untransform(folded);
            if (transform(human) != folded) {
                ++unstable;
                check(false, std::string("transformer not stable for ") + r.lang + ": " + folded +
                                 " -> " + human + " -> " + transform(human));
            }
            // Nothing readable should still be carrying a mark digit. A
            // code left behind means the decoder walked past one, which
            // the stability check alone can miss when both directions are
            // wrong in the same way.
            for (std::size_t i = 1; i < human.size(); ++i) {
                const char prev = human[i - 1];
                const bool prev_is_letter = (prev >= 'a' && prev <= 'z') ||
                                            (prev >= 'A' && prev <= 'Z');
                if (prev_is_letter && human[i] >= '0' && human[i] <= '9') {
                    ++leftover;
                    check(false, std::string("undecoded mark digit left in ") + r.lang + ": " +
                                     human);
                    break;
                }
            }
        }
        check(unstable == 0, "48 real sentences fold, unfold and fold again to the same string");
        check(leftover == 0, "no mark digit survives into readable text");

        // Four of those sentences with the answer written out by hand, so
        // that the stability check above is anchored to something a person
        // read rather than only to itself. Punctuation is gone because the
        // machine cannot carry it, and the case is back because the new
        // scheme carries it.
        struct Fixed { const char* folded_from; const char* human; };
        static const Fixed fixed[] = {
            // Albanian: e with diaeresis, c with cedilla.
            {"\x55\x6e\xc3\xab\x20\x66\x6c\x61\x73\x20\x73\x68\x71\x69\x70\x2e",
             "\x55\x6e\xc3\xab\x20\x66\x6c\x61\x73\x20\x73\x68\x71\x69\x70"},
            // German: three umlauts and a sharp s, which is the one that
            // does not come back.
            {"\x47\x72\x6f\xc3\x9f\x65\x73\x20\x4b\xc3\xa4\x73\x65\x62\x72\xc3\xb6\x74\x63\x68\x65\x6e\x21",
             "\x47\x72\x6f\x73\x73\x65\x73\x20\x4b\xc3\xa4\x73\x65\x62\x72\xc3\xb6\x74\x63\x68\x65\x6e"},
            // Polish: l with stroke, and a with ogonek.
            {"\x4c\xc4\x85\x6b\x61\x20\x69\x20\xc5\x82\xc4\x85\x6b\x61",
             "\x4c\xc4\x85\x6b\x61\x20\x69\x20\xc5\x82\xc4\x85\x6b\x61"},
            // Romanian: the comma below that the old scheme deleted.
            {"\xc8\x98\x69\x20\xc8\x9b\x61\x72\x61",
             "\xc8\x98\x69\x20\xc8\x9b\x61\x72\x61"},
        };
        for (const Fixed& f : fixed) {
            const std::string got = untransform(transform(f.folded_from));
            check(got == f.human, std::string("hand checked round trip -> ") + got);
        }
    }


    // 10. The universal transformer, which is what replaced the 48
    //     per-language tables. Two halves: the classic cases every
    //      language actually needs, and a set built to break it.
    //
    //      It takes no language. That is the point of it, and it is also
    //      what makes this shorter than the old per-language tests: one answer per
    //      character, rather than one per language per character.
    {
        // -- the classic cases -------------------------------------------
        struct Row { const char* plain; const char* folded; };
        static const Row rows[] = {
            {"The naive cafe owner smiled.", "t0he naive cafe owner smiled"},
            {"El nino comio pina en Espana.", "e0l nino comio pina en e0spana"},

            // The nine shape families, one letter each, lowercase so the
            // case code stays out of the way.
            {"\xc4\x81", "a1"},                  // macron
            {"\xc3\xa1", "a2"},                  // acute
            {"\xc7\x8e", "a3"},                  // caron
            {"\xc3\xa0", "a4"},                  // grave
            {"\xc3\xa2", "a5"},                  // circumflex
            {"\xc3\xa3", "a6"},                  // tilde
            {"\xc4\x83", "a7"},                  // breve
            {"\xc4\x8b", "c8"},                  // dot above
            {"\xc5\xaf", "u9"},                  // ring above

            // The second digit of a family: the variations.
            {"\xc5\x91", "o21"},                 // double acute
            {"\xc3\xa7", "c73"},                 // cedilla
            {"\xc4\x85", "a74"},                 // ogonek
            {"\xe1\xba\xb9", "e81"},             // dot below
            {"\xc3\xbc", "u82"},                 // diaeresis
            {"\xc3\xb8", "o12"},                 // stroke, which does not decompose
            {"\xc5\x82", "l12"},                 // the same stroke on another letter

            // Romanian comma-below, which the old scheme deleted outright
            // and this one carries.
            {"\xc8\x99", "s42"},
            {"\xc8\x9b", "t42"},

            // Two marks on one letter. Mandarin needs it, and Vietnamese
            // needs it on a letter the old scheme could not write at all.
            {"\xc7\x96", "u82/1"},               // u diaeresis + macron
            {"\xc7\x9c", "u82/4"},               // u diaeresis + grave
            {"\xe1\xba\xbf", "e5/2"},            // e circumflex + acute
            {"\xe1\xbb\x9d", "o75/4"},           // o horn + grave
            {"\xe1\xbb\xb1", "u75/81"},          // u horn + dot below

            // Case, which the old scheme threw away entirely. The code
            // always sits directly behind the letter it belongs to.
            {"A", "a0"},
            {"\xc3\x81", "a0/2"},                // A with acute
            {"\xc7\x95", "u0/82/1"},             // capital U diaeresis + macron
            {"INOP", "i0n0o0p0"},

            // Literal digits, and the double slash that keeps them apart
            // from a mark.
            {"Room A2", "r0oom a0//2"},
            {"Chateau Latour 1964", "c0hateau l0atour 1964"},
            {"m\xc3\xa1" "5", "ma2//5"},         // a mark and then a number
            {"a12", "a//12"},
            {"1964 and 1918", "1964 and 1918"},
        };
        int classic_failed = 0;
        for (const Row& r : rows) {
            const std::string got = transform(r.plain);
            if (got != r.folded) {
                ++classic_failed;
                check(false, std::string("transform(") + r.plain + ") -> " + got + ", wanted " +
                                 r.folded);
            }
        }
        check(classic_failed == 0,
              "transformer classic cases: " + std::to_string(sizeof(rows) / sizeof(rows[0])) +
                  " rows");

        // -- the adversarial cases ---------------------------------------
        //
        // Nothing here is a plausible message. That is the point of it.
        struct Bad { const char* in; const char* out; const char* why; };
        static const Bad bad[] = {
            {"", "", "empty input"},
            {"   ", "", "nothing but spaces"},
            {"\xc3\xa9", "e2", "one accented letter and nothing else"},
            {"a\xc3\xa9", "ae2", "a bare letter touching an accented one"},
            {"\xc3\xa9" "9", "e2//9", "a number right behind a mark"},
            {"9\xc3\xa9", "9e2", "a number right in front of one"},
            {"a//b", "ab", "a double slash typed by hand is dropped as punctuation"},
            {"a/b", "ab", "and so is a single one"},
            {"!@#$%^&*()", "", "punctuation only, all of it dropped"},
            {"\xe4\xbd\xa0\xe5\xa5\xbd", "", "chinese characters, none of them latin"},
            {"\xd0\xbf\xd1\x80\xd0\xb8", "", "cyrillic, likewise"},
            {"a\xf0\x9f\x98\x80" "b", "ab", "an emoji between two letters"},
            {"a\xc3", "a", "a lead byte with its tail cut off"},
            {"a\xbf" "b", "ab", "a continuation byte with no lead"},
            {"\xff\xfe", "", "bytes that are not utf-8 at all"},
            {"a\n\nb", "a b", "two newlines read as one space"},
            {"a \t b", "a b", "mixed whitespace reads as one space"},
            {" a ", "a", "leading and trailing space trimmed"},
            {"\xc3\x9f", "ss", "sharp s spelled out, and lost"},
            {"\xc3\xa6", "ae", "ae spelled out, and lost"},
            {"\xc4\xb1", "i", "turkish dotless i, flattened to a plain i"},
            {"\xc4\xb0", "i0/8", "turkish capital i with a dot survives whole"},
            {"I", "i0", "a plain capital I is not the turkish one"},
        };
        int bad_failed = 0;
        for (const Bad& b : bad) {
            const std::string got = transform(b.in);
            if (got != b.out) {
                ++bad_failed;
                check(false, std::string("transform edge case (") + b.why + ") -> " + got +
                                 ", wanted " + b.out);
            }
        }
        check(bad_failed == 0, "transformer adversarial cases: " +
                                   std::to_string(sizeof(bad) / sizeof(bad[0])) + " rows");
        check(untransform("A0") == "A0", "unknown uppercase transformer code stays unchanged");
        check(transform("\xc1\x81\xc0\xb5").empty(),
              "overlong UTF-8 letters and digits are rejected");

        check(validate_transform_input("Lowercase 42\n").status ==
                  TransformValidationStatus::Valid,
              "supported transformer input validates explicitly");
        check(validate_transform_input("word!").status ==
                  TransformValidationStatus::UnsupportedInput,
              "unsupported transformer input is reported explicitly");
        check(validate_transform_input("\xf0\x28\x8c\x28").status ==
                  TransformValidationStatus::InvalidUtf8,
              "invalid UTF-8 is distinct from unsupported input");
        check(validate_transformed_data("lowercase a0 a2 o75/4 a//42").status ==
                  TransformValidationStatus::Valid,
              "combined transformed data validates explicitly");
        check(validate_transformed_data("A0").status ==
                  TransformValidationStatus::LiteralContent,
              "literal text resembling a control code stays distinct");
        const std::vector<std::string> malformed_transform_data = {
            "a/", "a/2", "a//", "a//x", "a2/", "a2/x", "a31", "a999", "a2/999"};
        bool malformed_reported = true;
        for (const std::string& encoded : malformed_transform_data)
            malformed_reported = malformed_reported &&
                                 validate_transformed_data(encoded).status ==
                                     TransformValidationStatus::MalformedData &&
                                 untransform(encoded) == encoded;
        check(malformed_reported,
              "malformed transformer markers are reported and preserved without repair");

        bool declared_codes_valid = true;
        for (const std::pair<char, std::string>& code : declared_codes()) {
            const std::string encoded = std::string(1, code.first) + code.second;
            declared_codes_valid = declared_codes_valid &&
                                   validate_transformed_data(encoded).status ==
                                       TransformValidationStatus::Valid &&
                                   transform(untransform(encoded)) == encoded;
        }
        check(declared_codes_valid,
              "every declared transformer code validates and round trips");

        bool finite_code_space_valid = true;
        const std::vector<std::pair<char, std::string>> codes = declared_codes();
        for (char base = 'a'; base <= 'z'; ++base) {
            for (int value = 0; value <= 99; ++value) {
                const std::string digits = std::to_string(value);
                bool declared = digits == "0";
                for (const std::pair<char, std::string>& code : codes)
                    if (code.first == base && code.second == digits) declared = true;
                const TransformValidationStatus actual =
                    validate_transformed_data(std::string(1, base) + digits).status;
                finite_code_space_valid = finite_code_space_valid &&
                                          actual == (declared ? TransformValidationStatus::Valid
                                                             : TransformValidationStatus::MalformedData);
            }
        }
        check(finite_code_space_valid,
              "finite single modifier transformer space rejects every undeclared code");
        const std::string deterministic_sample = "\xc3\x81rvíztűrő 1964";
        check(transform(deterministic_sample) == transform(deterministic_sample),
              "repeated transformer output is deterministic");

        // -- the round trip ----------------------------------------------
        //
        // Every character the table carries, taken back the other way.
        // Exhaustive rather than a sample: this is the check that says the
        // scheme is reversible at all.
        int trip_checked = 0, trip_failed = 0;
        for (unsigned cp = 0x00C0; cp <= 0x1EFF; ++cp) {
            std::string ch;
            if (cp < 0x800) {
                ch += static_cast<char>(0xC0 | (cp >> 6));
                ch += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                ch += static_cast<char>(0xE0 | (cp >> 12));
                ch += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                ch += static_cast<char>(0x80 | (cp & 0x3F));
            }
            const std::string folded = transform(ch);
            if (folded.empty()) continue;  // not a character this scheme carries
            // The four spelled out ones are known not to come back. Naming
            // them here is what stops this test quietly growing more.
            if (folded == "ss" || folded == "s0s0" || folded == "ae" || folded == "a0e0" ||
                folded == "oe" || folded == "o0e0" || folded == "i")
                continue;
            ++trip_checked;
            const std::string back = untransform(folded);
            if (back != ch || transform(back) != folded) {
                ++trip_failed;
                if (trip_failed <= 5)
                    check(false, "transformer round trip failed at codepoint " +
                                     std::to_string(cp) + ", folded " + folded);
            }
        }
        check(trip_failed == 0, "transformer round trip: every one of " +
                                    std::to_string(trip_checked) +
                                    " carried characters comes back whole");

        // Whole sentences, which are the only thing that exercises the
        // case code, the marks, the double slash and the spaces at once.
        auto trip = [&](const std::string& text, const std::string& expected) {
            const std::string folded = transform(text);
            const std::string back = untransform(folded);
            check(back == expected, "sentence round trip: " + text + " -> " + folded + " -> " +
                                        back);
        };
        trip("The naive cafe owner smiled.", "The naive cafe owner smiled");
        trip("Room A2 costs 1964 crowns.", "Room A2 costs 1964 crowns");
        trip("\xc4\x90or\xc4\x91" "e voli \xc4\x8dokoladu.",
             "\xc4\x90or\xc4\x91" "e voli \xc4\x8dokoladu");
        trip("Ti\xe1\xba\xbfng Vi\xe1\xbb\x87t", "Ti\xe1\xba\xbfng Vi\xe1\xbb\x87t");
    }

    // 11. One test per guard in DESIGN section 6. The governing rule is
    //     that for every "do not remove" there must be a check that fails
    //     when it is removed, and every check below has been verified by
    //     deleting or inverting the thing it protects and watching it fail
    //     by name. verify_legacy_integrity() was already the right shape;
    //     nothing else copied it until now.
    {
        const Suite& s38 = suite("38");
        Alphabet a38(s38.alphabet);
        const std::string scratch = "inop_selftest_scratch.json";

        // G1. The entropy check runs BEFORE generation, not after. A batch
        //     drawn from a dead source looks exactly like a good one, so
        //     checking afterwards is checking nothing. Observed through a
        //     counter rather than a mock: if the call is deleted from
        //     build_wheel_batch(), this check fails.
        unsigned long before = entropy_check_count();
        WheelBatch good = build_wheel_batch(s38, true, 4, "SELFTESTG", 900, 2);
        check(entropy_check_count() > before,
              "entropy_self_check runs before wheel generation");
        check(wheel_batch_problem(good, s38).empty(),
              "a freshly generated batch passes its own validation");
        before = entropy_check_count();
        GeneratedSettings setup_generated = random_setup_settings(s38);
        check(entropy_check_count() > before && setup_generated.rotors.size() >= 5,
              "entropy_self_check runs before GUI setup generation");

        // G2. A batch whose wirings are not all distinct is refused. This
        //     is the guard that DESIGN section 6 asserted was working while
        //     it was not (register item 35).
        WheelBatch dup;
        dup.rotors = true;
        dup.wirings = {good.wirings[0], good.wirings[0]};
        dup.wheels = {good.wheels[0], good.wheels[0]};
        check(!wheel_batch_problem(dup, s38).empty(),
              "a batch with two identical wirings is refused");

        // G3. A pure rotation of the alphabet is a Caesar wheel. Refused at
        //     generation, so it never reaches disk to be refused on load.
        std::string rot38;
        for (size_t i = 1; i <= s38.alphabet.size(); ++i)
            rot38 += s38.alphabet[i % s38.alphabet.size()];
        WheelBatch caesar;
        caesar.rotors = true;
        caesar.wirings = {rot38};
        caesar.wheels = {GeneratedWheel{"SELFTESTROT", rot38, ""}};
        check(!wheel_batch_problem(caesar, s38).empty(),
              "a batch containing a pure rotation is refused at generation");

        // G4. Nothing is written before validation, append case: the target
        //     must be byte-identical after a refusal.
        const std::string sentinel = "# sentinel, must survive a refused batch\n";
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << sentinel;
        }
        auto slurp = [](const std::string& p) {
            std::ifstream f(p, std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        };
        // Snapshot the bytes as they actually landed rather than comparing
        // against the string that was written: an ofstream in text mode
        // translates newlines on Windows, and a byte-identical check that
        // trips over that is testing the platform, not the guard.
        const std::string baseline = slurp(scratch);
        std::string err;
        check(!write_wheel_batch(scratch, dup, s38, /*append=*/true, &err),
              "write_wheel_batch refuses an invalid batch in append mode");
        check(slurp(scratch) == baseline,
              "append refusal leaves the existing file byte-identical");

        // G5. Overwrite is the worse case and the one that was not filed:
        //     std::ios::trunc empties the target at open, so validating
        //     after opening destroys the good wheels being replaced.
        check(!write_wheel_batch(scratch, dup, s38, /*append=*/false, &err),
              "write_wheel_batch refuses an invalid batch in overwrite mode");
        check(slurp(scratch) == baseline,
              "overwrite refusal leaves the existing file byte-identical");

        // G6. Positive control. Without this, G4 and G5 would still pass if
        //     write_wheel_batch refused everything unconditionally.
        check(write_wheel_batch(scratch, good, s38, /*append=*/false, &err),
              "write_wheel_batch does write a valid batch");
        check(slurp(scratch) != baseline && slurp(scratch).find(good.wirings[0]) != std::string::npos,
              "the written file actually contains the batch");
        std::vector<std::string> check_problems;
        check(validate_wheel_file(scratch, &check_problems) && check_problems.empty(),
              "read-only wheel check accepts a valid generated catalogue");
        const std::string first_serialization = slurp(scratch);
        check(write_wheel_batch(scratch, good, s38, /*append=*/false, &err) &&
                  slurp(scratch) == first_serialization,
              "the same wheel batch serializes reproducibly");
        check(!write_wheel_batch(scratch, good, s38, /*append=*/true, &err) &&
                  slurp(scratch) == first_serialization,
              "append rejects duplicate wheel IDs without changing the catalogue");

        WheelBatch bad_id = good;
        bad_id.wheels[0].name.clear();
        check(!wheel_batch_problem(bad_id, s38).empty(),
              "generated wheel batches require every ID");
        WheelBatch bad_notch = good;
        bad_notch.wheels[0].notches = "aa";
        check(!wheel_batch_problem(bad_notch, s38).empty(),
              "generated wheel batches reject duplicate notch symbols");

        // G7. The wheel file is validated on LOAD, not only on generation,
        //     so a bad file left on disk cannot poison a later session.
        //     Written as JSON by hand rather than through write_wheel_batch,
        //     because write_wheel_batch refuses both of these at generation:
        //     the point is to prove the load path checks them too.
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << R"({"rotors":[{"name":"SELFTESTD1","wiring":")" << good.wirings[0]
              << R"("},{"name":"SELFTESTD2","wiring":")" << good.wirings[0] << R"("}]})"
              << "\n";
        }
        std::vector<std::string> problems;
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a file whose rotors share a wiring");
        problems.clear();
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << R"({"rotors":[{"name":"SELFTESTROT","wiring":")" << rot38 << R"("}]})" << "\n";
        }
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a file containing a rotation");

        // G7b. A file that is not JSON at all is refused rather than
        //      silently loading nothing, so a 2.2.x wheel file left in
        //      place under its new name cannot pass as an empty pool.
        problems.clear();
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << "rotor SELFTESTOLD " << good.wirings[0] << "\n";
        }
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a file that is not JSON");
        problems.clear();
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << R"({"reflectors":[{"name":"SELFTESTFIXED","wiring":")"
              << s38.alphabet << R"("}]})" << "\n";
        }
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a reflector with fixed points");
        problems.clear();
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << R"({"reflectors":[{"name":"SELFTESTNONINV","wiring":")"
              << rot38 << R"("}]})" << "\n";
        }
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a reflector that is not an involution");
        problems.clear();
        check(!validate_wheel_document(
                  R"({"rotors":[{"name":"DUP","wiring":"abcdefghijklmnopqrstuvwxyz0123456789#/"},{"name":"DUP","wiring":"abcdefghijklmnopqrstuvwxyz0123456789#/"}]})",
                  &problems) && !problems.empty(),
              "wheel validation rejects duplicate IDs before map insertion");
        problems.clear();
        check(!validate_wheel_document(R"({"rotors":[{"wiring":"missing-id"}]})", &problems) &&
                  !problems.empty(),
              "wheel validation rejects entries with missing IDs");
        problems.clear();
        check(!validate_wheel_document(R"({"rotors":[7]})", &problems) && !problems.empty(),
              "wheel validation rejects malformed catalogue entries");
        problems.clear();
        check(!validate_wheel_document("{}", &problems) && !problems.empty(),
              "wheel validation rejects a missing catalogue");
        std::remove(scratch.c_str());

        // G8. random_notches() clamps to a floor of one. A notch-less rotor
        //     never advances the rotor to its left, which collapses the
        //     period exactly as a fixed rotor would.
        check(random_notches(a38, 0).size() == 1, "random_notches(0) still yields one notch");
        check(random_notches(a38, -3).size() == 1, "random_notches(-3) still yields one notch");

        // G9. apply_suite_lock() forces the Legacy restrictions off. Also
        //     covered indirectly by verify_legacy_integrity(); stated here
        //     directly so deleting one line of it is visible.
        {
            PipelineConfig cfg;
            cfg.double_pass = true;
            cfg.padding = true;
            cfg.moving_reflector = true;
            bool locked = apply_suite_lock(cfg, /*historic_lock=*/true, 5);
            check(locked && !cfg.double_pass && !cfg.padding && !cfg.moving_reflector,
                  "apply_suite_lock forces double pass, padding and moving reflector off");
            PipelineConfig open_cfg;
            open_cfg.double_pass = true;
            open_cfg.padding = true;
            open_cfg.moving_reflector = true;
            bool unlocked = apply_suite_lock(open_cfg, /*historic_lock=*/false, 16);
            check(!unlocked && open_cfg.double_pass && open_cfg.padding &&
                      open_cfg.moving_reflector,
                  "apply_suite_lock leaves a non-historic suite alone");
        }

        // G10. The notch rule has one home. kMaxNotchesAnySuite is a
        //      compile-time constant an interface can size an array with,
        //      so the suite table is checked against it here rather than
        //      trusted to stay in step. It did not stay in step once: the
        //      GUI carried three notch boxes for a cap that had moved to
        //      five, and silently truncated generated rotors to fit.
        {
            int widest = 0;
            for (const auto& [code, su] : suites())
                if (su.max_notches > widest) widest = su.max_notches;
            check(widest == kMaxNotchesAnySuite,
                  "kMaxNotchesAnySuite (" + std::to_string(kMaxNotchesAnySuite) +
                      ") equals the widest suite max_notches (" + std::to_string(widest) + ")");
            check(duplicate_notch_symbols({"abc", "def"}).empty(),
                  "distinct notches across rotors are accepted");
            check(duplicate_notch_symbols({"abc", "cde"}) == "c",
                  "a notch symbol shared by two rotors is reported");
            check(duplicate_notch_symbols({"aa"}) == "a",
                  "a notch symbol repeated within one rotor is reported");
            check(duplicate_notch_symbols({"abc", "cda", "e"}) == "ac",
                  "every clashing symbol is reported, not just the first");
        }

        // G11. Key material must not be tracked by git. A ratchet, not a
        //      remedy: nothing is tracked today and this is what keeps it
        //      that way.
        std::vector<std::string> tracked = tracked_key_material();
        check(tracked.empty(),
              tracked.empty() ? "no key material is tracked by git"
                              : "KEY MATERIAL IS TRACKED BY GIT: " + tracked.front());
    }

    // 12. The interface layer. Everything above this point is the logic
    //     layer, which was the whole of the suite until now. These are the
    //     pieces sitting between that logic and a terminal or a window,
    //     and they were covered by nothing.
    {
        // Batch splitting. A message may span lines; a blank line, or a
        // line holding nothing but spaces, is what ends one.
        check(split_batch_messages("").empty(), "empty batch input gives no messages");

        std::vector<std::string> two = split_batch_messages("first\n\nsecond\n");
        check(two.size() == 2 && two[0] == "first" && two[1] == "second",
              "a blank line separates two batch messages");

        std::vector<std::string> joined = split_batch_messages("line one\nline two\n\nnext\n");
        check(joined.size() == 2 && joined[0] == "line one line two" && joined[1] == "next",
              "a batch message spanning lines is joined with a space");

        std::vector<std::string> padded = split_batch_messages("\n\n\nonly\n\n\n");
        check(padded.size() == 1 && padded[0] == "only",
              "blank runs at either end produce no empty batch messages");

        std::vector<std::string> spaced = split_batch_messages("one\n   \ntwo\n");
        check(spaced.size() == 2, "a line of nothing but spaces ends a batch message");
    }
    {
        const std::string path = "inop_selftest_batch.txt";
        { std::ofstream f(path, std::ios::binary); f << "one\n\ntwo\n"; }
        std::string raw, err;
        const bool ok = read_batch_file(path, raw, &err);
        std::remove(path.c_str());
        check(ok && raw == "one\n\ntwo\n", "a batch file reads back whole");

        std::string gone, gone_err;
        check(!read_batch_file("inop_selftest_no_batch.txt", gone, &gone_err) && !gone_err.empty(),
              "a batch file that is not there is refused, with a reason");
    }
    {
        // The cap is answered from the file size before a byte is read, so
        // an oversized file can never be half processed.
        const std::string path = "inop_selftest_big.txt";
        {
            std::ofstream f(path, std::ios::binary);
            f << std::string(MAX_BATCH_FILE_BYTES + 1, 'a');
        }
        std::string raw, err;
        const bool ok = read_batch_file(path, raw, &err);
        std::remove(path.c_str());
        check(!ok && !err.empty() && raw.empty(),
              "a batch file over the floppy cap is refused before it is read");
    }
    {
        Settings valid;
        valid.suite_code = "38";
        valid.rotors = {"R1", "R2", "R3", "R4", "R5"};
        valid.reflector = "D";
        valid.rings = {1, 2, 3, 4, 5};
        valid.notches = {"a", "b", "c", "d", "e"};
        valid.plugs = {"fg"};
        valid.master_key = "abcde0";
        std::string err;
        check(validate_settings(valid, &err), "complete valid settings are accepted");

        Settings changed = valid;
        changed.rings[0] = 0;
        check(!validate_settings(changed, &err), "ring zero is rejected before machine construction");
        changed = valid;
        changed.rotors[1] = changed.rotors[0];
        check(!validate_settings(changed, &err), "duplicate rotors are rejected before machine construction");
        changed = valid;
        changed.rotors[0] = "MISSING";
        check(!validate_settings(changed, &err), "unavailable rotors are rejected before machine construction");
        changed = valid;
        changed.reflector = "MISSING";
        check(!validate_settings(changed, &err), "unavailable reflectors are rejected before machine construction");
        changed = valid;
        changed.notches[1] = "a";
        check(!validate_settings(changed, &err), "repeated notch symbols are rejected before machine construction");
        changed = valid;
        changed.master_key.back() = '!';
        check(!validate_settings(changed, &err), "master key symbols outside the alphabet are rejected");
    }
    {
        const std::string path = "inop_selftest_bad_settings.json";
        {
            std::ofstream f(path, std::ios::binary);
            f << R"({"suite_code":"38","reflector":"D","master_key":"abcde0","rotors":[{"name":"R1","ring":"1x","notches":"a"}],"plugboard":[]})";
        }
        Settings unchanged;
        unchanged.suite_code = "sentinel";
        std::string err;
        const bool loaded = load_settings(unchanged, path, &err);
        std::remove(path.c_str());
        check(!loaded && unchanged.suite_code == "sentinel" && !err.empty(),
              "malformed JSON ring is rejected without changing active settings");
    }
    {
        // A settings file has to come back as what went into it. Generated
        // rather than hand written, so this covers whatever a real
        // configuration carries rather than whatever was easy to type.
        const Suite& s38 = suite("38");
        GeneratedSettings g = random_settings(s38, 5, 3, 1);
        Settings wrote;
        wrote.suite_code = g.suite_code;
        wrote.rotors = g.rotors;
        wrote.reflector = g.reflector;
        wrote.rings = g.rings;
        wrote.notches = g.notches;
        wrote.plugs = g.plugs;
        wrote.master_key = g.master_key;

        const std::string path = "inop_selftest_settings.json";
        const bool saved = save_settings(wrote, path);
        Settings read;
        std::string err;
        const bool loaded = load_settings(read, path, &err);
        std::remove(path.c_str());
        check(saved && loaded && read.suite_code == wrote.suite_code &&
                  read.rotors == wrote.rotors && read.reflector == wrote.reflector &&
                  read.rings == wrote.rings && read.notches == wrote.notches &&
                  read.plugs == wrote.plugs && read.master_key == wrote.master_key,
              "a settings file survives a save and a load unchanged");
    }
    {
        // Key sheet indexing. Entry n has to be entry n, and an index past
        // the end has to be refused rather than clamped to the last one.
        const std::string path = "inop_selftest_sheet.json";
        const Suite& s38 = suite("38");
        std::string first, err;
        const bool wrote = write_key_sheet(path, s38, 3, 2, 1, false, 5, &first, &err);
        const int n = count_keysheet_entries(path);

        Settings one, three, past;
        std::string e1, e3, ep;
        const bool got1 = load_keysheet_entry(path, 1, one, &e1);
        const bool got3 = load_keysheet_entry(path, 3, three, &e3);
        const bool got_past = load_keysheet_entry(path, 4, past, &ep);
        std::vector<KeySheetEntry> loaded_entries;
        std::string sheet_error;
        const bool loaded_once = load_keysheet(path, loaded_entries, &sheet_error);
        std::remove(path.c_str());

        check(wrote && n == 3, "a written key sheet counts its own entries");
        check(got1 && got3 && one.master_key != three.master_key,
              "key sheet entry one and entry three are different entries");
        check(!got_past && !ep.empty(), "a key sheet index past the end is refused");
        check(loaded_once && loaded_entries.size() == 3 && loaded_entries[0].valid &&
                  loaded_entries[2].valid,
              "a complete key sheet is parsed and validated in one operation");
    }
    {
        // The 2.2.x plain text settings file has to survive the move to
        // JSON. An operator upgrading has one on disk and nothing else.
        const std::string txt = "inop_selftest_old.settings";
        const std::string json = "inop_selftest_new.json";
        const Suite& s38 = suite("38");
        GeneratedSettings g = random_settings(s38, 5, 2, 1);
        {
            std::ofstream f(txt, std::ios::binary);
            f << settings_to_text(g);
        }

        const bool migrated = migrate_settings_from_text(txt, json);
        Settings read;
        std::string err;
        const bool loaded = migrated && load_settings(read, json, &err);
        std::remove(txt.c_str());
        std::remove(json.c_str());
        check(migrated && loaded && read.master_key == g.master_key &&
                  read.rotors == g.rotors && read.reflector == g.reflector,
              "an old plain text settings file migrates to JSON intact");
    }

    // 13. Whatever the GUI can be asked without opening a window. Silent
    //     in a build with no GUI compiled into it, because none of the
    //     files those checks cover are there to pass or fail.
    gui_self_test(check);

    rule();
    if (failures == 0) std::cout << GREEN << "all checks passed" << RST << "\n";
    else std::cout << RED << failures << " check(s) failed" << RST << "\n";
    return failures == 0 ? 0 : 1;
}

// ── batch processing ────────────────────────────────────────────────────
//
// Every message gets its own Machine — either the same indexed keysheet
// entry reused for all of them, or the next entry in file order for each
// one. `cfg` (double pass / padding / moving reflector) is the operator
// procedure choice made at session start and is reused across the batch;
// only the rotor/reflector/rings/notches/key vary per message.
void run_batch_mode(const PipelineConfig& cfg) {
    rule("batch");
    std::string src = ask("input: (p)aste or (f)ile [p]");
    std::string raw;
    if (!src.empty() && (src[0] == 'f' || src[0] == 'F')) {
        std::string path = ask("file path");
        std::string err;
        if (!read_batch_file(path, raw, &err)) { fail(err); return; }
    } else {
        std::cout << DIM << "  paste messages, a blank line between each; a line with :end finishes"
                  << RST << "\n";
        std::string line, all;
        while (std::getline(std::cin, line)) {
            std::string t = line;
            size_t a = t.find_first_not_of(" \t\r\n");
            t = a == std::string::npos ? "" : t.substr(a, t.find_last_not_of(" \t\r\n") - a + 1);
            if (t == ":end") break;
            all += line;
            all += "\n";
        }
        raw = all;
    }

    auto messages = split_batch_messages(raw);
    if (messages.empty()) { fail("no messages found"); return; }
    std::cout << DIM << "  " << messages.size() << " message(s)" << RST << "\n";

    std::string keysheet = ask("keysheet file [inop_keysheet.json]");
    if (keysheet.empty()) keysheet = "inop_keysheet.json";
    std::vector<KeySheetEntry> key_entries;
    std::string keysheet_error;
    if (!load_keysheet(keysheet, key_entries, &keysheet_error)) { fail(keysheet_error); return; }
    int entries = static_cast<int>(key_entries.size());
    if (entries == 0) { fail("no entries found in " + keysheet); return; }
    std::cout << DIM << "  " << entries << " config(s) available in " << keysheet << RST << "\n";

    std::string mode = ask("config: (a) one index for every message, or (s)equential through the file [a]");
    bool sequential = !mode.empty() && (mode[0] == 's' || mode[0] == 'S');

    int fixed_index = 1;
    if (!sequential) {
        while (true) {
            std::string s = ask("index (1-" + std::to_string(entries) + ")");
            try {
                int v = std::stoi(s);
                if (v >= 1 && v <= entries) { fixed_index = v; break; }
            } catch (...) {}
            fail("need a number 1-" + std::to_string(entries));
        }
    } else if (static_cast<int>(messages.size()) > entries) {
        std::cout << DIM << "  only " << entries << " config(s) available — the remaining "
                  << (messages.size() - static_cast<size_t>(entries))
                  << " message(s) will not be processed" << RST << "\n";
    }

    size_t n = sequential ? std::min(messages.size(), static_cast<size_t>(entries)) : messages.size();
    std::string last_lang = "eng";
    size_t processed = 0;

    // Fixed-index mode uses the exact same config for every message in the
    // batch, so the keysheet entry, Machine, and Pipeline are all built
    // once here rather than rebuilt from scratch (and the file reopened and
    // rescanned) on every single iteration — Pipeline::run_pass already
    // rewinds the Machine before each encipher, so one instance is safe to
    // reuse across repeated encrypt()/decrypt() calls.
    Settings fixed_settings;
    std::optional<Machine> fixed_machine;
    std::optional<Pipeline> fixed_pipe;
    if (!sequential) {
        const KeySheetEntry& entry = key_entries[static_cast<size_t>(fixed_index - 1)];
        if (!entry.valid) { fail(entry.error); return; }
        fixed_settings = entry.settings;
        try {
            fixed_machine.emplace(build_machine(fixed_settings));
            fixed_pipe.emplace(*fixed_machine, cfg);
        } catch (const std::exception& e) {
            fail(e.what());
            return;
        }
    }

    for (size_t i = 0; i < n; ++i) {
        int idx = sequential ? static_cast<int>(i) + 1 : fixed_index;
        try {
            Settings s;
            std::optional<Machine> seq_machine;
            std::optional<Pipeline> seq_pipe;
            Pipeline* pipe_ptr;
            if (sequential) {
                const KeySheetEntry& entry = key_entries[i];
                if (!entry.valid) { fail(entry.error); continue; }
                s = entry.settings;
                seq_machine.emplace(build_machine(s));
                seq_pipe.emplace(*seq_machine, cfg);
                pipe_ptr = &*seq_pipe;
            } else {
                s = fixed_settings;
                pipe_ptr = &*fixed_pipe;
            }
            Pipeline& pipe = *pipe_ptr;
            const Suite& su = suite(s.suite_code);

            std::cout << "\n" << BOLD << "  [" << (i + 1) << "/" << n << "] config #" << idx << RST << "\n";

            std::string to_send = messages[i];
            std::string lang;
            if (!su.historic_lock) {
                const TransformValidationResult validation = validate_transform_input(messages[i]);
                if (!validation.ok()) {
                    fail("message cannot be transformed at byte " +
                         std::to_string(validation.offset + 1) + ": " + validation.reason);
                    continue;
                }
                lang = ask_language(last_lang);
                last_lang = lang;
                to_send = transform(messages[i]);
            }

            Encrypted e = pipe.encrypt(to_send);
            std::string grouped = group(e.ciphertext, su.block);
            if (!lang.empty()) grouped += "  " + lang;
            std::cout << YELL << "  cipher " << RST << grouped << "\n";
            if (!e.marker.empty())
                std::cout << DIM << "  marker " << RST << e.marker << RST << "\n";
            std::string back = pipe.decrypt(e.ciphertext, e.marker);
            std::cout << GREEN << "  check  " << RST << back << "\n";
            if (!lang.empty())
                std::cout << GREEN << "  human  " << RST << untransform(back) << "\n";
            ++processed;
        } catch (const std::exception& ex) { fail(ex.what()); }
    }

    rule();
    std::cout << GREEN << "  batch complete: " << processed << "/" << n << " message(s) processed" << RST
              << "\n";
}

void banner() {
    std::cout << "\n" << BOLD << "INOP" << RST << DIM
              << "  rotor cipher machine  ::  terminal build" << RST << "\n";
}

}  // namespace

// ── main ────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    enable_vt();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-color") g_color = false;
    }
    // Test only. Fills the GUIs input struct from a file instead of from
    // the pointer and the keyboard, so the window can be driven and
    // photographed without anything touching the operators cursor. The
    // window is still real and still visible. See gui_script.hpp.
    std::string gui_script;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--gui-script") gui_script = argv[i + 1];
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--self-test" || a == "-t") { banner(); rule("self-test"); return self_test(); }
        if (a == "--help" || a == "-h") {
            banner();
            std::cout << "\n  inop              interactive session\n"
                      << "  inop --self-test  run correctness and speed checks\n"
                      << "  inop --no-color   plain output, no ANSI\n"
                      << "  inop --gui-script <file>  drive the window from a script\n\n";
            return 0;
        }
    }

    banner();
    std::cout << DIM << "  :q quits at any point" << RST << "\n";

    try {
        entropy_self_check();
    } catch (const std::exception& e) {
        std::cout << RED << "\n  !! " << e.what() << RST << "\n"
                  << "  !! Encryption is still safe to use, but DO NOT generate wheels or\n"
                  << "  !! key sheets on this machine until this is fixed.\n";
    }

    // Any wheels generated by the maintenance menu join the factory set —
    // but only if the file survives validation.
    {
        // A 2.2.x plain-text wheel file is converted on the way past, once,
        // and never deleted. Silent when there is nothing to do.
        std::vector<std::string> mig;
        int converted = migrate_wheels_from_text("inop_wheels.txt", kRotorsPath, kReflectorsPath,
                                                 &mig);
        if (converted > 0)
            std::cout << DIM << "  converted " << converted << " wheels from inop_wheels.txt into "
                      << kRotorsPath << " and " << kReflectorsPath
                      << " (the original is left alone)" << RST << "\n";
        for (size_t i = 0; i < mig.size(); ++i)
            std::cout << RED << "  !! wheel conversion: " << mig[i] << RST << "\n";

        // The settings file converts here too, not down where it is first
        // read: a migration that only runs if the operator happens to pick
        // "run INOP" is a migration that silently has not happened.
        if (migrate_settings_from_text("inop.settings", "inop_settings.json"))
            std::cout << DIM << "  converted inop.settings into inop_settings.json"
                      << " (the original is left alone)" << RST << "\n";

        int extra = 0;
        for (const char* path : {kRotorsPath, kReflectorsPath}) {
            std::vector<std::string> problems;
            extra += load_wheel_file(path, &problems);
            for (size_t i = 0; i < problems.size(); ++i)
                std::cout << RED << "  !! " << path << ": " << problems[i] << RST << "\n";
        }
        if (extra > 0)
            std::cout << DIM << "  loaded " << extra << " wheels" << RST << "\n";
        else
            std::cout << DIM << "  no generated wheels loaded — running on the built-in demo/"
                      << "regression wheels only; generate a batch before sending real traffic"
                      << RST << "\n";
    }

    // Refuse loudly if key material has been committed. This is checked at
    // startup rather than left to review because the failure is permanent:
    // a wheel file or key sheet that reaches a public remote is compromised
    // from that moment, and no later commit takes it back.
    {
        std::vector<std::string> tracked = tracked_key_material();
        if (!tracked.empty()) {
            std::cout << RED << "\n  !! KEY MATERIAL IS TRACKED BY GIT:" << RST << "\n";
            for (const std::string& t : tracked)
                std::cout << RED << "  !!   " << t << RST << "\n";
            std::cout << "  !! These files are the secret. Anything they configured must be\n"
                         "  !! treated as compromised: regenerate the wheels and the key sheet,\n"
                         "  !! and discard traffic enciphered under them.\n";
            return 1;
        }
    }

    verify_legacy_integrity();

    // ── the GUI is what this opens on ─────────────────────────────────
    // Deliberately after the startup checks above, not before: the tracked
    // key material check is a hard refusal, and opening a window first
    // would let an operator work in a compromised setup without ever
    // seeing it. A CLI-only build has no window to open and says nothing,
    // it simply lands on the menu below.
    if (gui_available()) {
        if (run_gui_settings(gui_script) == GuiExit::Quit) {
            std::cout << DIM << "  closed.\n" << RST;
            return 0;
        }
    }

    // ── mode choice ───────────────────────────────────────────────────
    while (true) {
        std::cout << "\n  1  run INOP\n"
                  << "  2  maintenance  " << DIM << "(generate wheels or key sheets)" << RST << "\n"
                  << "  3  GUI  " << DIM
                  << (gui_available() ? "(back to the window)" : "(not built into this binary)")
                  << RST << "\n"
                  << "  4  quit\n";
        std::string c = ask("choice [1]");
        if (c.empty() || c == "1") break;
        if (c == "4") { std::cout << DIM << "  closed.\n" << RST; return 0; }
        if (c == "2") {
            run_generator();
            // a fresh batch may have just been written — pick it up
            int more = load_wheel_file(kRotorsPath, 0) + load_wheel_file(kReflectorsPath, 0);
            if (more > 0)
                std::cout << DIM << "  wheel pool now " << more << " loaded wheels" << RST << "\n";
        }
        // Going back to the window and then closing it with Exit means the
        // same thing there as it does here, so it ends the process rather
        // than dropping the operator back on this menu a second time.
        if (c == "3" && run_gui_settings() == GuiExit::Quit) {
            std::cout << DIM << "  closed.\n" << RST;
            return 0;
        }
    }

    Settings settings;
    const std::string cfg_path = "inop_settings.json";
    bool loaded = false;
    {
        std::ifstream probe(cfg_path);
        if (probe) {
            std::string a = upper(ask("load settings from '" + cfg_path + "'? [Y/n]"));
            if (a.empty() || a == "Y" || a == "YES") {
                std::string err;
                loaded = load_settings(settings, cfg_path, &err);
                if (!loaded) fail(err);
            }
        }
    }
    if (!loaded) settings = collect_settings();
    else if (settings.suite_code == "26") verify_legacy_integrity();

    Machine machine = [&] {
        while (true) {
            try {
                std::string note;
                Machine m = build_machine(settings, &note);
                if (!note.empty()) std::cout << DIM << "  note: " << note << RST << "\n";
                return m;
            } catch (const std::exception& e) {
                fail(e.what());
                std::cout << DIM << "  re-entering settings" << RST << "\n";
                settings = collect_settings();
            }
        }
    }();

    show_settings(settings, machine);

    rule("pipeline");
    const Suite& active = suite(settings.suite_code);
    PipelineConfig cfg;
    if (active.historic_lock) {
        apply_suite_lock(cfg, true, active.block);
        std::cout << "  " << BOLD << active.name << RST
                  << " is a faithful period machine. INOP features are not available.\n"
                  << DIM
                  << "    double pass       locked OFF\n"
                  << "    padding           locked OFF\n"
                  << "    moving reflector  locked OFF\n"
                  << "    output groups     " << active.block << " letters, as transmitted\n"
                  << RST;
    } else {
        cfg.double_pass      = ask_toggle("double pass (encipher, swap halves, encipher)", true);
        cfg.padding          = ask_toggle("padding and cover traffic", true);
        cfg.moving_reflector = ask_toggle("moving reflector", true);
        apply_suite_lock(cfg, false, active.block);
    }
    Pipeline pipe(machine, cfg);

    rule();
    std::cout << DIM << "  commands: :q quit   :s save   :d decrypt   :b batch   :i settings   :? help"
              << RST << "\n\n";

    const Alphabet& active_alpha = machine.alphabet();
    std::string last_lang = "eng";

    while (true) {
        std::string line = ask("message >");
        if (line.empty()) continue;

        // Commands are case-insensitive, and anything starting with ':' that
        // is not recognised gets refused rather than enciphered — a mistyped
        // command should not quietly become a message.
        if (line[0] == ':') {
            std::string cmd = line;
            for (char& ch : cmd) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (cmd == ":q" || cmd == ":quit" || cmd == ":exit") break;
            if (cmd == ":i" || cmd == ":info") { show_settings(settings, machine); continue; }
            if (cmd == ":s" || cmd == ":save") {
                if (save_settings(settings, cfg_path))
                    std::cout << GREEN << "  settings written to " << cfg_path << RST << "\n";
                else
                    fail("cannot write " + cfg_path);
                continue;
            }
            if (cmd == ":d" || cmd == ":decrypt") line = ":d";
            else if (cmd == ":b" || cmd == ":batch") { run_batch_mode(cfg); continue; }
            else if (cmd == ":?" || cmd == ":h" || cmd == ":help") line = ":?";
            else {
                fail("unknown command " + line);
                std::cout << DIM << "  try :? for the list, or drop the colon to send it as a message"
                          << RST << "\n";
                continue;
            }
        }

        if (line == ":?" ) {
            std::cout << DIM << "  type a message to encipher, or:\n"
                      << "    :d   decipher a ciphertext (you will be asked for the marker)\n"
                      << "    :b   batch process pasted or file-based messages\n"
                      << "    :i   show the active settings again\n"
                      << "    :s   save current settings\n"
                      << "    :q   quit\n"
                      << "  (case does not matter, and :quit / :help / :info also work)\n" << RST;
            continue;
        }
        if (line == ":d") {
            std::string raw = ask("  ciphertext");
            auto toks = split(raw);
            std::string lang;
            if (!active.historic_lock && !toks.empty() && toks.back().size() == 3 &&
                is_supported_language(lower(toks.back()))) {
                lang = lower(toks.back());
                toks.pop_back();
            }
            std::string clean;
            for (auto& t : toks) for (char c : active_alpha.fold_case(t)) clean += c;
            std::string bad = foreign_symbol(clean, active_alpha);
            if (!bad.empty()) {
                fail("ciphertext contains " + bad + ", which is not in the " + active.name +
                     " alphabet — nothing was deciphered");
                std::cout << DIM << "  the alphabet is: " << active_alpha.str() << "\n"
                          << "  retype or repaste the line; dropping the symbol would shift "
                             "every position after it" << RST << "\n";
                continue;
            }
            std::string marker;
            if (cfg.padding) {
                marker = active_alpha.fold_case(ask("  marker"));
                std::string bad_marker = foreign_symbol(marker, active_alpha);
                if (!bad_marker.empty()) {
                    fail("marker contains " + bad_marker + ", which is not in the " +
                         active.name + " alphabet — nothing was deciphered");
                    continue;
                }
            }
            try {
                std::string plain = pipe.decrypt(clean, marker);
                std::cout << GREEN << "  plain  " << RST << plain << "\n";
                if (!lang.empty())
                    std::cout << GREEN << "  human  " << RST << untransform(plain)
                              << DIM << "  (" << lang << ")" << RST << "\n";
                std::cout << "\n";
            } catch (const std::exception& e) { fail(e.what()); }
            continue;
        }

        std::string to_send = line;
        std::string lang;
        if (!active.historic_lock) {
            const TransformValidationResult validation = validate_transform_input(line);
            if (!validation.ok()) {
                fail("message cannot be transformed at byte " +
                     std::to_string(validation.offset + 1) + ": " + validation.reason);
                continue;
            }
            lang = ask_language(last_lang);
            last_lang = lang;
            to_send = transform(line);
        }

        try {
            Encrypted e = pipe.encrypt(to_send);
            std::string grouped = group(e.ciphertext, cfg.block);
            if (!lang.empty()) grouped += "  " + lang;
            std::cout << YELL << "  cipher " << RST << grouped << "\n";
            if (!e.marker.empty())
                std::cout << DIM << "  marker " << RST << e.marker
                          << DIM << "   (needed to decipher)" << RST << "\n";
            std::string back = pipe.decrypt(e.ciphertext, e.marker);
            std::cout << GREEN << "  check  " << RST << back << "\n";
            if (!lang.empty())
                std::cout << GREEN << "  human  " << RST << untransform(back) << "\n\n";
            else
                std::cout << "\n";
        } catch (const std::exception& e) { fail(e.what()); }
    }

    std::cout << DIM << "  closed.\n" << RST;
    return 0;
}
