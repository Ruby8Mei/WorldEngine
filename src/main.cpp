// main.cpp — INOP terminal interface
#include <algorithm>
#include <cctype>    // std::toupper, std::isspace
#include <chrono>
#include <cstdlib>   // std::exit
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "generator.hpp"
#include "inop.hpp"
#include "pipeline.hpp"
#include "registry.hpp"
#include "rng.hpp"

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

// ── settings ────────────────────────────────────────────────────────────
struct Settings {
    std::string suite_code = "38";
    std::vector<std::string> rotors;
    std::string reflector;
    std::vector<int> rings;
    std::vector<std::string> notches;  // parallel to rotors
    std::vector<std::string> plugs;
    std::string master_key;
};

void save_settings(const Settings& s, const std::string& path) {
    std::ofstream f(path);
    if (!f) { fail("cannot write " + path); return; }
    f << "suite " << s.suite_code << "\n";
    f << "rotors";     for (auto& r : s.rotors)   f << " " << r; f << "\n";
    f << "reflector "  << s.reflector << "\n";
    f << "rings";      for (int r : s.rings)      f << " " << r; f << "\n";
    f << "notches";    for (auto& n : s.notches)  f << " " << (n.empty() ? "-" : n); f << "\n";
    f << "plugs";      for (auto& p : s.plugs)    f << " " << p; f << "\n";
    f << "key " << s.master_key << "\n";
    std::cout << GREEN << "  settings written to " << path << RST << "\n";
}

