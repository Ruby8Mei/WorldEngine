# debug.py
from __future__ import annotations

import logging
import time
from contextlib import contextmanager
from typing import Dict, List

# try to import Rich ------------------------------------------------------
try:
    from rich.logging import RichHandler
    from rich.console import Console
    from rich.table import Table
    _RICH = True
    _console = Console()
except ImportError:            # graceful fallback
    RichHandler = None         # type: ignore
    _RICH = False
    _console = None            # type: ignore

# ------------------------------------------------------------------------
class Debug:
    __slots__ = ("logger", "enabled", "components")

    _root_configured: bool = False     # class‑level guard

    COMPONENTS_DEFAULT: List[str] = [
        "keyboard", "plugboard", "rotor",
        "reflector", "stepping", "encipher",
    ]

    # ── setup -----------------------------------------------------------
    def __init__(self, *, log_to: str | None = None) -> None:
        """
        Parameters
        ----------
        log_to : str | None
            Optional file path – if given, log messages are also
            written there in plain text.
        """
        self._configure_root(log_to)

        self.logger = logging.getLogger("INOP")
        self.enabled: bool = True

        # per‑component switchboard
        self.components: Dict[str, bool] = {c: False for c in self.COMPONENTS_DEFAULT}

    # root logger only configured once -----------------------------------
    @classmethod
    def _configure_root(cls, log_to: str | None) -> None:
        if cls._root_configured:
            return

        handlers: List[logging.Handler] = []
        if _RICH:
            handlers.append(RichHandler(rich_tracebacks=True, markup=True))
        else:
            handlers.append(logging.StreamHandler())

        if log_to:
            handlers.append(logging.FileHandler(log_to, encoding="utf-8"))

        logging.basicConfig(
            level=logging.DEBUG,
            format="[%6.6s] %(message)s" if _RICH else "[%(asctime)s] [%(levelname)s] %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S",
            handlers=handlers,
        )
        cls._root_configured = True

    # ── logging API -----------------------------------------------------
    def log(self, component: str, message: str) -> None:
        if self.enabled and self.components.get(component, False):
            self.logger.debug(f"[{component.upper():9}] {message}")

    # ── component toggles ----------------------------------------------
    def enable(self, *components: str) -> None:
        for c in components:
            self._require(c)
            self.components[c] = True

    def disable(self, *components: str) -> None:
        for c in components:
            self._require(c)
            self.components[c] = False

    def toggle(self, component: str) -> None:
        self._require(component)
        self.components[component] = not self.components[component]

    def toggle_global(self, state: bool) -> None:
        """Turn *all* debug output on (`True`) or off (`False`)."""
        self.enabled = state

    # ── status helpers --------------------------------------------------
    def status(self) -> Dict[str, bool]:
        """Return a *copy* of the component map."""
        return self.components.copy()

    def show_status(self) -> None:
        """Pretty‑print component switches (Rich table if available)."""
        if _RICH:
            table = Table(title="Debug Component Status", expand=False)
            table.add_column("Component", style="bold")
            table.add_column("Active", justify="center")
            for name, active in self.components.items():
                table.add_row(name, "[green]ON[/]" if active else "[red]OFF[/]")
            _console.print(table)
        else:
            print("Debug status:")
            for k, v in self.components.items():
                print(f"  {k:<10}: {'ON' if v else 'OFF'}")

    # ── timer context manager ------------------------------------------
    @contextmanager
    def timer(self, component: str):
        """
        Usage:
            with dbg.timer("encipher"):
                run_code()
        """
        start = time.perf_counter()
        try:
            yield
        finally:
            elapsed = (time.perf_counter() - start) * 1000
            self.log(component, f"TIMER {elapsed:,.2f} ms")

    # ── helpers ---------------------------------------------------------
    def _require(self, component: str) -> None:
        if component not in self.components:
            raise ValueError(f"No such component: {component!r}")

    # representation -----------------------------------------------------
    def __repr__(self) -> str:
        active = [k for k, v in self.components.items() if v]
        return f"<Debug enabled={self.enabled} active={active}>"

# explicit re‑export
__all__ = ["Debug"]