# AetherFlow

AetherFlow is a CubeSat telemetry project that emulates spacecraft subsystems, transports their telemetry over a custom CAN protocol, and visualizes it in an Open MCT ground dashboard.

Goal: show how emulated CubeSat telemetry moves through сustom AetherFlow CAN protocol into Open MCT ground dashboard.

```text
eps_emulator -> SocketCAN vcan0 -> bridge_service -> HTTP/WebSocket -> Open MCT dashboard
```

![Project scheme](https://github.com/glocker/AetherFlow/blob/3d5ac8aa0df2bd974a6bb89b74e3de859b6f5f61/Scheme.png)

## Runtime components

- `aetherflow/` — App launcher that starts and monitors backend child processes.
- `eps_emulator/` — EPS emulator with dynamic battery/solar model and fault injection.
- `bridge_service/` — SocketCAN bridge, AetherFlow CAN Protocol reassembly, EPS telemetry decoder, HTTP/WebSocket API.
- `openmct/` — dashboard frontend.
- `compat/` — committed AetherFlow CAN Protocol golden vectors.

Detailed subsystem documentation:

- [`bridge_service/README.md`](bridge_service/README.md)
- [`eps_emulator/README.md`](eps_emulator/README.md)
- [`compat/README.md`](compat/README.md)

## Requirements

Linux with SocketCAN support:

```sh
sudo apt-get update
sudo apt-get install -y iproute2 python3 python3-venv nodejs npm
```

Optional CAN debugging tools:

```sh
sudo apt-get install -y can-utils
```

## Python virtual environment

## Setup `vcan0`

```sh
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

Check:

```sh
ip link show vcan0
```

## Start the full application:

```sh
python -m aetherflow
```

App launcher starts:

```text
python -m bridge_service
python -m eps_emulator
```

Open:

```text
http://127.0.0.1:8080/
```