bool load_settings(Settings& s, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    Settings out;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string key;
        if (!(is >> key)) continue;
        std::string tok;
        if (key == "suite")          is >> out.suite_code;
        else if (key == "reflector") is >> out.reflector;
        else if (key == "key")       is >> out.master_key;
        else if (key == "rotors")    while (is >> tok) out.rotors.push_back(upper(tok));
        else if (key == "plugs")     while (is >> tok) out.plugs.push_back(upper(tok));
        else if (key == "rings") {
            while (is >> tok) {
                try {
                    out.rings.push_back(std::stoi(tok));
                } catch (const std::exception&) {
                    fail("bad 'rings' value '" + tok + "' in " + path);
                    return false;
                }
            }
        }
        else if (key == "notches")   while (is >> tok) out.notches.push_back(tok == "-" ? "" : upper(tok));
    }

    // The rotor count is derived from the rotors line, not asserted
    // separately — so a mismatched rings/notches/key list must be caught
    // here, clearly, rather than crashing or silently truncating later.
    const size_t count = out.rotors.size();
    if (count == 0) { fail("no rotors listed in " + path); return false; }
    if (!suites().count(out.suite_code)) {
        fail("unknown suite '" + out.suite_code + "' in " + path);
        return false;
    }
    const Suite& su = suite(out.suite_code);
    if (static_cast<int>(count) < su.min_rotors || static_cast<int>(count) > su.max_rotors) {
        fail("rotor count " + std::to_string(count) + " in " + path + " is outside " +
             su.name + "'s range " + std::to_string(su.min_rotors) + "-" +
             std::to_string(su.max_rotors));
        return false;
    }
    if (out.rings.size() != count) {
        fail("rings count (" + std::to_string(out.rings.size()) + ") does not match rotor count (" +
             std::to_string(count) + ") in " + path);
        return false;
    }
    if (!su.notches_are_fixed && out.notches.size() != count) {
        fail("notches count (" + std::to_string(out.notches.size()) + ") does not match rotor count (" +
             std::to_string(count) + ") in " + path);
        return false;
    }
    const size_t need_key = su.historic_lock ? count : count + 1;
    if (out.master_key.size() != need_key &&
        !(su.historic_lock && out.master_key.size() == need_key + 1)) {
        fail("key length (" + std::to_string(out.master_key.size()) + ") does not match rotor count (" +
             std::to_string(count) + ") in " + path);
        return false;
    }

    s = out;
    return true;
}

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
        auto pairs = split(upper(ask("pairs")));
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
        for (size_t i = 0; i < s.rotors.size(); ++i) {
            while (true) {
                std::string n = upper(ask("notches for " + s.rotors[i] + " (0-" +
                                          std::to_string(su.max_notches) + " symbols)"));
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
    std::cout << DIM << "  suggestion (freshly drawn): " << RST << BOLD
              << secure_string(su.alphabet, need) << RST << "\n";
    while (true) {
        std::string k = upper(ask("key"));
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

Machine build_machine(const Settings& s) {
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
            std::cout << DIM << "  note: Legacy reflector is fixed — key symbol '"
                      << key.back() << "' ignored" << RST << "\n";
            key = key.substr(0, want);
        }
        key += alpha.at(0);  // reflector fixed at position 0
    }

    return Machine(alpha, std::move(rotors), make_reflector(s.reflector, alpha),
                   Plugboard(s.plugs, alpha), s.rings, key, su.historic_lock);
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
int self_test() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::cout << (ok ? GREEN : RED) << (ok ? "  ok   " : "  FAIL ") << RST << what << "\n";
        if (!ok) ++failures;
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
            r.set_notches("Q7#", a);
            rs.push_back(std::move(r));
        }
        Machine m(a, std::move(rs), make_reflector("E", a),
                  Plugboard({"AB", "3X", "#/"}, a), {5, 12, 30, 1, 22}, "K3M9QZ", false);
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
        s.notches = {"A", "5", "#", "Z", "/"};
        s.plugs = {"QW", "12"};
        s.master_key = "H4T#0P";
        Machine m = build_machine(s);
        Pipeline p(m, PipelineConfig{});

        std::string msg = "ATTACK AT DAWN / HOLD THE LINE 0800";
        Encrypted e = p.encrypt(msg);
        std::string back = p.decrypt(e.ciphertext, e.marker);
        check(back == "ATTACK AT DAWN / HOLD THE LINE 0800", "pipeline round trip -> " + back);
        check(e.ciphertext.size() % 16 == 0, "ciphertext is block aligned");
    }

    // 4. The double pass removes Enigma's fatal no-self-encipherment property.
    {
        Settings s;
        s.suite_code = "38";
        s.rotors = {"R1", "R2", "R3", "R4", "R5"};
        s.reflector = "D";
        s.rings = {1, 1, 1, 1, 1};
        s.notches = {"A", "B", "C", "D", "E"};
        s.master_key = "AAAAAA";

        auto self_hits = [&](bool double_pass) {
            Machine m = build_machine(s);
            PipelineConfig c;
            c.double_pass = double_pass;
            c.padding = false;
            Pipeline p(m, c);
            std::string plain(4000, 'A');
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
                r.set_notches("AM", a);
                rs.push_back(std::move(r));
            }
            return Machine(a, std::move(rs), make_reflector("D", a), Plugboard({}, a),
                           {1, 2, 3}, "ABCD", legacy_stepping);
        };
        Machine legacy_style = build(true);
        Machine inop38_style = build(false);
        std::string msg(50, 'A');
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
        const std::string r1 = "BXML2UOKH3#46705CYG19ETFPRID8SWQAVNZJ/";
        check(!wiring_is_rotation(r1, ALPHA38), "built-in R1 wiring is accepted, not a rotation");
    }

    // 8. Throughput.
    {
        Alphabet a(ALPHA38);
        std::vector<Rotor> rs;
        for (auto n : {"R1", "R2", "R3", "R4", "R5"}) rs.push_back(make_rotor(n, a));
        for (auto& r : rs) r.set_notches("AM", a);
        Machine m(a, std::move(rs), make_reflector("D", a), Plugboard({}, a), {1, 2, 3, 4, 5}, "ABCDEF", false);
        std::string text(200000, 'A');
        auto t0 = std::chrono::steady_clock::now();
        volatile size_t sink = m.encipher(text).size();
        (void)sink;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << DIM << "  --   " << RST << "throughput: "
                  << static_cast<long>(text.size() / ms / 1000.0) << "M symbols/s\n";
    }

    rule();
    if (failures == 0) std::cout << GREEN << "all checks passed" << RST << "\n";
    else std::cout << RED << failures << " check(s) failed" << RST << "\n";
    return failures == 0 ? 0 : 1;
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
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--self-test" || a == "-t") { banner(); rule("self-test"); return self_test(); }
        if (a == "--help" || a == "-h") {
            banner();
            std::cout << "\n  inop              interactive session\n"
                      << "  inop --self-test  run correctness and speed checks\n"
                      << "  inop --no-color   plain output, no ANSI\n\n";
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
        std::vector<std::string> problems;
        int extra = load_wheel_file("inop_wheels.txt", &problems);
        if (extra > 0)
            std::cout << DIM << "  loaded " << extra << " wheels from inop_wheels.txt" << RST << "\n";
        else
            std::cout << DIM << "  no generated wheels loaded — running on the built-in demo/"
                      << "regression wheels only; generate a batch before sending real traffic"
                      << RST << "\n";
        for (size_t i = 0; i < problems.size(); ++i)
            std::cout << RED << "  !! inop_wheels.txt: " << problems[i] << RST << "\n";
    }

    verify_legacy_integrity();

    // ── mode choice ───────────────────────────────────────────────────
    while (true) {
        std::cout << "\n  1  run INOP\n"
                  << "  2  maintenance  " << DIM << "(generate wheels or key sheets)" << RST << "\n"
                  << "  3  quit\n";
        std::string c = ask("choice [1]");
        if (c.empty() || c == "1") break;
        if (c == "3") { std::cout << DIM << "  closed.\n" << RST; return 0; }
        if (c == "2") {
            run_generator();
            // a fresh batch may have just been written — pick it up
            int more = load_wheel_file("inop_wheels.txt", 0);
            if (more > 0)
                std::cout << DIM << "  wheel pool now " << more << " loaded wheels" << RST << "\n";
        }
    }

    Settings settings;
    const std::string cfg_path = "inop.settings";
    bool loaded = false;
    {
        std::ifstream probe(cfg_path);
        if (probe) {
            std::string a = upper(ask("load settings from '" + cfg_path + "'? [Y/n]"));
            if (a.empty() || a == "Y" || a == "YES") loaded = load_settings(settings, cfg_path);
        }
    }
    if (!loaded) settings = collect_settings();
    else if (settings.suite_code == "26") verify_legacy_integrity();

    Machine machine = [&] {
        while (true) {
            try { return build_machine(settings); }
            catch (const std::exception& e) {
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
        cfg.double_pass      = ask_toggle("double pass (encipher, reverse, encipher)", true);
        cfg.padding          = ask_toggle("padding and cover traffic", true);
        cfg.moving_reflector = ask_toggle("moving reflector", true);
        apply_suite_lock(cfg, false, active.block);
    }
    Pipeline pipe(machine, cfg);

    rule();
    std::cout << DIM << "  commands: :q quit   :s save   :d decrypt   :i settings   :? help" << RST
              << "\n\n";

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
            if (cmd == ":s" || cmd == ":save") { save_settings(settings, cfg_path); continue; }
            if (cmd == ":d" || cmd == ":decrypt") line = ":d";
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
                      << "    :i   show the active settings again\n"
                      << "    :s   save current settings\n"
                      << "    :q   quit\n"
                      << "  (case does not matter, and :quit / :help / :info also work)\n" << RST;
            continue;
        }
        if (line == ":d") {
            std::string ct = upper(ask("  ciphertext"));
            std::string clean;
            for (char c : ct) if (!std::isspace(static_cast<unsigned char>(c))) clean += c;
            std::string marker;
            if (cfg.padding) marker = upper(ask("  marker"));
            try {
                std::cout << GREEN << "  plain  " << RST << pipe.decrypt(clean, marker) << "\n\n";
            } catch (const std::exception& e) { fail(e.what()); }
            continue;
        }

        try {
            Encrypted e = pipe.encrypt(line);
            std::cout << YELL << "  cipher " << RST << group(e.ciphertext, cfg.block) << "\n";
            if (!e.marker.empty())
                std::cout << DIM << "  marker " << RST << e.marker
                          << DIM << "   (needed to decipher)" << RST << "\n";
            std::cout << GREEN << "  check  " << RST << pipe.decrypt(e.ciphertext, e.marker)
                      << "\n\n";
        } catch (const std::exception& e) { fail(e.what()); }
    }

    std::cout << DIM << "  closed.\n" << RST;
    return 0;
}
