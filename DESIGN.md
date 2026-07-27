# INOP — design rules and decisions

Read this before analysing, reviewing, or changing this repository.

INOP is a **thought experiment**: what a rotor cipher machine would have been
if it had been built in the 1940s with the constraints of that era honoured
rather than engineered around. It is deliberately not a modern cipher, and
most standard cryptographic advice does not apply to it.

If an analysis of this code concludes "replace this with AES", "add an HMAC",
"derive the key with Argon2", or "use libsodium", that analysis has
misunderstood the project. Those are correct recommendations for a security
product and wrong for this one. See **Non-goals** below.

---

## 1. The rule

`src/inop.hpp` and `src/inop.cpp` may only ever contain a rotor machine:
wired permutations, a fixed-point-free reflector, a plugboard, ring settings,
notches. **No hash functions, no block ciphers, no modern primitives.** Every
element must have a mechanical analogue that could plausibly have existed in
1940.

Everything else — padding, cover traffic, markers, the double pass, wheel
generation, randomness, the terminal interface — is *operator procedure* and
is deliberately unconstrained. It lives in `pipeline.*`, `generator.*`,
`rng.*` and `main.cpp`, where it may change freely.

This split is also the architectural rationale: the core is frozen by the rule
above, so it never needs rewriting. The prep layer is where the project
evolves.

## 2. Threat model

The adversary is a **1940s cryptanalyst**: Bletchley Park with bombes, cribs,
traffic analysis, and thousands of people, but no computers in the modern
sense and no post-war mathematics.

INOP makes **no claim** of security against a modern attacker. It has no
diffusion, its structure is well understood, and it has received no
cryptanalytic review beyond its author's. Anyone reading this should assume it
is breakable by a competent modern cryptanalyst given enough ciphertext under
one key, and should not use it for anything real.

## 3. Suites

| Suite | Alphabet | Rotors | Purpose |
|-------|----------|--------|---------|
| Legacy | 26 | 3 | Faithful 1939 Enigma. Reference implementation and regression test. |
| INOP-38 | 38 | 5 | The actual machine. |

**Legacy is locked** and must stay locked: no padding, no double pass, no
reflector motion, 5-letter output groups. It is not a suite with options; it
is a museum exhibit that also serves as a correctness check against the
historic vector `BDZGOWCXLTKS` (rotors I II III, reflector B, rings 01, key
AAA, twelve presses of `A`).

Its limitations are the *point*. A Legacy machine silently drops digits from a
message, which is precisely why INOP-38's alphabet exists.

## 4. Deliberate decisions — do not "fix" these

Each of these looks like a defect and is not. They have been considered and
chosen.

**No authentication or integrity.** There is no MAC, no signature, no tamper
detection. Rotor machines had none. Out of scope.

**No key derivation function.** The master key is a rotor window setting, not
a password. Running it through PBKDF2 would be a category error.

**Ciphertext length tracks message length.** Padding scales with the message,
so length leaks for messages beyond about 30 symbols. Accepted. Padding's
purpose here is cover traffic and boundary hiding, not length hiding. (Below
~16 symbols the noise floor does bucket messages to a uniform 112.)

**Corrupting a marker destroys the message.** 32 of a typical ciphertext's
symbols are single points of failure, about 15%. Accepted; the operational
answer is retransmission under the day's backup settings, ideally reworded or
in another language.

**Legacy silently drops symbols outside its alphabet.** Historically accurate
and the motivating example for INOP-38.

**Spaces are enciphered as `#`.** This is a usability feature, not a security
one. It is understood that the space is the most frequent symbol in English
(~17.6%) and that including it *helps* an analyst. Sentences and dates
surviving a round trip is worth more here.

**A literal `#` typed by the operator is pruned in `preprocess()`, not
enciphered.** `#` is the space substitute, and `decrypt()` unconditionally
maps every `#` back to a space — so a literal `#` and a substituted space are
the same symbol once they reach the rotors, and there is no way to recover
which one an operator meant. Carrying it through would silently turn `"A#B"`
and `"A B"` into the same round trip with no error. Dropping it is treated the
same as any other symbol the machine has no key for. Markers and padding are
unaffected: both are drawn independently by the pipeline and never pass
through `preprocess()`, so they still carry `#` freely.

**No diffusion.** Changing one plaintext symbol changes exactly one ciphertext
symbol. This is inherent to rotor machines and cannot be fixed without
abandoning the design rule. It is also why a single garbled radio symbol costs
exactly one plaintext symbol rather than a whole block — the weakness and the
virtue are the same property.

**Word boundaries survive.** Known. Treated as a convenience, never as a
defence.

**Throughput is not a goal.** The machine is fast because table-baking was
cheap, not because speed matters. At 1940s speeds the binding constraint was
an operator typing.

**Cython, and modern crypto libraries generally, will never be used.**

## 5. Decisions with non-obvious rationale

