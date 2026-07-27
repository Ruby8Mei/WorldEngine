#include "registry.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace inop {
namespace {

struct Wiring {
    std::string wiring;
    std::string notches;
};

const std::map<std::string, Wiring>& rotor_wirings() {
    static const std::map<std::string, Wiring> w = {
        // Legacy — the historic Wehrmacht Enigma wheels, notches and all.
        {"I",    {"EKMFLGDQVZNTOWYHXUSPAIBRCJ", "Q"}},
        {"II",   {"AJDKSIRUXBLHWTMCQGZNPYFVOE", "E"}},
        {"III",  {"BDFHJLCPRTXVZNYEIWGAKMUSQO", "V"}},
        {"IV",   {"ESOVPZJAYQUIRHXLNFTGKDCMWB", "J"}},
        {"V",    {"VZBRGITYUPSDNHLXAWMJQOFECK", "Z"}},
        {"VI",   {"JPGVOUMFYQBENHZRDKASXLICTW", "ZM"}},
        {"VII",  {"NZJHGRCXMYSWBOUFAIVLPEKQDT", "ZM"}},

        // INOP-38 — 38 symbols, notches chosen per message.
        {"R1",   {"BXML2UOKH3#46705CYG19ETFPRID8SWQAVNZJ/", ""}},
        {"R2",   {"1Q27#CPZL3RHV6MKTJUXFBE5O9N0AS4DI/YWG8", ""}},
        {"R3",   {"ZO5RNUBY/K0SMTPAJWCX23EDL8FG9VH4176I#Q", ""}},
        {"R4",   {"6V10Z/8FHED9S73AMRT#KQGJCPLUOY5XIN2BW4", ""}},
        {"R5",   {"MUIC7Y09E/WZ4OHS3T6Q82PV#B5XNFD1AJRLGK", ""}},
        {"R6",   {"U85N9QOGZ6BC4XLS70Y/VWIHF#1MPADK23EJRT", ""}},
        {"R7",   {"5IASENWMJDQK1H38O2#P6T4RZBYX0U9VLCGF7/", ""}},
        {"R8",   {"Q6OLVEN1J8FWH3/7CY#KXAG4I2STBUZD05MP9R", ""}},
        {"R9",   {"TF/VKHQM1LRPX74WDC0BEAS89NY52IJU6GZ#O3", ""}},
        {"R10",  {"BOQEW5NCLHZTR748S2F90UP6#MYVGX/DK1I3AJ", ""}},
    };
    return w;
}

const std::map<std::string, std::string>& reflector_wirings() {
    static const std::map<std::string, std::string> w = {
        {"A", "EJMZALYXVBWFCRQUONTSPIKHGD"},
        {"B", "YRUHQSLDPXNGOKMIEBFZCWVJAT"},
        {"C", "FVPJIAOYEDRZXWGCTKUQSBNMHL"},

        {"D", "QZN6I4W9EY2V7CUTA8/POLG#JB10K5F3DMRHXS"},
        {"E", "RCBYWIPTFU#97X/G1A3HJ5END26QZS8V0M4LKO"},
        {"F", "V62P9W1#4S3MLYQDO8J7XAFUN0ZGCKI/BTREH5"},
        {"G", "#YRLN3ZPUXTD9E4H6C5KI07JBGV21FOSQW/MA8"},
        {"H", "SG#L0/B8X4PDT3WK51AMVUOI62ERZNJQY9H7CF"},
    };
    return w;
}

}  // namespace

const std::map<std::string, Suite>& suites() {
    static const std::map<std::string, Suite> s = {
        {"26", Suite{"26", "Legacy", ALPHA26, 3, 10, 2,
                     {"I", "II", "III", "IV", "V", "VI", "VII"},
                     {"A", "B", "C"},
                     true, true, 5}},
        {"38", Suite{"38", "INOP-38", ALPHA38, 5, 15, 3,
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
}  // namespace

Rotor make_rotor(const std::string& name, const Alphabet& alpha) {
    auto lit = loaded_rotors().find(name);
    if (lit != loaded_rotors().end())
        return Rotor(name, lit->second.wiring, lit->second.notches, alpha);
    auto it = rotor_wirings().find(name);
    if (it == rotor_wirings().end()) throw std::invalid_argument("unknown rotor: " + name);
    return Rotor(name, it->second.wiring, it->second.notches, alpha);
}

Reflector make_reflector(const std::string& name, const Alphabet& alpha) {
    auto lit = loaded_reflectors().find(name);
    if (lit != loaded_reflectors().end()) return Reflector(name, lit->second, alpha);
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
    std::map<char, int> pos;  // position of each symbol in the DECLARED alphabet
    for (int i = 0; i < n; ++i) pos[alphabet[static_cast<size_t>(i)]] = i;
    int shift = (pos[wiring[0]] - 0 + n) % n;
    for (int i = 1; i < n; ++i)
        if ((pos[wiring[static_cast<size_t>(i)]] - i + n) % n != shift) return false;
    return true;
}

int load_wheel_file(const std::string& path, std::vector<std::string>* problems) {
    std::ifstream f(path);
    if (!f) return 0;

    std::map<std::string, Wiring> rot;
    std::map<std::string, std::string> refl;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string kind, name, wiring, notches;
        if (!(is >> kind >> name >> wiring)) continue;
        is >> notches;  // optional, rotors only
        if (kind == "rotor")          rot[name] = Wiring{wiring, notches};
        else if (kind == "reflector") refl[name] = wiring;
    }
    if (rot.empty() && refl.empty()) return 0;

    // ---- validate before anything is trusted -------------------------
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
        }
    }

    if (bad) {
        note(problems, "file rejected — regenerate it, and discard anything enciphered with it");
        return 0;
    }

    for (std::map<std::string, Wiring>::const_iterator it = rot.begin(); it != rot.end(); ++it)
        loaded_rotors()[it->first] = it->second;
    for (std::map<std::string, std::string>::const_iterator it = refl.begin(); it != refl.end(); ++it)
        loaded_reflectors()[it->first] = it->second;
    return static_cast<int>(rot.size() + refl.size());
}

namespace {
// Natural-ish ordering: R2 before R10, and Roman numerals in sequence.
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

std::vector<std::string> collect(const Suite& s, bool rotors) {
    const size_t want = s.alphabet.size();
    std::vector<std::string> out;
    if (rotors) {
        for (const auto& kv : rotor_wirings())
            if (kv.second.wiring.size() == want) out.push_back(kv.first);
        for (const auto& kv : loaded_rotors())
            if (kv.second.wiring.size() == want &&
                std::find(out.begin(), out.end(), kv.first) == out.end())
                out.push_back(kv.first);
    } else {
        for (const auto& kv : reflector_wirings())
            if (kv.second.size() == want) out.push_back(kv.first);
        for (const auto& kv : loaded_reflectors())
            if (kv.second.size() == want &&
                std::find(out.begin(), out.end(), kv.first) == out.end())
                out.push_back(kv.first);
    }
    std::sort(out.begin(), out.end(), name_less);
    return out;
}
}  // namespace

std::vector<std::string> available_rotors(const Suite& s)     { return collect(s, true); }
std::vector<std::string> available_reflectors(const Suite& s) { return collect(s, false); }

bool rotor_exists(const std::string& name, const Suite& s) {
    std::vector<std::string> v = available_rotors(s);
    return std::find(v.begin(), v.end(), name) != v.end();
}
bool reflector_exists(const std::string& name, const Suite& s) {
    std::vector<std::string> v = available_reflectors(s);
    return std::find(v.begin(), v.end(), name) != v.end();
}

}  // namespace inop
