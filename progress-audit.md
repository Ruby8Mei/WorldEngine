# PROGRESS.md against the real application

Read only. Nothing here was fixed and nothing in PROGRESS.md was edited.
Checked 2026-09-06 against the working tree at head `aa35be3` plus this
session's uncommitted work.

Three verdicts are used:

- **true** the claim matches the code and the running application
- **stale** it was true once and has been overtaken
- **wrong** it does not match and, in some cases, never did

---

## 1. Keybindings and Navigation, marked "Finalized"

Six of the eight lines are wrong. This section is the worst part of the
file.

| Claim | Verdict | What is actually there |
|---|---|---|
| Shift+C clears text in any writable container | **wrong** | Never built, and it cannot be. Shift+C is how a capital C is typed, so it can never also mean Clear while a box has focus. |
| Ctrl+C copy | **wrong** | Not built. No handler anywhere. Copying is done by the four buttons on the enciphering screen. |
| Ctrl+V paste | **wrong** | Not built. Pasting is the Paste, Paste cipher and Paste marker buttons. |
| Ctrl+X cut | **wrong** | Not built. |
| Ctrl+A highlight all | **wrong** | Not built. A text box answers only Ctrl+Z and Ctrl+Y. |
| Ctrl+S save to current preset | **true** | `gui_setup_panel.cpp:350` |
| Ctrl+Shift+S save to new preset | **true** | Same place, the shift branch. |
| Ctrl+Shift+C reserved, does nothing | **wrong** | It is the composite copy. `gui_enciphering_panel.cpp:249` |

The list also leaves out most of what the application really has: the
arrow keys, Enter, Escape, Ctrl+Escape, Alt+Q, Alt+M, Alt+S, Alt+L,
Ctrl+F, Ctrl+Z, Ctrl+Y, Backspace, Delete and Ctrl and click.

**The keybind list inside the application is correct and this one is not.**
It lives in `gui_settings_panel.cpp:241`. It would be the better record to
keep, and this section could be replaced by a pointer to it.

## 2. Text Container and Input Behavior

All four are now true. This appendix can be marked done.

| Claim | Verdict | Evidence |
|---|---|---|
| Click places the cursor | **true** | Seen in the window today. Clicks landed a caret in a two line box and in a one line box. |
| Click and drag selects | **true** | `gui_widgets.cpp`, the field keeps a drag anchor while the button is held. |
| Backspace and Delete remove the selection or one character | **true** | Both branches are there and both work. |
| Composite copy is ciphertext, five spaces, marker | **true** | `kBothSeparator` is exactly five spaces. `gui_enciphering_panel.cpp:228` |

## 3. The freeform notes at the bottom

| Note | Verdict | Detail |
|---|---|---|
| clicking in containers still doesnt work | **stale** | It works. Fixed by `b4e6c3f` and seen working today. |
| superfocus and the arrow keys | **done this session** | See below. |
| settings reordering | **open** | Cannot be checked without knowing the order you want. Nothing has moved. |
| tooltips, mechanism ships, live only on the settings apply button | **true** | There is exactly one tooltip call in the whole interface. `gui_settings_panel.cpp:428` |

On superfocus, one line of your note is superseded rather than wrong. You
wrote "up arrow to beginning, right arrow to right, down arrow to end".
That was written when every box was one line. Boxes now wrap, so Up and
Down move one row and keep the column, and a box that is still one line
keeps your original meaning: Up is its start, Down is its end.

## 4. The Diacritic Mapping System table

The table matches the transformer, mark for mark. Every code in it is in
`tools/gen_transform_table.py` with the same number: 1, 11, 12, 13, 2, 21,
22, 23, 3, 32, 4, 41, 42, 5, 51, 6, 61, 7, 71, 72, 73, 74, 75, 8, 81, 82,
83, 9, 91, 92.

Two notes and one wrong line:

- Slot 31, the bevel, is absent from the generator on purpose, exactly as
  the table says it is reserved. They agree.
- Slot 0 capitalize is not in the mark table. It is written by
  `transform()` itself. Slot 01 is unused, as the table says.
- **Wrong:** the sentence "Appended to the end of the message between the
  actual end and the marker." Codes are written inline, right after the
  letter they belong to. Nothing is collected at the end any more. This
  sentence describes the scheme the transformer replaced.

