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
    // mangled or dropped before fold_diacritics ever sees them.
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
        for (size_t i = 0; i < s.rotors.size(); ++i) {
            while (true) {
                std::string raw = ask("notches for " + s.rotors[i] + " (1-" +
                                      std::to_string(su.max_notches) + " symbols)");
                if (raw.empty() || raw == "-") {
                    fail("at least one notch is required — a notch-less rotor never advances "
                         "the next rotor, which collapses the machine's period");
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
    {
        Settings s;
        s.suite_code = "38";
        s.rotors = {"R1", "R2", "R3", "R4", "R5"};
        s.reflector = "D";
        s.rings = {1, 1, 1, 1, 1};
        s.notches = {"a", "b", "c", "d", "e"};
        s.master_key = "aaaaaa";

        auto self_hits = [&](bool double_pass) {
            Machine m = build_machine(s);
            PipelineConfig c;
            c.double_pass = double_pass;
            c.padding = false;
            Pipeline p(m, c);
            std::string plain(4000, 'a');
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

    // 9. Numeral-suffix diacritic scheme: every worked example from the
    //    README, fold_diacritics() must match exactly, and folding what
    //    resubstitute() hands back must reproduce the same encoded form
    //    (encode/decode are inverses on these fixtures).
    {
        struct Row { const char* lang; const char* plain; const char* encoded; };
        static const Row rows[] = {
            {"sqi", "Unë flas shqip dhe pi çaj.", "une6 flas shqip dhe pi c8aj"},
            {"eus", "Kaixo, zer moduz zaude?", "kaixo zer moduz zaude"},
            {"bos", "Ćao, Đorđe voli čokoladu i čaj.",
             "c2ao d0ord0e voli c3okoladu i c3aj"},
            {"yue", "Néih hóu, ngóh dōu hóu.", "ne2ih ho2u ngo2h do1u ho2u"},
            {"cat", "El parallel és clar.", "el parallel e2s clar"},
            {"cpf", "Li fò, li gen kè kontan, e li rete bò lanmè a.",
             "li fo4 li gen ke4 kontan e li rete bo4 lanme4 a"},
            {"hrv", "Ćao, Đorđe voli čokoladu i čaj.",
             "c2ao d0ord0e voli c3okoladu i c3aj"},
            {"czr", "Děkuji, můj přítel má nový dům.",
             "de3kuji mu9j pr3i2tel ma2 novy2 du9m"},
            {"dan", "Håper du får en fin dag på øya, kjære venn.",
             "ha9per du fa9r en fin dag pa9 o0ya kjaere venn"},
            {"nld", "De coördinatie was ideeën waard.",
             "de coo6rdinatie was ideee6n waard"},
            {"eng", "The naïve café owner smiled.", "the nai6ve cafe2 owner smiled"},
            {"est", "Söödav õunapuu on hea.", "so6o6dav o7unapuu on hea"},
            {"fin", "Hän on täällä.", "ha6n on ta6a6lla6"},
            {"fra", "Le café est très cher.", "le cafe2 est tre4s cher"},
            {"deu", "Möchten Sie ein großes Käsebrötchen?",
             "mo6chten sie ein gros0es ka6sebro6tchen"},
            {"hin", "Maiṃ Kṛṣṇa kī gītā paṛhtā hūṃ.",
             "maim88 kr88s88n88a ki1 gi1ta1 par88hta1 hu1m88"},
            {"hun", "Ő szereti a gyümölcsöt és a tűzhelyet.",
             "o22 szereti a gyu6mo6lcso6t e2s a tu22zhelyet"},
            {"ibo", "Ị bụ ezigbo ụmụ nwoke.", "i88 bu88 ezigbo u88mu88 nwoke"},
            {"ind", "Selamat pagi, apa kabar?", "selamat pagi apa kabar"},
            {"gle", "Tá mo mháthair ag ithe úll sa ghairdín.",
             "ta2 mo mha2thair ag ithe u2ll sa ghairdi2n"},
            {"ita", "Perché è così città?", "perche2 e4 cosi4 citta4"},
            {"kor", "Annyeonghaseyo, jal jinaeseyo?", "annyeonghaseyo jal jinaeseyo"},
            {"kmr", "Ez kurdî me û ji çayê hez dikim, ne ji şerî.",
             "ez kurdi5 me u5 ji c8aye5 hez dikim ne ji s8eri5"},
            {"lat", "Vēnī, vīdī, vīcī", "ve1ni1 vi1di1 vi1ci1"},
            {"lit", "Ėjau prie ąžuolo su ūkininku.",
             "e33jau prie a8z3uolo su u1kininku"},
            {"ltz", "Lëtzebuerg ass e schéint Land.",
             "le6tzebuerg ass e sche2int land"},
            {"mly", "Selamat pagi, apa khabar?", "selamat pagi apa khabar"},
            {"mlt", "Ġorġ jiekol ċerasa, u żmien huwa sabiħ.",
             "g33org33 jiekol c33erasa u z33mien huwa sabih0"},
            {"cmn", "Wǒ hěn xǐhuān zhège dìfāng.", "wo3 he3n xi3hua1n zhe4ge di4fa1ng"},
            {"mri", "Kei te pai te rā, e hoa mā.", "kei te pai te ra1 e hoa ma1"},
            {"cnr", "Śever i źenica su śutra.", "s2ever i z2enica su s2utra"},
            {"nor", "Håper du får en fin dag på øya, kjære venn.",
             "ha9per du fa9r en fin dag pa9 o0ya kjaere venn"},
            {"pol", "Dziękuję, mój wujek ma ładny dom. Ćma i źrebię śpią, a łąka pachnie różą.",
             "dzie8kuje8 mo2j wujek ma l0adny dom c2ma i z2rebie8 s2pia8 a l0a8ka pachnie ro2z33a8"},
            {"por", "O irmão comeu pão com maçã.", "o irma7o comeu pa7o com mac8a7"},
            {"ron", "Câinele meu aleargă în grădină.",
             "ca5inele meu alearga3 i5n gra3dina3"},
            {"gla", "Chì mi bàta ùr agus tha e math.", "chi4 mi ba4ta u4r agus tha e math"},
            {"srp", "Ćao, Đorđe voli čokoladu i čaj.",
             "c2ao d0ord0e voli c3okoladu i c3aj"},
            {"svk", "Môj priateľ má nový dom v meste.",
             "mo5j priatel3 ma2 novy2 dom v meste"},
            {"slv", "Šla sem v Ljubljano videti čudovito reko.",
             "s3la sem v ljubljano videti c3udovito reko"},
            {"som", "Nabad, sidee tahay?", "nabad sidee tahay"},
            {"spa", "El niño comió piña en España.", "el nin7o comio2 pin7a en espan7a"},
            {"swa", "Habari, unaendeleaje?", "habari unaendeleaje"},
            {"swe", "Åsa äter äpplen och dricker öl.", "a9sa a6ter a6pplen och dricker o6l"},
            {"tgl", "Pinuntahan namin ang Peñafrancia.",
             "pinuntahan namin ang pen7afrancia"},
            {"tur", "Güzel bir gün, değil mi? Işık çok parlak.",
             "gu6zel bir gu6n deg3il mi i0s8i0k c8ok parlak"},
            {"cym", "Mae'r tŷ'n hardd a'r cŵn yn hapus.",
             "maer ty5n hardd ar cw5n yn hapus"},
            {"yor", "Ẹ ṣeun, ọmọ mi dára.", "e88 s88eun o88mo88 mi da2ra"},
            {"zul", "Sawubona, unjani?", "sawubona unjani"},
        };
        for (const auto& r : rows) {
            std::string got = fold_diacritics(r.plain, r.lang);
            check(got == r.encoded, std::string("fold[") + r.lang + "] -> " + got);
            std::string roundtrip = fold_diacritics(resubstitute(r.encoded, r.lang), r.lang);
            check(roundtrip == r.encoded,
                  std::string("resubstitute/fold round trip[") + r.lang + "] -> " + roundtrip);
        }
    }

    // 10. Digit-collision fix: a literal digit right after a letter gets a
    //     separating '/'; a diacritic-introduced digit never does.
    {
        auto encode = [](const std::string& raw, const std::string& lang) {
            return fold_diacritics(mark_literal_digits(raw), lang);
        };
        check(encode("Room A2", "eng") == "room a/2", "digit collision: Room A2");
        check(encode("Château Latour 1964", "fra") == "cha5teau latour 1964",
              "digit collision: Chateau Latour 1964 (no false slash on a bare number)");
        check(encode("Côte d'Ivoire", "fra") == "co5te divoire",
              "digit collision: apostrophe dropped, no space inserted");
    }

    // 11. Exhaustive per-language diacritic round trip: every (letter,
    //     digit) pair each language's resubstitute() table supports, not
    //     just the characters that happened to show up in a worked example
    //     sentence. Catches table gaps a natural-language sentence might
    //     never exercise (this is exactly what caught src/languages.cpp
    //     missing Catalan i6/u6 and Dutch u6 during a manual audit).
    {
        struct DiacRow { const char* lang; char base; int digit; const char* ch; };
        static const DiacRow rows[] = {
            {"sqi",'c',8,"ç"},{"sqi",'e',6,"ë"},

            {"bos",'c',3,"č"},{"bos",'s',3,"š"},{"bos",'z',3,"ž"},{"bos",'c',2,"ć"},{"bos",'d',0,"đ"},

            {"yue",'a',1,"ā"},{"yue",'e',1,"ē"},{"yue",'i',1,"ī"},{"yue",'o',1,"ō"},{"yue",'u',1,"ū"},
            {"yue",'a',2,"á"},{"yue",'e',2,"é"},{"yue",'i',2,"í"},{"yue",'o',2,"ó"},{"yue",'u',2,"ú"},
            {"yue",'a',3,"ǎ"},{"yue",'e',3,"ě"},{"yue",'i',3,"ǐ"},{"yue",'o',3,"ǒ"},{"yue",'u',3,"ǔ"},
            {"yue",'a',4,"à"},{"yue",'e',4,"è"},{"yue",'i',4,"ì"},{"yue",'o',4,"ò"},{"yue",'u',4,"ù"},
            {"yue",'a',5,"â"},{"yue",'e',5,"ê"},{"yue",'i',5,"î"},{"yue",'o',5,"ô"},{"yue",'u',5,"û"},
            {"yue",'a',7,"ã"},{"yue",'e',7,"ẽ"},{"yue",'i',7,"ĩ"},{"yue",'o',7,"õ"},{"yue",'u',7,"ũ"},

            {"cpf",'a',4,"à"},{"cpf",'e',4,"è"},{"cpf",'o',4,"ò"},

            {"hrv",'c',3,"č"},{"hrv",'s',3,"š"},{"hrv",'z',3,"ž"},{"hrv",'c',2,"ć"},{"hrv",'d',0,"đ"},

            {"lat",'a',1,"ā"},{"lat",'e',1,"ē"},{"lat",'i',1,"ī"},{"lat",'o',1,"ō"},{"lat",'u',1,"ū"},

            {"eng",'a',2,"á"},{"eng",'e',2,"é"},{"eng",'i',2,"í"},{"eng",'o',2,"ó"},{"eng",'u',2,"ú"},
            {"eng",'a',4,"à"},{"eng",'e',4,"è"},
            {"eng",'a',5,"â"},{"eng",'e',5,"ê"},{"eng",'i',5,"î"},{"eng",'o',5,"ô"},{"eng",'u',5,"û"},
            {"eng",'a',6,"ä"},{"eng",'e',6,"ë"},{"eng",'i',6,"ï"},{"eng",'o',6,"ö"},{"eng",'u',6,"ü"},
            {"eng",'n',7,"ñ"},

            {"spa",'a',2,"á"},{"spa",'e',2,"é"},{"spa",'i',2,"í"},{"spa",'o',2,"ó"},{"spa",'u',2,"ú"},
            {"spa",'n',7,"ñ"},{"spa",'u',6,"ü"},

            {"cat",'a',4,"à"},{"cat",'e',4,"è"},{"cat",'o',4,"ò"},
            {"cat",'e',2,"é"},{"cat",'i',2,"í"},{"cat",'o',2,"ó"},{"cat",'u',2,"ú"},
            {"cat",'i',6,"ï"},{"cat",'u',6,"ü"},{"cat",'c',8,"ç"},

            {"nld",'e',6,"ë"},{"nld",'i',6,"ï"},{"nld",'o',6,"ö"},{"nld",'u',6,"ü"},{"nld",'e',2,"é"},

            {"dan",'o',0,"ø"},{"dan",'a',9,"å"},

            {"czr",'a',2,"á"},{"czr",'e',2,"é"},{"czr",'i',2,"í"},{"czr",'o',2,"ó"},{"czr",'u',2,"ú"},
            {"czr",'y',2,"ý"},
            {"czr",'e',3,"ě"},{"czr",'s',3,"š"},{"czr",'c',3,"č"},{"czr",'r',3,"ř"},{"czr",'z',3,"ž"},
            {"czr",'d',3,"ď"},{"czr",'t',3,"ť"},{"czr",'n',3,"ň"},{"czr",'u',9,"ů"},

            {"por",'a',7,"ã"},{"por",'o',7,"õ"},
            {"por",'a',2,"á"},{"por",'e',2,"é"},{"por",'i',2,"í"},{"por",'o',2,"ó"},{"por",'u',2,"ú"},
            {"por",'a',5,"â"},{"por",'e',5,"ê"},{"por",'o',5,"ô"},{"por",'a',4,"à"},{"por",'c',8,"ç"},

            {"fra",'e',2,"é"},
            {"fra",'a',4,"à"},{"fra",'e',4,"è"},{"fra",'u',4,"ù"},
            {"fra",'a',5,"â"},{"fra",'e',5,"ê"},{"fra",'i',5,"î"},{"fra",'o',5,"ô"},{"fra",'u',5,"û"},
            {"fra",'e',6,"ë"},{"fra",'i',6,"ï"},{"fra",'u',6,"ü"},{"fra",'y',6,"ÿ"},{"fra",'c',8,"ç"},

            {"ita",'a',4,"à"},{"ita",'e',4,"è"},{"ita",'i',4,"ì"},{"ita",'o',4,"ò"},{"ita",'u',4,"ù"},
            {"ita",'e',2,"é"},

            {"deu",'a',6,"ä"},{"deu",'o',6,"ö"},{"deu",'u',6,"ü"},{"deu",'s',0,"ß"},

            {"hin",'a',1,"ā"},{"hin",'i',1,"ī"},{"hin",'u',1,"ū"},
            {"hin",'t',88,"ṭ"},{"hin",'d',88,"ḍ"},{"hin",'n',88,"ṇ"},{"hin",'s',88,"ṣ"},
            {"hin",'h',88,"ḥ"},{"hin",'m',88,"ṃ"},{"hin",'r',88,"ṛ"},{"hin",'l',88,"ḷ"},
            {"hin",'n',33,"ṅ"},{"hin",'n',7,"ñ"},{"hin",'s',2,"ś"},

            {"hun",'a',2,"á"},{"hun",'e',2,"é"},{"hun",'i',2,"í"},{"hun",'o',2,"ó"},{"hun",'u',2,"ú"},
            {"hun",'o',6,"ö"},{"hun",'u',6,"ü"},{"hun",'o',22,"ő"},{"hun",'u',22,"ű"},

            {"ibo",'i',88,"ị"},{"ibo",'o',88,"ọ"},{"ibo",'u',88,"ụ"},

            {"gle",'a',2,"á"},{"gle",'e',2,"é"},{"gle",'i',2,"í"},{"gle",'o',2,"ó"},{"gle",'u',2,"ú"},

            {"kmr",'c',8,"ç"},{"kmr",'e',5,"ê"},{"kmr",'i',5,"î"},{"kmr",'u',5,"û"},{"kmr",'s',8,"ş"},

            {"lit",'a',8,"ą"},{"lit",'e',8,"ę"},{"lit",'i',8,"į"},{"lit",'u',8,"ų"},
            {"lit",'c',3,"č"},{"lit",'s',3,"š"},{"lit",'z',3,"ž"},{"lit",'u',1,"ū"},{"lit",'e',33,"ė"},

            {"ltz",'e',6,"ë"},{"ltz",'e',2,"é"},

            {"mlt",'c',33,"ċ"},{"mlt",'g',33,"ġ"},{"mlt",'h',0,"ħ"},{"mlt",'z',33,"ż"},

            {"cmn",'a',1,"ā"},{"cmn",'e',1,"ē"},{"cmn",'i',1,"ī"},{"cmn",'o',1,"ō"},{"cmn",'u',1,"ū"},
            {"cmn",'a',2,"á"},{"cmn",'e',2,"é"},{"cmn",'i',2,"í"},{"cmn",'o',2,"ó"},{"cmn",'u',2,"ú"},
            {"cmn",'a',3,"ǎ"},{"cmn",'e',3,"ě"},{"cmn",'i',3,"ǐ"},{"cmn",'o',3,"ǒ"},{"cmn",'u',3,"ǔ"},
            {"cmn",'a',4,"à"},{"cmn",'e',4,"è"},{"cmn",'i',4,"ì"},{"cmn",'o',4,"ò"},{"cmn",'u',4,"ù"},
            {"cmn",'u',6,"ü"},{"cmn",'u',61,"ǖ"},{"cmn",'u',62,"ǘ"},{"cmn",'u',63,"ǚ"},{"cmn",'u',64,"ǜ"},

            {"mri",'a',1,"ā"},{"mri",'e',1,"ē"},{"mri",'i',1,"ī"},{"mri",'o',1,"ō"},{"mri",'u',1,"ū"},

            {"cnr",'c',3,"č"},{"cnr",'s',3,"š"},{"cnr",'z',3,"ž"},{"cnr",'c',2,"ć"},{"cnr",'d',0,"đ"},
            {"cnr",'s',2,"ś"},{"cnr",'z',2,"ź"},

            {"nor",'o',0,"ø"},{"nor",'a',9,"å"},

            {"pol",'a',8,"ą"},{"pol",'e',8,"ę"},
            {"pol",'c',2,"ć"},{"pol",'n',2,"ń"},{"pol",'s',2,"ś"},{"pol",'z',2,"ź"},{"pol",'o',2,"ó"},
            {"pol",'l',0,"ł"},{"pol",'z',33,"ż"},

            {"ron",'a',5,"â"},{"ron",'i',5,"î"},{"ron",'a',3,"ă"},

            {"gla",'a',4,"à"},{"gla",'e',4,"è"},{"gla",'i',4,"ì"},{"gla",'o',4,"ò"},{"gla",'u',4,"ù"},

            {"srp",'c',3,"č"},{"srp",'s',3,"š"},{"srp",'z',3,"ž"},{"srp",'c',2,"ć"},{"srp",'d',0,"đ"},

            {"svk",'a',2,"á"},{"svk",'e',2,"é"},{"svk",'i',2,"í"},{"svk",'o',2,"ó"},{"svk",'u',2,"ú"},
            {"svk",'y',2,"ý"},{"svk",'a',6,"ä"},{"svk",'o',5,"ô"},{"svk",'l',2,"ĺ"},{"svk",'r',2,"ŕ"},
            {"svk",'l',3,"ľ"},{"svk",'n',3,"ň"},{"svk",'s',3,"š"},{"svk",'c',3,"č"},{"svk",'z',3,"ž"},
            {"svk",'t',3,"ť"},{"svk",'d',3,"ď"},

            {"slv",'s',3,"š"},{"slv",'c',3,"č"},{"slv",'z',3,"ž"},

            {"swe",'a',9,"å"},{"swe",'a',6,"ä"},{"swe",'o',6,"ö"},

            {"tgl",'n',7,"ñ"},
            {"tgl",'a',2,"á"},{"tgl",'e',2,"é"},{"tgl",'i',2,"í"},{"tgl",'o',2,"ó"},{"tgl",'u',2,"ú"},

            {"tur",'g',3,"ğ"},{"tur",'o',6,"ö"},{"tur",'u',6,"ü"},{"tur",'i',0,"ı"},{"tur",'c',8,"ç"},
            {"tur",'s',8,"ş"},

            {"cym",'a',5,"â"},{"cym",'e',5,"ê"},{"cym",'i',5,"î"},{"cym",'o',5,"ô"},{"cym",'u',5,"û"},
            {"cym",'w',5,"ŵ"},{"cym",'y',5,"ŷ"},{"cym",'i',6,"ï"},

            {"yor",'e',88,"ẹ"},{"yor",'o',88,"ọ"},{"yor",'s',88,"ṣ"},

            {"fin",'a',6,"ä"},{"fin",'o',6,"ö"},

            {"est",'a',6,"ä"},{"est",'o',6,"ö"},{"est",'u',6,"ü"},{"est",'o',7,"õ"},
            {"est",'s',3,"š"},{"est",'z',3,"ž"},
        };
        int rows_checked = 0, rows_failed = 0;
        for (const auto& r : rows) {
            std::string encoded = std::string(1, r.base) + std::to_string(r.digit);
            std::string human = resubstitute(encoded, r.lang);
            bool decode_ok = human == r.ch;
            std::string back = fold_diacritics(human, r.lang);
            bool encode_ok = back == encoded;
            ++rows_checked;
            if (!decode_ok || !encode_ok) {
                ++rows_failed;
                check(false, std::string(r.lang) + " " + encoded + " <-> " + r.ch +
                                 (decode_ok ? "" : " (decode mismatch: got " + human + ")") +
                                 (encode_ok ? "" : " (re-encode mismatch: got " + back + ")"));
            }
        }
        check(rows_failed == 0, "exhaustive per-language diacritic round trip: " +
                                     std::to_string(rows_checked) + " (letter,digit) pairs across "
                                     "48 languages");

        // Pinyin ü + tone: the one case where two diacritic digits chain on
        // a single letter. Verified against the exact examples from the
        // audit that requested this feature.
        auto check_chain = [&](const std::string& word, const std::string& expected_folded) {
            std::string folded = fold_diacritics(word, "cmn");
            check(folded == expected_folded,
                  "pinyin u-tone chain " + word + " -> " + folded);
            std::string back = fold_diacritics(resubstitute(folded, "cmn"), "cmn");
            check(back == folded, "pinyin u-tone chain round trip " + word);
        };
        check_chain("lǜ", "lu64");
        check_chain("nǚ", "nu63");
        check_chain("lǖ", "lu61");
        check_chain("lǘ", "lu62");
        // No non-Chinese language should ever produce a two-digit chain —
        // ü alone (no tone) must stay a plain single-digit "u6" everywhere
        // else.
        check(fold_diacritics("über", "deu") == "u6ber",
              "German u with diaeresis does not chain (no tone system)");
    }

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

    std::string keysheet = ask("keysheet file [inop_keysheet.txt]");
    if (keysheet.empty()) keysheet = "inop_keysheet.txt";
    int entries = count_keysheet_entries(keysheet);
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
    // reuse across repeated encrypt()/decrypt() calls. Sequential mode
    // genuinely needs a fresh entry per message, so it streams through one
    // open ifstream instead (entries are read in order, so this costs one
    // forward scan total rather than one rescan-from-the-top per entry).
    Settings fixed_settings;
    std::optional<Machine> fixed_machine;
    std::optional<Pipeline> fixed_pipe;
    std::ifstream sequential_stream;
    if (!sequential) {
        std::string err;
        if (!load_keysheet_entry(keysheet, fixed_index, fixed_settings, &err)) { fail(err); return; }
        fixed_machine.emplace(build_machine(fixed_settings));
        fixed_pipe.emplace(*fixed_machine, cfg);
    } else {
        sequential_stream.open(keysheet);
    }

    for (size_t i = 0; i < n; ++i) {
        int idx = sequential ? static_cast<int>(i) + 1 : fixed_index;
        Settings s;
        std::optional<Machine> seq_machine;
        std::optional<Pipeline> seq_pipe;
        Pipeline* pipe_ptr;
        if (sequential) {
            std::string err;
            if (!load_keysheet_entry_from_stream(sequential_stream, idx, s, &err)) { fail(err); continue; }
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
            lang = ask_language(last_lang);
            last_lang = lang;
            to_send = fold_diacritics(mark_literal_digits(messages[i]), lang);
        }

        try {
            Encrypted e = pipe.encrypt(to_send);
            std::string grouped = group(e.ciphertext, su.block);
            if (!lang.empty()) grouped += "  " + lang;
            std::cout << YELL << "  cipher " << RST << grouped << "\n";
            if (!e.marker.empty())
                std::cout << DIM << "  marker " << RST << e.marker << RST << "\n";
            std::string back = pipe.decrypt(e.ciphertext, e.marker);
            std::cout << GREEN << "  check  " << RST << back << "\n";
            if (!lang.empty())
                std::cout << GREEN << "  human  " << RST << resubstitute(back, lang) << "\n";
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
                  << "  3  GUI  " << DIM << "(experimental, opt-in)" << RST << "\n"
                  << "  4  quit\n";
        std::string c = ask("choice [1]");
        if (c.empty() || c == "1") break;
        if (c == "4") { std::cout << DIM << "  closed.\n" << RST; return 0; }
        if (c == "2") {
            run_generator();
            // a fresh batch may have just been written — pick it up
            int more = load_wheel_file("inop_wheels.txt", 0);
            if (more > 0)
                std::cout << DIM << "  wheel pool now " << more << " loaded wheels" << RST << "\n";
        }
        if (c == "3") run_gui_settings();
    }

    Settings settings;
    const std::string cfg_path = "inop.settings";
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
        cfg.double_pass      = ask_toggle("double pass (encipher, reverse, encipher)", true);
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
            std::string marker;
            if (cfg.padding) marker = lower(ask("  marker"));
            try {
                std::string plain = pipe.decrypt(clean, marker);
                std::cout << GREEN << "  plain  " << RST << plain << "\n";
                if (!lang.empty())
                    std::cout << GREEN << "  human  " << RST << resubstitute(plain, lang)
                              << DIM << "  (" << lang << ")" << RST << "\n";
                std::cout << "\n";
            } catch (const std::exception& e) { fail(e.what()); }
            continue;
        }

        std::string to_send = line;
        std::string lang;
        if (!active.historic_lock) {
            lang = ask_language(last_lang);
            last_lang = lang;
            to_send = fold_diacritics(mark_literal_digits(line), lang);
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
                std::cout << GREEN << "  human  " << RST << resubstitute(back, lang) << "\n\n";
            else
                std::cout << "\n";
        } catch (const std::exception& e) { fail(e.what()); }
    }

    std::cout << DIM << "  closed.\n" << RST;
    return 0;
}
