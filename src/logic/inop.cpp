#include "inop.hpp"

#include <algorithm>
#include <cctype>

namespace inop {

Alphabet::Alphabet(std::string symbols)
    : symbols_(std::move(symbols)), size_(static_cast<int>(symbols_.size())), idx_(256, -1),
      uppercase_(symbols_.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos) {
    if (size_ == 0) throw std::invalid_argument("alphabet is empty");
    for (int i = 0; i < size_; ++i) {
        uint8_t c = static_cast<uint8_t>(symbols_[static_cast<size_t>(i)]);
        if (idx_[c] >= 0) throw std::invalid_argument("alphabet has a duplicate symbol");
        idx_[c] = static_cast<int16_t>(i);
    }
}

char Alphabet::fold_case(char c) const {
    return uppercase_ ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                       : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string Alphabet::fold_case(const std::string& s) const {
    std::string out(s);
    for (char& c : out) c = fold_case(c);
    return out;
}

Rotor::Rotor(std::string name, std::string wiring, std::string notches, const Alphabet& alpha)
    : name_(std::move(name)), size_(alpha.size()) {
    if (static_cast<int>(wiring.size()) != size_)
        throw std::invalid_argument("rotor " + name_ + ": wiring length != alphabet length");

    std::string sorted_w = wiring, sorted_a = alpha.str();
    std::sort(sorted_w.begin(), sorted_w.end());
    std::sort(sorted_a.begin(), sorted_a.end());
    if (sorted_w != sorted_a)
        throw std::invalid_argument("rotor " + name_ + ": wiring must be a permutation of the alphabet");

    fwd_.resize(static_cast<size_t>(size_));
    rev_.resize(static_cast<size_t>(size_));
    for (int i = 0; i < size_; ++i)
        fwd_[static_cast<size_t>(i)] = static_cast<uint8_t>(alpha.index(wiring[static_cast<size_t>(i)]));
    for (int i = 0; i < size_; ++i)
        rev_[fwd_[static_cast<size_t>(i)]] = static_cast<uint8_t>(i);

    notch_.assign(static_cast<size_t>(size_), false);
    set_notches(notches, alpha);
    bake();
    refresh();
}

void Rotor::bake() {
    const size_t n = static_cast<size_t>(size_);
    tbl_f_.resize(n * n);
    tbl_b_.resize(n * n);
    for (int off = 0; off < size_; ++off) {
        for (int sig = 0; sig < size_; ++sig) {
            int in = sig + off;
            if (in >= size_) in -= size_;
            int f = fwd_[static_cast<size_t>(in)] - off;
            if (f < 0) f += size_;
            int b = rev_[static_cast<size_t>(in)] - off;
            if (b < 0) b += size_;
            tbl_f_[static_cast<size_t>(off) * n + static_cast<size_t>(sig)] = static_cast<uint8_t>(f);
            tbl_b_[static_cast<size_t>(off) * n + static_cast<size_t>(sig)] = static_cast<uint8_t>(b);
        }
    }
}

void Rotor::set_ring(int ring_1based) {
    ring_ = ((ring_1based - 1) % size_ + size_) % size_;
    refresh();
}

void Rotor::set_notches(const std::string& notches, const Alphabet& alpha) {
    notch_.assign(static_cast<size_t>(size_), false);
    for (char c : notches) {
        if (!alpha.contains(c))
            throw std::invalid_argument("rotor " + name_ + ": notch symbol not in alphabet");
        notch_[static_cast<size_t>(alpha.index(c))] = true;
    }
}

std::string Rotor::notch_str(const Alphabet& alpha) const {
    std::string out;
    for (int i = 0; i < size_; ++i)
        if (notch_[static_cast<size_t>(i)]) out += alpha.at(i);
    return out;
}

Reflector::Reflector(std::string name, std::string wiring, const Alphabet& alpha)
    : name_(std::move(name)), size_(alpha.size()) {
    if (static_cast<int>(wiring.size()) != size_)
        throw std::invalid_argument("reflector " + name_ + ": wiring length != alphabet length");

    std::vector<uint8_t> m(static_cast<size_t>(size_));
    for (int i = 0; i < size_; ++i)
        m[static_cast<size_t>(i)] = static_cast<uint8_t>(alpha.index(wiring[static_cast<size_t>(i)]));

    for (int i = 0; i < size_; ++i) {
        int j = m[static_cast<size_t>(i)];
        if (j == i)
            throw std::invalid_argument("reflector " + name_ + ": has a fixed point");
        if (m[static_cast<size_t>(j)] != i)
            throw std::invalid_argument("reflector " + name_ + ": wiring is not an involution");
    }

    const size_t n = static_cast<size_t>(size_);
    tbl_.resize(n * n);
    for (int off = 0; off < size_; ++off) {
        for (int sig = 0; sig < size_; ++sig) {
            int in = sig + off;
            if (in >= size_) in -= size_;
            int v = m[static_cast<size_t>(in)] - off;
            if (v < 0) v += size_;
            tbl_[static_cast<size_t>(off) * n + static_cast<size_t>(sig)] = static_cast<uint8_t>(v);
        }
    }
}

Plugboard::Plugboard(const std::vector<std::string>& pairs, const Alphabet& alpha)
    : pairs_(pairs) {
    const int size = alpha.size();
    map_.resize(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i) map_[static_cast<size_t>(i)] = static_cast<uint8_t>(i);

    std::vector<bool> used(static_cast<size_t>(size), false);
    for (const std::string& p : pairs) {
        if (p.size() != 2)
            throw std::invalid_argument("plugboard pair '" + p + "' must be exactly 2 symbols");
        char a = p[0], b = p[1];
        if (a == b)
            throw std::invalid_argument("plugboard cannot connect a symbol to itself");
        if (!alpha.contains(a) || !alpha.contains(b))
            throw std::invalid_argument("plugboard pair '" + p + "' uses a symbol outside the alphabet");
        int ia = alpha.index(a), ib = alpha.index(b);
        if (used[static_cast<size_t>(ia)] || used[static_cast<size_t>(ib)])
            throw std::invalid_argument("plugboard pair '" + p + "' reuses an already-patched symbol");
        map_[static_cast<size_t>(ia)] = static_cast<uint8_t>(ib);
        map_[static_cast<size_t>(ib)] = static_cast<uint8_t>(ia);
        used[static_cast<size_t>(ia)] = used[static_cast<size_t>(ib)] = true;
    }
}

Machine::Machine(const Alphabet& alpha, std::vector<Rotor> rotors, Reflector reflector,
                 Plugboard plugboard, const std::vector<int>& rings, const std::string& master_key,
                 bool legacy_stepping)
    : alpha_(alpha),
      rotors_(std::move(rotors)),
      reflector_(std::move(reflector)),
      plugboard_(std::move(plugboard)),
      size_(alpha.size()) {
    if (rotors_.empty()) throw std::invalid_argument("machine needs at least one rotor");
    if (rings.size() != rotors_.size())
        throw std::invalid_argument("ring settings count != rotor count");
    if (master_key.size() != rotors_.size() + 1)
        throw std::invalid_argument("master key must be " + std::to_string(rotors_.size() + 1) +
                                    " symbols (one per rotor, plus one for the reflector)");
    if (legacy_stepping && rotors_.size() != 3)
        throw std::invalid_argument("legacy double-step stepping requires exactly 3 rotors");

    for (size_t i = 0; i < rotors_.size(); ++i) rotors_[i].set_ring(rings[i]);
    legacy_double_step_ = legacy_stepping;
    set_key(master_key);
}

void Machine::set_key(const std::string& master_key) {
    if (master_key.size() != rotors_.size() + 1)
        throw std::invalid_argument("master key length mismatch");
    master_key_ = master_key;
    for (size_t i = 0; i < rotors_.size(); ++i)
        rotors_[i].set_position(alpha_.index(master_key[i]));
    reflector_.rotate_to(master_key.back(), alpha_);
}

void Machine::step_rotors() const {
    if (legacy_double_step_) {
        Rotor& left = rotors_[0];
        Rotor& middle = rotors_[1];
        Rotor& right = rotors_[2];

        bool step_left = middle.on_notch();
        bool step_mid = step_left || right.on_notch();

        right.step();
        if (step_mid) middle.step();
        if (step_left) left.step();
    } else {
        for (size_t i = rotors_.size(); i-- > 0;) {
            if (!rotors_[i].step()) break;
        }
    }
    if (moving_reflector_) reflector_.step();
}

std::string Machine::encipher(const std::string& text) const {
    const int n = static_cast<int>(rotors_.size());
    const uint8_t* pb = plugboard_.map();
    const std::string& sym = alpha_.str();

    std::string out;
    out.resize(text.size());

    for (size_t k = 0; k < text.size(); ++k) {
        step_rotors();

        int sig = pb[alpha_.index_unchecked(text[k])];
        for (int i = n - 1; i >= 0; --i) sig = rotors_[static_cast<size_t>(i)].fwd_table()[sig];
        sig = reflector_.table()[sig];
        for (int i = 0; i < n; ++i) sig = rotors_[static_cast<size_t>(i)].bwd_table()[sig];
        out[k] = sym[static_cast<size_t>(pb[sig])];
    }
    return out;
}

}

