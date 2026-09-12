# INOP, a rotor cipher machine

Rejoice, for AES256 has a challenger at its reach! (It doesnt. Keep reading.)

INOP is a rotor cipher machine in the Enigma line, written in C++. It is a
thought experiment and not a security product. It asks what a machine like
this would have looked like if it had been built in the 1940s, and it keeps
the constraints of that era. It is not a modern cipher and doesnt try to be.

Several things here that look like defects are chosen on purpose. The rules
that matter are enforced by the build. It refuses a cipher core carrying a
modern primitive, and `--self-test` fails by name when one of the guards is
removed. Where a design decision has been measured, the numbers are in
[measurements/](measurements/).

## What this is

Two suites ship:

| Suite    | Alphabet | Rotors | Wheels                       | Reflectors | Blocks |
|----------|----------|--------|-------------------------------|------------|--------|
| Legacy   | 26       | 3      | I-VII, historic notches      | A B C      | 5      |
| INOP-38  | 38       | 5-10   | R1-R10, notches per message  | D E F G H  | 16     |

**Legacy** is a faithful, locked 1939 machine: no padding, no double pass,
no reflector motion, 5-letter output groups, exactly the historic machine
and nothing else. It exists as a correctness reference, checked at startup
against a known historic vector.

**INOP-38** is the actual machine. Its 38-symbol alphabet (`a-z`, `0-9`,
`#` for space, `/` for a literal slash) is why it exists at all. A Legacy
machine silently drops digits and spaces from a message, and that is the
failure INOP-38 was built to avoid. The operator picks a rotor count from 5
to 10 once per session, before anything else is asked for.

`src/logic/inop.hpp` and `src/logic/inop.cpp` may only ever contain a rotor
machine: wired permutations, a fixed-point-free reflector, a plugboard,
ring settings, notches, with no hash functions, no block ciphers and no
modern primitives. Everything else (padding, cover traffic, the double
pass, wheel generation, the language layer, the terminal interface) is
operator procedure, deliberately unconstrained, and lives in `pipeline.*`,
`generator.*`, `languages.*`, `settings.*`, `batch.*` and `main.cpp`, free
to change without ever touching the core. The core is frozen so it never
needs rewriting, and the prep layer is where this project evolves.
`cmake/check_source_rules.cmake` checks the split at build time, and fails
the build quoting the offending line.

## Why it is useful

It is not useful as a way to keep a secret. INOP makes no claim of security
against a modern attacker, has no diffusion, and has received no
professional cryptanalytic review. Dont use it for anything real.

It is useful as a working, buildable answer to what a better rotor machine
would have looked like. Every design choice traces back to a real historic
weakness. The double pass removes Enigmas fatal no-self-encipherment
property, the one that let Bletchley Park crib-drag. Daily wheel
regeneration removes the fixed-wiring assumption every Bletchley technique
depended on. The notch count is picked to keep as many rotors as possible
turning inside a single message, not to stretch a period that was already
longer than any message would ever reach. If you are curious how rotor
cryptanalysis actually worked, or what a determined but period-honest
redesign of Enigma would look like, that is what this project demonstrates.

All three of those claims have now been run against an actual attack.
`inop_bombe` recovers a Legacy setting in about two seconds, and against
INOP-38 with the wheels regenerated it recovers nothing at all, because the
answer is not in the search space. The double pass costs that attacker
roughly 7x and doesnt stop it. The notch count turns out to make no
measured difference to search cost. See
[measurements/3-bombe.md](measurements/3-bombe.md), which says where the
numbers disagree with the reasoning.

## How to get started

### Build

```sh
cmake -B build && cmake --build build
ctest --test-dir build      # correctness, entropy and throughput checks
./build/inop                # interactive session
```

The build runs `cmake/check_source_rules.cmake` before it compiles
anything. It fails the build, names the rule and quotes the line if the
cipher core picks up a modern primitive, or if anything outside the
benchmark harness reaches for `rand()` or `std::random_device`. It runs on
every build, so skipping the tests doesnt skip it.

Three offline targets build alongside `inop`. None of them is ever linked
into the live message pipeline, and none reads or writes key material:

```sh
./build/inop_benchmark --languages all     # correctness across 48 languages
./build/inop_langprobe --out probe_out     # dump the folded symbol streams
./build/inop_bombe --legacy-phase1         # break the Legacy machine
./build/inop_bombe --inop-ablation         # and measure what INOP costs it
```

`-DINOP_WERROR=ON` turns warnings into errors, which is how CI builds it.

Building by hand works too. `src/` is split by role (`logic/`, `settings/`,
`interface/`, `benchmark-debug/`, see the header comment in
`CMakeLists.txt`), so this needs `-I` for each and an explicit file list
instead of a single `src/*.cpp` glob. It also needs `nlohmann-json` on the
include path — add `-I` for wherever your copy lives, since the data files
are JSON:

