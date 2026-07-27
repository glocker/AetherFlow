# AetherFlow CAN Protocol ID parsing and packet reassembly

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum

from .can_wire import CAN_FRAME_MAX_DATA_LEN, CAN_STANDARD_ID_MAX, CanFrame, can_frame_init

AETHERFLOWCAN_NODE_BROADCAST = 0
AETHERFLOWCAN_NODE_MAX = 127
AETHERFLOWCAN_PACKET_MAX_SIZE = 255
AETHERFLOWCAN_FRAGMENT_SEQUENCE_MAX = 63

AETHERFLOWCAN_CAN_ID_SYNC = 0x080
AETHERFLOWCAN_CAN_ID_REPLY_BASE = 0x580
AETHERFLOWCAN_CAN_ID_REQUEST_BASE = 0x600
AETHERFLOWCAN_CAN_ID_HEARTBEAT_BASE = 0x700

AETHERFLOWCAN_PACKET_HEADER_LEN = 2
AETHERFLOWCAN_FRAGMENT_KIND_SHIFT = 6
AETHERFLOWCAN_FRAGMENT_SEQ_MASK = 0x3F
AETHERFLOWCAN_SINGLE_PAYLOAD_CAPACITY = 7
AETHERFLOWCAN_FIRST_PAYLOAD_CAPACITY = 6
AETHERFLOWCAN_CONT_PAYLOAD_CAPACITY = 7


class AetherflowCanStatus(IntEnum):
    OK = 0
    ERR_NULL = -1
    ERR_RANGE = -2
    ERR_BUFFER_TOO_SMALL = -3
    ERR_INVALID_FRAME = -4
    ERR_UNEXPECTED_FRAGMENT = -5
    ERR_SEQUENCE = -6
    ERR_IN_PROGRESS = -7


class AetherflowCanFrameClass(IntEnum):
    SYNC = 0
    HEARTBEAT = 1
    REQUEST = 2
    REPLY = 3


class AetherflowCanFragmentKind(IntEnum):
    SINGLE = 0
    FIRST = 1
    CONSECUTIVE = 2
    LAST = 3


@dataclass(slots=True)
class AetherflowCanId:
    frame_class: AetherflowCanFrameClass
    node_id: int


@dataclass(slots=True)
class AetherflowCanPacketView:
    service: int
    subtype: int
    payload: bytes

    @property
    def payload_len(self) -> int:
        return len(self.payload)


@dataclass(slots=True)
class AetherflowCanReassembly:
    active: bool = False
    expected_total_len: int = 0
    received_len: int = 0
    next_sequence: int = 0
    buffer: bytearray = field(default_factory=lambda: bytearray(AETHERFLOWCAN_PACKET_MAX_SIZE))

    def reset(self) -> None:
        self.active = False
        self.expected_total_len = 0
        self.received_len = 0
        self.next_sequence = 0
        self.buffer[:] = b"\x00" * AETHERFLOWCAN_PACKET_MAX_SIZE


def node_id_valid(node_id: int) -> bool:
    return 0 <= node_id <= AETHERFLOWCAN_NODE_MAX


def make_can_id(frame_class: AetherflowCanFrameClass | int, node_id: int) -> int:
    if not node_id_valid(node_id):
        raise ValueError("AetherFlow protocol node id out of range")
    frame_class = AetherflowCanFrameClass(frame_class)
    if frame_class == AetherflowCanFrameClass.SYNC:
        return AETHERFLOWCAN_CAN_ID_SYNC
    if frame_class == AetherflowCanFrameClass.HEARTBEAT:
        return AETHERFLOWCAN_CAN_ID_HEARTBEAT_BASE + node_id
    if frame_class == AetherflowCanFrameClass.REQUEST:
        return AETHERFLOWCAN_CAN_ID_REQUEST_BASE + node_id
    if frame_class == AetherflowCanFrameClass.REPLY:
        return AETHERFLOWCAN_CAN_ID_REPLY_BASE + node_id
    raise ValueError("unknown AetherFlow protocol frame class")


