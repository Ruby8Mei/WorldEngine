// rng.hpp — cryptographically secure randomness, no dependencies
//
// This sits in the PREP layer, not the cipher, so it is free to use whatever
// the operating system offers. Ruby's edge in the story is key hygiene, and
// key hygiene starts with the key actually being unpredictable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace inop {

// Fill buf with n bytes from the OS entropy source. Throws on failure —
// silently degrading to a weak PRNG would be the exact sin the lore mocks.
void secure_bytes(uint8_t* buf, size_t n);

// Uniform in [0, bound). Rejection-sampled, so no modulo bias.
uint32_t secure_below(uint32_t bound);

// n symbols drawn uniformly from alphabet.
std::string secure_string(const std::string& alphabet, size_t n);

// Prove the entropy source is alive before anything trusts it.
//
// This exists because a silently dead RNG is the worst possible failure for
// this program: it does not crash, it produces plausible-looking output, and
// every wheel and key sheet it generates is worthless in the same predictable
// way. Throws with a description if the source looks degenerate.
void entropy_self_check();

}  // namespace inop
