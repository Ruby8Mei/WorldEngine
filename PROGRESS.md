# INOP Roadmap

## Remaining audit limits

- E-012: Saved preset catalogue compatibility and historical-data disposition remain deferred pending the supplied compatibility design.
- Native macOS and Linux runtime validation remains outstanding. Builds have been validated on Windows.

## Status Tags

Every implementation-related item begins as `DUNNO`. Statuses are updated only after an audit of the actual codebase.

* `DUNNO` — implementation status has not been established by audit.
* Completed tasks are removed from this file.
* `PARTIAL` — implementation exists but does not fully satisfy the requirement.
* `NOT STARTED` — audit confirms that meaningful implementation has not begun.
* `DEFERRED` — intentionally postponed.
* `BLOCKED` — cannot currently proceed because of an unresolved dependency.
* `NEEDS DECISION` — implementation direction requires an operator decision.
* `NUCLEAR` — high-impact or architecturally sensitive; requires deliberate human review before changing or removing.

### Roadmap Audit Rule

Every roadmap item must be examined during an audit, including experimental, speculative, impractical, unusual, or deliberately ridiculous ideas.

An item must not be ignored, removed, or dismissed solely because it appears unreasonable. The audit should determine its implementation status and, where applicable, identify dependencies, conflicts, technical implications, or blockers.

The final decision to keep, remove, defer, redesign, or implement an item belongs to the operator.

---

# 1. UI/UX, Animations & Theater

## 1.1 Artificial Processing Delay

**Status:** `NEEDS DECISION`

**Phase 2 disposition:** No artificial operation delay exists. Implementation requires the operator to select `X` and approve the intended presentation timing. The existing reduced-motion preference must also govern any future animation.

The C++ backend is capable of processing approximately 32 million characters per second, making normal encryption and decryption effectively instantaneous.

The GUI should introduce an artificial visual processing delay so that encryption/decryption has visible weight and the interface can present a deliberate processing animation.

Requirements:

* The actual cryptographic operation must not be artificially slowed.
* The GUI should display the processing animation for at least a minimum duration of `X` seconds.
* If the real operation takes longer than `X` seconds, the GUI should use the actual processing time.
* The artificial delay exists purely for visual presentation.

**Open decision:** The exact value of `X` is not currently specified.

## 1.2 Account / Login

**Status:** `NOT STARTED`

**Phase 2 disposition:** No account model, authentication service, profile control, or persistence exists. The feature remains experimental and outside this corrective release.

Provide an account/login interface accessible through a circular image button in the top-right corner.

The feature is experimental and may eventually be merged with or replaced by Settings.

## 1.3 Audio System

**Status:** `NOT STARTED`

**Phase 2 disposition:** Settings explicitly reports that the application makes no sound. No playback, asset loading, music, cue, or speech subsystem exists.

Provide an audio system for interface and application events.

Planned uses include:

* Button-click sounds.
* Background music.
* An "INOP in progress" cue.
* Text-to-Speech (TTS).

The application should support externally supplied audio assets. Audio assets themselves are separate from the audio framework.

## 1.4 Tooltips

**Status:** `PARTIAL`

**Phase 2 disposition:** A delayed, animated, modal-aware tooltip mechanism exists and is exercised on Apply. Coverage is intentionally limited to that demonstration until the operator selects which controls need explanations.

Provide hover tooltips for UI elements whose purpose or behavior is not immediately obvious.

The tooltip mechanism and tooltip coverage should be considered separately during the audit.

The specific controls requiring tooltips remain to be determined.

## 1.6 Layout & Visual Adjustments

**Status:** `PARTIAL`

**Phase 2 disposition:** Shared responsive form columns, scroll regions, pinned Settings actions, transitions, zoom limits, font previews, and header alignment are implemented. The item remains open ended because final visual changes are not specified.

Perform required layout and visual adjustments as the final interface develops.

Specific changes are not currently defined.

---

# 2. Core Logic & Language Processing

## 2.1 Code Comment Quality

**Status:** `PARTIAL`

**Phase 2 disposition:** Existing comments generally explain intent, invariants, and non-obvious behavior, and no comment-stripping build step exists. Repository policy currently prohibits new comments, so future comment additions remain outside this phase.

Code comments should be written deliberately and should read as project documentation written by a human developer, rather than as generic AI-generated commentary.

Requirements:

* Existing useful comments should be retained.
* New comments should be added when they provide meaningful information.
* Comments should explain intent, non-obvious behavior, constraints, or important implementation details where appropriate.
* Do not add comments merely to narrate obvious code.
* Do not automatically generate large quantities of verbose, generic, or unnecessarily explanatory comments.
* Do not introduce a build step whose purpose is to strip comments from the source code.

