# INOP — rotor cipher machine

A rotor cipher in the Enigma line, written in C++. Rejoice, for AES256 has a
challenger at its reach! (It doesn't. Keep reading.) Terminal only, no
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
notches. No hash functions, no block ciphers, no modern primitives. Every
part of it has a mechanical analogue that could have existed in 1940.

Everything else is operator procedure and is deliberately unconstrained.
Padding, cover traffic, markers, wheel generation, the double pass, message
batching and the language layer live in `pipeline.cpp`, `generator.cpp`,
`languages.cpp`, `settings.cpp`, `batch.cpp` and `main.cpp`, where they are
free to change without ever touching the cipher.

That split is also why the core is worth writing in C++ and the rest is not
especially: the core is frozen by the rule above, so it will never need
rewriting.

## Suites

| Suite    | Alphabet | Rotors | Wheels                      | Reflectors | Blocks |
|----------|----------|--------|------------------------------|------------|--------|
| Legacy   | 26       | 3      | I–VII, historic notches     | A B C      | 5      |
| INOP-38  | 38       | 5      | R1–R10, notches per message | D E F G H  | 16     |

The 38-symbol alphabet is `a–z`, `0–9`, `#` and `/`. `#` carries the space, so
sentences survive a round trip; `/` allows dates like `28/2/1941`. Legacy has
neither, which is exactly why INOP-38 exists — a Legacy machine silently
drops the timestamp out of a weather report.

**Legacy is locked.** It is a 1939 machine, not a suite with options: no
padding, no double pass, no reflector motion, and output in 5-letter groups
the way it went out over the wire. The INOP-exclusive features below —
diacritic folding, the language tag, the sign-off check — are not offered
when it is selected either; Legacy gets exactly the historic machine and
nothing else.

**Everything is lowercase.** `preprocess()` folds input to lowercase, not
upper — a change from earlier builds. It matters specifically for languages
with case-dependent distinct letters, Turkish's dotless *ı* being the sharp
example (see below), and it's simply the more natural home for a scheme that
already spends digits `1`–`8` as diacritic markers.

## Build

```sh
cmake -B build && cmake --build build
ctest --test-dir build      # correctness, entropy and throughput checks
./build/inop                # interactive session
```

Or by hand:

```sh
g++ -std=c++11 -O2 -o inop src/*.cpp          # POSIX
g++ -std=c++11 -O2 -o INOP.exe src/*.cpp -lbcrypt   # Windows
```

C++11 is enough. On Windows, `BCryptGenRandom` supplies entropy and needs
`-lbcrypt`; everything else uses `/dev/urandom`. Nothing else is linked.

**Upgrading from an older build:** the alphabet case flip means anything
generated under the old uppercase convention — `inop_wheels.txt`,
`inop_keysheet.txt`, `inop.settings` — will fail validation and get rejected
with a clear error rather than silently misbehaving. Regenerate them from
the maintenance menu; they're local, gitignored key material, not something
that needs migrating in place.

## Session commands

| Command | Effect |
|---------|--------|
| *(text)* | encipher, and show the round trip as a check |
| `:d` | decipher a ciphertext (asks for the marker) |
| `:b` | batch process pasted or file-based messages |
| `:i` | show the active settings again |
| `:s` | write the current settings to `inop.settings` |
| `:q` | quit |

Case does not matter for commands, and `:quit` / `:help` / `:info` also
work. Anything starting with `:` that is not a command is refused rather
than enciphered — a mistyped command should never quietly become a message.

## The numeral-suffix diacritic scheme

INOP-38's alphabet has no accented letters — it's `a-z0-9#/`, nothing more —
so a naive pipeline just drops diacritics on the floor, the same way Legacy
drops digits. That loses real information: *é* and *e* stop being
distinguishable, and Pinyin's four tones on *ā/á/ǎ/à* collapse to one
letter. Since INOP-38 already carries digits, the fix reuses them rather
than inventing new symbols: an accented letter folds to its base letter
followed by a digit naming which mark it carried.

| Digit | Diacritic | Examples |
|---|---|---|
| 1 | macron | ā → a1 |
| 2 | acute | á → a2 |
| 3 | caron (also reused for breve) | ǎ → a3, ň → n3, ğ → g3, ă → a3 |
| 4 | grave | à → a4 |
| 5 | circumflex | â → a5, ê → e5, ô → o5 |
| 6 | umlaut / diaeresis | ü → u6, ö → o6, ä → a6 |
| 7 | tilde | ñ → n7, ã → a7, õ → o7 |
| 8 | a letter genuinely unique to one language | German ß → s8, Catalan l·l → l8, Turkish dotless ı → i8 |

Encoding is lossless in a way plain accent-stripping isn't: `e2` can only
have come from *é*, never from *e*, so decryption can put the accent back
exactly. This is also, not coincidentally, exactly Pinyin's own tone-number
convention — *mā/má/mǎ/mà* already write as *ma1/ma2/ma3/ma4* — so Pinyin
input needs no special handling at all; it already speaks this scheme
natively.

Three marks are **dropped with no encoding**, an accepted and deliberate
loss: cedilla (ç), the Czech ring-above (ů), and the Romanian comma-below
(ș/ț). Turkish's ş, which carries a visually similar mark, is dropped the
same way — only the base letter survives.

Officially supported: Latin, English, Spanish, Catalan, Dutch, Portuguese,
French, Italian, German, Indonesian, Malay, Tagalog, Mandarin Chinese (via
Pinyin), Czech, Slovak, Turkish, Romanian, Slovenian, Maori. `fold_diacritics()`
and `resubstitute()` live in `src/languages.cpp`; encoding is the same for
every language (a given accented character always folds the same way),
because decoding is where the language actually matters — digit 3 alone
covers Pinyin's caron *and* Romanian's breve, two different marks that never
show up in the same language, so only the declared language can tell `a3`
apart afterward.

**Turkish gets case-aware folding.** Plain lowercasing would turn a
word-initial capital *I* into dotted *i*, which is wrong — in Turkish, *I*
lowercases to dotless *ı*, and *İ* lowercases to dotted *i*. Both letters in
"Işık" get marked `i8`, not just the internal one; that's correct, not a
bug.

A second pass fixes a related ambiguity: digits are both literal alphabet
characters (phone numbers, dates, room numbers all appear in real
plaintext) *and* the diacritic marker above, and `a2` is genuinely ambiguous
between "á, folded" and "the letters a and 2, sitting next to each other."
Whenever a literal digit in the raw input directly follows a letter with no
separator, a `/` gets force-inserted between them — "Room A2" becomes
"room a/2". A diacritic-fold pair never has a separator; a forced literal
digit always does. On decrypt, that `/` is what tells `resubstitute()` to
strip it and hand back a plain number instead of trying a lookup.

```
Room A2               -> room a/2               -> room a2
Côte d'Ivoire          -> co5te divoire          -> côte divoire
Château Latour 1964    -> cha5teau latour 1964   -> château latour 1964
```

(Apostrophes are punctuation and get stripped like any contraction, same as
today — "d'Ivoire" permanently loses the apostrophe.)

Non-Latin scripts are a different problem this scheme doesn't try to solve.
Mandarin needs full transliteration to Pinyin by the operator before it's
INOP-safe input at all — that's romanization, not diacritic folding, and
happens upstream of everything above.

## Morse, hex, binary — already plaintext

These aren't a separate input mode. INOP-38's alphabet already contains
`0-9`, the hex letters `a-f`, and letters generally, so a hex string or a
binary string is already valid plaintext with no new code path required — it
just goes in. Morse is operator-defined: any two in-alphabet characters can
stand in for dot and dash (`.-` works as well as anything, since `.` isn't
in the alphabet and gets dropped, but `x` and `o` round-trip fine). Try
`deadbeef` or `101100111` at the message prompt; they come back unchanged.

## Human-readable decrypt

Raw INOP-38 ciphertext decrypts to raw INOP-38 plaintext — numeral suffixes
and all. Reading `cha5teau latour 1964` back as `château latour 1964`
without doing the substitution by hand is the point of `resubstitute()`,
called automatically after `:d` and inside batch processing whenever the
suite is INOP-38.

To make that possible without prior arrangement, the operator names the
message's language at encryption time, and its 2-letter code (`en`, `fr`,
`tr`, ...) is appended **unencrypted** to the very end of the transmitted
ciphertext, separated by two spaces:

