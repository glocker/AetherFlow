#Min WebSocket handshake and frame helpers for realtime telemetry

from __future__ import annotations

import base64
import hashlib
import socket

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def websocket_accept_key(client_key: str) -> str:
    digest = hashlib.sha1((client_key + WS_GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def send_all(sock: socket.socket, data: bytes | str) -> int:
    payload = data.encode("utf-8") if isinstance(data, str) else data
    try:
        sock.sendall(payload)
        return 0
    except OSError:
        return -1


def send_ws_text(sock: socket.socket, text: str) -> int:
    payload = text.encode("utf-8")
    length = len(payload)
    if length < 126:
        header = bytes((0x81, length))
    elif length <= 0xFFFF:
        header = bytes((0x81, 126, (length >> 8) & 0xFF, length & 0xFF))
    else:
        return -1
    return 0 if send_all(sock, header) == 0 and send_all(sock, payload) == 0 else -1
