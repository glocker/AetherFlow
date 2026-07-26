# AetherFlow

AetherFlow is an end-to-end CubeSat EPS telemetry demo for Linux/SocketCAN.

Goal: show how embedded-style EPS telemetry can move through a CAN/SpaceCAN protocol stack into an Open MCT-style ground dashboard.

![Project scheme](https://github.com/glocker/AetherFlow/blob/3d5ac8aa0df2bd974a6bb89b74e3de859b6f5f61/Scheme.png)

## Current runtime

The active runtime is Linux-only and uses SocketCAN with `vcan0` for local development:

```text
Python EPS emulator -> SocketCAN vcan0 -> Python bridge_service -> HTTP/WebSocket -> Open MCT dashboard
```

The remaining C code is kept only for SpaceCAN codec/EPS compatibility tests and vector generation.

## Components

### `eps_emulator`

`eps_emulator` is a Python EPS node simulator with SpaceCAN node id `1` by default.

It sends telemetry periodically without requiring a separate controller/SYNC process:

- critical EPS telemetry every `200 ms` by default;
- housekeeping telemetry every `1 s` by default;
- both are sent as SpaceCAN housekeeping packets fragmented into standard 8-byte CAN frames.

The simulator contains a small dynamic power model:

- sunlight/eclipse cycle;
- solar panel current generation;
- battery charge/discharge;
- payload load shedding in low-power modes;
- battery temperature drift;
- simple fault injection.

### `bridge_service`

`bridge_service` receives CAN frames from SocketCAN, reassembles SpaceCAN packets, decodes EPS telemetry and exposes it to the dashboard.

Available bridge outputs:

```text
GET /health
GET /telemetry/latest
WebSocket /realtime
GET / dashboard static files from openmct/dist
```

The bridge configures a SocketCAN kernel filter for EPS reply frames from the configured node. This avoids waking the process for unrelated CAN traffic.

Current bridge role is still telemetry receiver/ground bridge. It does not yet act as an OBC router or command authority.

### Open MCT dashboard

The frontend in `openmct/` consumes bridge JSON from `/telemetry/latest` and `/realtime`.

It displays:

- connection state;
- packet age/rate/gaps;
- EPS state and power mode;
- bus voltage/current/power;
- battery SOC/voltage/current;
- solar current;
- temperature;
- status/fault flags;
- raw JSON/history export.

## SocketCAN setup on Ubuntu

Create a virtual CAN interface:

```sh
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

Check it:

```sh
ip link show vcan0
```

Optional debugging tools:

```sh
sudo apt-get update
sudo apt-get install -y can-utils
candump vcan0
```

## Build and run

Install dashboard dependencies once:

```sh
make dashboard-install
```

Run tests:

```sh
make test
make compat
```

Build dashboard and run the local demo:

```sh
make demo
```

Manual mode:

```sh
make dashboard-build
python3 -m bridge_service
python3 -m eps_emulator
```

Open:

```text
http://127.0.0.1:8080/
```

## Runtime configuration

Defaults are loaded from `aetherflow.env`:

```text
AETHERFLOW_HTTP_PORT=8080
AETHERFLOW_CAN_INTERFACE=vcan0
AETHERFLOW_EPS_NODE_ID=1
AETHERFLOW_LOG_DIR=logs
```

You can override them per command:

```sh
AETHERFLOW_CAN_INTERFACE=vcan1 python3 -m bridge_service
python3 -m eps_emulator --interface vcan1 --critical-interval 0.1 --housekeeping-interval 1.0
```

## Fault injection

`eps_emulator` opens a local TCP command socket by default:

```text
127.0.0.1:40710
```

Examples:

```sh
printf '%s\n' '{"fault":"panel_short","enabled":true}' | nc 127.0.0.1 40710
printf '%s\n' '{"fault":"battery_degradation","level":0.35}' | nc 127.0.0.1 40710
printf '%s\n' '{"fault":"overcurrent","enabled":true}' | nc 127.0.0.1 40710
printf '%s\n' '{"fault":"clear"}' | nc 127.0.0.1 40710
```

Fault effects are visible in telemetry fields such as `solar_current_ma`, `battery_current_ma`, `power_mode`, `status_flags` and `fault_flags`.

## Protocol pieces

### SpaceCAN CAN IDs

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

Current bridge listens to `0x581` by default.

### EPS housekeeping payload v2

Current EPS telemetry payload is fixed-size and big-endian:

```text
sequence              uint16
state                 uint8
power_mode            uint8
bus_voltage_mv        uint16
bus_current_ma        int16
battery_percent       uint8
battery_voltage_mv    uint16
battery_current_ma    int16
solar_current_ma      uint16
temperature_cdeg      int16
status_flags          uint16
```

Status/fault flags:

```text
0x0001 SAFE_MODE
0x0002 LOW_BATTERY
0x0004 OVERTEMP
0x0008 PANEL_FAULT
0x0010 BATTERY_DEGRADED
0x0020 OVERCURRENT
0x0040 PAYLOAD_SHED
```

Power modes:

```text
NOMINAL
LOW_POWER
CRITICAL
SAFE
```

The EPS model uses hysteresis around SOC thresholds so the mode does not chatter near a boundary.

### SpaceCAN packet format

Application packets still use:

```text
service uint8
subtype uint8
payload bytes
```

Telemetry is fragmented/reassembled over standard CAN frames using the existing SpaceCAN codec.

Current EPS telemetry subtypes:

```text
service=3 subtype=25 HOUSEKEEPING_REPORT
service=3 subtype=26 CRITICAL_REPORT
```

## Current architecture

```text
bridge_service/
  server.py                  HTTP/WebSocket bridge orchestration
  can_wire.py                CAN frame model and AFC1 compatibility envelope
  spacecan.py                SpaceCAN ID, packet, fragmentation/reassembly
  telemetry.py               EPS telemetry decoding and JSON snapshots
  config.py                  environment configuration
  transports/
    base.py                  transport protocol interface
    socketcan.py             Linux SocketCAN backend and CAN filters
  eps/
    constants.py             EPS service/subtype/flag constants
    schema.py                EPS payload dataclasses and binary codec
    simulator.py             dynamic EPS model used by eps_emulator

eps_emulator/
  __main__.py                Python EPS emulator runtime

openmct/
  src/                       dashboard frontend

src/, include/, tests/, compat/
  C SpaceCAN codec, compatibility vectors and regression tests
```

## Future OBC/load-shedding idea

Not implemented yet.

The bridge can later grow from a passive telemetry bridge into a small OBC/router policy node:

1. EPS reports `LOW_POWER`, `CRITICAL` or `SAFE`.
2. OBC policy changes virtual power-channel state, for example `payload_enabled=false`.
3. Bridge stops forwarding payload commands or marks payload telemetry as disabled.
4. Dashboard exposes a control button and state indication.

For demo purposes, the dashboard button could send a command to the bridge, and the bridge could publish an OBC command frame or update an internal routing policy. EPS critical telemetry should always remain allowed; only non-essential payload traffic should be shed.
