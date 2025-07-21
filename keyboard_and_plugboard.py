# keyboard_and_plugboard.py
from __future__ import annotations

from collections.abc import Sequence
from typing import Dict, List, Tuple, Optional

# ── optional Debug helper ───────────────────────────────────────────────
try:
    from debug import Debug                     # your existing helper
    debug = Debug()
    debug.disable("plugboard")
except ImportError:                             # graceful fallback
    class _NoDebug:
        def disable(self, *_): ...
        def log(self, *_): ...
    debug = _NoDebug()                          # type: ignore

# ── Keyboard ────────────────────────────────────────────────────────────
class Keyboard:
    __slots__ = ("alphabet", "alpha_to_index")

    def __init__(self, alphabet: str) -> None:
        self.alphabet: str = alphabet
        # direct dict → O(1) lookup
        self.alpha_to_index: Dict[str, int] = {ch: i for i, ch in enumerate(alphabet)}

    # letter → integer signal
    def forward(self, letter: str) -> int:
        try:
            return self.alpha_to_index[letter]
        except KeyError as exc:
            raise ValueError(f"Invalid character {letter!r} for current alphabet.") from exc

    # integer signal → letter
    def backward(self, signal: int) -> str:
        hi = len(self.alphabet) - 1
        if not 0 <= signal <= hi:
            raise ValueError(f"Signal {signal} out of range 0–{hi}")
        return self.alphabet[signal]

# ── Plugboard ───────────────────────────────────────────────────────────
class Plugboard:
    __slots__ = ("alphabet", "mapping", "_index_map")

    def __init__(
        self,
        pairs: Optional[Sequence[str | Tuple[str, str]]] = None,
        alphabet: str = "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    ) -> None:
        self.alphabet: str = alphabet
        self.mapping: Dict[str, str] = {ch: ch for ch in alphabet}
        used: set[str] = set()

        for raw in pairs or ():               # allow None/empty
            # normalise to (a, b)
            if isinstance(raw, str):
                if len(raw) != 2:
                    raise ValueError(f"Pair {raw!r} must be exactly 2 symbols")
                a, b = raw
            else:
                a, b = raw

            if a == b:
                raise ValueError(f"Plugboard cannot map a symbol to itself: {a}")
            if a in used or b in used:
                dup = a if a in used else b
                raise ValueError(f"Character {dup!r} already used in plugboard")
            if a not in alphabet or b not in alphabet:
                bad = a if a not in alphabet else b
                raise ValueError(f"Symbol {bad!r} not in alphabet")

            # commit swap
            self.mapping[a], self.mapping[b] = b, a
            used.update((a, b))

        # pre‑compute index mapping for O(1) forward/back
        self._index_map: List[int] = [alphabet.index(self.mapping[ch]) for ch in alphabet]

    # signal in → signal out  (same both directions)
    def _map(self, signal: int) -> int:                      # noqa: D401
        mapped = self._index_map[signal]
        if debug:
            debug.log("plugboard", f"{signal}->{self.alphabet[signal]}->{self.alphabet[mapped]}")
        return mapped

    forward = _map        # alias
    backward = _map       # alias

    # nice repr for debugging
    def __repr__(self) -> str:
        swaps = [f"{a}{b}" for a, b in self.mapping.items() if a < b]
        return f"<Plugboard {' '.join(sorted(swaps))}>"     # deterministic order

# explicit re‑exports
__all__ = ["Keyboard", "Plugboard"]
