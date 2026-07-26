#AetherFlow SpaceCAN CAN-ID parsing and packet reassembly

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum

from .can_wire import CAN_FRAME_MAX_DATA_LEN, CAN_STANDARD_ID_MAX, CanFrame, can_frame_init

SPACECAN_NODE_BROADCAST = 0
SPACECAN_NODE_MAX = 127
SPACECAN_PACKET_MAX_SIZE = 255
SPACECAN_FRAGMENT_SEQUENCE_MAX = 63

SPACECAN_CAN_ID_SYNC = 0x080
SPACECAN_CAN_ID_REPLY_BASE = 0x580
SPACECAN_CAN_ID_REQUEST_BASE = 0x600
SPACECAN_CAN_ID_HEARTBEAT_BASE = 0x700

SPACECAN_PACKET_HEADER_LEN = 2
SPACECAN_FRAGMENT_KIND_SHIFT = 6
SPACECAN_FRAGMENT_SEQ_MASK = 0x3F
SPACECAN_SINGLE_PAYLOAD_CAPACITY = 7
SPACECAN_FIRST_PAYLOAD_CAPACITY = 6
SPACECAN_CONT_PAYLOAD_CAPACITY = 7


class SpaceCanStatus(IntEnum):
    OK = 0
    ERR_NULL = -1
    ERR_RANGE = -2
    ERR_BUFFER_TOO_SMALL = -3
    ERR_INVALID_FRAME = -4
    ERR_UNEXPECTED_FRAGMENT = -5
    ERR_SEQUENCE = -6
    ERR_IN_PROGRESS = -7


class SpaceCanFrameClass(IntEnum):
    SYNC = 0
    HEARTBEAT = 1
    REQUEST = 2
    REPLY = 3


class SpaceCanFragmentKind(IntEnum):
    SINGLE = 0
    FIRST = 1
    CONSECUTIVE = 2
    LAST = 3


@dataclass(slots=True)
class SpaceCanId:
    frame_class: SpaceCanFrameClass
    node_id: int


@dataclass(slots=True)
class SpaceCanPacketView:
    service: int
    subtype: int
    payload: bytes

    @property
    def payload_len(self) -> int:
        return len(self.payload)


@dataclass(slots=True)
class SpaceCanReassembly:
    active: bool = False
    expected_total_len: int = 0
    received_len: int = 0
    next_sequence: int = 0
    buffer: bytearray = field(default_factory=lambda: bytearray(SPACECAN_PACKET_MAX_SIZE))

    def reset(self) -> None:
        self.active = False
        self.expected_total_len = 0
        self.received_len = 0
        self.next_sequence = 0
        self.buffer[:] = b"\x00" * SPACECAN_PACKET_MAX_SIZE


def node_id_valid(node_id: int) -> bool:
    return 0 <= node_id <= SPACECAN_NODE_MAX


def make_can_id(frame_class: SpaceCanFrameClass | int, node_id: int) -> int:
    if not node_id_valid(node_id):
        raise ValueError("SpaceCAN node id out of range")
    frame_class = SpaceCanFrameClass(frame_class)
    if frame_class == SpaceCanFrameClass.SYNC:
        return SPACECAN_CAN_ID_SYNC
    if frame_class == SpaceCanFrameClass.HEARTBEAT:
        return SPACECAN_CAN_ID_HEARTBEAT_BASE + node_id
    if frame_class == SpaceCanFrameClass.REQUEST:
        return SPACECAN_CAN_ID_REQUEST_BASE + node_id
    if frame_class == SpaceCanFrameClass.REPLY:
        return SPACECAN_CAN_ID_REPLY_BASE + node_id
    raise ValueError("unknown SpaceCAN frame class")


