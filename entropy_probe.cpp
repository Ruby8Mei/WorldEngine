// entropy_probe.cpp — standalone diagnostic. Not part of INOP.
//
// Build:  g++ -std=c++11 -o probe.exe entropy_probe.cpp -lbcrypt
// If rand_s still will not resolve on your toolchain, build without it:
//         g++ -std=c++11 -DSKIP_RAND_S -o probe.exe entropy_probe.cpp -lbcrypt
//
// Asks the OS for randomness several ways and prints exactly what comes back,
// so we can see which call is lying rather than guessing.

// MUST be first: <stdlib.h> arrives transitively through almost any header,
// and rand_s is only declared if this is defined before it does.
#if defined(_WIN32) && !defined(SKIP_RAND_S)
#define _CRT_RAND_S
#include <stdlib.h>
#endif

#include <cstdio>
#include <cstring>

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

static void dump(const char* label, const unsigned char* b, int n, long status) {
    printf("  %-36s status=0x%08lX  bytes:", label, (unsigned long)status);
    for (int i = 0; i < n; ++i) printf(" %02X", b[i]);
    int allzero = 1;
    for (int i = 0; i < n; ++i) if (b[i]) allzero = 0;
    printf("   %s\n", allzero ? "<-- ALL ZERO, BROKEN" : "ok");
}

int main() {
    printf("\nentropy probe\n-------------\n");
#if defined(_WIN32)
    // 1. four bytes — the size secure_below() asks for, the call that failed
    {
        unsigned char b[4];
        memset(b, 0, sizeof b);
        long st = (long)BCryptGenRandom(NULL, b, 4, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        dump("BCryptGenRandom, 4 bytes", b, 4, st);
    }
    // 2. twenty bytes — the size secure_string() asks for, the call that worked
    {
        unsigned char b[20];
        memset(b, 0, sizeof b);
        long st = (long)BCryptGenRandom(NULL, b, 20, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        dump("BCryptGenRandom, 20 bytes", b, 8, st);
    }
    // 3. four bytes again, immediately after — is it only the FIRST call?
    {
        unsigned char b[4];
        memset(b, 0, sizeof b);
        long st = (long)BCryptGenRandom(NULL, b, 4, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        dump("BCryptGenRandom, 4 bytes (second try)", b, 4, st);
    }
    // 4. explicit algorithm handle instead of the system-preferred flag
    {
        unsigned char b[4];
        memset(b, 0, sizeof b);
        BCRYPT_ALG_HANDLE h = NULL;
        long open = (long)BCryptOpenAlgorithmProvider(&h, BCRYPT_RNG_ALGORITHM, NULL, 0);
        long st = -1;
        if (open == 0) {
            st = (long)BCryptGenRandom(h, b, 4, 0);
            BCryptCloseAlgorithmProvider(h, 0);
        }
        printf("  BCryptOpenAlgorithmProvider          status=0x%08lX\n", (unsigned long)open);
        dump("BCryptGenRandom via handle, 4 bytes", b, 4, st);
    }
    // 5. the CRT route, for comparison
#if !defined(SKIP_RAND_S)
    {
        unsigned char b[4];
        memset(b, 0, sizeof b);
        unsigned int v = 0;
        int err = rand_s(&v);
        memcpy(b, &v, 4);
        dump("rand_s", b, 4, (long)err);
    }
#else
    printf("  rand_s                               skipped (SKIP_RAND_S)\n");
#endif
#else
    printf("  (this probe is for Windows; POSIX uses /dev/urandom)\n");
#endif
    printf("\n");
    return 0;
}