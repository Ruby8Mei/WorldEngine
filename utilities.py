from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Set, Tuple, Iterable

import registry
from registry import ALPHA26, ALPHA38, ALPHA76
from copy import deepcopy

# ── optional Rich eye‑candy ────────────────────────────────────────────
try:
    from rich.console import Console
    from rich.table import Table
    from rich.panel import Panel
    from rich.text import Text
    _RICH = True
    _con = Console()
except ImportError:                          # graceful fallback
    _RICH = False

    class _Dummy:
        def print(self, *a, **k):  # noqa: D401
            print(*a)
        def input(self, prompt=""):  # noqa: D401
            return input(prompt)

    _con = _Dummy()

from rotor_and_reflector import Rotor, Reflector

# ── Suite metadata ------------------------------------------------------
@dataclass(frozen=True)
class Suite:
    code: str
    name: str
    alphabet: str
    rotor_prefix: str
    rotor_count_for_settings: int
    max_pairs: int
    max_notches: int
    reflector_start: str
    reflector_span: int

    _ROMAN = {
        "I": 1, "II": 2, "III": 3, "IV": 4, "V": 5,
        "VI": 6, "VII": 7, "VIII": 8, "IX": 9, "X": 10,
    }

    def expected_rotor_names(self, names: Iterable[str]) -> List[str]:
        """
        Return rotor names that belong to *this* suite, sorted in
        the natural order (numeric or Roman as appropriate).
        """
        up = [n.upper() for n in names]

        # Legacy (26-char) suite → pure Roman numerals
        if self.code == "26":
            romans = [n for n in up if n in self._ROMAN]
            return sorted(romans, key=self._ROMAN.get)

        # Modern suites → prefix followed by decimal digits
        pref = self.rotor_prefix.upper()
        numeric = [
            (int(n[len(pref):]), n)
            for n in up
            if n.startswith(pref) and n[len(pref):].isdigit()
        ]
        numeric.sort()
        return [n for _, n in numeric]

    def reflector_range(self) -> List[str]:
        base = ord(self.reflector_start)
        return [chr(base + i) for i in range(self.reflector_span)]

SUITES: Dict[str, Suite] = {
    "26": Suite(
        code="26", name="Legacy", alphabet=ALPHA26,
        rotor_prefix="I", rotor_count_for_settings=3,
        max_pairs=10, max_notches=2,
        reflector_start="A", reflector_span=3,
    ),
    "38": Suite(
        code="38", name="INOP‑38", alphabet=ALPHA38,
        rotor_prefix="R", rotor_count_for_settings=5,
        max_pairs=15, max_notches=3,
        reflector_start="D", reflector_span=5,
    ),
    "76": Suite(
        code="76", name="INOP‑76", alphabet=ALPHA76,
        rotor_prefix="S", rotor_count_for_settings=10,
        max_pairs=32, max_notches=6,
        reflector_start="J", reflector_span=10,
    ),
}

# ―― active suite pointer
CURRENT: Suite = SUITES["76"]

# ―― helper that returns wheels only for the active suite

def load_suite():
    alphabet = CURRENT.alphabet
    need_len = len(alphabet)

    rotor_pool = {
        name: deepcopy(r)
        for name, r in registry.rotor_dict.items()
        if len(r.alphabet) == need_len
    }
    reflector_pool = {
        name: deepcopy(r)
        for name, r in registry.reflector_dict.items()
        if len(r.alphabet) == need_len
    }
    if not rotor_pool or not reflector_pool:
        raise SystemExit("❌ No wheels compatible with this suite.")

    return CURRENT.name, alphabet, rotor_pool, reflector_pool

# ―― set suite

def set_current_suite(code: str) -> None:
    global CURRENT
    if code not in SUITES:
        raise ValueError(f"Unknown suite code {code}")
    CURRENT = SUITES[code]

# ── wheel registries ----------------------------------------------------
rotor_dict: Dict[str, Rotor] = {}
reflector_dict: Dict[str, Reflector] = {}


def register_wheels(objs: Dict[str, Rotor | Reflector]) -> None:
    for name, obj in objs.items():
        key = name.upper()
        if isinstance(obj, Rotor):
            obj.name = key  # ensure rotor has a name
            rotor_dict[key] = obj
        elif isinstance(obj, Reflector):
            obj.name = key  # ensure reflector has a name
            reflector_dict[key] = obj


def show_registry() -> None:
    if not _RICH:
        print(f"Rotors: {len(rotor_dict)}  Reflectors: {len(reflector_dict)}")
        return
    tbl = Table(title="Wheel Registry")
    tbl.add_column("Type")
    tbl.add_column("Count", justify="right")
    tbl.add_row("Rotors", str(len(rotor_dict)))
    tbl.add_row("Reflectors", str(len(reflector_dict)))
    _con.print(tbl)

# ── prompt helpers ------------------------------------------------------
def _ask(prompt: str) -> str:
    if _RICH:
        return _con.input(f"[bold cyan]{prompt}[/] ").strip().upper()
    return input(prompt + " ").strip().upper()

def _fail(msg: str) -> None:
    if _RICH:
        _con.print(f"[red]❌ {msg}[/]")
    else:
        print("❌", msg)

# ── suite chooser -------------------------------------------------------
def _choose_suite(default: str = "76") -> Suite:
    if _RICH:
        t = Table(title="Select Suite", header_style="bold magenta")
        t.add_column("Code")
        t.add_column("Name")
        t.add_column("Alphabet", justify="right")
        t.add_column("Need Rotors", justify="right")
        for s in SUITES.values():
            t.add_row(s.code, s.name, str(len(s.alphabet)), str(s.rotor_count_for_settings))
        _con.print(t)
    else:
        print("Suites:", ", ".join(f"{s.code}:{s.name}" for s in SUITES.values()))
    sel = _ask(f"Suite code [{default}]") or default
    set_current_suite(sel)
    return CURRENT

