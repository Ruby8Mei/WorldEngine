# INOP — rotor cipher machine

A rotor cipher in the Enigma line, written in C++. Terminal only, no
dependencies beyond a compiler.

INOP is a thought experiment: what a machine like this would have been in the
1940s, built with the constraints of that era honoured rather than worked
around. It is not a modern cipher and does not try to be.

See [DESIGN.md](DESIGN.md) for the constraints, the deliberate decisions, and
the open items. Read it before reviewing or changing anything here — several
things that look like defects are chosen.

## The design rule

`src/inop.hpp` and `src/inop.cpp` may only ever contain a rotor machine:
wired permutations, a fixed-point-free reflector, a plugboard, ring settings,
notches. No hash functions, no block ciphers, no modern primitives. Every part
of it has a mechanical analogue that could have existed in 1940.

Everything else is operator procedure and is deliberately unconstrained.
Padding, cover traffic, markers, wheel generation and the double pass live in
`pipeline.cpp`, `generator.cpp` and `rng.cpp`, where they are free to change
without ever touching the cipher.

That split is also why the core is worth writing in C++ and the rest is not
especially: the core is frozen by the rule above, so it will never need
rewriting.

## Suites

| Suite    | Alphabet | Rotors | Wheels                      | Reflectors | Blocks |
|----------|----------|--------|-----------------------------|------------|--------|
| Legacy   | 26       | 3      | I–VII, historic notches     | A B C      | 5      |
| INOP-38  | 38       | 5      | R1–R10, notches per message | D E F G H  | 16     |

The 38-symbol alphabet is `A–Z`, `0–9`, `#` and `/`. `#` carries the space, so
sentences survive a round trip; `/` allows dates like `28/2/1941`. Legacy has
neither, which is exactly why INOP-38 exists — a Legacy machine silently drops
the timestamp out of a weather report.

**Legacy is locked.** It is a 1939 machine, not a suite with options: no
padding, no double pass, no reflector motion, and output in 5-letter groups the
way it went out over the wire. The INOP-exclusive features are not offered when
it is selected.

## Build

```sh
make                # or: cmake -B build && cmake --build build
make test           # correctness, entropy and throughput checks
./inop              # interactive session
```

Or by hand:

```sh
g++ -std=c++11 -O2 -o inop src/*.cpp          # POSIX
g++ -std=c++11 -O2 -o INOP.exe src/*.cpp -lbcrypt   # Windows
```

C++11 is enough. On Windows, `BCryptGenRandom` supplies entropy and needs
`-lbcrypt`; everything else uses `/dev/urandom`. Nothing else is linked.

## Session commands

| Command | Effect |
|---------|--------|
| *(text)* | encipher, and show the round trip as a check |
| `:d` | decipher a ciphertext (asks for the marker) |
| `:i` | show the active settings again |
| `:s` | write the current settings to `inop.settings` |
| `:q` | quit |

Case does not matter, and `:quit` / `:help` / `:info` also work. Anything
starting with `:` that is not a command is refused rather than enciphered — a
mistyped command should never quietly become a message.

## Maintenance

The startup menu offers a generator for rotor batches, reflector batches and
key sheets. Generated wheels go to `inop_wheels.txt`, which is loaded
automatically at startup and merged with the factory set:

```
rotor     G1  <38-symbol permutation>  [notches]
reflector GX1 <38-symbol involution>
```

A wheel belongs to whichever suite its wiring length matches, so 26- and
38-symbol wheels can share one file.

Key sheets come out in the same directive format the program reads, so a block
copies straight into `inop.settings`.

### Guards

Generated key material is checked, because a silently broken generator is the
worst failure this program can have — it does not crash, and its output looks
plausible.

- The entropy source is proved alive at startup and before any generation:
  4096 raw bytes must show a normal spread, and 512 draws of `secure_below(38)`
  must actually vary.
- A rotor wiring that is a pure rotation of the alphabet is rejected. That is a
  Caesar rotor, and five of them in series still compose to one.
- A batch whose wirings are not all distinct is discarded rather than written.
- `inop_wheels.txt` is validated on **load**, not just on generation. Duplicate
  wirings or rotations cause the whole file to be rejected, so a bad file left
  on disk cannot poison later sessions.

`tools/entropy_probe.cpp` is a standalone diagnostic if the entropy check ever
fails.

### Never commit key material

`inop_wheels.txt`, `inop_keysheet.txt` and `inop.settings` are the secret and
are in `.gitignore`. Publishing them would hand over the wiring and the day's
settings in one place — the exact failure the design is built to avoid.

## Why the double pass

Enigma's fatal weakness was not its rotor mathematics. Because the reflector is
an involution with no fixed points, a letter could never encipher to itself —
which let Bletchley Park slide a crib along a ciphertext and discard every
alignment where a letter sat above itself.

The double pass enciphers, reverses the string, and enciphers again from a
rewound state. Ciphertext position *i* then depends on plaintext position
*L−1−i*, so the per-position guarantee disappears. The whole-message map stays
self-inverse, so decipherment is unchanged.

The self-test measures it: single pass gives zero self-mappings across a 4000-
symbol run, double pass gives roughly the 1-in-38 you would expect by chance.

## Layout

```
src/inop.hpp        cipher core, declarations   <- the constrained half
src/inop.cpp        table baking, stepping, signal path
src/registry.*      suite definitions, wirings, wheel loading and validation
src/rng.*           OS entropy, unbiased sampling, health check
src/pipeline.*      padding, markers, double pass, suite locks
src/generator.*     wheel and key sheet generation
src/main.cpp        terminal interface
tools/              standalone diagnostics
```

## Performance

Rotor wiring is baked at construction into per-offset lookup tables, so a
traversal is one indexed read rather than two modulo operations. A rotor caches
a pointer to its current table row and updates it only when it steps. This
changes no behaviour — output is identical to the original Python
implementation symbol for symbol.

The complete text of *Hamlet*, encrypted and decrypted through the full
pipeline, takes about 17 ms. That is not the point of the machine — at 1940s
speeds the binding constraint was an operator typing — but it does mean the
test suite can afford to hammer it.