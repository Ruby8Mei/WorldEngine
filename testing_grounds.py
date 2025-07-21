# testing_grounds.py
from __future__ import annotations

import argparse
import json
import random
import statistics
import sys
from copy import deepcopy
from pathlib import Path
from time import perf_counter
from typing import Any, Dict, List

# ── project‑local imports ───────────────────────────────────────────────
import registry                          # ALPHA26 / 38 / 76 and wheel pools
import utilities as wheels               # load_suite(), set_current_suite(), CURRENT, preprocess_message
import inop                              # Inop.Inop cipher machine
from keyboard_and_plugboard import Keyboard, Plugboard

# ── constants ──────────────────────────────────────────────────────────
ALPHABETS = {
    "26": registry.ALPHA26,
    "38": registry.ALPHA38,
    "76": registry.ALPHA76,
}


def suite_to_code(val: str) -> str:
    """Normalise suite field to canonical numeric code ('26', '38', '76')."""
    up = val.upper()
    if up in ("26", "38", "76"):
        return up
    if "76" in up:
        return "76"
    if "38" in up:
        return "38"
    if "26" in up or "LEGACY" in up:
        return "26"
    raise ValueError(f"Unrecognised suite value: {val!r}")


# ── JSON helpers ───────────────────────────────────────────────────────

def load_configs(path: Path) -> List[Dict[str, Any]]:
    """Return list of normalised config dicts."""
    raw = json.loads(path.read_text(encoding="utf‑8"))
    cfg_list = raw if isinstance(raw, list) else [raw]
    return [normalise_cfg(cfg) for cfg in cfg_list]


def normalise_cfg(cfg: Dict[str, Any]) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    out["suite"] = suite_to_code(cfg["suite"])
    out["rotors"] = cfg["rotors"]
    out["reflector"] = cfg["reflector"]
    out["rings"] = cfg.get("rings", cfg.get("ring_set", []))
    out["plugs"] = cfg.get("plugs", [])
    out["master_key"] = cfg["master_key"]
    out["notch_map"] = cfg.get("notch_map", {})
    return out


# ── fuzz helpers ───────────────────────────────────────────────────────

def random_plaintext(alpha: str, min_len: int = 30, max_len: int = 120) -> str:
    length = random.randint(min_len, max_len)
    chars = [random.choice(alpha + "    ") for _ in range(length)]  # sprinkle spaces
    return "".join(chars)


# ── core test routine ──────────────────────────────────────────────────

def run_case(cfg: Dict[str, Any], runs: int) -> None:
    scode = cfg["suite"]
    alpha = ALPHABETS[scode]

    if wheels.CURRENT.code != scode:
        wheels.set_current_suite(scode)
    _, _, rotor_pool, refl_pool = wheels.load_suite()

    def build_rotor(name: str):
        r = deepcopy(rotor_pool[name])
        custom = cfg["notch_map"].get(name)
        if custom is not None:
            r.notches = custom
        return r

    keyboard = Keyboard(alpha)  # stateless, safe to reuse

    def new_machine():
        return inop.Inop(
            kb=keyboard,
            pb=Plugboard(cfg["plugs"], alphabet=alpha),
            rotors=[build_rotor(r) for r in cfg["rotors"]],
            reflector=deepcopy(refl_pool[cfg["reflector"]]),
            ring_settings=cfg["rings"],
            master_key=cfg["master_key"],
        )

    def encipher_text(mach, text: str) -> str:
        processed = wheels.preprocess_message(text, alpha)
        return "".join(mach.encipher(ch) for ch in processed)

    durations: List[float] = []
    for i in range(1, runs + 1):
        msg = random_plaintext(alpha)
        t0 = perf_counter()
        enc = encipher_text(new_machine(), msg)
        dec = encipher_text(new_machine(), enc)
        durations.append(perf_counter() - t0)

        if wheels.preprocess_message(msg, alpha) != dec:
            print(f"\n❌ Round‑trip failed on iteration {i}")
            print(" Settings:", cfg)
            print(" Plaintext:", msg)
            print(" Cypher   :", enc)
            print(" Decrypted:", dec)
            sys.exit(1)

    mean_ms = statistics.mean(durations) * 1000
    p95_ms = statistics.quantiles(durations, n=20)[18] * 1000
    print(f"    ✔ {runs} runs OK  (avg {mean_ms:.2f} ms, p95 {p95_ms:.2f} ms)")


# ── CLI ────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(description="Fuzz‑test INOP / Enigma configs")
    ap.add_argument("--config", type=Path, default=Path("inop_config.json"))
    ap.add_argument("--runs", type=int, default=5000)
    args = ap.parse_args()

    cfgs = load_configs(args.config)
    print(f"Fuzzing {len(cfgs)} config(s) × {args.runs} messages each…\n")

    for cfg in cfgs:
        print("· Suite", cfg["suite"], cfg["rotors"])
        run_case(cfg, args.runs)

    print("\nAll done — no failures detected! 🥳")


if __name__ == "__main__":
    main()
