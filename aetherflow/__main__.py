"""Launch Python backend services"""

from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import IO

from bridge_service.config import CAN_INTERFACE, HTTP_PORT, PROJECT_ROOT

DEFAULT_LOG_DIR = Path(os.getenv("AETHERFLOW_LOG_DIR", "logs"))
DEFAULT_HEALTH_TIMEOUT_S = 10.0
DEFAULT_TELEMETRY_TIMEOUT_S = 10.0


@dataclass(slots=True)
class ManagedProcess:
    name: str
    process: subprocess.Popen[bytes]
    log_file: IO[bytes]

    @property
    def pid(self) -> int:
        return self.process.pid

    def alive(self) -> bool:
        return self.process.poll() is None

    def terminate(self) -> None:
        if self.alive():
            self.process.terminate()

    def kill(self) -> None:
        if self.alive():
            self.process.kill()

    def close_log(self) -> None:
        self.log_file.close()


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run AetherFlow bridge_service + eps_emulator")
    parser.add_argument("--interface", default=CAN_INTERFACE, help="SocketCAN interface, default: %(default)s")
    parser.add_argument("--http-port", type=int, default=HTTP_PORT, help="bridge HTTP port, default: %(default)s")
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR, help="log directory, default: %(default)s")
    parser.add_argument("--skip-dashboard-check", action="store_true", help="do not require openmct/dist/index.html")
    parser.add_argument("--health-timeout", type=float, default=DEFAULT_HEALTH_TIMEOUT_S, help="seconds to wait for bridge /health")
    parser.add_argument("--telemetry-timeout", type=float, default=DEFAULT_TELEMETRY_TIMEOUT_S, help="seconds to wait for first telemetry packet")
    return parser


def check_can_interface(interface: str) -> None:
    result = subprocess.run(
        ["ip", "link", "show", interface],
        cwd=PROJECT_ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{interface} is missing. Create it with: "
            f"sudo modprobe vcan && sudo ip link add dev {interface} type vcan && sudo ip link set up {interface}"
        )


def check_dashboard_built() -> None:
    index = PROJECT_ROOT / "openmct" / "dist" / "index.html"
    if not index.is_file():
        raise RuntimeError("dashboard is not built. Run: npm --prefix openmct run build")


def open_log(log_dir: Path, name: str):
    log_dir.mkdir(parents=True, exist_ok=True)
    return (log_dir / f"{name}.log").open("wb")


def spawn_service(name: str, module: str, *, env: dict[str, str], log_dir: Path) -> ManagedProcess:
    log_file = open_log(log_dir, name)
    process = subprocess.Popen(
        [sys.executable, "-m", module],
        cwd=PROJECT_ROOT,
        env=env,
        stdout=log_file,
        stderr=subprocess.STDOUT,
    )
    return ManagedProcess(name=name, process=process, log_file=log_file)


def http_get(url: str, timeout_s: float = 1.0) -> bytes | None:
    try:
        with urllib.request.urlopen(url, timeout=timeout_s) as response:
            return response.read()
    except (urllib.error.URLError, TimeoutError):
        return None


def wait_for_health(base_url: str, timeout_s: float, processes: list[ManagedProcess]) -> None:
    deadline = time.monotonic() + timeout_s
    health_url = f"{base_url}/health"
    while time.monotonic() < deadline:
        ensure_children_alive(processes)
        if http_get(health_url) is not None:
            return
        time.sleep(0.25)
    raise RuntimeError(f"bridge health check timed out: {health_url}")


def wait_for_telemetry(base_url: str, timeout_s: float, processes: list[ManagedProcess]) -> bytes | None:
    deadline = time.monotonic() + timeout_s
    latest_url = f"{base_url}/telemetry/latest"
    while time.monotonic() < deadline:
        ensure_children_alive(processes)
        payload = http_get(latest_url)
        if payload and b'"valid":false' not in payload:
            return payload
        time.sleep(0.5)
    return None


def ensure_children_alive(processes: list[ManagedProcess]) -> None:
    for child in processes:
        exit_code = child.process.poll()
        if exit_code is not None:
            raise RuntimeError(f"{child.name} exited early with code {exit_code}; see logs/{child.name}.log")


def stop_processes(processes: list[ManagedProcess]) -> None:
    for child in reversed(processes):
        child.terminate()
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        if all(not child.alive() for child in processes):
            break
        time.sleep(0.1)
    for child in reversed(processes):
        child.kill()
    for child in processes:
        child.close_log()


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    env = os.environ.copy()
    env["AETHERFLOW_CAN_INTERFACE"] = args.interface
    env["AETHERFLOW_HTTP_PORT"] = str(args.http_port)
    env["PYTHONUNBUFFERED"] = "1"

    try:
        check_can_interface(args.interface)
        if not args.skip_dashboard_check:
            check_dashboard_built()
    except RuntimeError as error:
        print(f"aetherflow: {error}", file=sys.stderr)
        return 1

    processes: list[ManagedProcess] = []
    keep_running = True

    def request_stop(_signal_number: int, _frame: object) -> None:
        nonlocal keep_running
        keep_running = False

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    base_url = f"http://127.0.0.1:{args.http_port}"
    try:
        bridge = spawn_service("bridge_service", "bridge_service", env=env, log_dir=args.log_dir)
        processes.append(bridge)
        print(f"[1/2] bridge_service pid={bridge.pid} {base_url}")
        wait_for_health(base_url, args.health_timeout, processes)

        eps = spawn_service("eps_emulator", "eps_emulator", env=env, log_dir=args.log_dir)
        processes.append(eps)
        print(f"[2/2] eps_emulator  pid={eps.pid} SocketCAN {args.interface}")

        telemetry = wait_for_telemetry(base_url, args.telemetry_timeout, processes)
        if telemetry:
            print("Telemetry is live. Latest packet:")
            print(telemetry.decode("utf-8", errors="replace"))
        else:
            print("Telemetry is not live yet; dashboard will keep waiting. Check logs if it stays empty.")

        print()
        print(f"Open dashboard: {base_url}/")
        print("Fault command examples:")
        print("printf '%s\\n' '{\"fault\":\"panel_short\",\"enabled\":true}' | nc 127.0.0.1 40710")
        print("printf '%s\\n' '{\"fault\":\"clear\"}' | nc 127.0.0.1 40710")
        print()
        print("Press Ctrl+C to stop AetherFlow processes.")

        while keep_running:
            ensure_children_alive(processes)
            time.sleep(1.0)
    except RuntimeError as error:
        print(f"aetherflow: {error}", file=sys.stderr)
        return 1
    finally:
        if processes:
            print("Stopping AetherFlow...")
            stop_processes(processes)
            print(f"Stopped. Logs are in {args.log_dir}/.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
