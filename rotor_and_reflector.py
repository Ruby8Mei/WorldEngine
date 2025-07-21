# rotor_and_reflector.py
from __future__ import annotations
from collections.abc import Iterable
from typing import List, Dict, Set

# ── Optional debug helper ───────────────────────────────────────────────
try:
    from debug import Debug                     # your existing helper
    debug = Debug()
    debug.disable("stepping")
except ImportError:                             # graceful fallback
    class _NoDebug:
        def disable(self, *_): ...
        def log(self, *_): ...
    debug = _NoDebug()                          # type: ignore

# ── Rotor ----------------------------------------------------------------
class Rotor:
    __slots__ = (
        "alphabet", "size", "_fwd", "_rev",
        "notches", "_notch_int", "position", "ring_setting", "name",
    )

    def __init__(self, wiring: str, notches: str, alphabet: str) -> None:
        if sorted(wiring) != sorted(alphabet):
            raise ValueError("wiring must be a permutation of alphabet")

        self.alphabet: str = alphabet
        self.size: int = len(alphabet)

        # integer lookup tables (list index = input signal)
        self._fwd: List[int] = [alphabet.index(c) for c in wiring]
        self._rev: List[int] = [wiring.index(c) for c in alphabet]

        self.notches: Set[str] = set(notches)
        self._notch_int: Set[int] = {alphabet.index(c) for c in self.notches}
        self.position: int = 0
        self.ring_setting: int = 0
        self.name: str | None = None   # optional, set externally for nicer repr

    # ── ring & notch helpers ──────────────────────────────────────────
    def set_ring(self, ring: int) -> "Rotor":
        """Ring setting 1…size  (stored zero‑based)."""
        self.ring_setting = (ring - 1) % self.size
        return self

    def set_notches(self, notches: str) -> "Rotor":
        if not set(notches) <= set(self.alphabet):
            raise ValueError("Notch characters must be in the alphabet")
        self.notches = set(notches)
        self._notch_int = {self.alphabet.index(c) for c in self.notches}
        return self

    # ── stepping ------------------------------------------------------
    def _rotate(self, steps: int = 1) -> None:
        self.position = (self.position + steps) % self.size

    def step(self) -> bool:
        """
        Advance by one; return True if the rotor *turns over*
        (i.e. its new position is a notch letter).
        """
        self._rotate(1)
        hit = self.position in self._notch_int
        debug.log("stepping", f"{self.name or ''} pos={self.position} notch={hit}")
        return hit

    # ── signal paths --------------------------------------------------
    def forward(self, sig: int) -> int:
        shift = (sig + self.position - self.ring_setting) % self.size
        mapped = self._fwd[shift]
        return (mapped - self.position + self.ring_setting) % self.size

    def backward(self, sig: int) -> int:
        shift = (sig + self.position - self.ring_setting) % self.size
        mapped = self._rev[shift]
        return (mapped - self.position + self.ring_setting) % self.size

    # ── niceties ------------------------------------------------------
    def __repr__(self) -> str:
        label = f"{self.name} " if getattr(self, 'name', None) else ""
        return f"<Rotor {label}pos={self.position} ring={self.ring_setting}>"

# ── Reflector ------------------------------------------------------------
class Reflector:
    __slots__ = ("alphabet", "size", "_map", "position", "name")

    def __init__(self, wiring: str, alphabet: str) -> None:
        if len(wiring) != len(alphabet):
            raise ValueError("Reflector wiring length must match alphabet length")

        # involution & no self‑map check
        for i, c in enumerate(wiring):
            j = alphabet.index(c)
            if wiring[j] != alphabet[i] or i == j:
                raise ValueError("Reflector wiring must be an involution with no fixed points")

        self.alphabet: str = alphabet
        self.size: int = len(alphabet)
        self._map: List[int] = [alphabet.index(c) for c in wiring]
        self.position: int = 0
        self.name: str | None = None

    # orientation helpers ---------------------------------------------
    def rotate_to_letter(self, letter: str) -> None:
        """Set reflector offset so that *letter* is in window."""
        if letter not in self.alphabet:
            raise ValueError(f"{letter!r} not in alphabet")
        self.position = self.alphabet.index(letter)

    def step(self, n: int = 1) -> None:
        """Rotate reflector (rarely used, mainly for exotic modes)."""
        self.position = (self.position + n) % self.size

    # signal reflection -----------------------------------------------
    def reflect(self, sig: int) -> int:
        adjusted = (sig + self.position) % self.size
        mapped = self._map[adjusted]
        return (mapped - self.position) % self.size

    # repr -------------------------------------------------------------
    def __repr__(self) -> str:
        label = f"{self.name} " if getattr(self, 'name', None) else ""
        return f"<Reflector {label}pos={self.position}>"

# explicit re‑exports
__all__ = ["Rotor", "Reflector"]
