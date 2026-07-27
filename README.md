# INOP — rotor cipher machine

A C++ rewrite of the INOP cipher machine. Terminal only, no dependencies
beyond a C++11 compiler.

## The design rule

`src/inop.hpp` and `src/inop.cpp` may only ever contain a rotor machine:
wired permutations, a fixed-point-free reflector, a plugboard, ring
settings, notches. No hash functions, no block ciphers, no modern
primitives. Every part of it has a mechanical analogue that could have
existed in 1940.

Everything else is operator procedure and is deliberately unconstrained.
Padding, cover traffic, markers, key generation and the double pass live in
`pipeline.cpp` and `rng.cpp`, where they are free to change without ever
touching the cipher.

That split is also why the core is worth writing in C++ and the rest is not
especially: the core is frozen by the rule above, so it will never need
rewriting.

## Suites

| Suite    | Alphabet | Rotors | Wheels available            | Reflectors | Blocks |
|----------|----------|--------|-----------------------------|------------|--------|
| Legacy   | 26       | 3      | I–VII, historic notches     | A B C      | 5      |
| INOP-38  | 38       | 5      | R1–R10, notches per message | D E F G H  | 16     |

**Legacy is locked.** It is a 1939 machine, not a suite with options: no
padding, no double pass, no reflector motion, and output in 5-letter groups
the way it went out over the wire. The INOP-exclusive features simply are not
offered when it is selected. Choosing Legacy means choosing the machine as it
was on the day Poland was invaded.

The 38-symbol alphabet is `A–Z`, `0–9`, `#` and `/`. `#` carries the space,
so spaces survive a round trip; `/` is a literal slash.

Legacy reproduces the Wehrmacht Enigma exactly, including the double-step
anomaly. The self-test checks it against the known vector: rotors I II III,
reflector B, rings 01, key AAA, twelve presses of `A` gives `BDZGOWCXLTKS`.

## Build

```sh
make            # or: cmake -B build && cmake --build build
make test       # correctness and throughput checks
./inop          # interactive session
```

On Windows either MSVC or MinGW works; `rand_s` supplies entropy there and
`/dev/urandom` does on everything else. Nothing links against an extra
library.

## Session commands

| Command | Effect |
|---------|--------|
| *(text)* | encipher, and show the round trip as a check |
| `:d` | decipher a ciphertext (asks for the marker) |
| `:s` | write the current settings to `inop.settings` |
| `:q` | quit |

Settings files are plain text, one directive per line, so they can be
written by hand or generated.

## Why the double pass matters

Enigma's fatal weakness was not its rotor mathematics. Because the reflector
is an involution with no fixed points, a letter could never encipher to
itself — which let Bletchley slide a crib along a ciphertext and discard
every alignment where a letter sat above itself. Most candidates died on
that test alone.

The double pass enciphers, reverses the string, and enciphers again from a
rewound state. Ciphertext position *i* then depends on plaintext position
*L−1−i*, so the per-position guarantee disappears and crib-dragging finds
nothing. The whole-message map stays self-inverse, so decipherment is
unchanged.

The self-test measures this directly: single pass gives zero self-mappings
across a 4000-symbol run, double pass gives roughly the ~1/38 you would
expect by chance.

This is strong against 1940s cryptanalysis, which is the target. It is not
a modern cipher and does not claim to be.

## Layout

```
src/inop.hpp      cipher core, declarations   <- the constrained half
src/inop.cpp      table baking, stepping, signal path
src/registry.hpp  suite definitions
src/registry.cpp  rotor and reflector wirings
src/rng.hpp/.cpp  OS entropy, unbiased sampling
src/pipeline.*    padding, markers, double pass
src/main.cpp      terminal interface
```

## Performance note

Rotor wiring is baked at construction into per-offset lookup tables, so a
traversal is one indexed read rather than two modulo operations. A rotor
caches a pointer to its current table row and updates it only when it
actually steps. This changes no behaviour — output is identical to the
Python implementation symbol for symbol — it only removes arithmetic that
was being redone for every character.

# INOP Cipher Machine

Welcome to **INOP** — a rotor-based encryption machine inspired by the Enigma. This project is a tribute to cryptographic history and a playground of personal design. It features three distinct modes, custom rotors, full plugboard configuration, and an extended alphabet engine capable of handling everything from short messages to 2,000+ character memory dumps.

---

## Modes

You can select between three suites, each representing a step in the machine’s evolution:

- **[1] Legacy**  
  Faithful to the **classical Enigma** structure. 3 rotors, 26-character alphabet, minimal variation. We honor the ancestors.

- **[2] INOP-38**  
  The **first official INOP version**. Based on a 38-character alphabet and 5 rotors. Offers ring settings, notches, master key seeding, randomized message markers, and full plugboard support.

- **[3] INOP-60**  
  The **newest version**. Built on a 60-character alphabet. Supports 10 rotors, large-scale plugboards (up to 25 pairs), and variable encryption depth. Designed to withstand massive input sizes without breaking stride.


## Features

###  **Extended Alphabet Support**  
  - `Legacy` uses standard A–Z.  
  - `INOP-38` adds digits, `#` and `/`.  
  - `INOP-60` includes symbols like `+-=()[]{}<>!?@&^%$£€_` on top of it.

###  **Full Rotor Stack**  
  - Custom rotor and reflector sets are supported. You can generate them yourself or use the built-in ones per suite.
  - Each rotor supports ring settings and notch configuration.
  - Rotors are selected per message (e.g. pick 5 of 10 or 10 of 20).

###  **Plugboard**  
  - Up to 25 pairwise swaps (non-overlapping).
  - Enforces rules: no self-pairing, no reused characters, strict alphabet validation.

###  **Master Key Seeding**  
  - Each suite uses a key to control rotor starting positions and reflector angle.
  - Different messages using the same settings and key will still encrypt differently (randomized marker and padding).

###  **Padding and Marker Logic**  
  - Messages are padded with randomized noise and marked with hidden boundaries.
  - You can adjust padding intensity.

###  **Encryption & Decryption**  
  - Encrypted text appears in blocks (default: 8 chars per block).
  - Message recovery is clean and faithful—even with very long, irregular, or symbol-heavy inputs.

## A Few Notes

- This machine **is not optimized** for industrial-scale throughput.
- Encryption time varies with message length and mode:
  - Small messages: near-instant.
  - Large (2,000+ characters): under a second on modern hardware.
- INOP modes encrypt differently even with the same message and config (per-message entropy).

## Example code

Select suite:
 [1] Legacy
 [2] INOP-38
 [3] INOP-60
> 1

Available Rotors: I II III IV V VI VII
Select 3 rotors in order: ii iii vi

Available Reflectors:  A, B, C
Select reflector: b

Plugboard pairs (≤10, e.g. AB CD EF):
Pairs (Enter for none):no pe lt
3 ring settings 1-26:   10 14 16
Master key (4 chars):keys

Message to encrypt (blank = quit): Test message for github gremlins.

Encrypted: PYHGLMXC  EUZZMMHI  HHLYJYSI  TTTIFQKM  HMPPCCEG  ATFNTMDP  ILKN

Decrypted: TESTMESSAGEFORGITHUBGREMLINS


Thats all from me, if you have suggestions then dont be rude. I will **not** be implementing cython. Happy encryption!