def parse_can_id(can_id: int) -> SpaceCanId:
    if can_id == SPACECAN_CAN_ID_SYNC:
        return SpaceCanId(SpaceCanFrameClass.SYNC, SPACECAN_NODE_BROADCAST)
    if SPACECAN_CAN_ID_REPLY_BASE <= can_id <= SPACECAN_CAN_ID_REPLY_BASE + SPACECAN_NODE_MAX:
        return SpaceCanId(SpaceCanFrameClass.REPLY, can_id - SPACECAN_CAN_ID_REPLY_BASE)
    if SPACECAN_CAN_ID_REQUEST_BASE <= can_id <= SPACECAN_CAN_ID_REQUEST_BASE + SPACECAN_NODE_MAX:
        return SpaceCanId(SpaceCanFrameClass.REQUEST, can_id - SPACECAN_CAN_ID_REQUEST_BASE)
    if SPACECAN_CAN_ID_HEARTBEAT_BASE <= can_id <= SPACECAN_CAN_ID_HEARTBEAT_BASE + SPACECAN_NODE_MAX:
        return SpaceCanId(SpaceCanFrameClass.HEARTBEAT, can_id - SPACECAN_CAN_ID_HEARTBEAT_BASE)
    raise ValueError("CAN ID is outside SpaceCAN ranges")


def make_frame(frame_class: SpaceCanFrameClass | int, node_id: int, data: bytes = b"") -> CanFrame:
    frame_id = make_can_id(frame_class, node_id)
    if frame_id > CAN_STANDARD_ID_MAX:
        raise ValueError("SpaceCAN frame id is not a standard CAN id")
    return can_frame_init(frame_id, data, len(data), False)


def packet_build(service: int, subtype: int, payload: bytes = b"") -> bytes:
    packet_len = SPACECAN_PACKET_HEADER_LEN + len(payload)
    if packet_len > SPACECAN_PACKET_MAX_SIZE:
        raise ValueError("SpaceCAN packet too large")
    return bytes((service & 0xFF, subtype & 0xFF)) + bytes(payload)


def packet_parse(packet: bytes | bytearray | memoryview) -> SpaceCanPacketView:
    data = bytes(packet)
    if len(data) < SPACECAN_PACKET_HEADER_LEN or len(data) > SPACECAN_PACKET_MAX_SIZE:
        raise ValueError("invalid SpaceCAN packet length")
    return SpaceCanPacketView(service=data[0], subtype=data[1], payload=data[2:])


def _fragment_header(kind: SpaceCanFragmentKind, sequence: int) -> int:
    return ((int(kind) << SPACECAN_FRAGMENT_KIND_SHIFT) | (sequence & SPACECAN_FRAGMENT_SEQ_MASK)) & 0xFF