The objective is **comment quality and authorship**, not minimizing the number of comments.

This requirement applies particularly to code produced or modified with AI assistance.

## 2.9 Special Characters

**Status:** `NEEDS DECISION`

**Phase 2 disposition:** The four special letters follow the documented spelling behavior and tests preserve that behavior. Dedicated meta-slot encoding would change the wire format and still requires an operator decision.

The current mapping does not provide dedicated representations for certain letters that are distinct letters rather than base letters plus diacritics:

* `ß` — Eszett
* `æ` — AE
* `œ` — OE
* `ı` — Turkish dotless I

The current behavior spells these characters out rather than preserving them as distinct characters.

Consequences include:

* `ß` becoming `ss`.
* `æ` and `œ` being expanded.
* Turkish dotless `ı` becoming `i`, losing the distinction between the two Turkish letters.

A possible future solution is to reserve meta slots `02`–`05` alongside the capitalization code.

This solution is not currently committed to implementation.

## 2.10 Punctuation Preservation

**Status:** `NEEDS DECISION`

**Phase 2 disposition:** The current sanitizer intentionally removes punctuation. No encoding space or escape grammar has been selected, so a reversible implementation cannot be inferred safely.

Punctuation currently does not survive enciphering because `transform()` removes it and the mapping provides no representation for punctuation.

A future solution would require additional encoding space.

The GUI retains punctuation in editable plaintext and rejects unsupported characters at the processing boundary with an explicit error. The exact punctuation encoding scheme remains unspecified.

## 2.11 Script Support

**Status:** `PARTIAL`

**Phase 2 disposition:** Latin is implemented and the GUI exposes the requested script choices, but non-Latin choices are locked. The current font atlas and machine alphabet cannot yet display or carry Greek, Cyrillic, Hebrew, or Hangul.

The application should support configurable INOP scripts:

* Latin — default
* Greek
* Cyrillic
* Hebrew
* Hangul

The universal transformer should not require a separate diacritic table for each language.

## 2.12 Transformer Validation

**Status:** `PARTIAL`

**Phase 2 disposition:** Base letters, capitalization, single and multiple marks, literal numbers, reserved behavior, current special-character behavior, malformed UTF-8, and all generated mappings are tested. Punctuation preservation and non-Latin scripts remain unresolved roadmap work.

The implementation should correctly handle:

* Base letters.
* Capitalization.
* Single diacritics.
* Multiple diacritics.
* Literal numbers.
* Reserved codes.
* Special characters.
* Punctuation.
* Supported scripts.
* Intended round-trip behavior.

---

# 3. Settings & Configuration

## 3.1 Interface Language

**Status:** `PARTIAL`

**Phase 2 disposition:** The Settings row and English default exist, but the control is locked and no translation catalogue or alternate interface text exists.

The application interface should support configurable interface languages.

Default:

* English.

## 3.2 INOP Script

**Status:** `PARTIAL`

**Phase 2 disposition:** The requested options are present in Settings, with Latin as the visible default. Selection is locked until the font atlas and processing path support the other scripts.

Provide a setting for selecting the INOP script.

Supported options:

* Latin — default
* Greek
* Cyrillic
* Hebrew
* Hangul

---

# 5. Bug Fixes & Maintenance

## 5.2 Rotor Regeneration

**Status:** `PARTIAL`

**Phase 2 disposition:** Generation and loading now share the critical wiring checks, entropy is gated, failed batches are not committed, and catalogue caches invalidate by generation. The historical cause of unavailable identifiers in saved records cannot be established from current evidence and remains with deferred E-012 compatibility work.

Identify and fix the root cause allowing phantom rotors and certain invalid reflectors to survive wheel regeneration.

The fix should address the underlying data/registry behavior rather than merely hiding the resulting entries.

---

# 6. Bombe

## 6.1 Bombe

**Status:** `PARTIAL`

**Phase 2 disposition:** The offline Bombe harness implements Legacy phase 1, INOP ablation, notch sweep, transposition, crash elimination, and self checks. Numeric safety is now enforced. Steckered search, a diagonal board, and broader cryptanalytic stages remain future work.

Develop the INOP Bombe functionality.

The Bombe is intended to support cryptanalytic searching against INOP ciphertext.

The feature may involve multiple implementation stages and should be audited at the component level rather than treated as a single binary feature.

Potential future work includes:

