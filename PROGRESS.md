# Phase 3 progress

Branch `hardening/phase3`, cut from `hardening/phase1-2`. 15 commits, not
pushed. Not committed itself, per the brief. **Run complete.**

Full write-up: `INOPphase3report.md` in this directory. Tables in
`measurements/`.

## Done

| stage | commit | what |
|---|---|---|
| 0.1 | `5f5cd84` | gitignore gaps, DESIGN.md untracked |
| 0.2 | `3e83db8` | literal-digit separator stranded after a diacritic, fixed |
| 1 | `fb9904d` | `inop_langprobe` measurement dumper |
| 1.1-1.3 | `b6a4cec` | density, predictability, search space |
| 1.4-1.5 | `50ee1e1` | fingerprint, permuted-table recovery |
| 1 | `eaa71da` | stage summary |
| 2.1-2.2 | `a1d300c` | core purity and RNG purity at build time |
| 2.3-2.5 | `c1c1f0d` | 22 guard checks, each verified by removal |
| 2.6 | `277e412` | notch rule gets one home, GUI stops truncating |
| 2.7 | `a1af1e5` | -Werror switch, sanitizer preset, vcpkg pin, CI |
| 3 | `3e340db` | `inop_bombe`, calibrated against Legacy |
| 3 | `9a79e18` | ablation, notch sweep, transposition sweep |
| 3 | `1de156c` | crash elimination |
| - | `173452e` | no apostrophes in the text this run added |
| - | `a66b5f6` | README pass: no more dangling DESIGN.md references |

## Headlines

- Item 4 contradicted, item 8 overkill, section E half right.
- Every section 6 guard now fails by name when removed.
- Daily regeneration stops the bombe dead. The double pass costs about 7x
  and does not. The notch curve is flat. Reversal and the half-swap cost
  the same.
- The Legacy control turned up the middle-rotor double-step anomaly
  unprompted, which is the best evidence the instrument is real.

## Verification

ctest green, `--self-test` green, 3,024 benchmark cases 0 failures, bombe
self-check green, `cli-werror` clean, GUI builds. Before and after both in
the report.

## Not done, deliberately

Bombe phase 2 (steckered search with a diagonal board), the diacritic
overlay redesign, the per-message indicator, README rewrite. Reasons in the
report under "What I chose not to do".

## Environment note

The Bash tool on this machine cannot capture the stdout of this program —
`inop.exe --self-test` comes back empty through it. PowerShell works. Every
run in the report went through PowerShell.


roadmap from the word file