def fragment_packet(frame_class: SpaceCanFrameClass | int, node_id: int, packet: bytes) -> list[CanFrame]:
    data = bytes(packet)
    packet_len = len(data)
    if packet_len == 0 or packet_len > SPACECAN_PACKET_MAX_SIZE:
        raise ValueError("invalid SpaceCAN packet length")

    if packet_len <= SPACECAN_SINGLE_PAYLOAD_CAPACITY:
        frame_data = bytes((_fragment_header(SpaceCanFragmentKind.SINGLE, 0),)) + data
        return [make_frame(frame_class, node_id, frame_data)]

    required_frames = 1 + ((packet_len - SPACECAN_FIRST_PAYLOAD_CAPACITY + SPACECAN_CONT_PAYLOAD_CAPACITY - 1) // SPACECAN_CONT_PAYLOAD_CAPACITY)
    if required_frames > SPACECAN_FRAGMENT_SEQUENCE_MAX + 1:
        raise ValueError("SpaceCAN packet requires too many fragments")

    frames: list[CanFrame] = []
    sequence = 0
    frames.append(
        make_frame(
            frame_class,
            node_id,
            bytes((_fragment_header(SpaceCanFragmentKind.FIRST, sequence), packet_len)) + data[:SPACECAN_FIRST_PAYLOAD_CAPACITY],
        )
    )
    sequence += 1
    offset = SPACECAN_FIRST_PAYLOAD_CAPACITY
    while offset < packet_len:
        chunk = data[offset : offset + SPACECAN_CONT_PAYLOAD_CAPACITY]
        kind = SpaceCanFragmentKind.LAST if offset + len(chunk) == packet_len else SpaceCanFragmentKind.CONSECUTIVE
        frames.append(make_frame(frame_class, node_id, bytes((_fragment_header(kind, sequence),)) + chunk))
        sequence += 1
        offset += len(chunk)
    return frames


def _validate_data_frame(frame: CanFrame) -> None:
    if frame.is_extended or frame.is_rtr or frame.is_error or frame.dlc == 0 or frame.dlc > CAN_FRAME_MAX_DATA_LEN:
        raise ValueError("invalid SpaceCAN data frame")
    parse_can_id(frame.id)


def reassembly_accept(state: SpaceCanReassembly, frame: CanFrame) -> tuple[SpaceCanStatus, bytes | None]:
    _validate_data_frame(frame)
    kind = SpaceCanFragmentKind(frame.data[0] >> SPACECAN_FRAGMENT_KIND_SHIFT)
    sequence = frame.data[0] & SPACECAN_FRAGMENT_SEQ_MASK

    if kind == SpaceCanFragmentKind.SINGLE:
        if state.active:
            return SpaceCanStatus.ERR_UNEXPECTED_FRAGMENT, None
        if sequence != 0 or frame.dlc < 2 or frame.dlc > SPACECAN_SINGLE_PAYLOAD_CAPACITY + 1:
            return SpaceCanStatus.ERR_INVALID_FRAME, None
        return SpaceCanStatus.OK, frame.data[1:frame.dlc]

    if kind == SpaceCanFragmentKind.FIRST:
        if state.active:
            return SpaceCanStatus.ERR_UNEXPECTED_FRAGMENT, None
        if sequence != 0 or frame.dlc != CAN_FRAME_MAX_DATA_LEN:
            return SpaceCanStatus.ERR_INVALID_FRAME, None
        total_len = frame.data[1]
        if total_len <= SPACECAN_SINGLE_PAYLOAD_CAPACITY:
            return SpaceCanStatus.ERR_INVALID_FRAME, None
        state.active = True
        state.expected_total_len = total_len
        state.received_len = SPACECAN_FIRST_PAYLOAD_CAPACITY
        state.next_sequence = 1
        state.buffer[0:SPACECAN_FIRST_PAYLOAD_CAPACITY] = frame.data[2:8]
        return SpaceCanStatus.ERR_IN_PROGRESS, None

    if kind in (SpaceCanFragmentKind.CONSECUTIVE, SpaceCanFragmentKind.LAST):
        if not state.active:
            return SpaceCanStatus.ERR_UNEXPECTED_FRAGMENT, None
        if sequence != state.next_sequence:
            state.reset()
            return SpaceCanStatus.ERR_SEQUENCE, None
        if frame.dlc < 2 or frame.dlc > SPACECAN_CONT_PAYLOAD_CAPACITY + 1:
            state.reset()
            return SpaceCanStatus.ERR_INVALID_FRAME, None
        chunk_len = frame.dlc - 1
        if state.received_len + chunk_len > state.expected_total_len:
            state.reset()
            return SpaceCanStatus.ERR_INVALID_FRAME, None
        if kind == SpaceCanFragmentKind.CONSECUTIVE and chunk_len != SPACECAN_CONT_PAYLOAD_CAPACITY:
            state.reset()
            return SpaceCanStatus.ERR_INVALID_FRAME, None
        state.buffer[state.received_len : state.received_len + chunk_len] = frame.data[1:frame.dlc]
        state.received_len += chunk_len
        state.next_sequence = (state.next_sequence + 1) & SPACECAN_FRAGMENT_SEQ_MASK
        if kind == SpaceCanFragmentKind.LAST:
            if state.received_len != state.expected_total_len:
                state.reset()
                return SpaceCanStatus.ERR_INVALID_FRAME, None
            packet = bytes(state.buffer[: state.received_len])
            state.reset()
            return SpaceCanStatus.OK, packet
        return SpaceCanStatus.ERR_IN_PROGRESS, None

    return SpaceCanStatus.ERR_INVALID_FRAME, None


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
