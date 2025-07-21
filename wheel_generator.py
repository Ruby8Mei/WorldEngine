# wheel_generator.py   (rotor / reflector generator)

from __future__ import annotations
import sys, hashlib
from random import Random, SystemRandom
from typing import List, Tuple, Dict

# single source of truth
from registry import ALPHA38, ALPHA76

# ── runtime alphabet map ───────────────────────────────────────────────
ALPHABETS: Dict[str, str] = {
    "38": ALPHA38,
    "76": ALPHA76,
}

# ── integrity helpers ──────────────────────────────────────────────────
def _validate(alpha: str, expect: int):
    if len(alpha) != expect:
        raise ValueError(f"Alphabet length {len(alpha)} ≠ {expect}")
    if len(set(alpha)) != len(alpha):
        raise ValueError("Duplicate symbols in alphabet.")
    if len(alpha) % 2:
        raise ValueError("Alphabet must be even‑length (reflector pairing).")
    if any(ord(c) < 32 or c.isspace() for c in alpha):
        raise ValueError("Whitespace/control char in alphabet.")

for a, n in [(ALPHA38, 38), (ALPHA76, 76)]:
    _validate(a, n)

def alpha_hash(alpha: str) -> str:
    return hashlib.sha256(alpha.encode()).hexdigest()[:12]

# ── RNG helpers ────────────────────────────────────────────────────────
def build_rng(seed: int | None):
    return Random(seed) if seed is not None else SystemRandom()

# ── wheel generators ───────────────────────────────────────────────────
def make_rotor(alpha: str, rng) -> str:
    chars = list(alpha)
    rng.shuffle(chars)
    return "".join(chars)

def make_reflector(alpha: str, rng) -> str:
    pool = list(alpha)
    rng.shuffle(pool)
    wiring = ["?"] * len(alpha)
    idx = {c: i for i, c in enumerate(alpha)}
    while pool:
        a, b = pool.pop(), pool.pop()
        wiring[idx[a]] = b
        wiring[idx[b]] = a
    w = "".join(wiring)
    # sanity check f(f(x)) == x
    if any(w[idx[ch]] != alpha[i] for i, ch in enumerate(w)):
        raise ValueError("Reflector involution check failed.")
    return w

# ── naming (non‑negotiable spec) ───────────────────────────────────────
def rotor_label(alpha_len: int, i: int) -> str:
    if alpha_len == 38:
        return f"R{i}"
    if alpha_len == 76:
        return f"S{i}"
    raise ValueError("Unsupported alphabet length for rotor naming.")

def reflector_label(alpha_len: int, i: int) -> str:
    if alpha_len == 38:
        return chr(ord("D") + i)          # D..H
    if alpha_len == 76:
        return chr(ord("J") + i)          # J..R  (skip I)
    raise ValueError("Unsupported alphabet length for reflector naming.")

# ── emitter ────────────────────────────────────────────────────────────
def emit_python(rotors: List[Tuple[str, str]],
                reflectors: List[Tuple[str, str]],
                alpha_const: str,
                alphabet: str,
                a_hash: str,
                seed: int | None) -> str:
    lines: List[str] = []
    lines.append("# Auto‑generated rotor/reflector set")
    lines.append(f"# Alphabet: {alpha_const} len={len(alphabet)} hash={a_hash}")
    lines.append(f"# Seed: {seed if seed is not None else 'random'}")
    lines.append("")
    for name, wiring in rotors:
        lines.append(f'{name} = Rotor({repr(wiring)}, "", alphabet={alpha_const})')
    lines.append("")
    for name, wiring in reflectors:
        lines.append(f'{name} = Reflector({repr(wiring)}, alphabet={alpha_const})')
    lines.append("")
    return "\n".join(lines)

# ── Rich interactive UI ────────────────────────────────────────────────
def interactive() -> Tuple[str, int, int, int | None]:
    try:
        from rich.console import Console
        from rich.table import Table
        from rich.prompt import Prompt
        from rich.panel import Panel
    except ImportError:
        alpha_code = input("Alphabet (38/76) [38]: ").strip() or "38"
        if alpha_code not in ALPHABETS:
            print("Only 38 or 76 supported.")
            sys.exit(1)
        n_rot = int(input("Rotors [10]: ") or 10)
        n_ref = int(input("Reflectors [5]: ") or 5)
        seed_txt = input("Seed (blank=random): ").strip()
        seed = int(seed_txt) if seed_txt else None
        return alpha_code, n_rot, n_ref, seed

    console = Console()
    table = Table(title="Wheel Generator", header_style="bold cyan")
    table.add_column("Code", style="bold")
    table.add_column("Suite")
    table.add_column("Alphabet length", justify="right")
    table.add_row("38", "INOP‑38", "38")
    table.add_row("76", "INOP‑76", "76")
    console.print(table)

    alpha_code = Prompt.ask("Alphabet (38/76)", default="38")
    if alpha_code not in ALPHABETS:
        console.print("[red]Choose 38 or 76.[/red]")
        sys.exit(1)

    n_rot = int(Prompt.ask("How many rotors", default="10"))
    n_ref = int(Prompt.ask("How many reflectors", default="5"))
    seed_txt = Prompt.ask("Seed (blank=random)", default="").strip()
    seed = int(seed_txt) if seed_txt else None

    panel = Panel(
        f"[bold]Selection[/bold]\n"
        f" Alphabet: {alpha_code}\n Rotors: {n_rot}\n Reflectors: {n_ref}\n Seed: {seed if seed is not None else 'random'}",
        border_style="green",
    )
    console.print(panel)
    return alpha_code, n_rot, n_ref, seed

# ── main ───────────────────────────────────────────────────────────────
def main():
    alpha_code, n_rot, n_ref, seed = interactive()

    alphabet = ALPHABETS[alpha_code]
    a_len = len(alphabet)
    a_hash = alpha_hash(alphabet)

    rng_rot = build_rng(seed)
    rng_ref = build_rng(None if seed is None else seed + 37_777)

    rotors = [(rotor_label(a_len, i + 1), make_rotor(alphabet, rng_rot))
            for i in range(n_rot)]
    reflectors = [(reflector_label(a_len, i), make_reflector(alphabet, rng_ref))
                for i in range(n_ref)]

    alpha_const = "ALPHA38" if a_len == 38 else "ALPHA76"
    out_text = emit_python(rotors, reflectors, alpha_const, alphabet, a_hash, seed)

    outfile = f"wheels_{alpha_code}.py"
    with open(outfile, "w", encoding="utf-8") as f:
        f.write(out_text)

    print(f"✔ Generated {len(rotors)} rotors & {len(reflectors)} reflectors "
        f"(alphabet {a_len} hash={a_hash}) → {outfile}")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[red]Interrupted.[/red]")