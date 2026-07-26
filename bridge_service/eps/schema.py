"""EPS telemetry schema and binary payload codec."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum

from bridge_service.spacecan import get_i16_be, get_u16_be, put_i16_be, put_u16_be

from .constants import (
    EPS_FLAG_LOW_BATTERY,
    EPS_FLAG_OVERTEMP,
    EPS_FLAG_PAYLOAD_SHED,
    EPS_FLAG_SAFE_MODE,
    EPS_HOUSEKEEPING_PAYLOAD_LEN,
)


class EpsState(IntEnum):
    BOOT = 0
    PRE_OPERATIONAL = 1
    OPERATIONAL = 2
    SAFE = 3


class EpsPowerMode(IntEnum):
    NOMINAL = 0
    LOW_POWER = 1
    CRITICAL = 2
    SAFE = 3


@dataclass(frozen=True, slots=True)
class EpsMeasurements:
    sequence: int
    state: EpsState | int
    power_mode: EpsPowerMode | int
    bus_voltage_mv: int
    bus_current_ma: int
    battery_percent: int
    battery_voltage_mv: int
    battery_current_ma: int
    solar_current_ma: int
    temperature_cdeg: int
    status_flags: int

    @property
    def fault_flags(self) -> int:
        return self.status_flags


def state_name(state: EpsState | int) -> str:
    try:
        return EpsState(state).name
    except ValueError:
        return "UNKNOWN"


def power_mode_name(power_mode: EpsPowerMode | int) -> str:
    try:
        return EpsPowerMode(power_mode).name
    except ValueError:
        return "UNKNOWN"


def power_mode_for_soc(battery_percent: float, current_mode: EpsPowerMode) -> EpsPowerMode:
    """SOC-based mode transition with hysteresis."""

    if current_mode == EpsPowerMode.SAFE:
        return EpsPowerMode.SAFE if battery_percent < 18 else EpsPowerMode.CRITICAL
    if battery_percent < 10:
        return EpsPowerMode.SAFE
    if current_mode == EpsPowerMode.CRITICAL:
        return EpsPowerMode.CRITICAL if battery_percent < 20 else EpsPowerMode.LOW_POWER
    if battery_percent < 15:
        return EpsPowerMode.CRITICAL
    if current_mode == EpsPowerMode.LOW_POWER:
        return EpsPowerMode.LOW_POWER if battery_percent < 35 else EpsPowerMode.NOMINAL
    if battery_percent < 30:
        return EpsPowerMode.LOW_POWER
    return EpsPowerMode.NOMINAL


def flags_for_measurements(measurements: EpsMeasurements) -> int:
    flags = measurements.status_flags
    if measurements.power_mode == EpsPowerMode.SAFE:
        flags |= EPS_FLAG_SAFE_MODE
    if measurements.power_mode in (EpsPowerMode.LOW_POWER, EpsPowerMode.CRITICAL, EpsPowerMode.SAFE):
        flags |= EPS_FLAG_PAYLOAD_SHED
    if measurements.battery_percent < 30:
        flags |= EPS_FLAG_LOW_BATTERY
    if measurements.temperature_cdeg > 6000:
        flags |= EPS_FLAG_OVERTEMP
    return flags & 0xFFFF


def build_housekeeping_payload(measurements: EpsMeasurements) -> bytes:
    flags = flags_for_measurements(measurements)
    return b"".join(
        (
            put_u16_be(measurements.sequence),
            bytes((int(measurements.state),)),
            bytes((int(measurements.power_mode),)),
            put_u16_be(measurements.bus_voltage_mv),
            put_i16_be(measurements.bus_current_ma),
            bytes((measurements.battery_percent & 0xFF,)),
            put_u16_be(measurements.battery_voltage_mv),
            put_i16_be(measurements.battery_current_ma),
            put_u16_be(measurements.solar_current_ma),
            put_i16_be(measurements.temperature_cdeg),
            put_u16_be(flags),
        )
    )


def decode_housekeeping_payload(payload: bytes) -> EpsMeasurements:
    if len(payload) != EPS_HOUSEKEEPING_PAYLOAD_LEN:
        raise ValueError("invalid EPS housekeeping payload length")
    try:
        state = EpsState(payload[2])
    except ValueError:
        state = payload[2]
    try:
        power_mode = EpsPowerMode(payload[3])
    except ValueError:
        power_mode = payload[3]
    return EpsMeasurements(
        sequence=get_u16_be(payload[0:2]),
        state=state,
        power_mode=power_mode,
        bus_voltage_mv=get_u16_be(payload[4:6]),
        bus_current_ma=get_i16_be(payload[6:8]),
        battery_percent=payload[8],
        battery_voltage_mv=get_u16_be(payload[9:11]),
        battery_current_ma=get_i16_be(payload[11:13]),
        solar_current_ma=get_u16_be(payload[13:15]),
        temperature_cdeg=get_i16_be(payload[15:17]),
        status_flags=get_u16_be(payload[17:19]),
    )
