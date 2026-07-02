#!/usr/bin/env python3
# pyright: reportAny=false, reportUnknownArgumentType=false, reportUnknownMemberType=false, reportUnknownVariableType=false, reportUnusedCallResult=false
"""Validate AetherFlow SpaceCAN compatibility vectors.

Default mode uses a small pure-Python reference implementation of the current
AetherFlow SpaceCAN dialect. This intentionally has no third-party dependency,
so compatibility checks are reproducible on a fresh checkout.

Optional LibreCube/python-spacecan probing is kept non-fatal until the concrete
upstream API is wired in. Use --backend librecube to report whether an optional
SpaceCAN Python package is importable in the current environment.
"""

from __future__ import annotations

import argparse
import importlib
import json
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

SINGLE = 0
FIRST = 1
CONSECUTIVE = 2
LAST = 3
FRAGMENT_KIND_SHIFT = 6
FRAGMENT_SEQ_MASK = 0x3F
SINGLE_PAYLOAD_CAPACITY = 7
FIRST_PAYLOAD_CAPACITY = 6
CONT_PAYLOAD_CAPACITY = 7
PACKET_MAX_SIZE = 255

# CAN bases mirror C helper values and lock dialect boundary
FRAME_CLASS_TO_CAN_BASE = {
    "sync": 0x080,
    "heartbeat": 0x700,
    "request": 0x600,
    "reply": 0x580,
}


@dataclass(frozen=True)
class Frame:
    can_id: int
    dlc: int
    data: bytes


def parse_can_id(value: str | int) -> int:
    if isinstance(value, int):
        return value
    return int(value, 16) if value.lower().startswith("0x") else int(value, 10)


def hex_to_bytes(value: str) -> bytes:
    if len(value) % 2 != 0:
        raise ValueError(f"hex string has odd length: {value!r}")
    return bytes.fromhex(value)


def make_can_id(frame_class: str, node_id: int) -> int:
    if frame_class == "sync":
        return FRAME_CLASS_TO_CAN_BASE[frame_class]
    if not 0 <= node_id <= 127:
        raise ValueError(f"node_id out of range: {node_id}")
    return FRAME_CLASS_TO_CAN_BASE[frame_class] + node_id


def fragment_header(kind: int, sequence: int) -> int:
    # Upper bits carry kind while lower bits carry sequence
    return (kind << FRAGMENT_KIND_SHIFT) | (sequence & FRAGMENT_SEQ_MASK)


def build_packet(service: int, subtype: int, payload: bytes) -> bytes:
    packet = bytes([service, subtype]) + payload
    if len(packet) > PACKET_MAX_SIZE:
        raise ValueError(f"packet too large: {len(packet)}")
    return packet


def fragment_packet(frame_class: str, node_id: int, packet: bytes) -> list[Frame]:
    if not packet or len(packet) > PACKET_MAX_SIZE:
        raise ValueError(f"invalid packet length: {len(packet)}")

    can_id = make_can_id(frame_class, node_id)
    if len(packet) <= SINGLE_PAYLOAD_CAPACITY:
        # Single-frame path has no total length byte in AetherFlow dialect
        data = bytes([fragment_header(SINGLE, 0)]) + packet
        return [Frame(can_id=can_id, dlc=len(data), data=data)]

    frames: list[Frame] = []
    sequence = 0
    # FIRST frame stores total packet length before first payload chunk
    first_data = (
        bytes([fragment_header(FIRST, sequence), len(packet)])
        + packet[:FIRST_PAYLOAD_CAPACITY]
    )
    frames.append(Frame(can_id=can_id, dlc=len(first_data), data=first_data))
    sequence += 1
    offset = FIRST_PAYLOAD_CAPACITY

    while offset < len(packet):
        remaining = len(packet) - offset
        chunk_len = min(remaining, CONT_PAYLOAD_CAPACITY)
        kind = LAST if offset + chunk_len == len(packet) else CONSECUTIVE
        data = (
            bytes([fragment_header(kind, sequence)])
            + packet[offset : offset + chunk_len]
        )
        frames.append(Frame(can_id=can_id, dlc=len(data), data=data))
        sequence += 1
        offset += chunk_len

    return frames


