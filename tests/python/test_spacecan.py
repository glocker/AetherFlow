from __future__ import annotations

import pytest

from bridge_service.can_wire import can_frame_init
from bridge_service.spacecan import (
    SpaceCanFrameClass,
    SpaceCanReassembly,
    SpaceCanStatus,
    fragment_packet,
    get_u16_be,
    make_can_id,
    packet_build,
    packet_parse,
    parse_can_id,
    put_i16_be,
    put_u16_be,
    reassembly_accept,
)




def test_can_id_helpers() -> None:
    assert make_can_id(SpaceCanFrameClass.SYNC, 0) == 0x080
    assert make_can_id(SpaceCanFrameClass.HEARTBEAT, 1) == 0x701
    assert make_can_id(SpaceCanFrameClass.REQUEST, 1) == 0x601
    assert make_can_id(SpaceCanFrameClass.REPLY, 1) == 0x581

    parsed = parse_can_id(0x581)
    assert parsed.frame_class == SpaceCanFrameClass.REPLY
    assert parsed.node_id == 1

    with pytest.raises(ValueError):
        parse_can_id(0x123)


def test_integer_encoding() -> None:
    assert put_u16_be(0x1234) == b"\x12\x34"
    assert get_u16_be(b"\x12\x34") == 0x1234
    assert put_i16_be(-2) == b"\xff\xfe"
    assert get_u16_be(put_i16_be(-2)) == 0xFFFE


def test_service_packet_parse() -> None:
    payload = b"\x01\x02\x03"
    packet = packet_build(3, 25, payload)
    assert packet == b"\x03\x19\x01\x02\x03"

    view = packet_parse(packet)
    assert view.service == 3
    assert view.subtype == 25
    assert view.payload == payload
    assert view.payload_len == len(payload)


def test_single_frame_roundtrip() -> None:
    packet = packet_build(20, 1, b"\x10")
    frames = fragment_packet(SpaceCanFrameClass.REQUEST, 1, packet)
    assert len(frames) == 1
    assert frames[0].id == 0x601
    assert frames[0].dlc == 4

    state = SpaceCanReassembly()
    status, out_packet = reassembly_accept(state, frames[0])
    assert status == SpaceCanStatus.OK
    assert out_packet == packet


def test_multi_frame_roundtrip() -> None:
    payload = bytes(range(1, 21))
    packet = packet_build(3, 25, payload)
    frames = fragment_packet(SpaceCanFrameClass.REPLY, 1, packet)

    assert len(frames) == 4
    assert frames[0].id == 0x581
    assert frames[0].dlc == 8
    assert frames[0].data[0] == 0x40
    assert frames[0].data[1] == len(packet)
    assert frames[1].data[0] == 0x81
    assert frames[2].data[0] == 0x82
    assert frames[3].data[0] == 0xC3

    state = SpaceCanReassembly()
    packet_out = None
    for index, frame in enumerate(frames):
        status, packet_out = reassembly_accept(state, frame)
        expected = SpaceCanStatus.OK if index + 1 == len(frames) else SpaceCanStatus.ERR_IN_PROGRESS
        assert status == expected
    assert packet_out == packet


def test_sequence_error_resets_reassembly() -> None:
    packet = packet_build(3, 25, bytes(range(1, 11)))
    frames = fragment_packet(SpaceCanFrameClass.REPLY, 1, packet)
    assert len(frames) == 2

    corrupted = can_frame_init(frames[1].id, bytes([0xC2]) + frames[1].data[1:], frames[1].dlc, False)
    state = SpaceCanReassembly()
    assert reassembly_accept(state, frames[0])[0] == SpaceCanStatus.ERR_IN_PROGRESS
    assert reassembly_accept(state, corrupted)[0] == SpaceCanStatus.ERR_SEQUENCE
    assert not state.active
