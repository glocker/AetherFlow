"""Dynamic EPS simulation model for Linux/vcan demos."""

from __future__ import annotations

import math
from dataclasses import dataclass, field

from bridge_service.can_wire import CanFrame
from bridge_service.spacecan import SpaceCanFrameClass, fragment_packet, packet_build

from .constants import (
    EPS_FLAG_BATTERY_DEGRADED,
    EPS_FLAG_OVERCURRENT,
    EPS_FLAG_PANEL_FAULT,
    EPS_HOUSEKEEPING_PAYLOAD_LEN,
    SPACECAN_HK_SUBTYPE_CRITICAL_REPORT,
    SPACECAN_HK_SUBTYPE_REPORT,
    SPACECAN_SERVICE_HOUSEKEEPING,
)
from .schema import EpsMeasurements, EpsPowerMode, EpsState, build_housekeeping_payload, power_mode_for_soc


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


@dataclass(slots=True)
class FaultState:
    panel_short: bool = False
    battery_degradation: float = 0.0
    overcurrent: bool = False

    def clear(self) -> None:
        self.panel_short = False
        self.battery_degradation = 0.0
        self.overcurrent = False


@dataclass(slots=True)
class EpsPhysicalModel:
    battery_capacity_mah: float = 5200.0
    battery_soc_percent: float = 78.0
    battery_temperature_c: float = 24.0
    orbit_period_s: float = 120.0
    sunlight_s: float = 75.0
    max_solar_current_ma: float = 950.0
    base_load_ma: float = 380.0
    payload_load_ma: float = 280.0
    panel_health: float = 1.0

    def sunlight_factor(self, sim_time_s: float) -> float:
        phase = sim_time_s % self.orbit_period_s
        edge_s = 4.0
        if phase < self.sunlight_s - edge_s:
            return 1.0
        if phase < self.sunlight_s:
            return (self.sunlight_s - phase) / edge_s
        if phase > self.orbit_period_s - edge_s:
            return (phase - (self.orbit_period_s - edge_s)) / edge_s
        return 0.0

    def sample(self, sim_time_s: float, dt_s: float, power_mode: EpsPowerMode, faults: FaultState) -> dict[str, int]:
        effective_capacity = self.battery_capacity_mah * (1.0 - clamp(faults.battery_degradation, 0.0, 0.8))
        sun_factor = self.sunlight_factor(sim_time_s)
        panel_factor = 0.05 if faults.panel_short else self.panel_health
        solar_current_ma = self.max_solar_current_ma * sun_factor * panel_factor

        payload_enabled = power_mode == EpsPowerMode.NOMINAL
        load_current_ma = self.base_load_ma + (self.payload_load_ma if payload_enabled else 0.0)
        if faults.overcurrent:
            load_current_ma += 750.0

        battery_current_ma = solar_current_ma - load_current_ma
        self.battery_soc_percent += battery_current_ma * dt_s / 3600.0 / effective_capacity * 100.0
        self.battery_soc_percent = clamp(self.battery_soc_percent, 0.0, 100.0)

        target_temp_c = 18.0 + 0.012 * load_current_ma + 3.0 * sun_factor
        if sun_factor <= 0.01:
            target_temp_c -= 5.0
        self.battery_temperature_c += (target_temp_c - self.battery_temperature_c) * clamp(dt_s * 0.08, 0.0, 1.0)

        battery_voltage_mv = int(6400 + self.battery_soc_percent / 100.0 * 2000)
        bus_voltage_mv = int(5000 + math.sin(sim_time_s / 8.0) * 25)
        bus_current_ma = int(load_current_ma)

        return {
            "battery_percent": int(round(self.battery_soc_percent)),
            "battery_voltage_mv": battery_voltage_mv,
            "battery_current_ma": int(round(battery_current_ma)),
            "solar_current_ma": int(round(solar_current_ma)),
            "bus_voltage_mv": bus_voltage_mv,
            "bus_current_ma": bus_current_ma,
            "temperature_cdeg": int(round(self.battery_temperature_c * 100)),
        }


@dataclass(slots=True)
class EpsSimulator:
    node_id: int
    model: EpsPhysicalModel
    state: EpsState = EpsState.BOOT
    power_mode: EpsPowerMode = EpsPowerMode.NOMINAL
    sequence: int = 0
    faults: FaultState = field(default_factory=FaultState)
    _boot_elapsed_s: float = 0.0

    def step(self, sim_time_s: float, dt_s: float) -> EpsMeasurements:
        self._boot_elapsed_s += dt_s
        if self._boot_elapsed_s > 1.0 and self.state == EpsState.BOOT:
            self.state = EpsState.PRE_OPERATIONAL
        if self._boot_elapsed_s > 2.0 and self.state == EpsState.PRE_OPERATIONAL:
            self.state = EpsState.OPERATIONAL

        self.power_mode = power_mode_for_soc(self.model.battery_soc_percent, self.power_mode)
        if self.power_mode == EpsPowerMode.SAFE:
            self.state = EpsState.SAFE

        sample = self.model.sample(sim_time_s, dt_s, self.power_mode, self.faults)
        flags = 0
        if self.faults.panel_short:
            flags |= EPS_FLAG_PANEL_FAULT
        if self.faults.battery_degradation > 0:
            flags |= EPS_FLAG_BATTERY_DEGRADED
        if self.faults.overcurrent:
            flags |= EPS_FLAG_OVERCURRENT

        self.sequence = (self.sequence + 1) & 0xFFFF
        return EpsMeasurements(
            sequence=self.sequence,
            state=self.state,
            power_mode=self.power_mode,
            status_flags=flags,
            **sample,
        )

    def build_report_frames(self, measurements: EpsMeasurements, subtype: int = SPACECAN_HK_SUBTYPE_REPORT) -> list[CanFrame]:
        payload = build_housekeeping_payload(measurements)
        if len(payload) != EPS_HOUSEKEEPING_PAYLOAD_LEN:
            raise ValueError("EPS payload encoder returned invalid length")
        packet = packet_build(SPACECAN_SERVICE_HOUSEKEEPING, subtype, payload)
        return fragment_packet(SpaceCanFrameClass.REPLY, self.node_id, packet)

    def build_critical_frames(self, measurements: EpsMeasurements) -> list[CanFrame]:
        return self.build_report_frames(measurements, SPACECAN_HK_SUBTYPE_CRITICAL_REPORT)

    def build_housekeeping_frames(self, measurements: EpsMeasurements) -> list[CanFrame]:
        return self.build_report_frames(measurements, SPACECAN_HK_SUBTYPE_REPORT)
