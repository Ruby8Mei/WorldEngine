#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace inop {

constexpr const char* ALPHA26 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr const char* ALPHA38 = "abcdefghijklmnopqrstuvwxyz0123456789#/";

constexpr char SPACE_SUB = '#';

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
    int index_unchecked(char c) const { return idx_[static_cast<uint8_t>(c)]; }

    bool uses_uppercase() const { return uppercase_; }

    char fold_case(char c) const;
    std::string fold_case(const std::string& s) const;

private:
    std::string symbols_;
    int size_;
    std::vector<int16_t> idx_;
    bool uppercase_;
};

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

class Machine {
public:
    Machine(const Alphabet& alpha, std::vector<Rotor> rotors, Reflector reflector,
            Plugboard plugboard, const std::vector<int>& rings, const std::string& master_key,
            bool legacy_stepping);

    void set_key(const std::string& master_key);
    void rewind() { set_key(master_key_); }

    void set_moving_reflector(bool on) { moving_reflector_ = on; }
    bool moving_reflector() const { return moving_reflector_; }

    std::string encipher(const std::string& text) const;

    const std::vector<Rotor>& rotors() const { return rotors_; }
    const Reflector& reflector() const { return reflector_; }
    const Plugboard& plugboard() const { return plugboard_; }
    const Alphabet& alphabet() const { return alpha_; }

private:
    void step_rotors() const;

    Alphabet alpha_;
    mutable std::vector<Rotor> rotors_;
    mutable Reflector reflector_;
    Plugboard plugboard_;
    std::string master_key_;
    int size_;
    bool moving_reflector_ = true;
    bool legacy_double_step_ = false;
};

}