def reassemble_frames(frames: list[Frame]) -> bytes:
    if not frames:
        raise ValueError("no frames to reassemble")

    first = frames[0]
    first_kind = first.data[0] >> FRAGMENT_KIND_SHIFT
    first_sequence = first.data[0] & FRAGMENT_SEQ_MASK

    if first_kind == SINGLE:
        if len(frames) != 1:
            raise ValueError("single-frame packet has extra frames")
        if (
            first_sequence != 0
            or first.dlc < 2
            or first.dlc > SINGLE_PAYLOAD_CAPACITY + 1
        ):
            raise ValueError("invalid single-frame header")
        return first.data[1 : first.dlc]

    if first_kind != FIRST:
        raise ValueError("multi-frame packet does not start with FIRST")
    if first_sequence != 0 or first.dlc != 8:
        raise ValueError("invalid FIRST frame")

    expected_total_len = first.data[1]
    # Reassembly mirrors C state machine while keeping JSON checks dependency free
    packet = bytearray(first.data[2 : first.dlc])
    expected_sequence = 1

    for index, frame in enumerate(frames[1:], start=1):
        kind = frame.data[0] >> FRAGMENT_KIND_SHIFT
        sequence = frame.data[0] & FRAGMENT_SEQ_MASK
        if sequence != expected_sequence:
            raise ValueError(
                f"frame {index} has sequence {sequence}, expected {expected_sequence}"
            )
        if kind not in (CONSECUTIVE, LAST):
            raise ValueError(f"frame {index} has invalid kind {kind}")
        if frame.dlc < 2 or frame.dlc > CONT_PAYLOAD_CAPACITY + 1:
            raise ValueError(f"frame {index} has invalid dlc {frame.dlc}")
        chunk = frame.data[1 : frame.dlc]
        if kind == CONSECUTIVE and len(chunk) != CONT_PAYLOAD_CAPACITY:
            raise ValueError(f"frame {index} consecutive chunk is not full-length")
        packet.extend(chunk)
        expected_sequence = (expected_sequence + 1) & FRAGMENT_SEQ_MASK
        if kind == LAST:
            if index != len(frames) - 1:
                raise ValueError("LAST frame is not the final frame")
            break
    else:
        raise ValueError("multi-frame packet has no LAST frame")

    if len(packet) != expected_total_len:
        raise ValueError(
            f"reassembled length {len(packet)} != expected {expected_total_len}"
        )
    return bytes(packet)


def get_mapping(value: object, field_name: str) -> Mapping[str, object]:
    # Strict JSON shape checks catch broken fixtures before byte comparison
    if not isinstance(value, Mapping):
        raise ValueError(f"{field_name} must be an object")
    return value


def get_sequence(value: object, field_name: str) -> Sequence[object]:
    if not isinstance(value, Sequence) or isinstance(value, (str, bytes, bytearray)):
        raise ValueError(f"{field_name} must be an array")
    return value


def get_str(mapping: Mapping[str, object], field_name: str) -> str:
    value = mapping[field_name]
    if not isinstance(value, str):
        raise ValueError(f"{field_name} must be a string")
    return value


def get_int(mapping: Mapping[str, object], field_name: str) -> int:
    value = mapping[field_name]
    if not isinstance(value, int):
        raise ValueError(f"{field_name} must be an integer")
    return value


def load_vector_frames(vector: Mapping[str, object]) -> list[Frame]:
    name = get_str(vector, "name")
    frames: list[Frame] = []
    for frame_value in get_sequence(vector["frames"], "frames"):
        frame = get_mapping(frame_value, "frame")
        data = hex_to_bytes(get_str(frame, "data_hex"))
        dlc = get_int(frame, "dlc")
        if dlc != len(data):
            raise ValueError(f"{name}: frame dlc {dlc} != data length {len(data)}")
        frames.append(
            Frame(can_id=parse_can_id(get_str(frame, "id")), dlc=dlc, data=data)
        )
    return frames


