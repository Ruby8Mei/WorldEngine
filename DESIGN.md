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

`src/logic/inop.hpp` and `src/logic/inop.cpp` may only ever contain a rotor machine:
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
professional cryptanalytic review. Anyone reading this should assume it
is breakable by a competent modern cryptanalyst given enough ciphertext under
one key, and should not use it for anything real.

## 3. Suites

| Suite | Alphabet | Rotors | Purpose |
|-------|----------|--------|---------|
| Legacy | 26 | 3 (fixed) | Faithful 1939 Enigma. Reference implementation and regression test. |
| INOP-38 | 38 | 5-10 | The actual machine. The operator picks the count once per session, first thing, before rings, notches or the master key are asked for — those all follow from it. |

**Legacy is locked** and must stay locked: no padding, no double pass, no
reflector motion, 5-letter output groups, and its rotor count is fixed at 3 —
it is not a suite with options. It is a museum exhibit that also serves as a
correctness check against the historic vector `BDZGOWCXLTKS` (rotors I II
III, reflector B, rings 01, key `AAA`, twelve presses of `A`). The alphabet
itself is **uppercase** — matching the convention the original wiring
tables and traffic were always published in — unlike INOP-38, whose
lowercase/numeral-suffix scheme (see the README's numeral-suffix section for
why) is a feature Legacy never touches at all. Rotor/reflector/suite names
like `I`, `B`, `26` are identifiers, not alphabet symbols, and keep whatever
case they've always had.

**Legacy's master key is 3 symbols, not 4**: one window letter per rotor, with
no orientation symbol. The historic reflector does not rotate — it is fixed
at position 0, not merely defaulted there. A 4-symbol key from an older sheet
still loads; the trailing symbol is ignored with a notice rather than
rejected outright, so old key sheets do not stop working. See
`verify_legacy_integrity()` in section 6.

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
- `verify_legacy_integrity()` — runs at startup, before the main menu, and
  again whenever Legacy is actually selected (interactively or via a loaded
  settings file). Checks the historic Enigma vector (rotors I II III,
  reflector B, rings 1 1 1, key `AAA`, twelve `A`s → `BDZGOWCXLTKS`), the
  Legacy suite descriptor itself (26 symbols / 3 rotors / block 5 /
  `historic_lock` / `notches_are_fixed`), and that `apply_suite_lock()`
  actually forces double pass, padding and moving reflector off. Any failure
  names the check and exits non-zero — Legacy silently drifting from the
  machine it claims to be would be a correctness failure, not a style issue.

**Key material is never committed.** `inop_wheels.txt`, `inop_keysheet.txt`
and `*.settings` are in `.gitignore`.

## 7. Known open items

Genuine, unresolved, and welcome:

1. **Reflector motion adds no period.** It advances once per character, as does
   the fast rotor, so its position is pinned to the fast rotor's and adds no
   state. Fixes: give it its own counter on a modulus coprime to 38 (37 gives
   ×37), or nest it at the end of the odometer. Its *starting* orientation is
   real key material either way.
2. **The master key is reused for a whole session.** Every message sent under
   one key is in depth with every other. A per-message indicator protocol is
   the right fix. This is the owner's decision and is deliberately unassigned.

Resolved since the last pass, kept here for history:

