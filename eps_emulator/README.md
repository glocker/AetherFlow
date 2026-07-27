# `eps_emulator`

`eps_emulator` is the Python EPS node simulator for AetherFlow.

It publishes EPS telemetry using the AetherFlow CAN Protocol onto Linux SocketCAN/vcan:

```text
eps_emulator -> vcan0 -> bridge_service
```

## Entry point

```sh
python3 -m eps_emulator
```

Useful options:

```sh
python3 -m eps_emulator --interface vcan0
python3 -m eps_emulator --critical-interval 0.1 --housekeeping-interval 1.0
python3 -m eps_emulator --no-command-socket
```

## Telemetry periods

Defaults:

```text
critical telemetry:     200 ms
housekeeping telemetry: 1 s
```

Both are encoded as AetherFlow protocol service `3` packets:

```text
subtype 25 = housekeeping report
subtype 26 = critical report
```

## Dynamic model

The emulator contains a lightweight EPS model:

- accelerated sunlight/eclipse orbit cycle;
- solar panel generation;
- battery charge/discharge;
- bus load and payload load;
- battery temperature drift;
- SOC-based power modes with hysteresis.

Power modes:

```text
NOMINAL
LOW_POWER
CRITICAL
SAFE
```

In low-power modes, the model virtually sheds payload load and sets `PAYLOAD_SHED` in telemetry flags.

## Fault injection

By default the emulator opens a localhost TCP command socket:

```text
127.0.0.1:40710
```

Commands are JSON lines:

```sh
printf '%s\n' '{"fault":"panel_short","enabled":true}' | nc 127.0.0.1 40710
printf '%s\n' '{"fault":"battery_degradation","level":0.35}' | nc 127.0.0.1 40710
printf '%s\n' '{"fault":"overcurrent","enabled":true}' | nc 127.0.0.1 40710
printf '%s\n' '{"fault":"clear"}' | nc 127.0.0.1 40710
```

Fault effects are visible in:

```text
solar_current_ma
battery_current_ma
power_mode
status_flags
fault_flags
```

## Shared code

The emulator reuses the same EPS schema/model package as the bridge:

```text
bridge_service/eps/constants.py
bridge_service/eps/schema.py
bridge_service/eps/simulator.py
```

This keeps the encoder and decoder aligned.
