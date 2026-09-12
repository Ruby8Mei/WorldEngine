// bombe_main.cpp — a crib-dragging bombe, and the harness that calibrates it.
//
// OFFLINE ATTACK TOOL. Never linked into the live message pipeline, never
// reads key material from disk, never writes any. It manufactures its own
// target settings, enciphers its own plaintext under them, and then tries
// to recover what it just did.
//
// Why the Legacy machine comes first, and why it is not decoration. A
// negative result from an uncalibrated instrument is worthless: "the bombe
// failed against INOP" and "the bombe is broken" produce identical output.
// The only thing separating them is a demonstrated break of Legacy, a
// machine already known to be breakable. The Legacy run is the control. If
// it ever stops recovering settings, nothing else this program prints
// means anything.
//
// What the instrument is. Given a ciphertext and a crib -- known plaintext
// at a known offset -- it enumerates rotor orders and start positions,
// deciphers under each, and keeps every setting that reproduces the crib.
// That is the brute-force half of the idea. The steckered half (assume a
// plugboard pair, propagate the implications of the crib through the
// wiring, reject on contradiction) is not implemented.
//
//   inop_bombe --self-check
//   inop_bombe --legacy-phase1 [--crib N] [--body N]
//   inop_bombe --inop-ablation [--pool N] [--body N] [--crib N]
//   inop_bombe --notch-sweep
//   inop_bombe --transposition
//   inop_bombe --crash-elimination
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "generator.hpp"
#include "inop.hpp"
#include "pipeline.hpp"
#include "registry.hpp"
#include "rng.hpp"

using namespace inop;

namespace {

// ── the transposition between the two passes ────────────────────────────
enum class Tau { None, Reverse, HalfSwap };

const char* tau_name(Tau t) {
    switch (t) {
        case Tau::None: return "double pass off";
        case Tau::Reverse: return "reversal";
        case Tau::HalfSwap: return "half-swap";
    }
    return "?";
}

void apply_tau(std::string& s, Tau t) {
    if (t == Tau::Reverse) {
        std::reverse(s.begin(), s.end());
    } else if (t == Tau::HalfSwap) {
        const size_t h = s.size() / 2;
        for (size_t i = 0; i < h; ++i) std::swap(s[i], s[i + h]);
    }
}

// The whole-message map with padding off. Mirrors Pipeline::run_double_pass.
// --self-check proves it symbol for symbol against the real Pipeline, so
// the attack is never quietly aimed at a private copy of the cipher that
// behaves differently from the shipped one.
std::string transform(Machine& m, const std::string& body, Tau t) {
    m.rewind();
    std::string s = m.encipher(body);
    if (t != Tau::None) {
        apply_tau(s, t);
        m.rewind();
        s = m.encipher(s);
    }
    return s;
}

// ── wheels ──────────────────────────────────────────────────────────────
// An empty `wiring` means "the factory wheel of this name", so the same
// structure carries both a published catalogue wheel and one regenerated
// this morning that appears in no catalogue at all.
struct Wheel {
    std::string name;
    std::string wiring;
    std::string notches;
};

Rotor make(const Wheel& w, const Alphabet& alpha) {
    if (w.wiring.empty()) {
        Rotor r = make_rotor(w.name, alpha);
        if (!w.notches.empty()) r.set_notches(w.notches, alpha);
        return r;
    }
    return Rotor(w.name, w.wiring, w.notches, alpha);
}

struct Setting {
    std::vector<Wheel> rotors;
    Wheel reflector;
    std::string key;  // one symbol per rotor, plus the reflector orientation
};

Machine build(const Suite& su, const Setting& s, bool legacy_stepping, bool moving_reflector) {
    Alphabet alpha(su.alphabet);
    std::vector<Rotor> rotors;
    rotors.reserve(s.rotors.size());
    for (const Wheel& w : s.rotors) rotors.push_back(make(w, alpha));
    Reflector refl = s.reflector.wiring.empty()
                         ? make_reflector(s.reflector.name, alpha)
                         : Reflector(s.reflector.name, s.reflector.wiring, alpha);
    std::vector<int> rings(s.rotors.size(), 1);
    // An empty pair list, not a default-constructed Plugboard: the default
    // one leaves map_ empty, so map() hands back nullptr and encipher()
    // indexes through it. See the report.
    Plugboard board(std::vector<std::string>{}, alpha);
    Machine m(alpha, std::move(rotors), std::move(refl), std::move(board), rings, s.key,
              legacy_stepping);
    m.set_moving_reflector(moving_reflector);
    return m;
}

// ── the search ──────────────────────────────────────────────────────────
struct SearchSpec {
    const Suite* su = nullptr;
    std::vector<Wheel> pool;    // wheels the attacker is willing to try
    Wheel reflector;            // assumed known
    int rotor_count = 3;
    bool legacy_stepping = false;
    bool moving_reflector = true;
    Tau tau = Tau::None;
    char refl_orientation = 'a';  // handed to the attacker, see the report
};

struct SearchResult {
    long long setups = 0;
    long long survivors = 0;
    bool found_truth = false;
    double seconds = 0.0;
    double first_hit_seconds = -1.0;
    std::vector<std::string> examples;  // first few survivors, for inspection
};

void each_order(const std::vector<Wheel>& pool, int k,
                const std::function<void(const std::vector<Wheel>&)>& fn) {
    std::vector<bool> used(pool.size(), false);
    std::vector<Wheel> current;
    std::function<void(int)> rec = [&](int depth) {
        if (depth == k) {
            fn(current);
            return;
        }
        for (size_t i = 0; i < pool.size(); ++i) {
            if (used[i]) continue;
            used[i] = true;
            current.push_back(pool[i]);
            rec(depth + 1);
            current.pop_back();
            used[i] = false;
        }
    };
    rec(0);
}

bool same_wheels(const std::vector<Wheel>& a, const std::vector<Wheel>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name) return false;
    return true;
}