```
cipher  fjqm cvpx ... zk9r  fr
```

The decrypting side reads that tag first, before touching the cipher body,
and uses it to pick the right lookup table. This sits entirely on the
ciphertext side of things — it has no interaction with the sign-off check
below, which governs the plaintext before encryption.

Output stays lowercase throughout; there's no attempt to restore original
capitalization beyond the Turkish casing rule above, since general case
information is discarded during preprocessing. Existing, accepted
limitation.

## Sign-off phrase

Before an INOP-38 message goes out — single or inside a batch — INOP checks
whether the preprocessed text **ends with** `lotuses to antraxia`. Appearing
somewhere in the middle doesn't count; it has to be trailing content. If
it's missing, the operator is asked whether to append it, per message, even
inside a batch. **Legacy is exempt entirely** — it's a faithful 1939 machine
and doesn't get INOP-38 procedure layered on top of it.

## Batch processing

`:b` reads a set of messages — pasted directly (blank line between each,
`:end` to finish) or from a file — and enciphers each one under a rotor
configuration pulled from `inop_keysheet.txt`. Two ways to pick
configurations:

- **one indexed entry** for every message in the batch, or
- **sequentially through the file**, one entry per message, in file order.

A batch input file is capped at 1.44MB — a standard 3.5" floppy's worth —
checked before a single byte is read; an oversized file is rejected cleanly
rather than partially processed.

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

