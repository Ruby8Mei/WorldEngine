# INOP — rotor cipher machine

Rejoice, for AES256 has a challenger at its reach! (It doesnt. Keep reading.)

INOP is a rotor cipher machine in the Enigma line, written in C++. It is a
thought experiment, not a security product — what a machine like this would
have looked like if it had been built in the 1940s, with the constraints of
that era honored rather than engineered around. It is not a modern cipher
and does not try to be.

Read [DESIGN.md](DESIGN.md) before reviewing or changing anything here.
Several things that look like defects are chosen on purpose, and DESIGN.md
is where the reasoning, the hard constraints, and the open items live.

## What this is

Two suites ship:

| Suite    | Alphabet | Rotors | Wheels                       | Reflectors | Blocks |
|----------|----------|--------|-------------------------------|------------|--------|
| Legacy   | 26       | 3      | I-VII, historic notches      | A B C      | 5      |
| INOP-38  | 38       | 5-10   | R1-R10, notches per message  | D E F G H  | 16     |

**Legacy** is a faithful, locked 1939 machine — no padding, no double pass,
no reflector motion, 5-letter output groups, exactly the historic machine
and nothing else. It exists as a correctness reference, checked at startup
against a known historic vector.

**INOP-38** is the actual machine. Its 38-symbol alphabet (`a-z`, `0-9`,
`#` for space, `/` for a literal slash) is why it exists at all — a Legacy
machine silently drops digits and spaces from a message, which is exactly
the failure INOP-38 was built to avoid. The operator picks a rotor count
from 5 to 10 once per session, before anything else is asked for.

`src/logic/inop.hpp` and `src/logic/inop.cpp` may only ever contain a rotor
machine: wired permutations, a fixed-point-free reflector, a plugboard,
ring settings, notches — no hash functions, no block ciphers, no modern
primitives. Everything else (padding, cover traffic, the double pass, wheel
generation, the language layer, the terminal interface) is operator
procedure, deliberately unconstrained, and lives in `pipeline.*`,
`generator.*`, `languages.*`, `settings.*`, `batch.*` and `main.cpp`, free
to change without ever touching the core. See DESIGN.md section 1 for why
that split exists.

## Why it is useful

Not as a way to actually keep a secret — DESIGN.md says plainly that INOP
makes no claim of security against a modern attacker, has no diffusion, and
has received no professional cryptanalytic review. Do not use it for
anything real.

What it is useful for: a working, buildable answer to "what would a better
rotor machine have looked like." Every design choice traces back to a real
historic weakness — the double pass exists specifically to remove Enigmas
fatal no-self-encipherment property that let Bletchley Park crib-drag;
daily wheel regeneration removes the fixed-wiring assumption every
Bletchley technique depended on; one notch per rotor maximizes the stepping
period instead of shortening it. If you are curious how rotor cryptanalysis
actually worked, or what a determined but period-honest redesign of Enigma
would look like, that is what this project demonstrates. See DESIGN.md
sections 2 and 5 for the full reasoning behind each of these.

## How to get started

### Build

```sh
cmake -B build && cmake --build build
ctest --test-dir build      # correctness, entropy and throughput checks
./build/inop                # interactive session
```

Or by hand. `src/` is split by role (`logic/`, `settings/`, `interface/`,
`benchmark-debug/` — see `CMakeLists.txt`s header comment), so this needs
`-I` for each and an explicit file list rather than a single `src/*.cpp`
glob:

```sh
g++ -std=c++23 -O2 -Isrc/logic -Isrc/settings -Isrc/interface -o inop \
    src/logic/inop.cpp src/logic/registry.cpp src/logic/pipeline.cpp \
    src/logic/rng.cpp src/logic/generator.cpp src/logic/languages.cpp \
    src/settings/settings.cpp src/interface/batch.cpp \
    src/interface/gui_stub.cpp src/interface/main.cpp             # POSIX
g++ -std=c++23 -O2 -Isrc/logic -Isrc/settings -Isrc/interface -o INOP.exe ^
    src/logic/inop.cpp src/logic/registry.cpp src/logic/pipeline.cpp ^
    src/logic/rng.cpp src/logic/generator.cpp src/logic/languages.cpp ^
    src/settings/settings.cpp src/interface/batch.cpp ^
    src/interface/gui_stub.cpp src/interface/main.cpp -lbcrypt    # Windows
```

Built as C++23, zero dependencies beyond the compiler. On Windows,
`BCryptGenRandom` supplies entropy and needs `-lbcrypt`; everything else
uses `/dev/urandom`. Nothing else is linked in a CLI-only build.

### Optional GUI

