from __future__ import annotations

import json
from pathlib import Path
from typing import Any, cast

from bridge_service.aetherflow_can import AetherflowCanFrameClass, AetherflowCanReassembly, fragment_packet, packet_build, reassembly_accept

PROJECT_ROOT = Path(__file__).resolve().parents[2]
VECTOR_FILE = PROJECT_ROOT / "compat" / "vectors" / "aetherflow_can_vectors.json"
FRAME_CLASSES = {
    "sync": AetherflowCanFrameClass.SYNC,
    "heartbeat": AetherflowCanFrameClass.HEARTBEAT,
    "request": AetherflowCanFrameClass.REQUEST,
    "reply": AetherflowCanFrameClass.REPLY,
}


def parse_can_id(value: str | int) -> int:
    if isinstance(value, int):
        return value
    return int(value, 16) if value.lower().startswith("0x") else int(value)


def load_vectors() -> list[dict[str, Any]]:
    with VECTOR_FILE.open("r", encoding="utf-8") as file:
        document = json.load(file)
    assert document["schema"] == "aetherflow.can_protocol.vectors"
    return cast(list[dict[str, Any]], document["vectors"])


def test_golden_vectors_are_stable_against_python_aetherflow_can() -> None:
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

        state = AetherflowCanReassembly()
        reassembled = None
        for frame in frames:
            _status, reassembled = reassembly_accept(state, frame)
        assert reassembled == expected_packet, vector["name"]
