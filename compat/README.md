# AetherFlow ↔ LibreCube compatibility layer

Stage 4 creates a reproducible byte-level compatibility layer around the current C SpaceCAN codec.

The goal is to verify both directions without making the runtime bridge depend on Python:

```text
C → Python
Python → C
```

At this stage, the default Python harness validates the checked-in C-generated vectors with a dependency-free Python reference implementation of the current AetherFlow dialect. Optional LibreCube/python-spacecan probing is available, but non-fatal, until the exact upstream API adapter is implemented.

## AetherFlow SpaceCAN dialect v1

AetherFlow currently implements a compact SpaceCAN/CANopen-style dialect used by the local telemetry demo.

### CAN identifiers

```text
SYNC      0x080
REPLY     0x580 + node_id
REQUEST   0x600 + node_id
HEARTBEAT 0x700 + node_id
```

Node IDs are in the range `0..127`. The Stage 3 EPS node uses node `1`, so its reply frames use CAN ID `0x581`.

### Application packet

```text
byte 0      service
byte 1      subtype
byte 2..N   payload
```

Current demo service/subtype examples:

```text
service 3, subtype 25  housekeeping.report
service 20, subtype 1  parameter.get
```

### Fragmentation

Each CAN data frame starts with a one-byte fragmentation header:

```text
bits 7..6   fragment kind
bits 5..0   sequence number
```

Fragment kinds:

```text
0  SINGLE
1  FIRST
2  CONSECUTIVE
3  LAST
```

Single-frame packet:

```text
data[0]      SINGLE | sequence 0
data[1..]    packet bytes
```

Multi-frame packet:

```text
FIRST frame:
  data[0]    FIRST | sequence 0
  data[1]    total packet length
  data[2..7] first 6 packet bytes

CONSECUTIVE/LAST frames:
  data[0]    kind | sequence
  data[1..]  next packet bytes, up to 7 bytes
```

### EPS housekeeping payload

AetherFlow's EPS housekeeping payload is fixed-size, big-endian and currently project-specific:

```text
sequence              uint16
state                 uint8
bus_voltage_mv        uint16
bus_current_ma        int16
battery_percent       uint8
temperature_cdeg      int16
status_flags          uint8
```

Total payload length: `11` bytes.

### UDP envelope is not LibreCube SpaceCAN

The `AFC1` UDP envelope used by the local virtual CAN bus is an AetherFlow development transport. It is intentionally outside the SpaceCAN packet/fragmentation compatibility vectors.

## Compatibility matrix

| Feature | AetherFlow C | Python reference harness | LibreCube/python-spacecan status |
|---|---:|---:|---:|
| CAN ID ranges | yes | yes | to verify |
| service/subtype packet header | yes | yes | to verify |
| big-endian EPS payload | yes | yes | project-specific |
| fragmentation/reassembly | yes | yes | to verify |
| `AFC1` UDP envelope | yes | not required | not applicable |
| runtime dependency on Python | no | n/a | no |

## Files

```text
compat/
  README.md
  vectors/
    aetherflow_spacecan_vectors.json
  python/
    check_vectors.py

tools/
  generate_spacecan_vectors.c
```

## Usage

From the repository root:

```sh
make vectors
make compat
```

`make vectors` rebuilds `compat/vectors/aetherflow_spacecan_vectors.json` from the C codec.

`make compat` runs the dependency-free Python harness and validates:

1. **C → Python**: Python parses/reassembles C-generated frames and verifies semantic fields.
2. **Python → C**: Python independently re-encodes the same scenarios and compares exact packet/frame bytes against C output.

Optional backend probing:

```sh
python3 compat/python/check_vectors.py --backend librecube
```

This command currently reports whether a likely Python SpaceCAN package is importable. It does not fail the project if LibreCube/python-spacecan is not installed.

## Non-goals for Stage 4

- Do not make `bridge_service` depend on Python.
- Do not claim full LibreCube compliance before upstream vectors pass both ways.
- Do not block OpenMCT/dashboard work on full LibreCube integration.
- Do not treat the `AFC1` UDP envelope as part of LibreCube SpaceCAN compatibility.

## Next steps

1. Add a concrete adapter for the real `python-spacecan` API once the dependency/API shape is selected.
2. Add MicroPython-compatible checks for `micropython-spacecan` if its API differs.
3. Publish compatibility notes showing which layers match LibreCube exactly and which remain AetherFlow-specific.