* Additional Bombe search functionality.
* Steckered search.
* Diagonal-board functionality.
* Other cryptanalytic capabilities as the design develops.

---

# 7. Filesystem and Distribution

## 7.1 Filesystem Revamp

**Status:** `NEEDS DECISION`

**Phase 2 disposition:** GUI setup persistence is now portable, but no broader target layout or migration requirement is defined. A general filesystem redesign would be speculative without operator direction.

Redesign or reorganize the application's filesystem structure as required.

Specific requirements remain to be defined.

## 7.2 Installer

**Status:** `NOT STARTED`

**Phase 2 disposition:** No installer project, packaging target, or installation workflow exists.

Create an installer for the application.

## 7.3 Uninstaller

**Status:** `NOT STARTED`

**Phase 2 disposition:** No uninstall manifest or removal workflow exists. This depends on a future installer and installation layout.

Create an uninstaller for the application.

---

# 8. GUI Assets and Typography

## 8.1 Custom GUI Art / Assets

**Status:** `NEEDS DECISION`

**Phase 2 disposition:** The GUI uses code-rendered controls, typography, and existing font assets. No custom asset specification exists, so content and style require operator direction.

Create and integrate custom art and visual assets for the GUI.

Specific asset requirements remain to be defined.

## 8.2 Phyrexian Font

**Status:** `BLOCKED`

**Phase 2 disposition:** The font picker and licence-sidecar discovery can accept a future face, but no repository candidate establishes both distributable licensing and required glyph coverage. The bundled Crimson Pro and SGA faces do not satisfy this item as a Phyrexian font.

Investigate and integrate a suitable Phyrexian-style font for the interface if a legally usable font with sufficient character coverage can be obtained.

Requirements:

* License must permit the intended distribution/bundling.
* Font must contain the characters required by the interface.
* The font should work with the application's font picker and licensing display system.
* A font placed in `fonts/` with a matching `-license.txt` file should be discoverable automatically.

Known character-coverage requirements include the ability to render interface text such as:

* `INOP`
* `Settings`
* `Apply`

A candidate font must not be accepted solely because its visual appearance is suitable; licensing and glyph coverage are both required.

---

# 9. Future Ideas

## 9.1 General Future Features

**Status:** `DEFERRED`

**Phase 2 disposition:** The placeholder is intentionally retained. It defines no implementable feature and remains available for later operator ideas.

Ideas that have not yet been fully conceptualized may be added here.

These ideas are intentionally retained for later investigation.

An idea does not need to be technically practical, fully specified, or serious to remain on the roadmap. Every such item must still be acknowledged during an audit.

## 9.2 Experimental / Ridiculous Ideas

**Status:** `DEFERRED`

**Phase 2 disposition:** The placeholder is intentionally retained without dismissal. No concrete idea is presently listed, so there is no safe implementation scope.

Ideas added spontaneously during development should be preserved here even when they are deliberately absurd, experimental, or unlikely to be implemented.

The purpose of this section is to prevent potentially useful ideas — or simply entertaining ones — from being lost.

Each item should eventually receive an audit result rather than being silently discarded.

Legal Section in Settings
- Add EULA under Settings → Legal
- Add Privacy Policy under Settings → Legal

Developer Setup Presets
- Add setup presets intended specifically for developer/debug workflows

Bombe GUI Panel
- Move/replace the current Bombe presentation with a dedicated Bombe panel in the GUI

Decommission random() for Lore-Consistent Markers
- Investigate replacing generic/random marker generation with a purely INOP-encoded marker system
- Motivation: lore/identity consistency
- Must be audited for security and determinism implications before implementation

Hardcoded Wheel Letter/Number Convention
- Remove unnecessary operator choice around wheel letter/number naming conventions
- Hardcode one canonical convention
- Goal: simpler code, fewer meaningless choices, and compatibility/future-proofing for already-generated settings

Custom Keybind System
- Add configurable keybinds
- Show a prototype keyboard UI with assigned keys visually highlighted
- Include mouse buttons
- Operator clicks a key/button in the prototype to assign a binding
- Esc unbinds, or allow explicit None
- Must handle conflicts and existing reserved/system bindings cleanly

Custom Setup Preset File Format
- Define a dedicated INOP file format for setup presets
- Should consider versioning, compatibility, validation, and future migration

INOP Feature Security Analysis
- Evaluate why each security-relevant INOP feature helps
- Quantify how much security it actually contributes where meaningful
- Distinguish cryptographic benefit from obscurity, operational friction, UI protection, lore-only behavior, and features that may add little or no real security
