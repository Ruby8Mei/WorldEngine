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
        {"I",    {"EKMFLGDQVZNTOWYHXUSPAIBRCJ", "Q"}},
        {"II",   {"AJDKSIRUXBLHWTMCQGZNPYFVOE", "E"}},
        {"III",  {"BDFHJLCPRTXVZNYEIWGAKMUSQO", "V"}},
        {"IV",   {"ESOVPZJAYQUIRHXLNFTGKDCMWB", "J"}},
        {"V",    {"VZBRGITYUPSDNHLXAWMJQOFECK", "Z"}},
        {"VI",   {"JPGVOUMFYQBENHZRDKASXLICTW", "ZM"}},
        {"VII",  {"NZJHGRCXMYSWBOUFAIVLPEKQDT", "ZM"}},

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

}

const std::map<std::string, Suite>& suites() {
    static const std::map<std::string, Suite> s = {
        {"26", Suite{"26", "Legacy", ALPHA26, 3, 3, 10, 2,
                     {"I", "II", "III", "IV", "V", "VI", "VII"},
                     {"A", "B", "C"},
                     true, true, 5}},
        {"38", Suite{"38", "INOP-38", ALPHA38, 5, 10, 15, 3,
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
std::map<std::string, Wiring>& loaded_rotors() {
    static std::map<std::string, Wiring> m;
    return m;
}
std::map<std::string, std::string>& loaded_reflectors() {
    static std::map<std::string, std::string> m;
    return m;
}

int& wheel_generation() {
    static int g = 0;
    return g;
}
}

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

const Suite* suite_for_length(size_t len) {
    for (const auto& kv : suites())
        if (kv.second.alphabet.size() == len) return &kv.second;
    return nullptr;
}

void note(std::vector<std::string>* out, const std::string& msg) {
    if (out) out->push_back(msg);
}
}

bool wiring_is_rotation(const std::string& wiring, const std::string& alphabet) {
    const int n = static_cast<int>(wiring.size());
    if (n != static_cast<int>(alphabet.size()) || n < 2) return n < 2;
    Alphabet alpha(alphabet);
    int shift = (alpha.index(wiring[0]) - 0 + n) % n;
    for (int i = 1; i < n; ++i)
        if ((alpha.index(wiring[static_cast<size_t>(i)]) - i + n) % n != shift) return false;
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
        is >> notches;
        if (kind == "rotor")          rot[name] = Wiring{wiring, notches};
        else if (kind == "reflector") refl[name] = wiring;
    }
    if (rot.empty() && refl.empty()) return 0;

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
    ++wheel_generation();
    return static_cast<int>(rot.size() + refl.size());
}

namespace {
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
}

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

}