Key sheets come out in the same directive format the program reads, as
numbered `# --- entry N ---` blocks, so a block copies straight into
`inop.settings` — or the whole file feeds batch processing above, indexed
by that same entry number.

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
src/pipeline.*      padding, markers, double pass, suite locks, digit collision
src/languages.*      numeral-suffix diacritic scheme, resubstitution
src/settings.*       machine configuration: parse, validate, load, save
src/batch.*           batch message splitting, file-size-capped batch input
src/generator.*      wheel and key sheet generation
src/main.cpp         terminal interface
src/benchmark_main.cpp  combinatorial benchmark harness entry point
benchmark/            corpus text and the benchmark log
```

## Performance

Rotor wiring is baked at construction into per-offset lookup tables, so a
traversal is one indexed read rather than two modulo operations. A rotor caches
a pointer to its current table row and updates it only when it steps. This
changes no behaviour — output is identical to the original Python
implementation symbol for symbol.

The live message pipeline stays at human words-per-minute throughput on
purpose — at 1940s speeds the binding constraint was an operator typing, and
that's not something to "optimize" away. The benchmark harness under
`benchmark/` is a separate, offline concern; see `GPU_FEASIBILITY.md` for
why it — and only it — is a candidate for GPU-parallel execution across
independent test messages, never the live pipeline.

**Hamlet, as a demonstration.** The first 8 scenes (all of Act I and II, plus
Act III Scene I — `benchmark/corpus/hamlet_first8scenes.txt`, sourced from
Project Gutenberg) go through the full pipeline — diacritic fold, digit
collision, padding, double pass, decrypt, exact-match verification — via
`inop_benchmark --hamlet benchmark/corpus/hamlet_first8scenes.txt`. On this
machine: 81,924 source characters, encrypt in ~111ms, decrypt in ~105ms,
exact match on decrypt. That figure includes real pipeline overhead (fold,
padding, double pass) on real prose, not the raw single-rotor-pass number —
the two aren't measuring the same thing, so don't expect them to match.

The full combinatorial benchmark — all 19 languages × 7 message categories,
5 independent configurations × 5 messages each, 3,325 cases — passes with
zero failures; see `log.txt` (CSV, one row per test case) for the raw
numbers.
