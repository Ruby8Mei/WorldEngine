# main.py
from __future__ import annotations

import registry  # populates global rotor/reflector dicts

import json
from pathlib import Path
from copy import deepcopy
from dataclasses import dataclass
from secrets import choice as secure_choice, randbelow
from typing import Sequence

from debug import Debug
from inop import Inop
from keyboard_and_plugboard import Keyboard, Plugboard
from rotor_and_reflector import Rotor, Reflector
from utilities import (
    register_wheels,
    collect_settings,
    preprocess_message,
    rotor_dict,
    reflector_dict,
    show_registry,
)

# ── Register wheels -----------------------------------------------------
register_wheels({**registry.rotor_dict, **registry.reflector_dict})

# ── logging toggle ------------------------------------------------------
debug = Debug()
debug.toggle_global(False)

# ── pipeline flags ------------------------------------------------------
@dataclass(slots=True)
class Config:
    double_pass: bool    = True
    do_padding: bool     = True
    step_reflector: bool = True
    block: int           = 5
    base_noise: int      = 12
    marker_len: int      = 6

# ── MachineContext helper ----------------------------------------------
class MachineContext:
    def __init__(
        self,
        alphabet: str,
        rotors: Sequence[Rotor],
        reflector: Reflector,
        plugs: Sequence[str],
        ring_set: Sequence[int],
        master_key: str,
    ) -> None:
        self.alphabet    = alphabet
        self.rotor_names = [getattr(r, "name", "?") for r in rotors]
        self.plugs       = list(plugs)
        self.ring_set    = list(ring_set)
        self.master_key  = master_key

        self.machine = Inop(
            Keyboard(alphabet),
            Plugboard(plugs, alphabet),
            list(rotors),
            reflector,
            list(ring_set),
            master_key,
        )

    @classmethod
    def from_interactive(cls) -> "MachineContext":
        r_names, refl, rings, notches, plugs, mkey, suite = collect_settings()
        rot_cp = [deepcopy(rotor_dict[n]) for n in r_names]
        for r_obj, lbl in zip(rot_cp, r_names):
            r_obj.set_notches(notches.get(lbl, ""))
        return cls(suite.alphabet, rot_cp, deepcopy(refl), plugs, rings, mkey)

    @classmethod
    def from_json(cls, data: dict) -> "MachineContext":
        rotor_labels = [lbl.strip().upper() for lbl in data["rotors"]]
        refl_label   = data["reflector"].strip().upper()

        missing = [lbl for lbl in rotor_labels if lbl not in rotor_dict]
        if missing:
            raise ValueError("Rotor(s) not in registry: " + ", ".join(missing))
        if refl_label not in reflector_dict:
            raise ValueError(f"Reflector '{refl_label}' not in registry")

        rot_cp       = [deepcopy(rotor_dict[lbl]) for lbl in rotor_labels]
        reflector_cp = deepcopy(reflector_dict[refl_label])

        notch_map = data.get("notch_map", {})
        for r_obj, lbl in zip(rot_cp, rotor_labels):
            r_obj.set_notches(notch_map.get(lbl, ""))

        return cls(
            rot_cp[0].alphabet,
            rot_cp,
            reflector_cp,
            data["plugs"],
            data["ring_set"],
            data["master_key"],
        )

    def rewind(self) -> None:
        self.machine.set_key(self.master_key[:-1])
        self.machine.reflector.rotate_to_letter(self.master_key[-1])

    def encipher_block(self, text: str) -> str:
        self.rewind()
        return "".join(self.machine.encipher(ch) for ch in text)

# ── padding helpers -----------------------------------------------------
def make_marker(alpha: str, length: int) -> str:
    return "".join(secure_choice(alpha) for _ in range(length))

def pad_message(msg: str, alpha: str, *, base_noise:int, block:int) -> str:
    scaled  = max(base_noise, int(len(msg) * 0.35))
    residue = (len(msg) + scaled) % block
    extra   = (-residue) % block
    n       = scaled + extra
    front   = randbelow(n + 1)
    back    = n - front
    return (
        "".join(secure_choice(alpha) for _ in range(front))
        + msg
        + "".join(secure_choice(alpha) for _ in range(back))
    )

def extract_message(full: str, marker: str) -> str:
    i, j = full.find(marker), full.rfind(marker)
    if i == -1 or j == -1 or i == j:
        raise ValueError("Markers not found – padding removal failed")
    return full[i + len(marker) : j]

# ── crypto pipeline -----------------------------------------------------
class CipherPipeline:
    def __init__(self, ctx: MachineContext, cfg: Config) -> None:
        self.ctx = ctx
        self.cfg = cfg
        ctx.machine._step_reflector_flag = cfg.step_reflector

    def encrypt(self, plaintext: str) -> tuple[str, str | None]:
        marker, padded = None, plaintext
        if self.cfg.do_padding:
            marker  = make_marker(self.ctx.alphabet, self.cfg.marker_len)
            wrapped = marker + plaintext + marker
            padded  = pad_message(
                wrapped, self.ctx.alphabet,
                base_noise=self.cfg.base_noise, block=self.cfg.block,
            )
        clean = preprocess_message(padded, self.ctx.alphabet)

        if self.cfg.double_pass:
            s1     = self.ctx.encipher_block(clean)
            cipher = self.ctx.encipher_block(s1[::-1])
        else:
            cipher = self.ctx.encipher_block(clean)
        return cipher, marker

    def decrypt(self, cipher: str, marker: str | None) -> str:
        if self.cfg.double_pass:
            s1   = self.ctx.encipher_block(cipher)
            full = self.ctx.encipher_block(s1[::-1])
        else:
            full = self.ctx.encipher_block(cipher)

        if self.cfg.do_padding and marker is not None:
            full = extract_message(full, marker)
        return full.replace("#", " ")

# ── tiny prompt helpers -------------------------------------------------
def yn_prompt(msg: str, default=True) -> bool:
    d = "Y/n" if default else "y/N"
    ans = input(f"{msg} ({d}) ").strip().lower()
    return default if not ans else ans in {"y", "yes"}

def on_off_prompt(msg: str, default_on=True) -> bool:
    d = "ON/off" if default_on else "on/OFF"
    ans = input(f"{msg} [{d}] ").strip().lower()
    return default_on if not ans else ans in {"on", "yes", "y"}

# ── main flow -----------------------------------------------------------
def main() -> None:
    cfg_file = Path("inop_config.json")
    if cfg_file.exists() and yn_prompt(f"Load settings from '{cfg_file}'?", True):
        cfg = json.loads(cfg_file.read_text(encoding="utf-8"))
        ctx = MachineContext.from_json(cfg)
    else:
        ctx = MachineContext.from_interactive()

    print("\n--- Pipeline Features ---")
    cfg = Config(
        double_pass    = on_off_prompt("Double‑pass (encrypt‑reverse‑encrypt)?", True),
        do_padding     = on_off_prompt("Padding / cover traffic?", True),
        step_reflector = on_off_prompt("Moving reflector?", True),
    )
    crypto = CipherPipeline(ctx, cfg)

    print("\nType blank line to quit.\n")
    while True:
        plain = input("Enter message: ").rstrip("\n")
        if not plain.strip():
            break
        cipher, marker = crypto.encrypt(plain)
        grouped = "  ".join(cipher[i:i+cfg.block] for i in range(0, len(cipher), cfg.block))
        print("Encrypted:", grouped)
        print("Decrypted:", crypto.decrypt(cipher, marker), "\n")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nInterrupted.")