# CAN frame model used by AetherFlow protocol code and transport backends

from __future__ import annotations

from dataclasses import dataclass

CAN_FRAME_MAX_DATA_LEN = 8
CAN_STANDARD_ID_MAX = 0x7FF
CAN_EXTENDED_ID_MAX = 0x1FFFFFFF


@dataclass(slots=True)
class CanFrame:
    """CAN frame passed between protocol code and transport backends.

    id: CAN arbitration ID. Standard frames use 11-bit IDs, extended frames use 29-bit IDs.
    dlc: Data Length Code: how many payload bytes are valid, max 8.
    data: Payload bytes. Only first `dlc` bytes are valid.
    is_extended: True for extended 29-bit CAN ID, False for standard 11-bit CAN ID.
    is_rtr: Remote Transmission Request flag.
    is_error: Error frame flag.
    """

    id: int
    dlc: int
    data: bytes = b""
    is_extended: bool = False
    is_rtr: bool = False
    is_error: bool = False

    def __post_init__(self) -> None:
        if self.dlc < 0 or self.dlc > CAN_FRAME_MAX_DATA_LEN:
            raise ValueError("CAN frame DLC must be in range 0..8")
        max_id = CAN_EXTENDED_ID_MAX if self.is_extended else CAN_STANDARD_ID_MAX
        if self.id < 0 or self.id > max_id:
            raise ValueError("CAN arbitration ID is out of range")
        if len(self.data) < self.dlc:
            raise ValueError("CAN frame data shorter than DLC")
        self.data = bytes(self.data[: self.dlc])

    @property
    def padded_data(self) -> bytes:
        return self.data + (b"\x00" * (CAN_FRAME_MAX_DATA_LEN - self.dlc))


def can_frame_init(
    frame_id: int,
    data: bytes | bytearray | memoryview | None = None,
    dlc: int | None = None,
    is_extended: bool = False,
) -> CanFrame:
    """Create and validate a `CanFrame`."""

    payload = bytes(data or b"")
    actual_dlc = len(payload) if dlc is None else dlc
    return CanFrame(id=frame_id, dlc=actual_dlc, data=payload, is_extended=is_extended)
