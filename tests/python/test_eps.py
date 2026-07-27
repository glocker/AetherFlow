from __future__ import annotations

from bridge_service.eps import (
    EPS_FLAG_BATTERY_DEGRADED,
    EPS_FLAG_LOW_BATTERY,
    EPS_FLAG_OVERCURRENT,
    EPS_FLAG_PANEL_FAULT,
    EPS_FLAG_PAYLOAD_SHED,
    EPS_HOUSEKEEPING_PAYLOAD_LEN,
    EpsMeasurements,
    EpsPhysicalModel,
    EpsPowerMode,
    EpsSimulator,
    EpsState,
    FaultState,
    build_housekeeping_payload,
    decode_housekeeping_payload,
)


def test_housekeeping_payload_roundtrip() -> None:
    measurements = EpsMeasurements(
        sequence=42,
        state=EpsState.OPERATIONAL,
        power_mode=EpsPowerMode.NOMINAL,
        bus_voltage_mv=5012,
        bus_current_ma=650,
        battery_percent=77,
        battery_voltage_mv=7940,
        battery_current_ma=220,
        solar_current_ma=900,
        temperature_cdeg=2510,
        status_flags=0,
    )

    payload = build_housekeeping_payload(measurements)
    assert len(payload) == EPS_HOUSEKEEPING_PAYLOAD_LEN

    decoded = decode_housekeeping_payload(payload)
    assert decoded == measurements


def test_low_power_flags_are_derived_from_measurements() -> None:
    measurements = EpsMeasurements(
        sequence=1,
        state=EpsState.OPERATIONAL,
        power_mode=EpsPowerMode.LOW_POWER,
        bus_voltage_mv=4975,
        bus_current_ma=380,
        battery_percent=25,
        battery_voltage_mv=6900,
        battery_current_ma=-350,
        solar_current_ma=0,
        temperature_cdeg=2400,
        status_flags=0,
    )

    decoded = decode_housekeeping_payload(build_housekeeping_payload(measurements))
    assert decoded.status_flags & EPS_FLAG_LOW_BATTERY
    assert decoded.status_flags & EPS_FLAG_PAYLOAD_SHED


def test_dynamic_model_charges_in_sunlight_and_discharges_in_eclipse() -> None:
    model = EpsPhysicalModel(battery_soc_percent=50.0)
    sun_sample = model.sample(sim_time_s=1.0, dt_s=10.0, power_mode=EpsPowerMode.NOMINAL, faults=FaultState())
    assert sun_sample["solar_current_ma"] > 0
    assert sun_sample["battery_current_ma"] > 0

    eclipse_sample = model.sample(sim_time_s=90.0, dt_s=10.0, power_mode=EpsPowerMode.NOMINAL, faults=FaultState())
    assert eclipse_sample["solar_current_ma"] == 0
    assert eclipse_sample["battery_current_ma"] < 0


def test_eps_simulator_faults_set_fault_flags() -> None:
    eps = EpsSimulator(node_id=1, model=EpsPhysicalModel())
    eps.faults.panel_short = True
    eps.faults.battery_degradation = 0.35
    eps.faults.overcurrent = True

    measurements = eps.step(sim_time_s=1.0, dt_s=0.2)
    assert measurements.status_flags & EPS_FLAG_PANEL_FAULT
    assert measurements.status_flags & EPS_FLAG_BATTERY_DEGRADED
    assert measurements.status_flags & EPS_FLAG_OVERCURRENT


def test_eps_simulator_builds_fragmented_frames() -> None:
    eps = EpsSimulator(node_id=1, model=EpsPhysicalModel())
    measurements = eps.step(sim_time_s=1.0, dt_s=0.2)
    frames = eps.build_housekeeping_frames(measurements)

    assert frames
    assert all(frame.id == 0x581 for frame in frames)
    assert len(frames) >= 2