```sh
g++ -std=c++23 -O2 -Isrc/logic -Isrc/settings -Isrc/interface -o inop \
    src/logic/inop.cpp src/logic/registry.cpp src/logic/pipeline.cpp \
    src/logic/rng.cpp src/logic/generator.cpp src/logic/languages.cpp \
    src/logic/transform.cpp \
    src/settings/settings.cpp src/interface/batch.cpp \
    src/interface/gui_stub.cpp src/interface/main.cpp             # POSIX
g++ -std=c++23 -O2 -Isrc/logic -Isrc/settings -Isrc/interface -o INOP.exe ^
    src/logic/inop.cpp src/logic/registry.cpp src/logic/pipeline.cpp ^
    src/logic/rng.cpp src/logic/generator.cpp src/logic/languages.cpp ^
    src/logic/transform.cpp ^
    src/settings/settings.cpp src/interface/batch.cpp ^
    src/interface/gui_stub.cpp src/interface/main.cpp -lbcrypt    # Windows
```

Built as C++23. One dependency, `nlohmann-json`, which is header-only:
the wheel files, settings and key sheets are JSON as of 2.3.0, and the
reader lives in `src/logic` and `src/settings`, so every build needs it
rather than only the GUI. That cost was accepted deliberately in exchange
for one file format across both interfaces — a configuration saved in the
window opens in the terminal and the other way round. On Windows,
`BCryptGenRandom` supplies entropy and needs `-lbcrypt`; everything else
uses `/dev/urandom`. Nothing else is linked in a CLI-only build.

### Optional GUI

`cmake -B build -DINOP_WITH_GUI=ON` (see the `gui` preset in
`CMakePresets.json` for a ready-made vcpkg + MinGW + `x64-mingw-static`
invocation) additionally builds a windowed interface: GLFW for the window
and context, raw OpenGL for drawing, `stb_truetype` for text,
`nlohmann-json` for the Save/Load Setup file format. It is reached from a
menu option in the same terminal session, not a separate executable, and a
CLI-only build never links any of it. GLFW, OpenGL and `stb_truetype` stay
GUI-only; `nlohmann-json` is the one that is now needed everywhere. This
stays a deliberate, bounded exception: it doesnt touch the cipher core and
doesnt open the door to a general GUI framework. The header comment in
`src/interface/gui.hpp` draws the boundary.

Settings → Graphics provides V-Sync and a frame-rate limit of 30, 60, 120,
144, or 180 FPS, plus Unlimited. Apply saves both choices and updates the
running window. V-Sync is on by default; Unlimited removes the additional
frame cap, while V-Sync can still limit presentation to the display refresh
rate. Reset to default restores those initial choices.

### First session

```
1  run INOP
2  maintenance   (generate wheels or key sheets)
3  GUI                  (experimental, opt-in)
4  quit
```

The first time through, pick option 2 and generate a rotor/reflector batch
and a key sheet before sending anything you care about. The program ships
with a small built-in demo and regression wheel set, not real key material.

Once a machine is configured, the session commands are:

| Command | Effect |
|---------|--------|
| *(text)* | encipher, and show the round trip as a check |
| `:d` | decipher a ciphertext (asks for the marker) |
| `:b` | batch process pasted or file-based messages |
| `:i` | show the active settings again |
| `:s` | write the current settings to `inop_settings.json` |
| `:q` | quit |

Case doesnt matter for commands, and `:quit` / `:help` / `:info` also work.
Anything starting with `:` that isnt a recognized command is refused
instead of enciphered, so a mistyped command never quietly becomes a
message.

`:d` refuses a ciphertext containing anything outside the alphabet, naming
the character and its position instead of quietly discarding it. A hyphen
picked up from a wrapped line is the common case, and under Legacy so is
any digit. Dropping one would shift every position after it and hand back
noise, so retyping the line is the only useful answer and the message says
so.

### Whats inside, briefly

A few features exist that are worth knowing about before you start:

- **The universal text transformer.** INOP-38s alphabet has no accented
  letters, so `transform()` folds supported Unicode letters to a base letter
  plus one or more numeral codes. A single slash joins multiple mark codes on
  one letter. A double slash introduces literal digits after a letter or mark.
  Code `0` carries capitalization, so `untransform()` restores case as well as
  marks. The transformer takes no language and lives in
  `src/logic/transform.cpp`; `src/logic/languages.cpp` contains the supported
  interface language list only. How much the wire grammar costs, measured
  against real corpus text in 48 languages, is in [measurements/](measurements/).
- **Morse, hex, binary** are not a separate input mode. INOP-38s alphabet
  already contains `0-9`, the hex letters `a-f`, and letters generally, so
  a hex string or a binary string is already valid plaintext. Try
  `deadbeef` or `101100111` at the message prompt.
- **Human-readable decrypt.** Raw INOP-38 ciphertext decrypts back to folded
  INOP-38 plaintext, numeral suffixes and all. `untransform()` restores the
  supported Unicode text automatically. The displayed 3-letter language tag
  identifies interface context but does not control decoding. Capitalization
  is restored when the folded text contains code `0`.
- **Batch processing** (`:b`) reads a set of pasted or file-based messages
  and enciphers each one under a rotor configuration pulled from
  `inop_keysheet.json`, either one indexed entry for every message or
  sequentially through the file. Input files are capped at 1.44MB.
