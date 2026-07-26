#Entrypoint for python3 -m bridge_service

from __future__ import annotations

from .server import main


if __name__ == "__main__":
    raise SystemExit(main())
