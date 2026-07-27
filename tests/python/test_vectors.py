from __future__ import annotations

import json
from pathlib import Path
from typing import Any, cast

from bridge_service.spacecan import SpaceCanFrameClass, SpaceCanReassembly, fragment_packet, packet_build, reassembly_accept

PROJECT_ROOT = Path(__file__).resolve().parents[2]
VECTOR_FILE = PROJECT_ROOT / "compat" / "vectors" / "aetherflow_spacecan_vectors.json"
FRAME_CLASSES = {
    "sync": SpaceCanFrameClass.SYNC,
    "heartbeat": SpaceCanFrameClass.HEARTBEAT,
    "request": SpaceCanFrameClass.REQUEST,
    "reply": SpaceCanFrameClass.REPLY,
}


def parse_can_id(value: str | int) -> int:
    if isinstance(value, int):
        return value
    return int(value, 16) if value.lower().startswith("0x") else int(value)


def load_vectors() -> list[dict[str, Any]]:
    with VECTOR_FILE.open("r", encoding="utf-8") as file:
        document = json.load(file)
    assert document["schema"] == "aetherflow.spacecan.vectors"
    return cast(list[dict[str, Any]], document["vectors"])


def test_golden_vectors_are_stable_against_python_spacecan() -> None:
    vectors = load_vectors()
    assert vectors

    for vector in vectors:
        payload = bytes.fromhex(vector["payload_hex"])
        expected_packet = bytes.fromhex(vector["packet_hex"])
        packet = packet_build(vector["service"], vector["subtype"], payload)
        assert packet == expected_packet, vector["name"]

        frame_class = FRAME_CLASSES[vector["frame_class"]]
        frames = fragment_packet(frame_class, vector["node_id"], packet)
        expected_frames = cast(list[dict[str, Any]], vector["frames"])
        assert len(frames) == len(expected_frames), vector["name"]

        for frame, expected in zip(frames, expected_frames, strict=True):
            assert frame.id == parse_can_id(expected["id"]), vector["name"]
            assert frame.dlc == expected["dlc"], vector["name"]
            assert frame.data.hex() == expected["data_hex"], vector["name"]

        state = SpaceCanReassembly()
        reassembled = None
        for frame in frames:
            _status, reassembled = reassembly_accept(state, frame)
        assert reassembled == expected_packet, vector["name"]
