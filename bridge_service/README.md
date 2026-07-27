# `bridge_service`

`bridge_service` is written on Python telemetry bridge

It acts as ground/OBC-side receiver:

```text
SocketCAN vcan0 -> CAN frame -> AetherFlow protocol reassembly -> EPS decode -> JSON -> HTTP/WebSocket
```

## Responsibilities

1. Open a Linux SocketCAN interface (`vcan0`).
2. Install kernel CAN filter for EPS reply frames from the configured node.
3. Receive standard 11-bit CAN frames.
4. Reassemble fragmented AetherFlow protocol packets.
5. Decode EPS housekeeping/critical telemetry.
6. Expose latest telemetry over HTTP and realtime telemetry over WebSocket.
7. Serve the built dashboard from `openmct/dist`.

## Entry point

```sh
python3 -m bridge_service
```

Environment:

```text
AETHERFLOW_HTTP_PORT=8080
AETHERFLOW_CAN_INTERFACE=vcan0
AETHERFLOW_EPS_NODE_ID=1
```

## HTTP/WebSocket API

```text
GET /health
GET /telemetry/latest
WebSocket /realtime
GET / static dashboard files
```

## Internal modules

```text
can_wire.py
  CAN frame dataclass and validation helpers.

aetherflow_can.py
  AetherFlow protocol CAN-ID helpers, packet build/parse, fragmentation and reassembly.

telemetry.py
  EPS telemetry decoder and JSON snapshot serialization.

transports/base.py
  Small transport protocol interface.

transports/socketcan.py
  Linux raw SocketCAN backend and CAN filters.

eps/
  EPS constants, payload schema and shared simulator model.
```

## Reassembly note

The bridge keeps reassembly state per source key:

```python
(frame_class, node_id) -> AetherflowCanReassembly
```

That prevents fragmented packets from multiple future nodes, such as EPS and ADCS, from corrupting each other.

## OBC/router TODO:

1. EPS reports `LOW_POWER`, `CRITICAL` or `SAFE`.
2. Bridge updates virtual power-channel policy, for example `payload_enabled=false`.
3. Non-essential payload commands/telemetry are blocked or marked disabled.
4. Dashboard sends operator commands and displays power-channel state.

EPS critical telemetry should remain allowed in every mode.