- **Maintenance** (menu option 2) generates rotor batches, reflector
  batches, and key sheets, all checked against a live entropy self-test and
  rejected if they turn out degenerate. A silently broken generator is the
  worst failure this program can have, because it doesnt crash and its
  output still looks plausible.

`inop_rotors.json`, `inop_reflectors.json`, `inop_keysheet.json` and
`inop_settings.json` are real or potential key material and are in
`.gitignore`. Never commit them. The 2.2.x names (`inop_wheels.txt`,
`inop_keysheet.txt`, `inop.settings`) are still ignored too, because a
converted install has both on disk.

**Upgrading from 2.2.x:** the data files moved from plain text to JSON, and
the wheel file split in two. Nothing has to be done by hand — on the first
run, `inop_wheels.txt` is converted into `inop_rotors.json` plus
`inop_reflectors.json`, and `inop.settings` into `inop_settings.json`. The
originals are never deleted and never overwritten, and a conversion is
skipped entirely if its target already exists, so a stale text file cannot
replace current key material. Key sheets are not converted: regenerate one
from the maintenance menu, or keep using the old build to read an old
sheet.

**Upgrading from an older build:** the alphabet case flip (INOP-38 is
lowercase, Legacy stays uppercase) means anything generated under the old
uppercase convention will fail validation and get rejected with a clear
error instead of silently misbehaving. Regenerate it from the maintenance
menu.

Separately, **stored ciphertext doesnt carry across this version.** The
transposition between the two passes of the double pass changed from a
reversal to a half-swap, and the reflector now turns on its own counter, so
the same setup sheet produces different ciphertext than it used to. Key
sheets, wheel files, settings files and the wire format are all unaffected.
Only already-enciphered traffic is. Decipher anything you still need with
the old build before upgrading.

Reversal fixes the middle index of an odd-length message, which hands back
the exact property the double pass exists to destroy, at a position anyone
can compute from the length. The half-swap has no fixed index at all. The
reflector used to advance once per character, which is what the fast rotor
does, so its position was a relabelling of that rotor and contributed no
state. It now runs one tooth short of the alphabet.

One visible behaviour change comes with it. With **padding switched off**, a
message whose body length is odd gets one extra symbol drawn from the
alphabet before enciphering, because the half-swap needs an even length.
That symbol comes back on the round trip as a single trailing character.
With padding on, the default, it never happens, since padding already
rounds the body to a whole number of blocks.

## Where to get help

Start with `./build/inop --help` and `./build/inop --self-test`. The second
runs every correctness, entropy, and throughput check the project has, and
is the fastest way to confirm a build or a change didnt break anything.

For anything the self-test doesnt answer, [measurements/](measurements/) is
the next place to look. It holds one markdown table per experiment, each
saying what it settles and in which direction, including the ones that came
out against the design. If your question isnt answered there either, open
an issue on this repository.

## Who maintains this

This repository is maintained through normal GitHub pull requests and
issues, with no separate contribution process. Before proposing a change to
`src/logic/inop.hpp` or `src/logic/inop.cpp` specifically, note that the
pair may only ever contain a rotor machine, and a change that would turn it
into anything else (a hash-based construction, a modern block cipher, a
dependency on a crypto library) will not be accepted regardless of how it
is justified. The build enforces that: it scans those two files for an
include allowlist and for the symbols a modern primitive would arrive
under, and refuses to compile if one turns up.

## Repository layout

```
src/logic/            the cipher core + prep layer
  inop.hpp/cpp           cipher core: tables, stepping, signal path
  registry.*             suite definitions, wirings, wheel loading
  pipeline.*             padding, markers, double pass, suite locks
  rng.*                  OS entropy, unbiased sampling, health check
  generator.*            wheel and key sheet generation
  languages.*             numeral-suffix diacritic scheme

src/settings/          machine configuration: parse, validate, load, save

src/interface/         every user-facing entry point
  main.cpp                terminal interface
  batch.*                 batch message splitting
  gui.hpp/gui_stub.cpp     GUI entry point + CLI-only fallback
  gui_render.*             OpenGL drawing + text (GUI builds only)
  gui_widgets.*            small immediate-mode widget set (GUI builds only)
  gui_setup_panel.*        the machine setup screen (GUI builds only)
  gui_main_menu.*          the main menu screen (GUI builds only)
  gui_enciphering_panel.*  the enciphering screen (GUI builds only)
  gui_config_store.*       Save/Load Setup JSON (GUI builds only)
  gui_file_tile_panel.*    reusable file-tile browser overlay (GUI builds only)

src/benchmark-debug/  combinatorial benchmark harness, and the diacritic
                       scheme dumper (offline only, never wired into the
                       live message pipeline)

src/bombe/            the cryptanalysis harness. An attack tool, filed
                       apart from the correctness harness because it is a
                       different kind of thing

cmake/                 the source rules the build enforces

benchmark/             corpus text, the benchmark log, and the analysis
                       script for the diacritic measurements

measurements/          one markdown table per experiment, and what each
                       one settles
```
