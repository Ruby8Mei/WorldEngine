# inop.py
from __future__ import annotations

from typing import List, Sequence

# optional debug helper --------------------------------------------------
try:
    from debug import Debug
    _dbg = Debug()
    _dbg.disable("encipher")
except ImportError:                     # graceful fallback
    class _NoDebug:
        def disable(self, *_): ...
        def log(self, *_): ...
    _dbg = _NoDebug()                   # type: ignore

# ── Inop machine --------------------------------------------------------
class Inop:
    __slots__ = (
        "kb", "pb", "rotors", "reflector",
        "_step_reflector_flag",
    )

    # ------------------------------------------------------------------
    def __init__(
        self,
        kb,
        pb,
        rotors: Sequence,
        reflector,
        ring_settings: Sequence[int],
        master_key: str,
    ) -> None:
        if len(ring_settings) != len(rotors):
            raise ValueError("ring_settings length mismatch")
        if len(master_key) != len(rotors) + 1:
            raise ValueError("master_key length mismatch")

        self.kb = kb
        self.pb = pb
        self.rotors: List = list(rotors)
        self.reflector = reflector
        self._step_reflector_flag = False  # can be toggled externally

        self.set_rings(ring_settings)
        self.set_key(master_key[:-1])
        self.reflector.rotate_to_letter(master_key[-1])

    # ── key & ring helpers -------------------------------------------
    def set_rings(self, rings: Sequence[int]) -> None:
        for rotor, ring in zip(self.rotors, rings):
            rotor.set_ring(ring)

    def set_key(self, window: str) -> None:
        for rotor, letter in zip(self.rotors, window):
            rotor.position = rotor.alphabet.index(letter)

    # ── stepping logic -----------------------------------------------
    def _step_rotors(self) -> None:
        """Advance rotors one key‑press according to suite rules."""

        if len(self.rotors) == 3:
            # historic double‑step
            left, middle, right = self.rotors

            step_L = middle.position in middle._notch_int
            step_M = step_L or (right.position in right._notch_int)

            right.step()
            if step_M:
                middle.step()
            if step_L:
                left.step()
        else:
            # generic cascade
            carry = True
            for rotor in reversed(self.rotors):
                if not carry:
                    break
                carry = rotor.step()          # returns bool rollover

        if self._step_reflector_flag:
            self.reflector.step()

    # ── encipher single character ------------------------------------
    def encipher(self, letter: str) -> str:
        """Encipher a single uppercase symbol."""
        self._step_rotors()
        _dbg.log("encipher", f"Rotor pos {[r.position for r in self.rotors]}")

        signal = self.kb.forward(letter)
        signal = self.pb.forward(signal)

        for rotor in reversed(self.rotors):
            signal = rotor.forward(signal)

        signal = self.reflector.reflect(signal)

        for rotor in self.rotors:
            signal = rotor.backward(signal)

        signal = self.pb.backward(signal)
        return self.kb.backward(signal)

    # legacy typo kept for backward compatibility
    encypher = encipher          # type: ignore[attr-defined]

    # ------------------------------------------------------------------
    def __repr__(self) -> str:
        names = [getattr(r, "name", "?") for r in self.rotors]
        return f"<Inop rotors={'-'.join(names)} refl={getattr(self.reflector,'name','?')}>"

__all__ = ["Inop"]