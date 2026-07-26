# HTTP/WebSocket bridge service orchestration

from __future__ import annotations

import mimetypes
import select
import signal
import socket
import sys
from pathlib import Path
from socket import socket as Socket

from .can_wire import CanFrame
from .config import CAN_INTERFACE, EPS_NODE_ID, HTTP_PORT
from .spacecan import (
    SpaceCanFrameClass,
    SpaceCanReassembly,
    SpaceCanStatus,
    packet_parse,
    parse_can_id,
    reassembly_accept,
)
from .telemetry import TelemetrySnapshot, decode_eps_housekeeping
from .transports import eps_reply_filter, open_socketcan_transport
from .websocket import send_all, send_ws_text, websocket_accept_key

MAX_WS_CLIENTS = 8
HTTP_REQUEST_MAX = 2048
DASHBOARD_DIST_DIR = Path("openmct/dist")
DASHBOARD_INDEX = DASHBOARD_DIST_DIR / "index.html"
DASHBOARD_NOT_BUILT_MESSAGE = "Dashboard is not built. Run: make dashboard-build"

_keep_running = True


def handle_signal(_signal_number: int, _frame: object) -> None:
    global _keep_running
    _keep_running = False


def http_header(
    status_line: str,
    content_type: str,
    content_length: int,
    extra_headers: tuple[str, ...] = (),
) -> bytes:
    lines = [
        status_line,
        f"Content-Type: {content_type}",
        "Access-Control-Allow-Origin: *",
        *extra_headers,
        f"Content-Length: {content_length}",
        "Connection: close",
        "",
        "",
    ]
    return "\r\n".join(lines).encode("utf-8")


def open_http_server(port: int) -> Socket:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("0.0.0.0", port))
        server.listen(8)
        return server
    except OSError:
        server.close()
        raise


def send_http_response(client_socket: Socket, content_type: str, body: str | bytes) -> int:
    payload = body.encode("utf-8") if isinstance(body, str) else body
    header = http_header("HTTP/1.1 200 OK", content_type, len(payload))

    if send_all(client_socket, header) != 0:
        return -1
    return send_all(client_socket, payload)


def send_http_status(client_socket: Socket, status: int, reason: str, body: str) -> int:
    payload = body.encode("utf-8")
    header = http_header(
        f"HTTP/1.1 {status} {reason}",
        "text/plain; charset=utf-8",
        len(payload),
    )

    if send_all(client_socket, header) != 0:
        return -1
    return send_all(client_socket, payload)


def content_type_for_path(path: Path) -> str:
    suffix_map = {
        ".js": "text/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".html": "text/html; charset=utf-8",
        ".json": "application/json; charset=utf-8",
        ".svg": "image/svg+xml",
        ".png": "image/png",
        ".jpg": "image/jpeg",
        ".jpeg": "image/jpeg",
        ".gif": "image/gif",
        ".webp": "image/webp",
        ".woff": "font/woff",
        ".woff2": "font/woff2",
        ".ttf": "font/ttf",
    }
    detected_type = mimetypes.guess_type(path.name)[0]
    return suffix_map.get(path.suffix, detected_type or "application/octet-stream")


def request_path_safe(path: str) -> bool:
    return path.startswith("/") and ".." not in path and "\\" not in path


def dashboard_file(request_path: str) -> Path | None:
    if not DASHBOARD_INDEX.is_file():
        return None

    path = request_path.split("?", 1)[0]
    if path == "/":
        path = "/index.html"

    dist_file = DASHBOARD_DIST_DIR / path.lstrip("/")
    if dist_file.is_file():
        return dist_file

    if path.startswith(("/assets/", "/openmct/")):
        return None

    return DASHBOARD_INDEX


def send_static_file(client_socket: Socket, request_path: str) -> int:
    if not request_path_safe(request_path):
        return send_http_status(client_socket, 403, "Forbidden", "Forbidden")

    file_path = dashboard_file(request_path)
    if file_path is None and not DASHBOARD_INDEX.is_file():
        return send_http_status(client_socket, 503, "Service Unavailable", DASHBOARD_NOT_BUILT_MESSAGE)
    if file_path is None:
        return send_http_status(client_socket, 404, "Not Found", "Not Found")

    payload = file_path.read_bytes()
    header = http_header(
        "HTTP/1.1 200 OK",
        content_type_for_path(file_path),
        len(payload),
        ("Cache-Control: no-store",),
    )
    if send_all(client_socket, header) != 0:
        return -1
    return send_all(client_socket, payload)


def request_target(request: str) -> str | None:
    if not request.startswith("GET "):
        return None
    parts = request.split(" ", 2)
    if len(parts) < 2 or not parts[1]:
        return None
    return parts[1]


def find_header_value(request: str, header_name: str) -> str | None:
    prefix = header_name.lower() + ":"
    for line in request.split("\r\n"):
        if line.lower().startswith(prefix):
            return line[len(header_name) + 1 :].lstrip(" ")
    return None