**The double pass** (encipher → reverse → encipher from a rewound state) is
the single most important line in the pipeline. Enigma's reflector guarantees
a letter never enciphers to itself, which is what let Bletchley crib-drag. The
reversal makes ciphertext position *i* depend on plaintext position *L−1−i*,
destroying that guarantee while keeping the whole-message map self-inverse.
Removing the reversal turns the double pass into an identity function.

**Daily wheel regeneration** is the other structural defence. Bletchley never
had to solve Enigma's wiring — the Poles obtained it in 1932, and every
technique afterwards assumed it as a known constant. Regenerating wheels daily
removes that constant, which is why a bombe has nothing to grip.

**One notch per rotor maximises the period.** More notches make carries fire
more often and *shorten* it. Measured on 3 rotors: 1 notch gives 54,872 (=38³,
the ceiling), 2 or 3 give 13,718, 19 gives 152. Zero and "all" both collapse to
38. The `max_notches` setting is a key-space/period tradeoff, not a safety
rail.

**15 plugboard pairs is both the maximum and the optimum** for a 38-symbol
alphabet (2⁷⁸·⁰). Beyond the peak the count falls, mirroring the historical
result where 11 plugs beat 13 on the 26-letter Enigma.

**Baked offset tables** in `Rotor` are a pure performance choice with no
behavioural effect. Output is identical to the original Python implementation
symbol for symbol.

## 6. Guards — do not remove

These exist because a silently broken generator is the worst failure this
program can have: it does not crash, and its output looks plausible. This has
already happened once, producing 100 rotors that were all the same shift
cipher.

- `entropy_self_check()` — runs at startup, before generation, and in the
  self-test. 4096 raw bytes must show normal spread; 512 draws of
  `secure_below(38)` must actually vary.
- Rotor wirings that are a pure rotation of the alphabet are rejected. That is
  a Caesar rotor, and five in series still compose to one.
- The rotation check ranks symbols by their position in the **declared**
  alphabet, never by `std::sort` of the wiring — ALPHA26 happens to already
  be in ASCII order, which is exactly what let that distinction go unnoticed
  once. It lives in a single shared function so the generator and the loader
  can never diverge on it again.
- A batch whose wirings are not all distinct is discarded, not written.
- `inop_wheels.txt` is validated on **load**, not only on generation, so a bad
  file left on disk cannot poison later sessions.
- `random_notches()` clamps to a minimum of one.
- `apply_suite_lock()` enforces the Legacy restrictions and is covered by the
  self-test.

**Key material is never committed.** `inop_wheels.txt`, `inop_keysheet.txt`
and `*.settings` are in `.gitignore`.

## 7. Known open items

Genuine, unresolved, and welcome:

1. **Interactive notch entry accepts a blank** (`src/main.cpp`, the notch
   prompt). The generator enforces a minimum of one notch; the hand-entry path
   does not, and the prompt advertises `0-3`. A five-rotor machine with no
   notches has a period of 38. This is a real bug.
2. **Reflector motion adds no period.** It advances once per character, as does
   the fast rotor, so its position is pinned to the fast rotor's and adds no
   state. Fixes: give it its own counter on a modulus coprime to 38 (37 gives
   ×37), or nest it at the end of the odometer. Its *starting* orientation is
   real key material either way.
3. **Legacy's reflector orientation is still settable** from the last symbol of
   the master key. A real 1939 Enigma had no such control; the historic vector
   uses `AAAA`. Strict fidelity would take a 3-symbol Legacy key and force the
   reflector to `A`.
4. **Digits in Legacy.** Either the operator learns the historical convention
   (`NULL EINS ZWEI …`) or a per-language lookup table is added to the *prep*
   layer, never to Legacy itself. Note that spelling digits out is what gave
   Bletchley the Eins Catalogue.
5. **The master key is reused for a whole session.** Every message sent under
   one key is in depth with every other. A per-message indicator protocol is
   the right fix. This is the owner's decision and is deliberately unassigned.

## 8. Known code-level issues

Accepted or open, but already identified — no need to report these again:

- `Machine::encipher` is `const` while mutating `mutable` rotor state.
- `rng.cpp`'s POSIX branch holds a static `FILE*` that is never closed and is
  not thread-safe. Single-threaded program.
- `std::exit()` inside `ask()` bypasses destructors.
- `main.cpp` is long and could be split.
- `extract_message` uses `find`/`rfind`; a marker sequence occurring by chance
  in the padding would break extraction. Probability is negligible at 16
  symbols from 38, but the case is unguarded.

## 9. Non-goals

INOP will never:

- become a block cipher, or use hash functions in the cipher core
- add authenticated encryption, KDFs, or modern primitives
- depend on a cryptographic library
- acquire a graphical interface
- claim security against a modern adversary
- optimise for throughput as an end in itself

The category is the achievement. A better rotor machine, not a modern one.