`cmake -B build -DINOP_WITH_GUI=ON` (see `CMakePresets.json`s `gui` preset
for a ready-made vcpkg + MinGW + `x64-mingw-static` invocation) additionally
builds a settings-panel window — GLFW for the window/context, raw OpenGL
for drawing, `stb_truetype` for text, `nlohmann-json` for the Save/Load
Settings file format. It is reached from a menu option in the same
terminal session, not a separate executable, and a CLI-only build (the
default) never links any of it. This is a deliberate, bounded exception to
the zero-dependency rule above, not a reversal of it — see DESIGN.md
section 10.

### First session

```
1  run INOP
2  maintenance   (generate wheels or key sheets)
3  GUI                  (experimental, opt-in)
4  quit
```

The first time through, pick option 2 and generate a rotor/reflector batch
and a key sheet before sending anything you care about — the program ships
with a small built-in demo/regression wheel set, not real key material.

Once a machine is configured, the session commands are:

| Command | Effect |
|---------|--------|
| *(text)* | encipher, and show the round trip as a check |
| `:d` | decipher a ciphertext (asks for the marker) |
| `:b` | batch process pasted or file-based messages |
| `:i` | show the active settings again |
| `:s` | write the current settings to `inop.settings` |
| `:q` | quit |

Case does not matter for commands, and `:quit` / `:help` / `:info` also
work. Anything starting with `:` that is not a recognized command is
refused rather than enciphered, so a mistyped command never quietly becomes
a message.

### Whats inside, briefly

A few features exist that are worth knowing about before you start, each
covered in full in DESIGN.md rather than here:

- **The numeral-suffix diacritic scheme.** INOP-38s alphabet has no accented
  letters, so an accented character folds to its base letter plus a digit
  naming which mark it carried (é becomes `e2`, for instance) instead of
  being dropped. It supports 49 languages by name, is fully reversible, and
  needs no special handling for Pinyin input since it already speaks this
  scheme natively. `fold_diacritics()` and `resubstitute()` in
  `src/logic/languages.cpp` are the entry points; see DESIGN.md for the
  full digit table and language list.
- **Morse, hex, binary** are not a separate input mode — INOP-38s alphabet
  already contains `0-9`, the hex letters `a-f`, and letters generally, so
  a hex string or a binary string is already valid plaintext. Try
  `deadbeef` or `101100111` at the message prompt.
- **Human-readable decrypt.** Raw INOP-38 ciphertext decrypts back to raw
  INOP-38 plaintext, numeral suffixes and all — `resubstitute()` turns that
  back into normal text automatically, driven by a 3-letter language tag
  appended unencrypted to the end of the transmitted ciphertext.
  Capitalization is not restored; output stays lowercase.
- **Batch processing** (`:b`) reads a set of pasted or file-based messages
  and enciphers each one under a rotor configuration pulled from
  `inop_keysheet.txt`, either one indexed entry for every message or
  sequentially through the file. Input files are capped at 1.44MB.
- **Maintenance** (menu option 2) generates rotor batches, reflector
  batches, and key sheets, all checked against a live entropy self-test and
  rejected if they turn out degenerate — a silently broken generator is the
  worst failure this program can have, since it does not crash and its
  output still looks plausible.

`inop_wheels.txt`, `inop_keysheet.txt` and `inop.settings` are real or
potential key material and are in `.gitignore`. Never commit them.

**Upgrading from an older build:** the alphabet case flip (INOP-38 is
lowercase, Legacy stays uppercase) means anything generated under the old
uppercase convention will fail validation and get rejected with a clear
error rather than silently misbehaving. Regenerate it from the maintenance
menu.

## Where to get help

Start with `./build/inop --help` and `./build/inop --self-test` — the
second one runs every correctness, entropy, and throughput check the
project has, and is the fastest way to confirm a build or a change did not
break anything.

For anything the self-test does not answer, DESIGN.md is the deeper
reference: the threat model, every deliberate decision and why it is not a
bug, the guards that must never be removed, and the known open items. If
your question is not answered there either, open an issue on this
repository.

## Who maintains this

This repository is maintained through normal GitHub pull requests and
issues — there is no separate contribution process. Before proposing a
change to `src/logic/inop.hpp` or `src/logic/inop.cpp` specifically, read
DESIGN.md section 1: that pair of files may only ever contain a rotor
machine, and a change that would turn it into anything else (a hash-based
construction, a modern block cipher, a dependency on a crypto library) will
not be accepted regardless of how it is justified.

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
  gui_config_store.*       Save/Load Setup JSON (GUI builds only)
  gui_file_tile_panel.*    reusable file-tile browser overlay (GUI builds only)

src/benchmark-debug/  combinatorial benchmark harness (offline only,
                       never wired into the live message pipeline)

benchmark/             corpus text and the benchmark log
```
