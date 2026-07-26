"""Runtime configuration for the Python bridge service."""

from __future__ import annotations

import os
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENV_FILE = PROJECT_ROOT / "aetherflow.env"

DEFAULT_HTTP_PORT = 8080
DEFAULT_UDP_GROUP = "224.0.0.1"
DEFAULT_UDP_PORT = 40700


def _load_env_file(path: Path) -> None:
    if not path.is_file():
        return

    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            continue

        name, value = stripped.split("=", 1)
        os.environ.setdefault(name.strip(), value.strip())


_load_env_file(DEFAULT_ENV_FILE)


def _int_from_env(name: str, default: int) -> int:
    raw_value = os.getenv(name)
    if raw_value is None or raw_value == "":
        return default

    try:
        value = int(raw_value, 10)
    except ValueError:
        return default

    if value <= 0 or value > 65535:
        return default
    return value


HTTP_PORT = _int_from_env("AETHERFLOW_HTTP_PORT", DEFAULT_HTTP_PORT)
UDP_GROUP = os.getenv("AETHERFLOW_UDP_GROUP", DEFAULT_UDP_GROUP)
UDP_PORT = _int_from_env("AETHERFLOW_UDP_PORT", DEFAULT_UDP_PORT)
