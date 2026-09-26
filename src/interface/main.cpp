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
#include "cli_compat.hpp"
#include "cli_self_test.hpp"
#include "generator.hpp"
#include "gui.hpp"
#include "inop.hpp"
#include "languages.hpp"
#include "pipeline.hpp"
#include "registry.hpp"
#include "rng.hpp"
#include "settings.hpp"
#include "transform.hpp"
#include "cli_text.hpp"
#include "developer_presets.hpp"

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

std::string collect_setup_marker(const Suite& su, bool offer_suggestion) {
    if (su.historic_lock) return {};
    Alphabet alpha(su.alphabet);
    rule("setup marker");
    std::cout << DIM << "  exactly " << kSetupMarkerLength
              << " alphabet symbols, shared with Setup" << RST << "\n";
    if (offer_suggestion) {
        try {
            entropy_self_check();
            std::cout << DIM << "  suggestion (freshly drawn): " << RST << BOLD
                      << secure_string(su.alphabet, kSetupMarkerLength) << RST << "\n";
        } catch (const std::exception& e) {
            std::cout << DIM << "  no marker suggestion: " << e.what() << RST << "\n";
        }
    }
    while (true) {
        std::string marker = alpha.fold_case(ask("marker"));
        if (!setup_marker_valid(marker, alpha)) {
            fail("need exactly " + std::to_string(kSetupMarkerLength) +
                 " symbols from the active alphabet");
            continue;
        }
        const std::string warning = marker_reliability_warning(marker);
        if (!warning.empty()) std::cout << YELL << "  warning: " << warning << RST << "\n";
        return marker;
    }
}

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
    s.marker = collect_setup_marker(su, true);
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
    if (!su.historic_lock) {
        std::cout << "  marker    " << s.marker << "\n";
        const std::string warning = marker_reliability_warning(s.marker);
        if (!warning.empty()) std::cout << YELL << "  warning: " << warning << RST << "\n";
    }
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
        if (entry.marker_missing)
            fixed_settings.marker = collect_setup_marker(suite(fixed_settings.suite_code), false);
        try {
            fixed_machine.emplace(build_machine(fixed_settings));
            PipelineConfig entry_cfg = cfg;
            entry_cfg.marker = fixed_settings.marker;
            fixed_pipe.emplace(*fixed_machine, entry_cfg);
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
                if (entry.marker_missing)
                    s.marker = collect_setup_marker(suite(s.suite_code), false);
                seq_machine.emplace(build_machine(s));
                PipelineConfig entry_cfg = cfg;
                entry_cfg.marker = s.marker;
                seq_pipe.emplace(*seq_machine, entry_cfg);
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
                to_send = prepare_cli_text(messages[i], CliTextProfile::Latin);
            }

            Encrypted e = pipe.encrypt(to_send);
            std::string grouped = group(e.ciphertext, su.block);
            if (!lang.empty()) grouped += "  " + lang;
            std::cout << YELL << "  cipher " << RST << grouped << "\n";
            std::string back = pipe.decrypt(e.ciphertext);
            for (const CliTextLine& row : cli_encipher_lines(
                     back, lang.empty() ? CliTextProfile::Raw : CliTextProfile::Latin))
                std::cout << GREEN << "  " << row.label << RST << row.value << "\n";
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
        if (a == "--self-test" || a == "-t") { banner(); rule("self-test"); return cli_self_test(g_color); }
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
    std::optional<DeveloperSetupPreset> selected_public_preset;
    while (true) {
        std::cout << "\n  1  run INOP\n"
                  << "  2  maintenance  " << DIM << "(generate wheels or key sheets)" << RST << "\n"
                  << "  3  GUI  " << DIM
                  << (gui_available() ? "(back to the window)" : "(not built into this binary)")
                  << RST << "\n"
                  << "  4  quit\n"
                  << "  5  public benchmark setup\n";
        std::string c = ask("choice [1]");
        if (c.empty() || c == "1") break;
        if (c == "4") { std::cout << DIM << "  closed.\n" << RST; return 0; }
        if (c == "5") {
            const auto& presets = developer_setup_presets();
            std::cout << "  Public developer material. Do not use for private traffic.\n";
            for (size_t i = 0; i < presets.size(); ++i)
                std::cout << "  " << i + 1 << "  " << presets[i].name << " - "
                          << presets[i].purpose << "\n";
            const std::string choice = ask("preset number, or Enter to cancel");
            if (choice.size() == 1 && choice[0] >= '1' &&
                choice[0] < static_cast<char>('1' + presets.size())) {
                selected_public_preset = presets[static_cast<size_t>(choice[0] - '1')];
                break;
            }
            continue;
        }
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
    bool loaded_marker_missing = false;
    if (selected_public_preset) {
        settings = selected_public_preset->settings;
        loaded = true;
    } else {
        std::ifstream probe(cfg_path);
        if (probe) {
            std::string a = upper(ask("load settings from '" + cfg_path + "'? [Y/n]"));
            if (a.empty() || a == "Y" || a == "YES") {
                std::string err;
                loaded = load_settings(settings, cfg_path, &err, &loaded_marker_missing);
                if (!loaded) fail(err);
            }
        }
    }
    if (!loaded) settings = collect_settings();
    else if (settings.suite_code == "26") verify_legacy_integrity();
    else if (loaded_marker_missing) {
        std::cout << YELL << "  older settings loaded; complete the Setup marker before use" << RST
                  << "\n";
        settings.marker = collect_setup_marker(suite(settings.suite_code), false);
    }

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
    if (selected_public_preset)
        std::cout << YELL << "  PUBLIC BENCHMARK SETUP: " << selected_public_preset->name
                  << ". Do not use for private traffic.\n" << RST;

    rule("pipeline");
    const Suite& active = suite(settings.suite_code);
    PipelineConfig cfg;
    cfg.marker = settings.marker;
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
        cfg.double_pass = selected_public_preset
            ? selected_public_preset->double_pass
            : ask_toggle("double pass (encipher, swap halves, encipher)", true);
        cfg.padding = selected_public_preset
            ? selected_public_preset->padding
            : ask_toggle("padding and cover traffic", true);
        cfg.moving_reflector = selected_public_preset
            ? selected_public_preset->moving_reflector
            : ask_toggle("moving reflector", true);
        apply_suite_lock(cfg, false, active.block);
    }
    Pipeline pipe(machine, cfg);

    rule();
    std::cout << DIM << "  commands: :q quit   :s save   :d decrypt   :d-old compatibility   :b batch   :i settings   :? help"
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
            const CliCommand command = parse_cli_command(line);
            if (command == CliCommand::Quit) break;
            if (command == CliCommand::Info) { show_settings(settings, machine); continue; }
            if (command == CliCommand::Save) {
                if (settings.public_builtin_preset) {
                    fail("public benchmark setups cannot be saved as normal settings");
                    continue;
                }
                if (save_settings(settings, cfg_path))
                    std::cout << GREEN << "  settings written to " << cfg_path << RST << "\n";
                else
                    fail("cannot write " + cfg_path);
                continue;
            }
            if (command == CliCommand::Decrypt) line = ":d";
            else if (command == CliCommand::DecryptOld) line = ":d-old";
            else if (command == CliCommand::Batch) { run_batch_mode(cfg); continue; }
            else if (command == CliCommand::Help) line = ":?";
            else {
                fail("unknown command " + line);
                std::cout << DIM << "  try :? for the list, or drop the colon to send it as a message"
                          << RST << "\n";
                continue;
            }
        }

        if (line == ":?" ) {
            std::cout << DIM << "  type a message to encipher, or:\n"
                      << "    :d       decipher with the marker in active Setup\n"
                      << "    :d-old   decipher older ciphertext with its separate marker\n"
                      << "    :b   batch process pasted or file-based messages\n"
                      << "    :i   show the active settings again\n"
                      << "    :s   save current settings\n"
                      << "    :q   quit\n"
                      << "  (case does not matter, and :quit / :help / :info also work)\n" << RST;
            continue;
        }
        if (line == ":d" || line == ":d-old") {
            const bool compatibility = line == ":d-old";
            std::string raw = ask("  ciphertext");
            auto toks = split(raw);
            const std::string lang = take_trailing_language_tag(toks, active.historic_lock);
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
            try {
                std::string plain;
                if (compatibility && cfg.padding) {
                    const std::string old_marker = collect_setup_marker(active, false);
                    plain = pipe.decrypt_with_marker(clean, old_marker);
                } else {
                    plain = pipe.decrypt(clean);
                }
                for (const CliTextLine& row : cli_decrypt_lines(
                         plain, lang.empty() ? CliTextProfile::Raw : CliTextProfile::Latin)) {
                    std::cout << GREEN << "  " << row.label << RST << row.value;
                    if (row.label == "human  ") std::cout << DIM << "  (" << lang << ")" << RST;
                    std::cout << "\n";
                }
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
            to_send = prepare_cli_text(line, CliTextProfile::Latin);
        }

        try {
            Encrypted e = pipe.encrypt(to_send);
            std::string grouped = group(e.ciphertext, cfg.block);
            if (!lang.empty()) grouped += "  " + lang;
            std::cout << YELL << "  cipher " << RST << grouped << "\n";
            std::string back = pipe.decrypt(e.ciphertext);
            for (const CliTextLine& row : cli_encipher_lines(
                     back, lang.empty() ? CliTextProfile::Raw : CliTextProfile::Latin))
                std::cout << GREEN << "  " << row.label << RST << row.value << "\n";
            std::cout << "\n";
        } catch (const std::exception& e) { fail(e.what()); }
    }

    std::cout << DIM << "  closed.\n" << RST;
    return 0;
}
