"""Linux SocketCAN transport backend.

This module intentionally uses Python's stdlib raw CAN socket support. It keeps
AetherFlow runnable on a minimal Ubuntu Server VM with only vcan/can kernel
support enabled.
"""

from __future__ import annotations

import socket
import struct
from dataclasses import dataclass

from bridge_service.can_wire import CAN_FRAME_MAX_DATA_LEN, CAN_STANDARD_ID_MAX, CanFrame
from bridge_service.aetherflow_can import AetherflowCanFrameClass, make_can_id

CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_ERR_FLAG = 0x20000000
CAN_SFF_MASK = 0x000007FF
CAN_EFF_MASK = 0x1FFFFFFF

_CAN_FRAME_STRUCT = struct.Struct("=IB3x8s")
_CAN_FILTER_STRUCT = struct.Struct("=II")


@dataclass(frozen=True, slots=True)
class CanFilter:
    can_id: int
    can_mask: int

    def pack(self) -> bytes:
        return _CAN_FILTER_STRUCT.pack(self.can_id, self.can_mask)


class SocketCanTransport:
    """Small wrapper around a Linux raw CAN socket."""

    def __init__(self, sock: socket.socket, interface: str) -> None:
        self._sock = sock
        self.interface = interface

    @property
    def fd(self) -> int:
        return self._sock.fileno()

    @property
    def socket(self) -> socket.socket:
        return self._sock

    def send(self, frame: CanFrame) -> None:
        self._sock.send(_encode_socketcan_frame(frame))

    def recv(self) -> CanFrame | None:
        try:
            data = self._sock.recv(_CAN_FRAME_STRUCT.size)
        except BlockingIOError:
            return None
        if not data:
            return None
        return _decode_socketcan_frame(data)

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass


def eps_reply_filter(node_id: int) -> CanFilter:
    """Exact standard-ID filter for AetherFlow protocol replies from one EPS node."""

    return CanFilter(make_can_id(AetherflowCanFrameClass.REPLY, node_id), CAN_SFF_MASK)


def open_socketcan_transport(
    interface: str,
    filters: list[CanFilter] | tuple[CanFilter, ...] | None = None,
    *,
    nonblocking: bool = True,
) -> SocketCanTransport:
    if not interface:
        raise ValueError("SocketCAN interface name is required")
    if not hasattr(socket, "PF_CAN") or not hasattr(socket, "CAN_RAW"):
        raise OSError("SocketCAN is available only on Linux with CAN socket support")

    sock = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    try:
        if filters:
            packed_filters = b"".join(can_filter.pack() for can_filter in filters)
            sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER, packed_filters)
        sock.bind((interface,))
        sock.setblocking(not nonblocking)
        return SocketCanTransport(sock, interface)
    except OSError:
        sock.close()
        raise


def _encode_socketcan_frame(frame: CanFrame) -> bytes:
    can_id = frame.id
    if frame.is_extended:
        can_id = (can_id & CAN_EFF_MASK) | CAN_EFF_FLAG
    else:
        can_id &= CAN_SFF_MASK
    if frame.is_rtr:
        can_id |= CAN_RTR_FLAG
    if frame.is_error:
        can_id |= CAN_ERR_FLAG
    return _CAN_FRAME_STRUCT.pack(can_id, frame.dlc, frame.padded_data)


def _decode_socketcan_frame(data: bytes) -> CanFrame:
    if len(data) != _CAN_FRAME_STRUCT.size:
        raise ValueError("invalid SocketCAN frame size")
    raw_id, dlc, payload = _CAN_FRAME_STRUCT.unpack(data)
    is_extended = (raw_id & CAN_EFF_FLAG) != 0
    is_rtr = (raw_id & CAN_RTR_FLAG) != 0
    is_error = (raw_id & CAN_ERR_FLAG) != 0
    mask = CAN_EFF_MASK if is_extended else CAN_STANDARD_ID_MAX
    return CanFrame(
        id=raw_id & mask,
        dlc=min(dlc, CAN_FRAME_MAX_DATA_LEN),
        data=payload[: min(dlc, CAN_FRAME_MAX_DATA_LEN)],
        is_extended=is_extended,
        is_rtr=is_rtr,
        is_error=is_error,
    )
