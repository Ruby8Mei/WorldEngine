// inop.hpp — INOP cipher core
//
// Design rule: this file may only ever contain a rotor machine. Wired
// permutations, a fixed-point-free reflector, a plugboard, ring settings,
// notches. No hashes, no block ciphers, no modern primitives. Everything
// here has a mechanical analogue that could have existed in 1940.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace inop {

// ── alphabets ───────────────────────────────────────────────────────────
// constexpr at namespace scope is implicitly const, which gives internal
// linkage — so these are header-safe without C++17 inline variables.
constexpr const char* ALPHA26 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr const char* ALPHA38 = "abcdefghijklmnopqrstuvwxyz0123456789#/";

// '#' is the space substitute; '/' is the literal slash.
constexpr char SPACE_SUB = '#';

// ── Alphabet: char <-> index, done once ─────────────────────────────────
class Alphabet {
public:
    explicit Alphabet(std::string symbols);

    int size() const { return size_; }
    const std::string& str() const { return symbols_; }
    char at(int i) const { return symbols_[static_cast<size_t>(i)]; }

    bool contains(char c) const { return idx_[static_cast<uint8_t>(c)] >= 0; }
    int index(char c) const {
        int i = idx_[static_cast<uint8_t>(c)];
        if (i < 0) throw std::invalid_argument(std::string("symbol not in alphabet: ") + c);
        return i;
    }
    // no-throw variant for hot paths where membership is already known
    int index_unchecked(char c) const { return idx_[static_cast<uint8_t>(c)]; }

    // True if this alphabet's letters are uppercase (detected once, from
    // the declared symbol string, at construction) — Legacy's alphabet is
    // uppercase, INOP-38's is lowercase.
    bool uses_uppercase() const { return uppercase_; }

    // Folds c (or every character of s) toward whichever case this
    // alphabet actually uses, instead of a caller hardcoding tolower/
    // toupper and silently breaking if a suite's alphabet case ever
    // differs from what that caller assumed.
    char fold_case(char c) const;
    std::string fold_case(const std::string& s) const;

private:
    std::string symbols_;
    int size_;
    std::vector<int16_t> idx_;  // 256 entries, -1 == absent
    bool uppercase_;
};

// ── Rotor ───────────────────────────────────────────────────────────────
//
// The wiring is baked into per-offset lookup tables at construction:
//   tbl_f_[off * size + sig]  ==  (fwd[(sig + off) % size] - off) mod size
// where off == (position - ring) mod size. A traversal is then one indexed
// read instead of two modulos, which is the whole trick.
class Rotor {
public:
    Rotor(std::string name, std::string wiring, std::string notches, const Alphabet& alpha);

    const std::string& name() const { return name_; }
    void set_ring(int ring_1based);
    void set_notches(const std::string& notches, const Alphabet& alpha);
    void set_position(int pos) { position_ = ((pos % size_) + size_) % size_; refresh(); }
    int position() const { return position_; }
    int ring() const { return ring_; }
    std::string notch_str(const Alphabet& alpha) const;

    bool on_notch() const { return notch_[static_cast<size_t>(position_)]; }

    // advance one step; returns true if the NEW position sits on a notch
    bool step() {
        position_ = position_ + 1 == size_ ? 0 : position_ + 1;
        refresh();
        return notch_[static_cast<size_t>(position_)];
    }

    const uint8_t* fwd_table() const { return cur_f_; }
    const uint8_t* bwd_table() const { return cur_b_; }

private:
    void bake();
    void refresh() {
        int off = position_ - ring_;
        if (off < 0) off += size_;
        cur_f_ = tbl_f_.data() + static_cast<size_t>(off) * size_;
        cur_b_ = tbl_b_.data() + static_cast<size_t>(off) * size_;
    }

    std::string name_;
    int size_;
    std::vector<uint8_t> fwd_, rev_;
    std::vector<uint8_t> tbl_f_, tbl_b_;
    std::vector<bool> notch_;
    int position_ = 0, ring_ = 0;
    const uint8_t *cur_f_ = nullptr, *cur_b_ = nullptr;
};

// ── Reflector ───────────────────────────────────────────────────────────
//
// Must be an involution with no fixed points — that is what makes it a
// reflector rather than just another rotor. It may rotate: the real
// Umkehrwalze D was field-rewireable, so an orientable reflector is a
// small step rather than a departure.
class Reflector {
public:
    Reflector(std::string name, std::string wiring, const Alphabet& alpha);

    const std::string& name() const { return name_; }
    void set_position(int pos) { position_ = ((pos % size_) + size_) % size_; }
    void rotate_to(char letter, const Alphabet& alpha) { set_position(alpha.index(letter)); }
    int position() const { return position_; }
    void step() { position_ = position_ + 1 == size_ ? 0 : position_ + 1; }

    const uint8_t* table() const {
        return tbl_.data() + static_cast<size_t>(position_) * size_;
    }

private:
    std::string name_;
    int size_;
    std::vector<uint8_t> tbl_;
    int position_ = 0;
};

// ── Plugboard ───────────────────────────────────────────────────────────
class Plugboard {
public:
    Plugboard() = default;
    Plugboard(const std::vector<std::string>& pairs, const Alphabet& alpha);

    const uint8_t* map() const { return map_.data(); }
    const std::vector<std::string>& pairs() const { return pairs_; }

private:
    std::vector<uint8_t> map_;
    std::vector<std::string> pairs_;
};

// ── Machine ─────────────────────────────────────────────────────────────
class Machine {
public:
    // legacy_stepping selects the historic three-rotor double-step quirk vs.
    // the generic odometer cascade. It is a property of the SUITE the caller
    // is building for, not of how many rotors happen to be in the machine —
    // the caller (registry/suite-aware code) decides and states it explicitly.
    Machine(const Alphabet& alpha, std::vector<Rotor> rotors, Reflector reflector,
            Plugboard plugboard, const std::vector<int>& rings, const std::string& master_key,
            bool legacy_stepping);

    // master_key is (rotor_count + 1) symbols: one window letter per rotor,
    // plus a final symbol giving the reflector orientation.
    void set_key(const std::string& master_key);
    void rewind() { set_key(master_key_); }

    void set_moving_reflector(bool on) { moving_reflector_ = on; }
    bool moving_reflector() const { return moving_reflector_; }

    // Encipher a whole message from the current state. Because the machine
    // is its own inverse position-by-position, this is also decipherment.
    // Precondition: every character of text must already be a member of
    // this machines alphabet — unchecked here, since text reaching this
    // point has already gone through preprocess().
    std::string encipher(const std::string& text) const;

    const std::vector<Rotor>& rotors() const { return rotors_; }
    const Reflector& reflector() const { return reflector_; }
    const Plugboard& plugboard() const { return plugboard_; }
    const Alphabet& alphabet() const { return alpha_; }

private:
    void step_rotors() const;

    Alphabet alpha_;
    // mutable despite encipher() being const: rotor/reflector position is
    // session state that advances on every keypress, not machine identity —
    // accepted tradeoff, see DESIGN.md section 8.
    mutable std::vector<Rotor> rotors_;
    mutable Reflector reflector_;
    Plugboard plugboard_;
    std::string master_key_;
    int size_;
    bool moving_reflector_ = true;
    bool legacy_double_step_ = false;
};

}  // namespace inop
