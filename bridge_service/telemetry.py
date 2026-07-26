# Telemetry snapshot model and JSON serialization helpers

from __future__ import annotations

import json
import time
from dataclasses import dataclass

from .eps import (
    EPS_HOUSEKEEPING_PAYLOAD_LEN,
    EpsPowerMode,
    EpsState,
    SPACECAN_HK_SUBTYPE_CRITICAL_REPORT,
    SPACECAN_HK_SUBTYPE_REPORT,
    SPACECAN_SERVICE_HOUSEKEEPING,
    decode_housekeeping_payload,
    power_mode_name,
    state_name,
)
from .spacecan import SpaceCanPacketView


@dataclass(slots=True)
class TelemetrySnapshot:
    valid: bool = False
    node_id: int = 0
    sequence: int = 0
    subtype: int = SPACECAN_HK_SUBTYPE_REPORT
    state: EpsState | int = EpsState.BOOT
    power_mode: EpsPowerMode | int = EpsPowerMode.NOMINAL
    bus_voltage_mv: int = 0
    bus_current_ma: int = 0
    battery_percent: int = 0
    battery_voltage_mv: int = 0
    battery_current_ma: int = 0
    solar_current_ma: int = 0
    temperature_cdeg: int = 0
    status_flags: int = 0
    fault_flags: int = 0
    timestamp_ms: int = 0
    json: str = ""


def now_ms() -> int:
    return int(time.time() * 1000)


def telemetry_to_json(telemetry: TelemetrySnapshot) -> str:
    payload = {
        "node": telemetry.node_id,
        "service": SPACECAN_SERVICE_HOUSEKEEPING,
        "subtype": telemetry.subtype,
        "sequence": telemetry.sequence,
        "state": state_name(telemetry.state),
        "power_mode": power_mode_name(telemetry.power_mode),
        "power_mode_id": int(telemetry.power_mode),
        "bus_voltage_mv": telemetry.bus_voltage_mv,
        "bus_current_ma": telemetry.bus_current_ma,
        "battery_percent": telemetry.battery_percent,
        "battery_voltage_mv": telemetry.battery_voltage_mv,
        "battery_current_ma": telemetry.battery_current_ma,
        "solar_current_ma": telemetry.solar_current_ma,
        "temperature_cdeg": telemetry.temperature_cdeg,
        "status_flags": telemetry.status_flags,
        "fault_flags": telemetry.fault_flags,
        "timestamp_ms": telemetry.timestamp_ms,
    }
    telemetry.json = json.dumps(payload, separators=(",", ":"))
    return telemetry.json


def decode_eps_housekeeping(node_id: int, view: SpaceCanPacketView, telemetry: TelemetrySnapshot) -> bool:
    if view.service != SPACECAN_SERVICE_HOUSEKEEPING:
        return False
    if view.subtype not in (SPACECAN_HK_SUBTYPE_REPORT, SPACECAN_HK_SUBTYPE_CRITICAL_REPORT):
        return False
    if view.payload_len != EPS_HOUSEKEEPING_PAYLOAD_LEN:
        return False

    try:
        measurements = decode_housekeeping_payload(view.payload)
    except ValueError:
        return False

    telemetry.valid = True
    telemetry.node_id = node_id
    telemetry.sequence = measurements.sequence
    telemetry.subtype = view.subtype
    telemetry.state = measurements.state
    telemetry.power_mode = measurements.power_mode
    telemetry.bus_voltage_mv = measurements.bus_voltage_mv
    telemetry.bus_current_ma = measurements.bus_current_ma
    telemetry.battery_percent = measurements.battery_percent
    telemetry.battery_voltage_mv = measurements.battery_voltage_mv
    telemetry.battery_current_ma = measurements.battery_current_ma
    telemetry.solar_current_ma = measurements.solar_current_ma
    telemetry.temperature_cdeg = measurements.temperature_cdeg
    telemetry.status_flags = measurements.status_flags
    telemetry.fault_flags = measurements.fault_flags
    telemetry.timestamp_ms = now_ms()
    telemetry_to_json(telemetry)
    return True