def validate_vector(vector: Mapping[str, object]) -> list[str]:
    errors: list[str] = []
    name = get_str(vector, "name")
    payload = hex_to_bytes(get_str(vector, "payload_hex"))
    expected_packet = hex_to_bytes(get_str(vector, "packet_hex"))
    expected_can_id = parse_can_id(get_str(vector, "can_id"))
    frames = load_vector_frames(vector)
    service = get_int(vector, "service")
    subtype = get_int(vector, "subtype")
    frame_class = get_str(vector, "frame_class")
    node_id = get_int(vector, "node_id")

    # C → Python path parses C-generated bytes and checks semantic fields
    try:
        reassembled = reassemble_frames(frames)
        if reassembled != expected_packet:
            errors.append(
                f"{name}: reassembled packet {reassembled.hex()} != packet_hex {expected_packet.hex()}"
            )
        if len(expected_packet) < 2:
            errors.append(f"{name}: packet is too short")
        else:
            if expected_packet[0] != service:
                errors.append(f"{name}: service mismatch")
            if expected_packet[1] != subtype:
                errors.append(f"{name}: subtype mismatch")
            if expected_packet[2:] != payload:
                errors.append(f"{name}: packet payload does not match payload_hex")
    except ValueError as exc:
        errors.append(f"{name}: C→Python parse failed: {exc}")

    # Python → C path independently encodes reference bytes and compares exact output
    try:
        encoded_packet = build_packet(service, subtype, payload)
        if encoded_packet != expected_packet:
            errors.append(
                f"{name}: Python packet {encoded_packet.hex()} != C packet {expected_packet.hex()}"
            )
        encoded_frames = fragment_packet(frame_class, node_id, encoded_packet)
        if make_can_id(frame_class, node_id) != expected_can_id:
            errors.append(f"{name}: Python CAN ID mismatch")
        if encoded_frames != frames:
            expected = [(frame.can_id, frame.dlc, frame.data.hex()) for frame in frames]
            actual = [
                (frame.can_id, frame.dlc, frame.data.hex()) for frame in encoded_frames
            ]
            errors.append(f"{name}: Python frames {actual} != C frames {expected}")
    except ValueError as exc:
        errors.append(f"{name}: Python→C encode failed: {exc}")

    return errors


def probe_optional_librecube_backend() -> str:
    # Optional probe keeps LibreCube dependency outside normal local workflow
    candidates = (
        "spacecan",
        "librecube.spacecan",
        "micropython_spacecan",
    )
    for module_name in candidates:
        try:
            importlib.import_module(module_name)
        except ImportError:
            continue
        return f"optional backend importable: {module_name}; API adapter not wired yet"
    return "optional LibreCube/python-spacecan backend not installed; skipped"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "vectors",
        nargs="?",
        default=Path("compat/vectors/aetherflow_spacecan_vectors.json"),
        type=Path,
        help="Path to generated compatibility vector JSON.",
    )
    parser.add_argument(
        "--backend",
        choices=("reference", "librecube"),
        default="reference",
        help="Validation backend. 'reference' has no dependencies; 'librecube' currently probes optional imports.",
    )
    args = parser.parse_args(argv)

    with args.vectors.open("r", encoding="utf-8") as file:
        raw_document = json.load(file)

    document = get_mapping(raw_document, "document")
    schema = document.get("schema")
    if schema != "aetherflow.spacecan.vectors.v1":
        print(f"unsupported vector schema: {schema}", file=sys.stderr)
        return 2

    all_errors: list[str] = []
    vectors = get_sequence(document["vectors"], "vectors")
    for vector_value in vectors:
        all_errors.extend(validate_vector(get_mapping(vector_value, "vector")))

    if args.backend == "librecube":
        print(probe_optional_librecube_backend())

    if all_errors:
        for error in all_errors:
            print(error, file=sys.stderr)
        return 1

    print(f"validated {len(vectors)} AetherFlow SpaceCAN vector(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
