#AFC1 CAN frame wire-format encoder/decoder

from __future__ import annotations

from dataclasses import dataclass

CAN_FRAME_MAX_DATA_LEN = 8
CAN_STANDARD_ID_MAX = 0x7FF
CAN_EXTENDED_ID_MAX = 0x1FFFFFFF
CAN_FRAME_WIRE_SIZE = 19

WIRE_MAGIC = b"AFC1"
WIRE_VERSION = 1
WIRE_FLAG_EXTENDED = 0x01
WIRE_FLAG_RTR = 0x02
WIRE_FLAG_ERROR = 0x04


@dataclass(slots=True)
class CanFrame:
    """CAN frame passed between protocol code and transport backends.

    id: CAN arbitration ID. Standard frames use 11-bit IDs, extended frames use 29-bit IDs.
    dlc: Data Length Code: how many payload bytes are valid, max 8.
    data: Payload bytes. Only first `dlc` bytes are sent; wire format pads rest with zeroes.
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
    """Create and validate a `CanFrame`.

    Checks CAN ID/DLC ranges, converts payload-like objects to `bytes`, and returns
    a ready-to-send frame. If `dlc` is omitted, it is calculated from `len(data)`.
    """
    payload = bytes(data or b"")
    actual_dlc = len(payload) if dlc is None else dlc
    return CanFrame(id=frame_id, dlc=actual_dlc, data=payload, is_extended=is_extended)


def encode_can_frame(frame: CanFrame) -> bytes:
    """Serialize a `CanFrame` into the stable 19-byte AFC1 compatibility envelope.

    AFC1 layout: magic/version, flags, big-endian CAN ID, DLC and 8 padded data bytes.
    The explicit layout is kept for protocol vectors and non-runtime compatibility checks.
    """
    if frame.dlc > CAN_FRAME_MAX_DATA_LEN:
        raise ValueError("CAN frame DLC must be <= 8")

    flags = 0
    if frame.is_extended:
        flags |= WIRE_FLAG_EXTENDED
    if frame.is_rtr:
        flags |= WIRE_FLAG_RTR
    if frame.is_error:
        flags |= WIRE_FLAG_ERROR

    out = bytearray(CAN_FRAME_WIRE_SIZE)
    out[0:4] = WIRE_MAGIC
    out[4] = WIRE_VERSION
    out[5] = flags
    out[6:10] = frame.id.to_bytes(4, "big", signed=False)
    out[10] = frame.dlc
    out[11:19] = frame.padded_data
    return bytes(out)


def decode_can_frame(data: bytes | bytearray | memoryview) -> CanFrame:
    """Parse a 19-byte AFC1 compatibility envelope back into a `CanFrame`.

    Validates magic/version and size before rebuilding the frame. Raises `ValueError`
    for malformed compatibility packets so callers can drop bad data cleanly.
    """
    wire = bytes(data)
    if len(wire) != CAN_FRAME_WIRE_SIZE:
        raise ValueError("invalid AFC1 frame size")
    if wire[0:4] != WIRE_MAGIC or wire[4] != WIRE_VERSION:
        raise ValueError("invalid AFC1 magic/version")

    flags = wire[5]
    dlc = wire[10]
    frame = CanFrame(
        id=int.from_bytes(wire[6:10], "big", signed=False),
        dlc=dlc,
        data=wire[11 : 11 + dlc],
        is_extended=(flags & WIRE_FLAG_EXTENDED) != 0,
    )
    frame.is_rtr = (flags & WIRE_FLAG_RTR) != 0
    frame.is_error = (flags & WIRE_FLAG_ERROR) != 0
    return frame