## 5. The transformer decisions of 2026-09-06

All eight lines are **true**, checked against `transform.cpp`,
`transform.hpp` and the generator. One thing is missing rather than wrong:

- The Turkish capital I with a dot is **not** spelled out. It decomposes
  honestly into I plus a dot above, so it encodes as `i0/8` and comes back
  whole. Only the dotless i is lost. The note reads as though both were.

## 6. The "special characters BROKEN" note

**True.** The generator spells out sharp s as `ss`, capital sharp s as
`s0s0`, ae as `ae`, AE as `a0e0`, oe as `oe`, OE as `o0e0`, and the Turkish
dotless i as plain `i`. None of them come back. Turkish is the casualty
you said it was, with the one correction in section 5 above.

## 7. Section 1, UI/UX and theater

| Claim | Verdict | Detail |
|---|---|---|
| Artificial processing delay | **not built** | Nothing in the code does this. |
| Account or login button, circular, top right | **not built** | The top right is the INOP wordmark. |
| Audio effects | **not built** | The settings screen has an Audio heading that says the application makes no sound yet. |
| Tooltips | **part built** | Mechanism ships, one tooltip live. |
| Tutorial system, first launch and replay | **being built now** | `gui_tutorial.cpp` and `.hpp` exist in the tree, untracked. Another session is writing them as this was checked. |
| Layout and visual adjustments | **empty** | The line has no sub-items left. Nothing to check. |

## 8. Section 2, code hygiene

"Strip all comments from the codebase at all times during build or
compilation" is **flat wrong** now. The code is heavily commented on
purpose. `gui_widgets.cpp` alone has 463 comment lines, and there is no
comment stripping step anywhere in `CMakeLists.txt`. This was reversed
deliberately and the line was never taken out.

## 9. Section 3, settings

| Claim | Verdict | Detail |
|---|---|---|
| Interface language, English default | **true** | The row is there, locked, with a note saying so until there are translations. |
| INOP Script: Latin, Greek, Cyrillic, Hebrew, Hangul | **part built** | The dropdown row exists and is deliberately inert. Its note says the font can only draw latin so far. Locked on the atlas, not on the cipher. |
| Arachnophobia mode, default off, removing it bricks the app | **wrong** | The toggle is there and defaults off, with the note "nothing in the interface to hide yet". Nothing in the code would break if the row went. The bricking claim has no basis. |

## 10. Section 4, utilities

| Claim | Verdict | Detail |
|---|---|---|
| Plaintext buffer clear, a button and a shortcut | **half** | The Clear button is there and empties whichever box has the focus. The shortcut is dropped for good, see section 1. |
| Composite copy utility | **true** | The Copy both button, and Ctrl+Shift+C. |

## 11. Section 5, bug fixes

| Claim | Verdict | Detail |
|---|---|---|
| Purge the phantom Oxx rotors | **appears done** | `inop_rotors.json` holds 50 entries and every one is U prefixed. No O anywhere. |
| Fix the root cause of survival through regeneration | **cannot confirm** | Generated wheels take the prefix U for rotors and K for reflectors. No O rotor exists now to test against. |

## 12. Section 6, pending

| Claim | Verdict |
|---|---|
| The Bombe, shelved for this pass | **true**, `src/bombe` exists and is not wired into the GUI |
| Filesystem revamp | **open** |
| Installer and uninstaller | **open**, you are researching it yourself |
| Custom art and assets | **open** |

## 13. The last line of the file

"encipher decipher containers broken, would like to type in diacritics
capital letters and punctuation."

- Diacritics: **fixed this session.**
- Capital letters: **fixed this session.**
- Punctuation: **still open, and not a build problem.** `transform()`
  drops punctuation, and the shape family table has no slot for it. Making
  punctuation survive means adding meta slots, which is the same kind of
  change you parked for the four broken letters.

---

## What I would change in PROGRESS.md, if you want it

1. Delete the whole finalized keybind list and point at the one inside the
   application instead.
2. Mark the Text Container appendix done.
3. Delete the "clicking in containers" line.
4. Delete the superfocus line.
5. Delete the sentence about codes being appended at the end.
6. Delete or rewrite the strip all comments line.
7. Cut the arachnophobia bricking claim.
8. Narrow the last line to punctuation only.

Your call on every one of them.