def parse_can_id(can_id: int) -> AetherflowCanId:
    if can_id == AETHERFLOWCAN_CAN_ID_SYNC:
        return AetherflowCanId(AetherflowCanFrameClass.SYNC, AETHERFLOWCAN_NODE_BROADCAST)
    if AETHERFLOWCAN_CAN_ID_REPLY_BASE <= can_id <= AETHERFLOWCAN_CAN_ID_REPLY_BASE + AETHERFLOWCAN_NODE_MAX:
        return AetherflowCanId(AetherflowCanFrameClass.REPLY, can_id - AETHERFLOWCAN_CAN_ID_REPLY_BASE)
    if AETHERFLOWCAN_CAN_ID_REQUEST_BASE <= can_id <= AETHERFLOWCAN_CAN_ID_REQUEST_BASE + AETHERFLOWCAN_NODE_MAX:
        return AetherflowCanId(AetherflowCanFrameClass.REQUEST, can_id - AETHERFLOWCAN_CAN_ID_REQUEST_BASE)
    if AETHERFLOWCAN_CAN_ID_HEARTBEAT_BASE <= can_id <= AETHERFLOWCAN_CAN_ID_HEARTBEAT_BASE + AETHERFLOWCAN_NODE_MAX:
        return AetherflowCanId(AetherflowCanFrameClass.HEARTBEAT, can_id - AETHERFLOWCAN_CAN_ID_HEARTBEAT_BASE)
    raise ValueError("CAN ID is outside AetherFlow protocol ranges")


def make_frame(frame_class: AetherflowCanFrameClass | int, node_id: int, data: bytes = b"") -> CanFrame:
    frame_id = make_can_id(frame_class, node_id)
    if frame_id > CAN_STANDARD_ID_MAX:
        raise ValueError("AetherFlow protocol frame id is not a standard CAN id")
    return can_frame_init(frame_id, data, len(data), False)


def packet_build(service: int, subtype: int, payload: bytes = b"") -> bytes:
    packet_len = AETHERFLOWCAN_PACKET_HEADER_LEN + len(payload)
    if packet_len > AETHERFLOWCAN_PACKET_MAX_SIZE:
        raise ValueError("AetherFlow protocol packet too large")
    return bytes((service & 0xFF, subtype & 0xFF)) + bytes(payload)


def packet_parse(packet: bytes | bytearray | memoryview) -> AetherflowCanPacketView:
    data = bytes(packet)
    if len(data) < AETHERFLOWCAN_PACKET_HEADER_LEN or len(data) > AETHERFLOWCAN_PACKET_MAX_SIZE:
        raise ValueError("invalid AetherFlow protocol packet length")
    return AetherflowCanPacketView(service=data[0], subtype=data[1], payload=data[2:])


def _fragment_header(kind: AetherflowCanFragmentKind, sequence: int) -> int:
    return ((int(kind) << AETHERFLOWCAN_FRAGMENT_KIND_SHIFT) | (sequence & AETHERFLOWCAN_FRAGMENT_SEQ_MASK)) & 0xFF