INOP Project: Master Braindump & Technical Spec
1. UI/UX, Animations & "Theater"
•	Performance Context: The C++ backend pipes ~32 million characters a second. Encryption/decryption is effectively instant. 
•	Artificial Processing Delay: To accommodate UI animations and give the encryption "weight," the visual process is artificially slowed down. 
o	Design Philosophy: This is purely for visual "theater." The actual computation is instant, but the UI will take at least X seconds (arbitrary, subject to change) to animate. If actual processing ever exceeds X, the real time is used. No one but the developer will know it's artificially slowed.
•	Account/Login: Circular image button top right (experimental, may merge with settings).
•	Audio Effects: Button clicks, background music, "INOP in progress" cue, Text-to-Speech (TTS).
•	Tooltips: Small hover pop-ups for ambiguous UI elements.
•	Tutorial System: 
o	First launch tutorial with "Focus Mode" (restricts clicks/Enter to intended areas until finished).
o	Options to skip on first launch and replay later in Settings.
•	Layout & Visual Adjustments:
2. Core Logic & Language Processing
•	Code Hygiene: Strip all comments from the codebase at all times during build/compilation.
🧩 The INOP Diacritic Mapping System (0-9 Base)
Rule: "Close marks stick alike." The first digit (0-9) represents the core shape family. The second digit represents the variation (position, smoothness, quantity). Appended to the end of the message between the actual end and the marker.
0: Meta / Base
•	0: Capitalize
•	01: Lowercase (reserved for future)
1: Horizontal Lines (—)
•	1: Macron (above)
•	11: Macron (below)
•	12: Stroke / Bar (horizontal through letter)
•	13: Underline (solid line directly below)
2: Forward Slants (/)
•	2: Acute (above)
•	21: Double Acute (above - Hungarian)
•	22: Acute (below)
•	23: Slash / Solidus (diagonal through letter)
3: Wedges / Angles (v)
•	3: Caron / Háček (above, sharp)
•	31: Bevel (smooth caron - custom INOP logic)
•	32: Caron (below)
4: Backward Slants ()
•	4: Grave (above)
•	41: Grave (below)
•	42: Comma below (reversed cedilla / backward tail)
5: Roofs / Double Slants (^)
•	5: Circumflex (above)
•	51: Circumflex (below)
6: Waves (~)
•	6: Tilde (above)
•	61: Tilde (below)
7: Curves & Hooks ( ( )
•	7: Breve (half-circle above)
•	71: Inverted Breve (half-circle opening down, above)
•	72: Breve (below)
•	73: Cedilla (small tail curving under)
•	74: Ogonek (little hook attaching to bottom right)
•	75: Horn (small hook attaching to top right - Vietnamese)
8: Dots (•)
•	8: Dot (above)
•	81: Dot (below)
•	82: Umlaut / Diaeresis (two dots above)
•	83: Umlaut / Diaeresis (two dots below)
9: Rings & Circles (°)
•	9: Ring (above)
•	91: Ring (below)
•	92: Overlay Circle (circle drawn around letter)
3. Settings & Configuration
•	Interface Language: Currently "English" (default).
•	INOP Script: Latin (default), Greek, Cyrillic, Hebrew, Hangul.
•	Arachnophobia Mode: "Sacred supreme setting." Default OFF. Warning: Removing this from the setting panel currently bricks the entire app.
4. Utilities & Quality of Life
•	Plaintext Buffer Clear: Dedicated "Clear" button and shortcut to wipe the input field.
•	Composite Copy Utility: Single "Copy Ciphertext + Marker" button to push both to the clipboard simultaneously.
5. Bug Fixes & Maintenance
•	Rotor Registry & Data Hygiene: 
o	Purge "Phantom Oxx Rotors" hiding in the registry/file loader.
o	Fix root cause of why they (and certain reflectors) survive wheel regeneration.
6. Pending / Future Considerations
•	The "Bombe": Feature is currently in active development. Shelved for this specific UI/logic pass; will be reintegrated later.
•	Filesystem revamp.
•	Installer / Uninstaller creation.
•	Actual custom art/assets for the GUI.
•	General future features not yet conceptualized.

ROADMAP APPENDIX: UI Logic & Quality of Life
1. Keybindings & Navigation (Finalized)

    Shift+C: Clear text in any writable container.
    Ctrl+C: Copy.
    Ctrl+V: Paste.
    Ctrl+X: Cut.
    Ctrl+A: Highlight all text.
    Ctrl+S: Save to current preset (NEW FEATURE).
    Ctrl+Shift+S: Save to new preset.
    Ctrl+Shift+C: Reserved / does nothing.

2. Text Container & Input Behavior

    Full Cursor Interaction: All text containers support clicking to place the cursor (to fix specific typos inline without deleting everything).
    Selection: Click and drag to highlight specific text.
    Deletion: Backspace / Delete removes selected text or the character at the cursor.
    Composite Copy Format: When copying ciphertext, the clipboard output is formatted as: [CIPHERTEXT]     [MARKER] (Ciphertext, exactly five spaces, then the marker).


clicking in containers still doesnt work
when super focused on a container operator must press escape to normal focus (use arrow keys as standard) other wise in superfocus arrowkeys move the "I" cursor around (left arrow one left, up arrow to beginning, right arrow to rightm down arrow to end)
settings reordering
tooltips: the mechanism ships and works, live only on the settings apply button. blocked on a list of which controls get one, operator to supply

diacritic transformer decisions 2026-09-06:
- one universal transformer replaces the 49 per language tables. the shape family code table above is unchanged
- a run of digits after a letter is exactly one whole code, so no longest match rule is needed
- a single slash between two codes stacks a second mark on the same letter. a1/12 is macron above plus stroke. this is what unblocks vietnamese
- a double slash means the digits after it are a literal number. ma2//5 is a with acute then the number 5. this replaces the old single slash literal escape
- capitals are now encoded. a0 is capital A. the case code always comes first, so a0/2 is capital A with acute
- accepted tradeoff: a capital after a space marks a sentence start in the ciphertext. the old scheme threw case away and leaked nothing here
- slot 31 bevel stays reserved and unused. unicode has one caron codepoint, so no character can decompose into a bevel
- old ciphertext will not decode under the new scheme. accepted, nothing is public

special characters BROKEN. these four are letters in their own right and not a base plus a mark, so the shape family table has no slot for them: ss-zet, ae, oe and turkish dotless i. today ae and oe are already spelled out and lost, while ss-zet is s0 and dotless i is i0, and the new table takes 0 for capitalise. under the transformer all four are spelled out, so they do not come back: strasse for the ss-zet word, and isik for the turkish one. turkish is the real casualty, it treats i and dotless i as different letters. the fix if wanted later is meta slots 02 to 05 beside capitalise, which would make all four round trip. parked on purpose 2026-09-06, operator to think about it
