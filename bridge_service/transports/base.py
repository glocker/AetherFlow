"""Transport interface used by protocol code and runtime services."""

from __future__ import annotations

from typing import Protocol

from bridge_service.can_wire import CanFrame


class CanTransport(Protocol):
    """Minimal CAN transport contract.

    Runtime services should depend on this protocol instead of a concrete Linux
    SocketCAN implementation. That keeps the SpaceCAN/EPS code independent from
    the bus backend.
    """

    @property
    def fd(self) -> int:
        """File descriptor suitable for select/selectors."""
        ...

    def send(self, frame: CanFrame) -> None:
        """Send one CAN frame."""

    def recv(self) -> CanFrame | None:
        """Receive one CAN frame if available, otherwise return None."""

    def close(self) -> None:
        """Close transport resources."""