- ~~Interactive notch entry accepted a blank~~ — fixed, and tightened
  further: the hand-entry prompt now requires at least one notch symbol,
  full stop. Neither a blank answer nor `-` is accepted. This matches the
  floor `random_notches()` already enforced on the generator side (`if
  (count < 1) count = 1`) — a notch-less rotor never lands on a notch, so it
  never advances the rotor to its left, which collapses the whole machine's
  period the same way a fixed rotor would. `-` still means "none" when
  *displayed* (`save_settings`, the settings display) for suites whose
  wheels carry no notches at all (Legacy's historic wheels), and a
  zero-notch rotor loaded from a hand-edited settings file or key sheet
  still builds — this fix is specifically about the interactive prompt not
  being able to produce one by accident.
- ~~Digits in Legacy~~ — the per-language lookup table now exists
  (`src/logic/languages.cpp`), exactly as anticipated: it's prep-layer only,
  gated to INOP-38, and never touches Legacy. Legacy keeps the historical
  convention (`NULL EINS ZWEI …`, or simply dropping digits) as its only
  option, unchanged.

## 8. Known code-level issues

Accepted or open, but already identified — no need to report these again:

- `Machine::encipher` is `const` while mutating `mutable` rotor state.
- `rng.cpp`'s POSIX branch holds a static `FILE*` that is never closed and is
  not thread-safe. Single-threaded program.
- `std::exit()` inside `ask()` bypasses destructors.
- `main.cpp` is long and could be split.
- `carve()` (pipeline.cpp) uses `find`/`rfind`; a marker sequence occurring by
  chance in the padding would break extraction. Probability is negligible at
  16 symbols from 38, but the case is unguarded.

## 9. The numeral-suffix diacritic scheme

INOP-38s alphabet is `a-z0-9#/`, with no accented letters. Dropping
diacritics on the floor loses real information — *é* and *e* stop being
distinguishable, and Pinyins four tones on *ā/á/ǎ/à* collapse to one letter.
Since INOP-38 already carries digits, the fix reuses them: an accented
letter folds to its base letter followed by a digit naming which mark it
carried.

| Digit | Diacritic | Examples |
|---|---|---|
| 1 | macron | ā → a1 |
| 2 | acute | á → a2, ć → c2, ĺ → l2 |
| 3 | caron (also reused for breve) | ǎ → a3, ň → n3, ğ → g3, ă → a3 |
| 4 | grave | à → a4 |
| 5 | circumflex | â → a5, ê → e5, ô → o5, ŵ → w5, ŷ → y5 |
| 6 | umlaut / diaeresis | ü → u6, ö → o6, ä → a6 |
| 7 | tilde | ñ → n7, ã → a7, õ → o7 |
| 8 | cedilla (consonants) / ogonek (vowels) | ç → c8, ş → s8, ą → a8, ę → e8 |
| 9 | ring-above | å → a9, ů → u9 |
| 0 | one genuinely distinct (non-diacritic) letter, at most one per language | German ß → s0, Turkish dotless ı → i0, Croatian đ → d0, Polish ł → l0, Danish ø → o0, Maltese ħ → h0 |
| 88 | dot-below, its own doubled slot | Yoruba ẹ → e88, Hindi ṭ → t88, Hebrew ḥ → h88 |

Cedilla and ogonek never land on the same base letter, so sharing digit 8
between them is unambiguous. A repeated digit chains a second mark on top
of the first — Pinyins ü-with-tone stacks a tone digit after the umlaut
(*ǖ/ǘ/ǚ/ǜ → u61/u62/u63/u64*); Hungarians double-acute (*ő/ű*) and the
dot-above mark (Lithuanian *ė*, Polish/Maltese *ż*, Maltese *ċ/ġ*) each get
their own doubled slot (`22`, `33`) rather than a fresh single digit.

Ligatures (French *œ/æ*, Danish/Norwegian *æ*) decompose to their two plain
base letters in sequence (*œ → oe*, *æ → ae*) instead of using the
special-letter scheme. This is a one-way simplification — nothing decodes
an *oe* back into *œ*. Romanians comma-below (*ș/ț*) is the one mark still
dropped with no encoding at all, an accepted loss.

Turkish gets case-aware folding: plain lowercasing would turn a
word-initial capital *I* into dotted *i*, which is wrong. In Turkish, *I*
lowercases to dotless *ı*, and *İ* lowercases to dotted *i* — both letters
in "Işık" get marked `i0`, correctly.

A second pass fixes digit collision: a literal digit right after a letter
in the raw input is genuinely ambiguous with the diacritic marker (`a2`
could mean *á, folded* or *the letters a and 2*). Whenever a literal digit
directly follows a letter with no separator, a `/` gets force-inserted
between them — "Room A2" becomes "room a/2". A diacritic-fold pair never
gets a separator; a forced literal digit always does. On decrypt, that `/`
tells `resubstitute()` to strip it and hand back a plain number instead of
attempting a lookup. A `/` is not inserted before a bare number with no
preceding letter, so "1964" stays "1964".

Encoding is lossless in a way plain accent-stripping is not: `e2` can only
have come from *é*, never from *e*, so decryption restores the accent
exactly. `fold_diacritics()` and `resubstitute()` live in
`src/logic/languages.cpp`; encoding is the same for every language, because
decoding is where the language actually matters — digit 3 alone covers
Pinyins caron and Romanians breve, two marks that never show up in the
same language, so only the declared language can tell `a3` apart
afterward.

Officially supported, 49 languages, alphabetical by name: Albanian,
Basque, Bosnian, Cantonese, Catalan, Creole, Croatian, Czech, Danish,
Dutch, English, Estonian, Finnish, French, German, Hebrew (Latin), Hindi
(Latin), Hungarian, Igbo, Indonesian, Irish, Italian, Korean (Latin),
Kurdish (Kurmanji), Latin, Lithuanian, Luxembourgish, Malay, Maltese,
Mandarin (via Pinyin), Maori, Montenegrin, Norwegian, Polish, Portuguese,
Romanian, Scottish Gaelic, Serbian (Latin), Slovak, Slovenian, Somali,
Spanish, Swahili, Swedish, Tagalog, Turkish, Welsh, Yoruba, Zulu/Xhosa.
Cantonese is a diacritic tone scheme devised for this project specifically
(not a claim to match Yale or Jyutping, which are both tone-number
systems) — six tones over the five plain vowels, reusing the same
mark-shape digits Mandarin already uses for its own four tones.

## 10. The sign-off phrase

Before an INOP-38 message goes out, single or inside a batch, INOP checks
whether the preprocessed text **ends with** a fixed phrase. Appearing
somewhere in the middle does not count; it has to be trailing content. If
it is missing, the operator is asked whether to append it, per message,
even inside a batch. This is a check on the plaintext before encryption,
unrelated to the ciphertext-side language tag described above. **Legacy is
exempt entirely** — it is a faithful 1939 machine and does not get INOP-38
procedure layered on top of it.

## 11. Non-goals

INOP will never:

- become a block cipher, or use hash functions in the cipher core
- add authenticated encryption, KDFs, or modern primitives
- depend on a cryptographic library
- claim security against a modern adversary
- optimise for throughput as an end in itself

**The graphical interface is a deliberate, bounded exception, not a
reversal of this section.** The terminal remains the primary, complete
interface — it has zero dependencies beyond a compiler and works exactly
as it always has. The optional `INOP_WITH_GUI` build adds a single
settings-panel window (GLFW + raw OpenGL + stb_truetype, opt-in via a
terminal menu option) once GLFW became reliably buildable on the
operator's machine. It does not touch the cipher core, does not change
what a CLI-only build depends on, and does not open the door to a general
GUI framework — see `src/interface/gui.hpp`'s header comment and the
GUI-specific files under `src/interface/gui_*` for the boundary.

The category is the achievement. A better rotor machine, not a modern one.