def fragment_packet(frame_class: AetherflowCanFrameClass | int, node_id: int, packet: bytes) -> list[CanFrame]:
    data = bytes(packet)
    packet_len = len(data)
    if packet_len == 0 or packet_len > AETHERFLOWCAN_PACKET_MAX_SIZE:
        raise ValueError("invalid AetherFlow protocol packet length")

    if packet_len <= AETHERFLOWCAN_SINGLE_PAYLOAD_CAPACITY:
        frame_data = bytes((_fragment_header(AetherflowCanFragmentKind.SINGLE, 0),)) + data
        return [make_frame(frame_class, node_id, frame_data)]

    required_frames = 1 + ((packet_len - AETHERFLOWCAN_FIRST_PAYLOAD_CAPACITY + AETHERFLOWCAN_CONT_PAYLOAD_CAPACITY - 1) // AETHERFLOWCAN_CONT_PAYLOAD_CAPACITY)
    if required_frames > AETHERFLOWCAN_FRAGMENT_SEQUENCE_MAX + 1:
        raise ValueError("AetherFlow protocol packet requires too many fragments")

    frames: list[CanFrame] = []
    sequence = 0
    frames.append(
        make_frame(
            frame_class,
            node_id,
            bytes((_fragment_header(AetherflowCanFragmentKind.FIRST, sequence), packet_len)) + data[:AETHERFLOWCAN_FIRST_PAYLOAD_CAPACITY],
        )
    )
    sequence += 1
    offset = AETHERFLOWCAN_FIRST_PAYLOAD_CAPACITY
    while offset < packet_len:
        chunk = data[offset : offset + AETHERFLOWCAN_CONT_PAYLOAD_CAPACITY]
        kind = AetherflowCanFragmentKind.LAST if offset + len(chunk) == packet_len else AetherflowCanFragmentKind.CONSECUTIVE
        frames.append(make_frame(frame_class, node_id, bytes((_fragment_header(kind, sequence),)) + chunk))
        sequence += 1
        offset += len(chunk)
    return frames


def _validate_data_frame(frame: CanFrame) -> None:
    if frame.is_extended or frame.is_rtr or frame.is_error or frame.dlc == 0 or frame.dlc > CAN_FRAME_MAX_DATA_LEN:
        raise ValueError("invalid AetherFlow protocol data frame")
    parse_can_id(frame.id)


def reassembly_accept(state: AetherflowCanReassembly, frame: CanFrame) -> tuple[AetherflowCanStatus, bytes | None]:
    _validate_data_frame(frame)
    kind = AetherflowCanFragmentKind(frame.data[0] >> AETHERFLOWCAN_FRAGMENT_KIND_SHIFT)
    sequence = frame.data[0] & AETHERFLOWCAN_FRAGMENT_SEQ_MASK

    if kind == AetherflowCanFragmentKind.SINGLE:
        if state.active:
            return AetherflowCanStatus.ERR_UNEXPECTED_FRAGMENT, None
        if sequence != 0 or frame.dlc < 2 or frame.dlc > AETHERFLOWCAN_SINGLE_PAYLOAD_CAPACITY + 1:
            return AetherflowCanStatus.ERR_INVALID_FRAME, None
        return AetherflowCanStatus.OK, frame.data[1:frame.dlc]

    if kind == AetherflowCanFragmentKind.FIRST:
        if state.active:
            return AetherflowCanStatus.ERR_UNEXPECTED_FRAGMENT, None
        if sequence != 0 or frame.dlc != CAN_FRAME_MAX_DATA_LEN:
            return AetherflowCanStatus.ERR_INVALID_FRAME, None
        total_len = frame.data[1]
        if total_len <= AETHERFLOWCAN_SINGLE_PAYLOAD_CAPACITY:
            return AetherflowCanStatus.ERR_INVALID_FRAME, None
        state.active = True
        state.expected_total_len = total_len
        state.received_len = AETHERFLOWCAN_FIRST_PAYLOAD_CAPACITY
        state.next_sequence = 1
        state.buffer[0:AETHERFLOWCAN_FIRST_PAYLOAD_CAPACITY] = frame.data[2:8]
        return AetherflowCanStatus.ERR_IN_PROGRESS, None

    if kind in (AetherflowCanFragmentKind.CONSECUTIVE, AetherflowCanFragmentKind.LAST):
        if not state.active:
            return AetherflowCanStatus.ERR_UNEXPECTED_FRAGMENT, None
        if sequence != state.next_sequence:
            state.reset()
            return AetherflowCanStatus.ERR_SEQUENCE, None
        if frame.dlc < 2 or frame.dlc > AETHERFLOWCAN_CONT_PAYLOAD_CAPACITY + 1:
            state.reset()
            return AetherflowCanStatus.ERR_INVALID_FRAME, None
        chunk_len = frame.dlc - 1
        if state.received_len + chunk_len > state.expected_total_len:
            state.reset()
            return AetherflowCanStatus.ERR_INVALID_FRAME, None
        if kind == AetherflowCanFragmentKind.CONSECUTIVE and chunk_len != AETHERFLOWCAN_CONT_PAYLOAD_CAPACITY:
            state.reset()
            return AetherflowCanStatus.ERR_INVALID_FRAME, None
        state.buffer[state.received_len : state.received_len + chunk_len] = frame.data[1:frame.dlc]
        state.received_len += chunk_len
        state.next_sequence = (state.next_sequence + 1) & AETHERFLOWCAN_FRAGMENT_SEQ_MASK
        if kind == AetherflowCanFragmentKind.LAST:
            if state.received_len != state.expected_total_len:
                state.reset()
                return AetherflowCanStatus.ERR_INVALID_FRAME, None
            packet = bytes(state.buffer[: state.received_len])
            state.reset()
            return AetherflowCanStatus.OK, packet
        return AetherflowCanStatus.ERR_IN_PROGRESS, None

    return AetherflowCanStatus.ERR_INVALID_FRAME, None


def get_u16_be(data: bytes | bytearray | memoryview) -> int:
    return int.from_bytes(bytes(data[:2]), "big", signed=False)


def get_i16_be(data: bytes | bytearray | memoryview) -> int:
    return int.from_bytes(bytes(data[:2]), "big", signed=True)


def get_u32_be(data: bytes | bytearray | memoryview) -> int:
    return int.from_bytes(bytes(data[:4]), "big", signed=False)


def get_i32_be(data: bytes | bytearray | memoryview) -> int:
    return int.from_bytes(bytes(data[:4]), "big", signed=True)


def put_u16_be(value: int) -> bytes:
    return int(value & 0xFFFF).to_bytes(2, "big", signed=False)


def put_i16_be(value: int) -> bytes:
    return int(value).to_bytes(2, "big", signed=True)


def put_u32_be(value: int) -> bytes:
    return int(value & 0xFFFFFFFF).to_bytes(4, "big", signed=False)


def put_i32_be(value: int) -> bytes:
    return int(value).to_bytes(4, "big", signed=True)