SearchResult search(const SearchSpec& spec, const std::string& ciphertext, const std::string& crib,
                    size_t crib_offset, const Setting& truth) {
    const Suite& su = *spec.su;
    Alphabet alpha(su.alphabet);
    const int n = alpha.size();
    SearchResult res;
    auto t0 = std::chrono::steady_clock::now();

    // With the double pass off, a candidate is rejected as soon as the crib
    // disagrees, so only the prefix up to the end of the crib is ever
    // deciphered. With it on, ciphertext position i depends on plaintext
    // position tau(i), so there is no prefix to test: the whole message
    // must go through both passes before anything can be compared. That
    // difference in work per candidate is most of what the double pass
    // costs an attacker, and it is why this is measured rather than argued.
    const size_t prefix = crib_offset + crib.size();
    const std::string window =
        spec.tau == Tau::None ? ciphertext.substr(0, std::min(prefix, ciphertext.size()))
                              : ciphertext;

    each_order(spec.pool, spec.rotor_count, [&](const std::vector<Wheel>& order) {
        Setting cand;
        cand.rotors = order;
        cand.reflector = spec.reflector;
        cand.key = std::string(static_cast<size_t>(spec.rotor_count) + 1, alpha.at(0));
        cand.key.back() = spec.refl_orientation;
        Machine m = build(su, cand, spec.legacy_stepping, spec.moving_reflector);

        std::vector<int> odo(static_cast<size_t>(spec.rotor_count), 0);
        while (true) {
            for (int i = 0; i < spec.rotor_count; ++i)
                cand.key[static_cast<size_t>(i)] = alpha.at(odo[static_cast<size_t>(i)]);
            m.set_key(cand.key);
            ++res.setups;

            std::string plain = transform(m, window, spec.tau);
            if (plain.size() >= prefix && plain.compare(crib_offset, crib.size(), crib) == 0) {
                ++res.survivors;
                if (res.first_hit_seconds < 0)
                    res.first_hit_seconds =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                if (same_wheels(order, truth.rotors) && cand.key == truth.key)
                    res.found_truth = true;
                if (res.examples.size() < 4) {
                    std::string e;
                    for (const Wheel& w : order) e += w.name + " ";
                    e += "key " + cand.key;
                    if (same_wheels(order, truth.rotors) && cand.key == truth.key) e += "  <- truth";
                    res.examples.push_back(e);
                }
            }

            int i = spec.rotor_count - 1;
            while (i >= 0 && ++odo[static_cast<size_t>(i)] == n) odo[static_cast<size_t>(i--)] = 0;
            if (i < 0) break;
        }
    });

    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

// ── helpers ─────────────────────────────────────────────────────────────
// Drawn from the alphabet minus SPACE_SUB. preprocess() prunes a literal
// '#' rather than carrying it, so a body containing one would come out of
// Pipeline shorter than it went in and the harness would be comparing two
// different messages. The pipeline draws its markers the same way and for
// the same reason.
std::string filler(const Alphabet& alpha, size_t n) {
    std::string pool = alpha.str();
    pool.erase(std::remove(pool.begin(), pool.end(), SPACE_SUB), pool.end());
    return secure_string(pool, n);
}

std::vector<Wheel> catalogue(const Suite& su, size_t n) {
    std::vector<Wheel> out;
    for (const std::string& name : available_rotors(su)) {
        if (out.size() >= n) break;
        out.push_back(Wheel{name, "", ""});
    }
    return out;
}

// Wheels regenerated this morning: real random permutations that appear in
// no catalogue, which is what daily regeneration actually produces.
std::vector<Wheel> fresh_wheels(const Alphabet& alpha, int count, int notches_per_rotor) {
    std::vector<Wheel> out;
    std::vector<std::string> notches =
        random_variable_notches(alpha, count, notches_per_rotor < 1 ? 1 : notches_per_rotor);
    for (int i = 0; i < count; ++i)
        out.push_back(Wheel{"FRESH" + std::to_string(i + 1), random_rotor_wiring(alpha),
                            notches[static_cast<size_t>(i)]});
    return out;
}

void row(const std::string& label, const SearchResult& r) {
    std::cout << "  " << std::left << std::setw(38) << label << std::right << " setups "
              << std::setw(11) << r.setups << "  survivors " << std::setw(7) << r.survivors
              << "  truth " << (r.found_truth ? "FOUND " : "MISSED") << "  " << std::fixed
              << std::setprecision(2) << std::setw(8) << r.seconds << " s";
    if (r.first_hit_seconds >= 0)
        std::cout << "  first hit " << std::setprecision(2) << r.first_hit_seconds << " s";
    std::cout << "\n";
    // Printed whenever more than one setting survives. A second survivor at
    // a long crib is not chance agreement, and a report that says so has to
    // be able to say what the second one was.
    if (r.survivors > 1)
        for (const std::string& e : r.examples) std::cout << "      survivor: " << e << "\n";
}

// ── modes ───────────────────────────────────────────────────────────────

// The harness enciphers with its own copy of the double pass. If that copy
// ever drifts from Pipeline, every number this program prints is about a
// machine nobody ships. So prove they agree first.
int self_check() {
    const Suite& su = suite("38");
    Alphabet alpha(su.alphabet);
    int failures = 0;

    Setting t;
    t.rotors = catalogue(su, 3);
    t.reflector = Wheel{available_reflectors(su).front(), "", ""};
    t.key = secure_string(alpha.str(), 4);

    std::string body = filler(alpha, 40);

    for (bool dp : {false, true}) {
        Machine m1 = build(su, t, false, true);
        PipelineConfig cfg;
        cfg.padding = false;
        cfg.double_pass = dp;
        cfg.moving_reflector = true;
        Pipeline pipe(m1, cfg);
        std::string via_pipeline = pipe.encrypt(body).ciphertext;

        Machine m2 = build(su, t, false, true);
        std::string via_harness = transform(m2, body, dp ? Tau::HalfSwap : Tau::None);

        bool ok = via_pipeline == via_harness;
        if (!ok) ++failures;
        std::cout << "  " << (ok ? "ok  " : "FAIL") << "  harness matches Pipeline, double pass "
                  << (dp ? "on" : "off") << "\n";
    }

    // The whole-message map must be its own inverse, or the attack is
    // deciphering with something that is not the decipherment.
    for (Tau tau : {Tau::None, Tau::Reverse, Tau::HalfSwap}) {
        Machine m = build(su, t, false, true);
        std::string ct = transform(m, body, tau);
        std::string back = transform(m, ct, tau);
        bool ok = back == body;
        if (!ok) ++failures;
        std::cout << "  " << (ok ? "ok  " : "FAIL") << "  round trip under " << tau_name(tau)
                  << "\n";
    }

    std::cout << (failures ? "  self-check FAILED\n" : "  self-check passed\n");
    return failures ? 1 : 0;
}

int legacy_phase1(size_t crib_len, size_t body_len) {
    const Suite& su = suite("26");
    Alphabet alpha(su.alphabet);

    std::vector<Wheel> pool = catalogue(su, 99);
    std::cout << "\n  Legacy, phase 1, no plugboard. The control.\n"
              << "  catalogue " << pool.size() << " rotors, reflector "
              << available_reflectors(su).front() << ", rings 1 1 1, 3 rotors.\n";

    Setting truth;
    truth.reflector = Wheel{available_reflectors(su).front(), "", ""};
    std::vector<Wheel> shuffled = pool;
    for (size_t i = shuffled.size(); i > 1; --i)
        std::swap(shuffled[i - 1], shuffled[secure_below(static_cast<uint32_t>(i))]);
    truth.rotors.assign(shuffled.begin(), shuffled.begin() + 3);
    // Legacy does not rotate its reflector and is fixed at orientation 0,
    // which is what the settings layer means by a 3-symbol master key.
    truth.key = secure_string(alpha.str(), 3) + alpha.at(0);

    std::string crib = "ATTACKATDAWNSTOPHOLDTHEBRIDGE";
    if (crib.size() > crib_len) crib = crib.substr(0, crib_len);
    std::string plain = crib + filler(alpha, body_len > crib.size() ? body_len - crib.size() : 0);

    Machine m = build(su, truth, /*legacy_stepping=*/true, /*moving_reflector=*/false);
    std::string ct = transform(m, plain, Tau::None);

    std::cout << "  hidden setting: rotors";
    for (const Wheel& w : truth.rotors) std::cout << " " << w.name;
    std::cout << ", key " << truth.key.substr(0, 3) << ", crib " << crib.size()
              << " symbols at offset 0\n\n";

    SearchSpec spec;
    spec.su = &su;
    spec.pool = pool;
    spec.reflector = truth.reflector;
    spec.rotor_count = 3;
    spec.legacy_stepping = true;
    spec.moving_reflector = false;
    spec.tau = Tau::None;
    spec.refl_orientation = alpha.at(0);

    SearchResult r = search(spec, ct, crib, 0, truth);
    row("legacy phase 1", r);
    std::cout << "\n  " << (r.found_truth ? "RECOVERED" : "FAILED TO RECOVER")
              << " the setting the harness enciphered under.\n";
    return r.found_truth ? 0 : 1;
}

int inop_ablation(size_t pool_size, size_t body_len, size_t crib_len) {
    const Suite& su = suite("38");
    Alphabet alpha(su.alphabet);
    std::vector<Wheel> pool = catalogue(su, pool_size);

    std::cout << "\n  INOP-38 ablation, deliberately reduced platform.\n"
              << "  3 rotors (shipped: 5 to 10), catalogue of " << pool.size()
              << " (shipped: regenerated daily), rings all 1 (shipped: random),\n"
              << "  no plugboard (shipped: up to 15 pairs), reflector orientation handed to the "
                 "attacker.\n"
              << "  body " << body_len << " symbols, crib " << crib_len << " at offset 0, padding "
                 "off.\n\n";

    std::string crib = filler(alpha, crib_len);
    std::string plain = crib + filler(alpha, body_len - crib_len);

    struct Cell {
        const char* label;
        bool known;
        Tau tau;
    };
    const Cell cells[] = {
        {"wirings known, double pass off", true, Tau::None},
        {"wirings known, double pass on", true, Tau::HalfSwap},
        {"wirings unknown, double pass off", false, Tau::None},
        {"wirings unknown, double pass on", false, Tau::HalfSwap},
    };

    for (const Cell& c : cells) {
        Setting truth;
        truth.reflector = Wheel{available_reflectors(su).front(), "", ""};
        truth.key = secure_string(alpha.str(), 3) + alpha.at(0);
        if (c.known) {
            std::vector<Wheel> shuffled = pool;
            for (size_t i = shuffled.size(); i > 1; --i)
                std::swap(shuffled[i - 1], shuffled[secure_below(static_cast<uint32_t>(i))]);
            truth.rotors.assign(shuffled.begin(), shuffled.begin() + 3);
        } else {
            // Regenerated this morning. Not in the catalogue the attacker
            // is searching, which is the whole of what daily regeneration
            // does to a bombe.
            truth.rotors = fresh_wheels(alpha, 3, 5);
        }

        Machine m = build(su, truth, false, true);
        std::string ct = transform(m, plain, c.tau);

        SearchSpec spec;
        spec.su = &su;
        spec.pool = pool;
        spec.reflector = truth.reflector;
        spec.rotor_count = 3;
        spec.moving_reflector = true;
        spec.tau = c.tau;
        spec.refl_orientation = alpha.at(0);

        row(c.label, search(spec, ct, crib, 0, truth));
    }
    return 0;
}

int notch_sweep(size_t pool_size, size_t body_len, size_t crib_len) {
    const Suite& su = suite("38");
    Alphabet alpha(su.alphabet);
    std::cout << "\n  Notch density sweep. Everything else fixed: 3 rotors, wirings known,\n"
              << "  double pass off, same crib. If search cost does not rise with notch count,\n"
              << "  the notch argument was wrong and this says so.\n\n";

    std::string crib = filler(alpha, crib_len);
    std::string plain = crib + filler(alpha, body_len - crib_len);

    for (int notches = 1; notches <= 5; ++notches) {
        // The same wheels every time, differing only in notch count, so the
        // sweep varies one thing.
        std::vector<Wheel> pool;
        std::vector<Wheel> base = catalogue(su, pool_size);
        std::vector<std::string> ns = random_variable_notches(alpha, static_cast<int>(base.size()),
                                                              notches);
        for (size_t i = 0; i < base.size(); ++i)
            pool.push_back(Wheel{base[i].name, "", ns[i]});

        Setting truth;
        truth.reflector = Wheel{available_reflectors(su).front(), "", ""};
        truth.key = secure_string(alpha.str(), 3) + alpha.at(0);
        truth.rotors.assign(pool.begin(), pool.begin() + 3);

        Machine m = build(su, truth, false, true);
        std::string ct = transform(m, plain, Tau::None);

        SearchSpec spec;
        spec.su = &su;
        spec.pool = pool;
        spec.reflector = truth.reflector;
        spec.rotor_count = 3;
        spec.moving_reflector = true;
        spec.tau = Tau::None;
        spec.refl_orientation = alpha.at(0);

        row("notches per rotor up to " + std::to_string(notches),
            search(spec, ct, crib, 0, truth));
    }
    return 0;
}

// The cheap half of the classical method, and the half the double pass was
// built to destroy. Before a bombe turns a single rotor it slides the crib
// along the ciphertext and throws out every placement where a symbol would
// have to encipher to itself, which the reflector makes impossible. That
// filter costs nothing and removes a third of the placements.
//
// The double pass makes ciphertext position i depend on plaintext position
// tau(i), so self-encipherment becomes possible and the filter stops being
// sound. This measures both halves of that: how many placements the filter
// removes, and -- the part that matters -- how often it throws away the
// right answer once the double pass is on.
int crash_elimination(size_t pool_size, size_t body_len, size_t crib_len, int trials) {
    const Suite& su = suite("38");
    Alphabet alpha(su.alphabet);
    std::vector<Wheel> pool = catalogue(su, pool_size);

    std::cout << "\n  Crash elimination. " << trials << " trials, body " << body_len
              << ", crib " << crib_len << ".\n"
              << "  A placement crashes when crib[k] equals ciphertext[j+k] for some k, which\n"
              << "  a reflector without fixed points makes impossible -- so a crash proves the\n"
              << "  placement wrong, for free, before any rotor turns.\n\n";

    for (Tau tau : {Tau::None, Tau::HalfSwap}) {
        long long placements = 0, crashed = 0, trials_true_crashed = 0;
        for (int t = 0; t < trials; ++t) {
            Setting truth;
            std::vector<Wheel> shuffled = pool;
            for (size_t i = shuffled.size(); i > 1; --i)
                std::swap(shuffled[i - 1], shuffled[secure_below(static_cast<uint32_t>(i))]);
            truth.rotors.assign(shuffled.begin(), shuffled.begin() + 3);
            truth.reflector = Wheel{available_reflectors(su).front(), "", ""};
            truth.key = secure_string(alpha.str(), 3) + alpha.at(0);

            std::string plain = filler(alpha, body_len);
            Machine m = build(su, truth, false, true);
            std::string ct = transform(m, plain, tau);

            const size_t span = body_len - crib_len;
            size_t true_off = secure_below(static_cast<uint32_t>(span + 1));
            std::string crib = plain.substr(true_off, crib_len);

            for (size_t j = 0; j <= span; ++j) {
                bool crash = false;
                for (size_t k = 0; k < crib_len && !crash; ++k)
                    if (crib[k] == ct[j + k]) crash = true;
                ++placements;
                if (crash) ++crashed;
                if (crash && j == true_off) ++trials_true_crashed;
            }
        }
        std::cout << "  " << std::left << std::setw(20) << tau_name(tau) << std::right
                  << "  placements " << std::setw(8) << placements << "  crashed "
                  << std::setw(8) << crashed << " (" << std::fixed << std::setprecision(1)
                  << 100.0 * static_cast<double>(crashed) / static_cast<double>(placements)
                  << "%)   TRUE placement wrongly discarded " << trials_true_crashed << "/"
                  << trials << " (" << std::setprecision(1)
                  << 100.0 * trials_true_crashed / trials << "%)\n";
    }
    std::cout << "\n  With the double pass off the true placement can never crash. A non-zero\n"
                 "  figure in that row would mean this harness is wrong, not that the machine\n"
                 "  is.\n";
    return 0;
}

int transposition_sweep(size_t pool_size, size_t body_len, size_t crib_len) {
    const Suite& su = suite("38");
    Alphabet alpha(su.alphabet);
    std::cout << "\n  Transposition sweep. Same crib, same wheels, same key.\n"
              << "  If reversal and half-swap cost the same, the half-swap buys only the\n"
              << "  odd-length fixed point, and DESIGN section 5 should not claim more.\n\n";

    std::string crib = filler(alpha, crib_len);
    std::string plain = crib + filler(alpha, body_len - crib_len);
    std::vector<Wheel> pool = catalogue(su, pool_size);

    Setting truth;
    truth.reflector = Wheel{available_reflectors(su).front(), "", ""};
    truth.key = secure_string(alpha.str(), 3) + alpha.at(0);
    truth.rotors.assign(pool.begin(), pool.begin() + 3);

    for (Tau tau : {Tau::None, Tau::Reverse, Tau::HalfSwap}) {
        Machine m = build(su, truth, false, true);
        std::string ct = transform(m, plain, tau);

        SearchSpec spec;
        spec.su = &su;
        spec.pool = pool;
        spec.reflector = truth.reflector;
        spec.rotor_count = 3;
        spec.moving_reflector = true;
        spec.tau = tau;
        spec.refl_orientation = alpha.at(0);

        row(tau_name(tau), search(spec, ct, crib, 0, truth));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Unbuffered: this program is expected to crash while it is being
    // developed, and a buffered line lost at the crash is a line that
    // points at the wrong place.
    std::cout << std::unitbuf;
    std::string mode;
    size_t pool = 6, body = 48, crib = 16;
    bool arguments_ok = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](size_t* out) {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << a << "\n";
                arguments_ok = false;
                return;
            }
            const std::string text = argv[++i];
            size_t consumed = 0;
            try {
                const unsigned long long parsed = std::stoull(text, &consumed);
                if (consumed != text.size() || (!text.empty() && text[0] == '-'))
                    throw std::invalid_argument("invalid numeric value");
                *out = static_cast<size_t>(parsed);
                if (static_cast<unsigned long long>(*out) != parsed)
                    throw std::out_of_range("numeric value is too large");
            } catch (const std::exception&) {
                std::cerr << "invalid value for " << a << ": " << text << "\n";
                arguments_ok = false;
            }
        };
        if (a == "--pool") val(&pool);
        else if (a == "--body") val(&body);
        else if (a == "--crib") val(&crib);
        else if (a.rfind("--", 0) == 0 && mode.empty()) mode = a;
        else {
            std::cerr << "unknown argument: " << a << "\n";
            return 2;
        }
    }
    if (!arguments_ok) return 2;

    const bool needs_pool = mode == "--inop-ablation" || mode == "--notch-sweep" ||
                            mode == "--transposition" || mode == "--crash-elimination";
    if (needs_pool && pool < 3) {
        std::cerr << "bombe: --pool must be at least 3 for this mode\n";
        return 2;
    }
    if (needs_pool && crib > body) {
        std::cerr << "bombe: --crib cannot exceed --body for this mode\n";
        return 2;
    }

    try {
        if (mode == "--self-check") return self_check();
        if (mode == "--legacy-phase1") return legacy_phase1(crib, body);
        if (mode == "--inop-ablation") return inop_ablation(pool, body, crib);
        if (mode == "--notch-sweep") return notch_sweep(pool, body, crib);
        if (mode == "--transposition") return transposition_sweep(pool, body, crib);
        if (mode == "--crash-elimination") return crash_elimination(pool, body, crib, 200);
    } catch (const std::exception& e) {
        std::cerr << "bombe: " << e.what() << "\n";
        return 1;
    }

    std::cerr << "usage: inop_bombe --self-check | --legacy-phase1 | --inop-ablation"
                  " | --notch-sweep | --transposition | --crash-elimination\n"
                 "       [--pool N] [--body N] [--crib N]\n";
    return 2;
}
