# AetherFlow
Demo to transfer data from CubeSat to OpenMCT based dashboard

<h3>Summary</h3>
## CubeSat simulator
Only sends data in SpaceCAN format
(telemetry test parameters generator or real data)
## Bridge service
Converts SpaceCAN data to JSON
## OpenMCT based dashboard
Represents data on dashboard

### Stage 1: SpaceCAN codec

Implemented as a transport-independent SpaceCAN codec in C.

- builds application packets from `service`, `subtype` and payload bytes
- parses SpaceCAN packets back into service/subtype/payload views
- calculates CAN arbitration IDs for `SYNC`, `REQUEST`, `REPLY` and `HEARTBEAT`
- fragments packets into standard 8-byte CAN frames
- reassembles single-frame and multi-frame SpaceCAN packets
- uses big-endian integer helpers for future C ↔ Python compatibility checks

Important IDs used by the codec:

- `SYNC`: `0x080`
- `REQUEST` from node 1: `0x601`
- `REPLY` from node 1: `0x581`
- `HEARTBEAT` from node 1: `0x701`

Run codec tests:

```sh
make tests/test_spacecan_codec
./tests/test_spacecan_codec
```

Or run the whole current test suite:

```sh
make test
```

### Stage 2: EPS simulator

Implemented as `eps_simulator` on top of the Stage 1 SpaceCAN codec.

- node id: `1`
- states: `BOOT`, `PRE_OPERATIONAL`, `OPERATIONAL`, `SAFE`
- input: SpaceCAN `SYNC` frame (`0x080`)
- output: SpaceCAN `REP` frames from node 1 (`0x581`)
- telemetry packet: Service 3 housekeeping report (`service=3`, `subtype=25`)

Synthetic/debug mode without a real transport is still available:

```sh
make eps_simulator
./eps_simulator 5
```

The first `SYNC` moves EPS from `BOOT` to `PRE_OPERATIONAL`; the second and later `SYNC` events generate deterministic housekeeping measurements and fragment the report into SpaceCAN CAN frames.

### Stage 3: UDP multicast virtual CAN bus + C bridge service

Implemented as separate macOS processes connected through a UDP multicast virtual CAN bus.

- multicast group: `224.0.0.1`
- UDP port: `40700`
- wire format: stable `AFC1` CAN frame envelope, not raw C struct memory
- `controller_simulator` publishes SpaceCAN/CAN `SYNC` frames (`0x080`)
- `eps_simulator` listens for `SYNC` and publishes fragmented housekeeping `REPLY` frames (`0x581`)
- `bridge_service` listens to the same bus, reassembles SpaceCAN packets, decodes EPS housekeeping telemetry and serves HTTP/WebSocket from C

Build Stage 3:

```sh
make stage3
```

Run in separate terminals:

```sh
./bridge_service
./eps_simulator
./controller_simulator
```

Optional controller rate argument:

```sh
./controller_simulator 5
```

Bridge endpoints:

```sh
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/telemetry/latest
open http://127.0.0.1:8080/
```

The root page is a minimal live dashboard backed by WebSocket `/realtime`. It is intentionally small and dependency-free; the same C bridge-service boundary can later back a fuller Open MCT frontend.

## Roadmap

Not implemented yet:

Stage 4: LibreCube compatibility vectors (`C → Python`, `Python → C`).
Stage 5: Linux VM SocketCAN validation (`vcan0`, arbitration IDs, `candump`, packet loss, timeouts, multiple nodes).
Stage 6: MCU/RTOS transports (`stm32_can_transport.c`, `zephyr_can_transport.c`, `freertos_vendor_can_transport.c`).