# ── interactive selectors ----------------------------------------------
def _get_rotors(suite: Suite) -> List[str]:
    available = suite.expected_rotor_names(rotor_dict)
    if _RICH:
        _con.print(Panel("Available Rotors:\n" + " ".join(available),
                        title="Rotors", border_style="blue"))
    else:
        print("Available Rotors:", " ".join(available))

    need = suite.rotor_count_for_settings
    while True:
        picks = _ask(f"Select {need} rotors").split()
        if len(picks) != need:
            _fail(f"Need exactly {need}.")
            continue
        if any(p not in rotor_dict for p in picks):
            _fail("Unknown rotor(s).")
            continue
        return [p.upper() for p in picks]

def _get_reflector(suite: Suite) -> Reflector:
    expect = set(suite.reflector_range())
    avail = sorted(r for r in reflector_dict if r in expect)
    if _RICH:
        _con.print(Panel("Available Reflectors: " + " ".join(avail),
                        title="Reflectors", border_style="blue"))
    else:
        print("Reflectors:", " ".join(avail))
    while True:
        ref = _ask("Reflector")
        if ref in reflector_dict and ref in expect:
            return reflector_dict[ref]
        _fail("Not a valid reflector.")

def _get_plugboard(suite: Suite) -> List[str]:
    valid = set(suite.alphabet)
    max_pairs = suite.max_pairs
    if _RICH:
        _con.print(f"[bold]Plugboard pairs[/bold] (≤{max_pairs}, e.g. AB CD; Enter for none)")
    while True:
        raw = _ask("Pairs")
        if not raw:
            return []
        pairs = raw.split()
        if len(pairs) > max_pairs:
            _fail(f"Too many (max {max_pairs}).")
            continue
        used: Set[str] = set()
        out: List[str] = []
        for p in pairs:
            if len(p) != 2:
                _fail(f"Pair {p!r} must be 2 chars")
                break
            a, b = p
            if a == b or a not in valid or b not in valid or a in used or b in used:
                _fail(f"Invalid or duplicate char in {p!r}")
                break
            used.update(p)
            out.append(p)
        else:
            return out  # only executed if no break

def _get_rings(suite: Suite, rotors: List[str]) -> List[int]:
    hi = len(suite.alphabet)
    need = len(rotors)
    while True:
        nums = _ask(f"{need} ring settings 1–{hi}").split()
        if len(nums) != need:
            _fail(f"Need {need} numbers.")
            continue
        try:
            vals = [int(n) for n in nums]
        except ValueError:
            _fail("All must be integers.")
            continue
        if any(not 1 <= v <= hi for v in vals):
            _fail(f"Values must be in 1..{hi}.")
            continue
        return vals

def _get_notches(suite: Suite, rotors: List[str]) -> Dict[str, str]:
    max_n  = suite.max_notches
    alpha  = set(suite.alphabet)
    out: Dict[str, str] = {}

    for r in rotors:
        preset = rotor_dict[r].notches              # ← grab factory notch(s)
        if preset:                                  # legacy rotors land here
            out[r] = preset
            continue

        # --- interactive branch for blank wheels ---
        while True:
            raw = _ask(f"Notches for {r} (0–{max_n})")
            if len(raw) <= max_n and set(raw) <= alpha:
                out[r] = raw
                break
            _fail(f"Use ≤{max_n} symbols from alphabet.")

    return out

def _get_master_key(suite: Suite, rotors: List[str]) -> str:
    length = len(rotors) + 1
    alpha = set(suite.alphabet)
    while True:
        key = _ask(f"Master key ({length} chars)")
        if len(key) == length and set(key) <= alpha:
            return key
        _fail("Incorrect length or invalid symbols.")

# ── public orchestrator -------------------------------------------------
def collect_settings() -> Tuple[
    List[str], Reflector, List[int], Dict[str, str], List[str], str, Suite
]:
    suite = _choose_suite()
    rotors     = _get_rotors(suite)
    reflector  = _get_reflector(suite)
    plugs      = _get_plugboard(suite)
    rings      = _get_rings(suite, rotors)
    notches    = _get_notches(suite, rotors)
    master_key = _get_master_key(suite, rotors)

    if _RICH:
        panel = Panel(
            f"[bold]{suite.name} Settings[/bold]\n"
            f" Rotors   : {' '.join(rotors)}\n"
            f" Reflector: {reflector.name}\n"
            f" Rings    : {' '.join(f'{r:02d}' for r in rings)}\n"
            f" Plugs    : {' '.join(plugs) if plugs else '(none)'}\n"
            f" Notches  : " + ", ".join(f"{k}:{v}" for k, v in notches.items()) + "\n"
            f" Master   : {master_key}",
            border_style="green",
        )
        _con.print(panel)

    return rotors, reflector, rings, notches, plugs, master_key, suite

# ── preprocessing helper -----------------------------------------------
def preprocess_message(msg: str, alpha: str, *, log_dropped: bool = False) -> str:
    up = msg.upper()
    dropped: List[str] = []
    out: List[str] = []
    space_sub = "#" if "#" in alpha else ""
    for ch in up:
        if ch == " ":
            if space_sub:
                out.append(space_sub)
        elif ch in alpha:
            out.append(ch)
        else:
            if log_dropped:
                dropped.append(ch)
    if log_dropped and dropped:
        _con.print(f"[yellow]Dropped:[/yellow] {''.join(dropped)}")
    return "".join(out)

__all__ = [
    "register_wheels",
    "collect_settings",
    "preprocess_message",
    "rotor_dict",
    "reflector_dict",
    "Suite",
    "show_registry",
]
