# settings_generator.py
from __future__ import annotations

import json, hashlib
from random import SystemRandom
from pathlib import Path
from typing import Dict, List

# single source of truth
from registry import ALPHA26, ALPHA38, ALPHA68

# ── validation helpers ────────────────────────────────────────────────

def _validate(alpha: str, expected: int):
    if len(alpha) != expected:
        raise ValueError(f"Alphabet length {len(alpha)} != {expected}")
    if len(set(alpha)) != len(alpha):
        raise ValueError("Duplicate symbols in alphabet.")
    if any(ord(c) < 32 or c.isspace() for c in alpha):
        raise ValueError("Whitespace/control char in alphabet.")

for a, n in [(ALPHA26, 26), (ALPHA38, 38), (ALPHA68, 68)]:
    _validate(a, n)


def alpha_hash(alpha: str) -> str:
    return hashlib.sha256(alpha.encode()).hexdigest()[:12]


ALPHA_HASHES = {
    26: alpha_hash(ALPHA26),
    38: alpha_hash(ALPHA38),
    68: alpha_hash(ALPHA68),
}

# ── suite definitions ─────────────────────────────────────────────────
SUITES: Dict[str, Dict] = {
    "1": {
        "name": "Legacy",
        "alphabet": ALPHA26,
        "rotors": ["I", "II", "III", "IV", "V", "VI", "VII"],
        "reflectors": ["A", "B", "C"],
        "n_rot": 3,
        "max_pairs": 10,
        "max_notches": 0,
        "rev": 1,
    },
    "2": {
        "name": "INOP-38",
        "alphabet": ALPHA38,
        "rotors": [f"R{i}" for i in range(1, 11)],
        "reflectors": list("DEFGH"),
        "n_rot": 5,
        "max_pairs": 15,
        "max_notches": 3,
        "rev": 1,
    },
    "3": {
        "name": "INOP-68",
        "alphabet": ALPHA68,
        "rotors": [f"S{i}" for i in range(1, 21)],
        "reflectors": list("JKLMNOPQR"),  # J..R  (skip I)
        "n_rot": 10,
        "max_pairs": 32,
        "max_notches": 6,
        "rev": 3,
    },
}

# ── RNG & Rich UI ──────────────────────────────────────────────────────
RNG = SystemRandom()
from rich.console import Console
from rich.table import Table
from rich.panel import Panel
from rich.box import SIMPLE

console = Console()

# ── helpers ───────────────────────────────────────────────────────────

def _choose_pairs(alpha: str, k: int) -> List[str]:
    k = min(k, len(alpha) // 2)
    pool = list(alpha)
    RNG.shuffle(pool)
    return [pool[i] + pool[i + 1] for i in range(0, 2 * k, 2)]


def _choose_notches(rotors, alpha, max_n: int):
    if max_n == 0:
        return {}
    out = {}
    for r in rotors:
        count = RNG.randint(0, max_n)
        out[r] = "".join(sorted(RNG.sample(alpha, count)))
    return out


def _pick_suite():
    table = Table(title="INOP Settings Generator", box=SIMPLE, header_style="bold cyan")
    table.add_column("Key", style="bold")
    table.add_column("Name", style="magenta")
    table.add_column("Rotors", justify="right")
    table.add_column("Alphabet", justify="right")
    table.add_column("MaxPairs", justify="right")
    table.add_column("MaxNotch", justify="right")
    for k, cfg in SUITES.items():
        table.add_row(
            k,
            cfg["name"],
            str(cfg["n_rot"]),
            str(len(cfg["alphabet"])),
            str(cfg["max_pairs"]),
            str(cfg["max_notches"]),
        )
    console.print(table)
    console.print("[dim]Press ENTER for default [3].[/dim]")
    while True:
        choice = console.input("> ").strip() or "3"
        if choice in SUITES:
            return SUITES[choice]
        console.print("[red]Invalid choice.[/red]")


def _generate(cfg: Dict):
    alpha = cfg["alphabet"]
    rotors = RNG.sample(cfg["rotors"], cfg["n_rot"])
    reflector = RNG.choice(cfg["reflectors"])
    ring_set = [RNG.randint(1, len(alpha)) for _ in rotors]
    plugs = _choose_pairs(alpha, cfg["max_pairs"])
    notch_map = _choose_notches(rotors, alpha, cfg["max_notches"])
    master_key = "".join(RNG.choice(alpha) for _ in range(len(rotors) + 1))
    return {
        "suite": cfg["name"],
        "rev": cfg["rev"],
        "rotors": rotors,
        "reflector": reflector,
        "ring_set": ring_set,
        "notch_map": notch_map,
        "plugs": plugs,
        "master_key": master_key,
        "alphabet_size": len(alpha),
        "alphabet_hash": alpha_hash(alpha),
    }


def _print_summary(cfg: Dict, path: Path):
    body = Table.grid(padding=(0, 1))
    body.add_row("Suite:", cfg["suite"])
    body.add_row("Revision:", str(cfg["rev"]))
    body.add_row("Rotors:", " ".join(cfg["rotors"]))
    body.add_row("Reflector:", cfg["reflector"])
    body.add_row("Ring set:", " ".join(f"{r:02d}" for r in cfg["ring_set"]))
    body.add_row("Master key:", cfg["master_key"])
    body.add_row("Plug pairs:", str(len(cfg["plugs"])))
    body.add_row("First plugs:", " ".join(cfg["plugs"][:8]) if cfg["plugs"] else "(none)")
    body.add_row("Alphabet hash:", cfg["alphabet_hash"])
    body.add_row("File:", str(path))
    console.print(Panel(body, title="Generation Complete", border_style="green"))

# ── main entry ─────────────────────────────────────────────────────────

def main():
    suite_cfg = _pick_suite()
    data = _generate(suite_cfg)
    out_path = Path("inop_config.json")
    out_path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    _print_summary(data, out_path)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        console.print("\n[red]Interrupted.[/red]")
