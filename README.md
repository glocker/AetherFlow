# AetherFlow

AetherFlow is end-to-end CubeSat telemetry demo written around a transport independent SpaceCAN codec in C.

Goal: show how embedded-style telemetry can move through a protocol stack into a ground-segment dashboard:
![Project scheme](https://github.com/glocker/AetherFlow/blob/3d5ac8aa0df2bd974a6bb89b74e3de859b6f5f61/Scheme.png)

Current demo runs as separate local macOS processes. UDP multicast acts as a virtual CAN bus for development. Later transports can replace it with SocketCAN, STM32 HAL, Zephyr CAN or FreeRTOS vendor CAN while keeping the SpaceCAN codec and simulator logic unchanged.

## How it works

### `controller_simulator`

`controller_simulator` is a simple bus time source. It periodically publishes SpaceCAN/CAN `SYNC` frames:

```text
CAN ID: 0x080
DLC: 0
```

EPS nodes react to these ticks. This mirrors a minimal spacecraft bus heartbeat without adding command handling yet.

### `eps_simulator`

`eps_simulator` represents an EPS node with node id `1`.

It listens to the virtual CAN bus and feeds incoming frames into the existing EPS state machine:

```text
BOOT -> PRE_OPERATIONAL -> OPERATIONAL
```

After it reaches `OPERATIONAL`, every next `SYNC` produces deterministic housekeeping telemetry. That telemetry is packed as SpaceCAN service 3 subtype 25 and fragmented into CAN frames:

```text
CAN ID: 0x581
service: 3
subtype: 25
payload: EPS housekeeping bytes
```

Synthetic mode still exists for quick codec/state-machine checks without transport.

### UDP multicast virtual CAN bus

Transport layer connects local processes through UDP multicast:

```text
multicast group: 224.0.0.1
UDP port: 40700
```

Each UDP datagram contains one CAN frame encoded with `AFC1` wire envelope. This avoids sending raw C structs between processes, so padding, alignment and compiler layout do not become protocol details.

`can_frame_t` stays as internal boundary between protocol code and transport backend:

```text
SpaceCAN codec <-> can_frame_t <-> UDP transport
```

This is the main reason later work can swap UDP for SocketCAN or MCU CAN drivers without rewriting the SpaceCAN parser or EPS logic.

### `bridge_service`

`bridge_service` listens to the same multicast bus as a passive ground-side receiver.

It does four jobs:

1. receives CAN frames from UDP bus
2. reassembles fragmented SpaceCAN packets
3. parses housekeeping reports `service=3 subtype=25`
4. exposes decoded telemetry as JSON over HTTP and WebSocket

Available bridge outputs:

```text
GET /health
GET /telemetry/latest
WebSocket /realtime
GET / minimal live dashboard
```

The built-in dashboard is intentionally small and dependency-free. It proves the bridge boundary works before adding fuller Open MCT frontend integration.

### WebSocket handshake code

`bridge_service` includes small SHA-1 and Base64 helpers only for WebSocket upgrade handshake:

```text
Sec-WebSocket-Accept = base64(sha1(client_key + RFC6455_GUID))
```

Constants like `0x67452301u` are standard SHA-1 algorithm constants. `258EAFA5-E914-47DA-95CA-C5AB0DC85B11` is fixed WebSocket RFC 6455 GUID. They are public protocol constants.

## Protocol pieces

### SpaceCAN codec

Implemented as transport-independent C code:

- builds application packets from `service`, `subtype` and payload bytes
- parses SpaceCAN packets back into views
- calculates CAN arbitration IDs
- fragments packets into standard 8-byte CAN frames
- reassembles single-frame and multi-frame packets
- uses big-endian integer helpers for C ↔ Python compatibility checks

Important CAN IDs:

```text
SYNC      0x080
REQUEST   0x600 + node_id
REPLY     0x580 + node_id
HEARTBEAT 0x700 + node_id
```

For EPS node `1`:

```text
REPLY 0x581
REQUEST 0x601
HEARTBEAT 0x701
```

### EPS housekeeping payload

Current EPS telemetry payload is fixed-size and big-endian:

```text
sequence              uint16
state                 uint8
bus_voltage_mv        uint16
bus_current_ma        int16
battery_percent       uint8
temperature_cdeg      int16
status_flags          uint8
```

This fixed layout is useful for future compatibility vectors against Python/LibreCube tooling.

### `AFC1` CAN frame envelope

UDP transport uses explicit wire encoding:

```text
magic[4]   = AFC1
version[1]
flags[1]
id[4]      = big-endian CAN arbitration ID
dlc[1]
data[8]
```

This keeps transport packets stable even if C compiler, platform or struct layout changes.

## Current architecture

```text
include/
  can_frame.h             CAN frame model
  can_frame_wire.h        AFC1 wire envelope API
  eps_simulator.h         EPS node API
  spacecan.h              SpaceCAN codec API
  spacecan_services.h     service/subtype constants
  transport.h             UDP transport API and defaults

src/
  controller_simulator_main.c
  eps_simulator_main.c
  bridge_service_main.c
  can_frame_wire.c
  eps_simulator.c
  spacecan_*.c

transport/
  udp_transport.c         current macOS local virtual CAN bus
  memory_transport.c      reserved future backend
  socketcan_transport.c   reserved Linux SocketCAN backend

tests/
  test_spacecan_codec.c
  test_eps_simulator.c
```

## Build and run

Run full local demo:

```sh
make demo
```

This builds all needed backend services (bridge, EPS simulator, controller simulator), installs OpenMCT dashboard dependencies and runs dashboard dev server, then prints dashboard URL:

```text
http://127.0.0.1:5173/
```

Logs are written to `logs/`. Press `Ctrl+C` in the `make demo` terminal to stop all demo processes.

Useful environment overrides:

```sh
AETHERFLOW_CONTROLLER_RATE_HZ=10 make demo
AETHERFLOW_BRIDGE_URL=http://127.0.0.1:8080 make demo
AETHERFLOW_DASHBOARD_URL=http://127.0.0.1:5173 make demo
```

Build everything and run tests:

```sh
make all
```

Build only backend service binaries:

```sh
make backend
```

Run OpenMCT based dashboard separately:

```sh
make dashboard-dev
```

Build or preview dashboard production bundle:

```sh
make dashboard-build
make dashboard-preview
```

The dashboard connects to `http://127.0.0.1:8080` by default. To point it at another bridge endpoint, pass `?bridge=http://host:port` in the dashboard URL.

### Docker demo

Build and run the complete Linux demo in Docker:

```sh
docker compose up --build
```

The Docker image is based on `debian:bookworm-slim`. During image build it runs the same verification/build pipeline as local Linux:

```sh
make clean
make test
make backend
make compat
npm ci --prefix openmct
make dashboard-build
```

After the container starts, open:

```text
http://127.0.0.1:5173/
```

Bridge API is exposed on:

```text
http://127.0.0.1:8080/health
http://127.0.0.1:8080/telemetry/latest
```

Stop the demo with `Ctrl+C`, or from another terminal:

```sh
docker compose down
```

### Manual mode for debugging

Use this when you want each service in its own terminal:

```sh
make backend
./bridge_service
./eps_simulator
./controller_simulator 5
```

Check bridge output:

```sh
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/telemetry/latest
open http://127.0.0.1:8080/
```

Synthetic EPS mode without UDP transport:

```sh
make eps_simulator
./eps_simulator 5
```

Run codec tests directly:

```sh
make tests/test_spacecan_codec
./tests/test_spacecan_codec
```

Run current test suite:

```sh
make test
```

Generate and validate Stage 4 compatibility vectors:

```sh
make vectors
make compat
```

`make compat` validates the current C-generated SpaceCAN vectors with a dependency-free Python reference harness. This checks both directions at byte level:

```text
C → Python: parse/reassemble C-generated packets and frames
Python → C: independently re-encode the same scenarios and compare exact bytes
```

Details are documented in `compat/README.md`.

## Development notes

- UDP multicast transport is tuned for local macOS demo runs
- `SO_REUSEPORT` is used so multiple local processes can observe same multicast port
- RX and TX UDP sockets are separate to avoid macOS multicast send/receive edge cases found during smoke tests
- hardcoded IP/port values are just public demo defaults
- WebSocket SHA-1 code exists only for RFC 6455 handshake, not for cryptographic security
- Open MCT integration can sit behind existing `bridge_service` HTTP/WebSocket boundary

## Roadmap

Implemented foundations:

- Stage 4 compatibility vectors and Python harness: `C -> Python`, `Python -> C`

Not implemented yet:

- concrete adapter for upstream `python-spacecan` / `micropython-spacecan` API
- Linux VM SocketCAN validation: `vcan0`, arbitration IDs, `candump`, packet loss, timeouts, multiple nodes
- MCU/RTOS transports: `stm32_can_transport.c`, `zephyr_can_transport.c`, `freertos_vendor_can_transport.c`
