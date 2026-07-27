"""Run the dynamic EPS emulator on Linux SocketCAN/vcan."""

from __future__ import annotations

import argparse
import json
import selectors
import signal
import socket
import sys
import time
from dataclasses import asdict
from typing import cast

from bridge_service.config import CAN_INTERFACE, EPS_NODE_ID
from bridge_service.eps import EpsPhysicalModel, EpsSimulator, power_mode_name, state_name
from bridge_service.eps.constants import AETHERFLOWCAN_HK_SUBTYPE_CRITICAL_REPORT, AETHERFLOWCAN_HK_SUBTYPE_REPORT
from bridge_service.transports import open_socketcan_transport

DEFAULT_CRITICAL_INTERVAL_S = 0.2
DEFAULT_HOUSEKEEPING_INTERVAL_S = 1.0
DEFAULT_COMMAND_HOST = "127.0.0.1"
DEFAULT_COMMAND_PORT = 40710

_keep_running = True


def handle_signal(_signal_number: int, _frame: object) -> None:
    global _keep_running
    _keep_running = False


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="AetherFlow EPS emulator for SocketCAN/vcan")
    parser.add_argument("--interface", default=CAN_INTERFACE, help="SocketCAN interface, default: %(default)s")
    parser.add_argument("--node-id", type=int, default=EPS_NODE_ID, help="AetherFlow protocol EPS node id")
    parser.add_argument("--critical-interval", type=float, default=DEFAULT_CRITICAL_INTERVAL_S, help="critical telemetry period in seconds")
    parser.add_argument("--housekeeping-interval", type=float, default=DEFAULT_HOUSEKEEPING_INTERVAL_S, help="housekeeping telemetry period in seconds")
    parser.add_argument("--command-host", default=DEFAULT_COMMAND_HOST, help="fault command TCP bind host")
    parser.add_argument("--command-port", type=int, default=DEFAULT_COMMAND_PORT, help="fault command TCP port")
    parser.add_argument("--no-command-socket", action="store_true", help="disable local fault command socket")
    return parser


def open_command_server(host: str, port: int) -> socket.socket:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((host, port))
        server.listen(4)
        server.setblocking(False)
        return server
    except OSError:
        server.close()
        raise


def accept_command_client(server: socket.socket, selector: selectors.BaseSelector) -> None:
    try:
        client, _ = server.accept()
        client.setblocking(False)
        selector.register(client, selectors.EVENT_READ, "command-client")
        client.sendall(b"AetherFlow EPS fault socket ready. Send JSON lines.\n")
    except OSError:
        return


def apply_command(eps: EpsSimulator, raw_command: bytes) -> str:
    text = raw_command.decode("utf-8", errors="replace").strip()
    if not text:
        return "empty command ignored"
    try:
        command = json.loads(text)
    except json.JSONDecodeError as error:
        return f"invalid JSON: {error.msg}"

    action = command.get("action") or command.get("fault")
    if action == "clear" or action == "clear_all":
        eps.faults.clear()
        return "faults cleared"
    if action == "panel_short":
        eps.faults.panel_short = bool(command.get("enabled", True))
        return f"panel_short={eps.faults.panel_short}"
    if action == "battery_degradation":
        level = float(command.get("level", 0.25))
        eps.faults.battery_degradation = max(0.0, min(0.8, level))
        return f"battery_degradation={eps.faults.battery_degradation:.2f}"
    if action == "overcurrent":
        eps.faults.overcurrent = bool(command.get("enabled", True))
        return f"overcurrent={eps.faults.overcurrent}"
    if action == "status":
        return json.dumps(asdict(eps.faults), separators=(",", ":"))
    return f"unknown action: {action}"


def handle_command_client(client: socket.socket, selector: selectors.BaseSelector, eps: EpsSimulator) -> None:
    try:
        data = client.recv(4096)
    except OSError:
        data = b""
    if not data:
        try:
            selector.unregister(client)
        except Exception:
            pass
        client.close()
        return
    response = apply_command(eps, data)
    try:
        client.sendall((response + "\n").encode("utf-8"))
    except OSError:
        pass
    print(f"eps_emulator: command {response}", flush=True)


def send_frames(transport, frames) -> None:
    for frame in frames:
        transport.send(frame)


def main(argv: list[str] | None = None) -> int:
    global _keep_running
    args = build_arg_parser().parse_args(argv)

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    try:
        transport = open_socketcan_transport(args.interface, filters=None)
    except OSError as error:
        print(f"eps_emulator: failed to open SocketCAN interface {args.interface}: {error}", file=sys.stderr)
        return 1

    selector = selectors.DefaultSelector()
    command_server: socket.socket | None = None
    if not args.no_command_socket:
        try:
            command_server = open_command_server(args.command_host, args.command_port)
            selector.register(command_server, selectors.EVENT_READ, "command-server")
        except OSError as error:
            transport.close()
            print(f"eps_emulator: failed to open command socket {args.command_host}:{args.command_port}: {error}", file=sys.stderr)
            return 1

    eps = EpsSimulator(node_id=args.node_id, model=EpsPhysicalModel())
    start = time.monotonic()
    last_update = start
    next_critical = start
    next_housekeeping = start

    print(
        "eps_emulator: "
        f"node={args.node_id} SocketCAN={args.interface} "
        f"critical={args.critical_interval:.3f}s housekeeping={args.housekeeping_interval:.3f}s",
        flush=True,
    )
    if command_server is not None:
        print(f"eps_emulator: fault command socket tcp://{args.command_host}:{args.command_port}", flush=True)

    try:
        while _keep_running:
            now = time.monotonic()
            timeout = max(0.0, min(next_critical, next_housekeeping) - now)
            for key, _ in selector.select(timeout):
                sock = cast(socket.socket, key.fileobj)
                if key.data == "command-server":
                    accept_command_client(sock, selector)
                elif key.data == "command-client":
                    handle_command_client(sock, selector, eps)

            now = time.monotonic()
            sim_time = now - start
            if now >= next_critical:
                dt = max(0.001, now - last_update)
                last_update = now
                measurements = eps.step(sim_time, dt)
                send_frames(transport, eps.build_critical_frames(measurements))
                next_critical += args.critical_interval
                print(
                    "eps_emulator: TX critical "
                    f"seq={measurements.sequence} state={state_name(measurements.state)} "
                    f"mode={power_mode_name(measurements.power_mode)} soc={measurements.battery_percent}% "
                    f"solar={measurements.solar_current_ma}mA subtype={AETHERFLOWCAN_HK_SUBTYPE_CRITICAL_REPORT}",
                    flush=True,
                )
            if now >= next_housekeeping:
                dt = max(0.001, now - last_update)
                last_update = now
                measurements = eps.step(sim_time, dt)
                send_frames(transport, eps.build_housekeeping_frames(measurements))
                next_housekeeping += args.housekeeping_interval
                print(
                    "eps_emulator: TX housekeeping "
                    f"seq={measurements.sequence} state={state_name(measurements.state)} "
                    f"mode={power_mode_name(measurements.power_mode)} soc={measurements.battery_percent}% "
                    f"batt_i={measurements.battery_current_ma}mA subtype={AETHERFLOWCAN_HK_SUBTYPE_REPORT}",
                    flush=True,
                )
    finally:
        for key in list(selector.get_map().values()):
            try:
                selector.unregister(key.fileobj)
            except Exception:
                pass
            fileobj = cast(socket.socket, key.fileobj)
            try:
                fileobj.close()
            except OSError:
                pass
        selector.close()
        transport.close()

    print("eps_emulator: stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
