#!/bin/sh

set -u

PORT=${PORT:-8080}
BRIDGE_URL=${AETHERFLOW_BRIDGE_URL:-http://127.0.0.1:$PORT}
CONTROLLER_RATE_HZ=${AETHERFLOW_CONTROLLER_RATE_HZ:-5}
LOG_DIR=${AETHERFLOW_LOG_DIR:-logs}
PID_FILE="$LOG_DIR/hosted-demo.pids"

BRIDGE_PID=""
EPS_PID=""
CONTROLLER_PID=""
CLEANED_UP=0

mkdir -p "$LOG_DIR"
: > "$PID_FILE"

info() {
    printf '%s\n' "$1"
}

fail() {
    printf 'AetherFlow hosted demo error: %s\n' "$1" >&2
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
    info "Stopping AetherFlow hosted demo..."

    for pid in "$CONTROLLER_PID" "$EPS_PID" "$BRIDGE_PID"; do
        if pid_alive "$pid"; then
            kill "$pid" 2>/dev/null || true
        fi
    done

    sleep 1

    for pid in "$CONTROLLER_PID" "$EPS_PID" "$BRIDGE_PID"; do
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

[ -x ./bridge_service ] || fail "./bridge_service is missing; run make backend"
[ -x ./eps_simulator ] || fail "./eps_simulator is missing; run make backend"
[ -x ./controller_simulator ] || fail "./controller_simulator is missing; run make backend"
[ -f openmct/dist/index.html ] || fail "openmct/dist/index.html is missing; run make dashboard-build"

info "AetherFlow hosted demo starting..."
info "Logs: $LOG_DIR/"
info "PID file: $PID_FILE"
info "HTTP port: $PORT"
info ""

PORT="$PORT" ./bridge_service > "$LOG_DIR/bridge_service.log" 2>&1 &
BRIDGE_PID=$!
write_pid bridge_service "$BRIDGE_PID"
info "[1/3] bridge_service       pid=$BRIDGE_PID $BRIDGE_URL"

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

./eps_simulator > "$LOG_DIR/eps_simulator.log" 2>&1 &
EPS_PID=$!
write_pid eps_simulator "$EPS_PID"
info "[2/3] eps_simulator        pid=$EPS_PID UDP virtual CAN bus"

./controller_simulator "$CONTROLLER_RATE_HZ" > "$LOG_DIR/controller_simulator.log" 2>&1 &
CONTROLLER_PID=$!
write_pid controller_simulator "$CONTROLLER_PID"
info "[3/3] controller_simulator pid=$CONTROLLER_PID rate=${CONTROLLER_RATE_HZ}Hz"
info ""

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
        fail "eps_simulator exited early; see $LOG_DIR/eps_simulator.log"
    fi
    if ! pid_alive "$CONTROLLER_PID"; then
        fail "controller_simulator exited early; see $LOG_DIR/controller_simulator.log"
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
info "Open dashboard on the public Render URL or locally at:"
info "$BRIDGE_URL/"
info ""
info "Bridge API:"
info "$BRIDGE_URL/health"
info "$BRIDGE_URL/telemetry/latest"
info ""

while :; do
    if ! pid_alive "$BRIDGE_PID"; then
        fail "bridge_service stopped; see $LOG_DIR/bridge_service.log"
    fi
    if ! pid_alive "$EPS_PID"; then
        fail "eps_simulator stopped; see $LOG_DIR/eps_simulator.log"
    fi
    if ! pid_alive "$CONTROLLER_PID"; then
        fail "controller_simulator stopped; see $LOG_DIR/controller_simulator.log"
    fi
    sleep 2
done