def compact_ws_clients(ws_clients: list[Socket]) -> None:
    ws_clients[:] = [client for client in ws_clients if client.fileno() >= 0][:MAX_WS_CLIENTS]


def broadcast_ws(ws_clients: list[Socket], payload_json: str) -> None:
    alive: list[Socket] = []
    for client in ws_clients:
        if send_ws_text(client, payload_json) == 0:
            alive.append(client)
        else:
            try:
                client.close()
            except OSError:
                pass
    ws_clients[:] = alive[:MAX_WS_CLIENTS]


def handle_http_client(server: Socket, ws_clients: list[Socket], telemetry: TelemetrySnapshot) -> None:
    try:
        client, _ = server.accept()
    except OSError:
        return
    try:
        request_bytes = client.recv(HTTP_REQUEST_MAX - 1)
        if not request_bytes:
            return
        request = request_bytes.decode("iso-8859-1", errors="replace")
        path = request_target(request)
        if path is None:
            send_http_status(client, 405, "Method Not Allowed", "Method Not Allowed")
            return

        if path == "/realtime":
            key = find_header_value(request, "Sec-WebSocket-Key")
            if key is None:
                return
            accept_key = websocket_accept_key(key)
            response = (
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                f"Sec-WebSocket-Accept: {accept_key}\r\n\r\n"
            )
            if send_all(client, response) != 0:
                return
            if len(ws_clients) < MAX_WS_CLIENTS:
                ws_clients.append(client)
                if telemetry.valid:
                    send_ws_text(client, telemetry.json)
                return
            return

        if path == "/telemetry/latest":
            body = telemetry.json if telemetry.valid else '{"valid":false}'
            send_http_response(client, "application/json", body)
        elif path == "/health":
            send_http_response(client, "application/json", '{"status":"ok"}')
        else:
            send_static_file(client, path)
    finally:
        if client not in ws_clients:
            try:
                client.close()
            except OSError:
                pass


def handle_can_frame(
    frame: CanFrame,
    reassemblies: dict[tuple[int, int], SpaceCanReassembly],
    telemetry: TelemetrySnapshot,
    ws_clients: list[Socket],
) -> None:
    try:
        parsed_id = parse_can_id(frame.id)
    except ValueError:
        return
    if parsed_id.frame_class != SpaceCanFrameClass.REPLY:
        return

    reassembly_key = (int(parsed_id.frame_class), parsed_id.node_id)
    reassembly = reassemblies.setdefault(reassembly_key, SpaceCanReassembly())

    try:
        status, packet = reassembly_accept(reassembly, frame)
    except ValueError:
        print(f"bridge_service: reassembly error for id=0x{frame.id:03X}", file=sys.stderr)
        reassembly.reset()
        return
    if status == SpaceCanStatus.ERR_IN_PROGRESS:
        return
    if status != SpaceCanStatus.OK or packet is None:
        print(f"bridge_service: reassembly error {int(status)} for id=0x{frame.id:03X}", file=sys.stderr)
        reassembly.reset()
        return

    try:
        view = packet_parse(packet)
    except ValueError:
        print("bridge_service: failed to parse SpaceCAN packet", file=sys.stderr)
        return
    if decode_eps_housekeeping(parsed_id.node_id, view, telemetry):
        print(f"bridge_service: HK {telemetry.json}", flush=True)
        broadcast_ws(ws_clients, telemetry.json)


def main() -> int:
    global _keep_running
    _keep_running = True
    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)
    if hasattr(signal, "SIGPIPE"):
        signal.signal(signal.SIGPIPE, signal.SIG_IGN)

    telemetry = TelemetrySnapshot()
    reassemblies: dict[tuple[int, int], SpaceCanReassembly] = {}
    ws_clients: list[Socket] = []
    http_port = HTTP_PORT

    try:
        transport = open_socketcan_transport(CAN_INTERFACE, [eps_reply_filter(EPS_NODE_ID)])
    except OSError as error:
        print(f"bridge_service: failed to open SocketCAN interface {CAN_INTERFACE}: {error}", file=sys.stderr)
        return 1

    try:
        server = open_http_server(http_port)
    except OSError:
        print(f"bridge_service: failed to open HTTP/WebSocket server on port {http_port}", file=sys.stderr)
        transport.close()
        return 1

    print(f"bridge_service: SocketCAN {CAN_INTERFACE} HTTP/WebSocket http://0.0.0.0:{http_port}/")
    try:
        while _keep_running:
            ready, _, _ = select.select([transport.fd, server], [], [], 0.5)
            if server in ready:
                handle_http_client(server, ws_clients, telemetry)
            if transport.fd in ready:
                frame = transport.recv()
                if frame is not None:
                    handle_can_frame(frame, reassemblies, telemetry, ws_clients)
    finally:
        for client in ws_clients:
            try:
                client.close()
            except OSError:
                pass
        server.close()
        transport.close()
    print("bridge_service: stopped")
    return 0
