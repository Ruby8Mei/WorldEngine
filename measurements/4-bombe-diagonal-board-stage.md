# Bombe diagonal-board stage

## Selected stage

The selected Job D stage is a digital model of one historic Turing and Welchman Bombe run against three-rotor Legacy Enigma.

The engine builds a menu from a crib aligned to ciphertext, models every menu link as an Enigma scrambler at its message position, applies reciprocal stecker implications through a diagonal board, scans all rotor core positions for one selected rotor order, rejects contradictions, and reports surviving stops for later checking.

This stage does not enumerate plugboard combinations. It reproduces the implication method that made the historic Bombe useful against steckered Enigma traffic.

## Acceptance criteria

- A deterministic steckered Legacy fixture retains the true rotor core position as a stop.
- A deliberately wrong rotor order does not report the true position as a valid result.
- Every one of the 26 cubed rotor core positions is tested unless cancellation is requested.
- Menu propagation enforces reciprocal stecker relationships through the diagonal board.
- Contradictory stecker implications are rejected without pruning any consistent hypothesis.
- Existing Legacy phase 1, INOP ablation, notch sweep, transposition, crash elimination, numeric safety, and self checks remain available.
- The GUI remains responsive during a run and supports cancellation.
- The GUI distinguishes invalid input, weak menu, no result, success, cancellation, and internal failure.
- GUI scripts use only synthetic public fixtures and never expose repository key material.

## Search constraints

- Legacy 26-symbol alphabet only.
- Exactly three distinct built-in Legacy rotors in a caller-selected order.
- Reflector B.
- Ring basis AAA.
- Fixed crib alignment supplied by the operator.
- One connected menu containing 6 to 12 links.
- At least one menu loop.
- Ten-pair military Enigma steckering, with partial implications completed during later checking.
- Ciphertext length at most 256 symbols.
- Crib length at most 64 symbols.
- At most 64 displayed stops. The full stop count remains available.

## Resource limits

- One run scans at most 17,576 rotor core positions.
- Each position tests 26 initial stecker hypotheses through a bounded 26 by 26 diagonal board.
- Search memory is bounded by the menu, the diagonal board, progress state, and the displayed-stop cap.
- Cancellation is checked at every rotor position.
- Progress publication is rate limited so GUI rendering is not coupled to search speed.
- The first implementation uses one background worker. Parallel rotor-order batching is outside this stage.

## Historical basis

The design follows the 1944 US 6812th Division report on the British Bombe, Alan Turing material describing hypotheses and contradictions, and the documented Turing and Welchman menu and diagonal-board method. A historic Bombe run tested rotor core positions for configured rotor orders and produced stops containing a rotor order, rotor core position, and one stecker implication. A checking machine then completed and rejected stops.

References:

- https://www.codesandciphers.org.uk/documents/bmbrpt/index.htm
- https://www.turing.org.uk/sources/mathenigma.html
- https://www.rutherfordjournal.org/article030108.html

## Deferred stages

- Automatic rotor-order batching
- Ring-setting normalization beyond the AAA core-position basis
- A checking-machine stage that completes the stecker board
- Four-rotor Naval Enigma support
- INOP-38 adaptation
- Multi-worker search

## Validation evidence

The public deterministic fixture uses rotor order II V III, reflector B, the AAA ring basis, ten stecker pairs, and rotor core DKX. Its 12-link menu produced one stop. The stop retained DKX and the expected I/K test implication, with seven additional pair implications and two self-steckered letters.

Five release runs scanned all 17,576 rotor positions in 0.459, 0.507, 0.460, 0.435, and 0.440 seconds. The mean was 0.460 seconds. Every run produced the same single stop.

The preserved Legacy phase 1 control scanned 3,690,960 setups in 1.54 seconds, produced one survivor, and recovered its generated truth setting.

Both the `cli-werror` and GUI test directories passed 2 of 2 tests. The standalone INOP self test and Bombe self check passed. GUI script runs validated the main-menu button, Alt+B, the transition from above, success, no result, invalid input, cancellation during a partial run, and the locked future-stage explanation.
