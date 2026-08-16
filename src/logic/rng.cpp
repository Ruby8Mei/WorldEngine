// rng.cpp — OS entropy, no third-party dependencies.
//
// Windows: BCryptGenRandom (Vista+). Needs -lbcrypt at link time.
// Everything else: /dev/urandom.
//
// Both are cryptographically secure. There is deliberately no fallback to
// rand() or std::random_device — silently degrading to a weak generator is
// exactly the failure this file exists to prevent, so it throws instead.

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#ifndef BCRYPT_USE_SYSTEM_PREFERRED_RNG
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002
#endif
#endif

#include "rng.hpp"

#include <stdexcept>
#include <vector>

#if !defined(_WIN32)
#include <cstdio>
#endif

namespace inop {

void secure_bytes(uint8_t* buf, size_t n) {
    if (n == 0) return;
#if defined(_WIN32)
    // A NULL algorithm handle with this flag uses the system default RNG,
    // so there is no handle to open, close, or leak.
    while (n > 0) {
        ULONG chunk = n > 0x40000000u ? 0x40000000u : static_cast<ULONG>(n);
        if (BCryptGenRandom(NULL, reinterpret_cast<PUCHAR>(buf), chunk,
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            throw std::runtime_error("BCryptGenRandom failed: no OS entropy");
        buf += chunk;
        n -= chunk;
    }
#else
    static FILE* src = nullptr;
    if (!src) {
        src = std::fopen("/dev/urandom", "rb");
        if (!src) throw std::runtime_error("cannot open /dev/urandom");
    }
    if (std::fread(buf, 1, n, src) != n)
        throw std::runtime_error("short read from /dev/urandom");
#endif
}

namespace {
// Fisher-Yates shuffling a wheel batch draws once per swap, which used to
// cost one OS entropy call (BCryptGenRandom/fread) per draw. Buffered in
// bulk instead, the same batching secure_string() already does below —
// refilled only when exhausted, not on every single draw.
uint32_t next_uint32() {
    static std::vector<uint8_t> pool;
    static size_t pos = 0;
    if (pos + 4 > pool.size()) {
        pool.resize(4096);
        secure_bytes(pool.data(), pool.size());
        pos = 0;
    }
    uint32_t v = static_cast<uint32_t>(pool[pos]) | (static_cast<uint32_t>(pool[pos + 1]) << 8) |
                 (static_cast<uint32_t>(pool[pos + 2]) << 16) | (static_cast<uint32_t>(pool[pos + 3]) << 24);
    pos += 4;
    return v;
}
}  // namespace

uint32_t secure_below(uint32_t bound) {
    if (bound == 0) throw std::invalid_argument("secure_below(0)");
    if (bound == 1) return 0;

    // Reject the ragged tail so every value is equally likely.
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % bound) - 1;
    uint32_t v;
    do {
        v = next_uint32();
    } while (v > limit);
    return v % bound;
}

std::string secure_string(const std::string& alphabet, size_t n) {
    const uint32_t size = static_cast<uint32_t>(alphabet.size());
    if (size == 0) throw std::invalid_argument("secure_string: empty alphabet");
    if (n == 0) return std::string();

    // Draw in bulk and reject the biased tail — one call, not n.
    const uint32_t limit = 256 - (256 % size);
    std::string out;
    out.reserve(n);
    std::vector<uint8_t> buf;
    while (out.size() < n) {
        size_t want = (n - out.size()) * 2 + 8;
        buf.resize(want);
        secure_bytes(buf.data(), want);
        for (size_t i = 0; i < want && out.size() < n; ++i)
            if (buf[i] < limit) out += alphabet[buf[i] % size];
    }
    return out;
}

void entropy_self_check() {
    // 1. raw bytes must not be constant, and must cover a decent spread
    const size_t N = 4096;
    std::vector<uint8_t> buf(N);
    secure_bytes(buf.data(), N);
    bool seen[256] = {false};
    int distinct = 0;
    for (size_t i = 0; i < N; ++i)
        if (!seen[buf[i]]) { seen[buf[i]] = true; ++distinct; }
    if (distinct < 64)
        throw std::runtime_error(
            "entropy source is degenerate: " + std::to_string(distinct) +
            " distinct byte values in " + std::to_string(N) + " bytes (expected ~250)");

    // 2. secure_below must actually vary. This is the exact path that failed
    //    silently once: keys looked random while every shuffle returned 0.
    const int draws = 512;
    bool hit[38] = {false};
    int spread = 0;
    for (int i = 0; i < draws; ++i) {
        uint32_t v = secure_below(38);
        if (v >= 38) throw std::runtime_error("secure_below returned out of range");
        if (!hit[v]) { hit[v] = true; ++spread; }
    }
    if (spread < 15)
        throw std::runtime_error(
            "secure_below is degenerate: only " + std::to_string(spread) +
            " distinct values in " + std::to_string(draws) + " draws (expected 38)");
}

}  // namespace inop
