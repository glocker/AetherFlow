#!/bin/sh

set -u

. ./aetherflow.env

HTTP_PORT=$AETHERFLOW_HTTP_PORT
CAN_INTERFACE=${AETHERFLOW_CAN_INTERFACE:-vcan0}
BRIDGE_URL=${AETHERFLOW_BRIDGE_URL:-http://127.0.0.1:$HTTP_PORT}
LOG_DIR=${AETHERFLOW_LOG_DIR:-logs}
PID_FILE="$LOG_DIR/demo.pids"

BRIDGE_PID=""
EPS_PID=""
CLEANED_UP=0

mkdir -p "$LOG_DIR"
: > "$PID_FILE"

info() {
    printf '%s\n' "$1"
}

fail() {
    printf 'AetherFlow demo error: %s\n' "$1" >&2
    exit 1
}

pid_alive() {
    pid="$1"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

write_pid() {
    name="$1"
    pid="$2"
    printf '%s=%s\n' "$name" "$pid" >> "$PID_FILE"
}

cleanup() {
    if [ "$CLEANED_UP" -eq 1 ]; then
        return
    fi
    CLEANED_UP=1
    trap - INT TERM EXIT
    info ""
    info "Stopping AetherFlow demo..."

    for pid in "$EPS_PID" "$BRIDGE_PID"; do
        if pid_alive "$pid"; then
            kill "$pid" 2>/dev/null || true
        fi
    done

    sleep 1

    for pid in "$EPS_PID" "$BRIDGE_PID"; do
        if pid_alive "$pid"; then
            kill -9 "$pid" 2>/dev/null || true
        fi
    done

    rm -f "$PID_FILE"
    info "Stopped. Logs are in $LOG_DIR/."
}

trap 'cleanup; exit 0' INT TERM
trap cleanup EXIT

command -v curl >/dev/null 2>&1 || fail "curl is required"
command -v ip >/dev/null 2>&1 || fail "iproute2 is required"
python3 -c 'import socket; raise SystemExit(0 if hasattr(socket, "PF_CAN") else 1)' >/dev/null 2>&1 || fail "Python SocketCAN support is required"

if ! ip link show "$CAN_INTERFACE" >/dev/null 2>&1; then
    fail "$CAN_INTERFACE is missing. Create it with: sudo modprobe vcan && sudo ip link add dev $CAN_INTERFACE type vcan && sudo ip link set up $CAN_INTERFACE"
fi

[ -f openmct/dist/index.html ] || fail "openmct/dist/index.html is missing; run make dashboard-build"

info "AetherFlow SocketCAN demo starting..."
info "Logs: $LOG_DIR/"
info "PID file: $PID_FILE"
info "CAN interface: $CAN_INTERFACE"
info "HTTP port: $HTTP_PORT"
info ""

AETHERFLOW_HTTP_PORT="$HTTP_PORT" AETHERFLOW_CAN_INTERFACE="$CAN_INTERFACE" python3 -m bridge_service > "$LOG_DIR/bridge_service.log" 2>&1 &
BRIDGE_PID=$!
write_pid bridge_service "$BRIDGE_PID"
info "[1/2] bridge_service  pid=$BRIDGE_PID $BRIDGE_URL"

health_ok=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl -fsS "$BRIDGE_URL/health" >/dev/null 2>&1; then
        health_ok=1
        break
    fi

    if ! pid_alive "$BRIDGE_PID"; then
        fail "bridge_service exited early; see $LOG_DIR/bridge_service.log"
    fi

    sleep 1
done

[ "$health_ok" -eq 1 ] || fail "bridge health check failed at $BRIDGE_URL/health; see $LOG_DIR/bridge_service.log"

AETHERFLOW_CAN_INTERFACE="$CAN_INTERFACE" python3 -m eps_emulator > "$LOG_DIR/eps_emulator.log" 2>&1 &
EPS_PID=$!
write_pid eps_emulator "$EPS_PID"
info "[2/2] eps_emulator   pid=$EPS_PID SocketCAN $CAN_INTERFACE"

telemetry_ok=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    latest=$(curl -fsS "$BRIDGE_URL/telemetry/latest" 2>/dev/null || true)
    case "$latest" in
        *'"valid":false'*|'') ;;
        *)
            telemetry_ok=1
            break
            ;;
    esac

    if ! pid_alive "$EPS_PID"; then
        fail "eps_emulator exited early; see $LOG_DIR/eps_emulator.log"
    fi

    sleep 1
done

if [ "$telemetry_ok" -eq 1 ]; then
    info "Telemetry is live. Latest packet:"
    info "$latest"
else
    info "Telemetry is not live yet; dashboard will keep waiting. Check logs if it stays empty."
fi

info ""
info "Open dashboard:"
info "$BRIDGE_URL/"
info ""
info "Fault command socket examples:"
info "printf '%s\n' '{\"fault\":\"panel_short\",\"enabled\":true}' | nc 127.0.0.1 40710"
info "printf '%s\n' '{\"fault\":\"clear\"}' | nc 127.0.0.1 40710"
info ""
info "Press Ctrl+C to stop demo processes."

while :; do
    if ! pid_alive "$BRIDGE_PID"; then
        fail "bridge_service stopped; see $LOG_DIR/bridge_service.log"
    fi
    if ! pid_alive "$EPS_PID"; then
        fail "eps_emulator stopped; see $LOG_DIR/eps_emulator.log"
    fi
    sleep 2
done
