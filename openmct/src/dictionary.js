import { decodeStatusFlags, formatAge } from "@/format.js";
import { connectionState, telemetryState } from "@/status.js";

export const NAMESPACE = "aetherflow.telemetry";
export const ROOT_KEY = "aetherflow.root";

export const subsystemFolders = [
  { key: "mission", name: "Mission" },
  { key: "eps", name: "EPS" },
  { key: "raw", name: "Raw" },
  { key: "future", name: "Future Subsystems" },
];

export const futureSubsystems = [
  { key: "obc", name: "OBC" },
  { key: "adcs", name: "ADCS" },
  { key: "comms", name: "COMMS" },
  { key: "thermal", name: "Thermal" },
];

const telemetryPoints = [
  {
    key: "connection.status",
    name: "Connection Status",
    subsystem: "mission",
    type: "telemetry.telemetry",
    format: "string",
    source: (_packet, context) => context.connectionStatus,
    state: (_value, _point, context) =>
      connectionState(context.connectionStatus, context.latestPacket),
  },
  {
    key: "packet.age_ms",
    name: "Last Packet Age",
    subsystem: "mission",
    type: "telemetry.telemetry",
    units: "ms",
    format: "integer",
    source: (_packet, context) =>
      context.latestPacket
        ? Date.now() - context.latestPacket.timestamp_ms
        : undefined,
    display: (value) => formatAge(value),
    state: (value, point, context) =>
      telemetryState(value, point.limits, context.latestPacket),
    limits: { warningHigh: 3000, criticalHigh: 10000 },
  },
  {
    key: "packet.counter",
    name: "Packet Counter",
    subsystem: "mission",
    type: "telemetry.telemetry",
    format: "integer",
    source: (_packet, context) => context.packetCounter,
  },
  {
    key: "packet.gap_count",
    name: "Packet Gap Count",
    subsystem: "mission",
    type: "telemetry.telemetry",
    format: "integer",
    source: (_packet, context) => context.gapCount,
    limits: { warningHigh: 1, criticalHigh: 10 },
  },
  {
    key: "packet.rate_hz",
    name: "Packet Rate",
    subsystem: "mission",
    type: "telemetry.telemetry",
    units: "Hz",
    format: "float2",
    source: (_packet, context) => context.packetRateHz,
  },
  {
    key: "packet.node",
    name: "Node",
    subsystem: "mission",
    type: "telemetry.telemetry",
    format: "integer",
    source: "node",
  },
  {
    key: "packet.service",
    name: "Service",
    subsystem: "mission",
    type: "telemetry.telemetry",
    format: "integer",
    source: "service",
  },
  {
    key: "packet.subtype",
    name: "Subtype",
    subsystem: "mission",
    type: "telemetry.telemetry",
    format: "integer",
    source: "subtype",
  },
  {
    key: "packet.timestamp_ms",
    name: "Bridge Timestamp",
    subsystem: "mission",
    type: "telemetry.telemetry",
    format: "integer",
    source: "timestamp_ms",
  },
  {
    key: "eps.sequence",
    name: "Sequence",
    subsystem: "eps",
    type: "telemetry.telemetry",
    format: "integer",
    source: "sequence",
  },
  {
    key: "eps.state",
    name: "State",
    subsystem: "eps",
    type: "telemetry.telemetry",
    format: "string",
    source: "state",
    state: (value) => (value === "UNKNOWN" ? "unknown" : "nominal"),
  },
  {
    key: "eps.bus_voltage_mv",
    name: "Bus Voltage",
    subsystem: "eps",
    type: "telemetry.telemetry",
    units: "mV",
    format: "integer",
    source: "bus_voltage_mv",
    limits: {
      warningLow: 4900,
      criticalLow: 4700,
      warningHigh: 5300,
      criticalHigh: 5500,
    },
  },
  {
    key: "eps.bus_voltage_v",
    name: "Bus Voltage",
    subsystem: "eps",
    type: "telemetry.telemetry",
    units: "V",
    format: "float2",
    source: (packet) => packet.bus_voltage_mv / 1000,
    limits: {
      warningLow: 4.9,
      criticalLow: 4.7,
      warningHigh: 5.3,
      criticalHigh: 5.5,
    },
  },
  {
    key: "eps.bus_current_ma",
    name: "Bus Current",
    subsystem: "eps",
    type: "telemetry.telemetry",
    units: "mA",
    format: "integer",
    source: "bus_current_ma",
    limits: { warningHigh: 700, criticalHigh: 900 },
  },
  {
    key: "eps.bus_current_a",
    name: "Bus Current",
    subsystem: "eps",
    type: "telemetry.telemetry",
    units: "A",
    format: "float2",
    source: (packet) => packet.bus_current_ma / 1000,
    limits: { warningHigh: 0.7, criticalHigh: 0.9 },
  },
  {
    key: "eps.power_w",
    name: "Bus Power",
    subsystem: "eps",
    type: "telemetry.telemetry",
    units: "W",
    format: "float2",
    source: (packet) =>
      (packet.bus_voltage_mv / 1000) * (packet.bus_current_ma / 1000),
  },
  {
    key: "eps.battery_percent",
    name: "Battery",
    subsystem: "eps",
    type: "telemetry.telemetry",
    units: "%",
    format: "integer",
    source: "battery_percent",
    limits: { warningLow: 30, criticalLow: 20 },
  },
  {
    key: "eps.temperature_cdeg",
    name: "Temperature",
    subsystem: "eps",
    type: "telemetry.telemetry",
    units: "cdegC",
    format: "integer",
    source: "temperature_cdeg",
    limits: { warningHigh: 5000, criticalHigh: 6000 },
  },
  {
    key: "eps.temperature_c",
    name: "Temperature",
    subsystem: "eps",
    type: "telemetry.telemetry",
    units: "°C",
    format: "float1",
    source: (packet) => packet.temperature_cdeg / 100,
    limits: { warningHigh: 50, criticalHigh: 60 },
  },
  {
    key: "eps.status_flags",
    name: "Status Flags",
    subsystem: "eps",
    type: "telemetry.telemetry",
    format: "hex8",
    source: "status_flags",
    state: (value, _point, context) => {
      if (!context.latestPacket) return "unknown";
      if (!Number.isFinite(value)) return "invalid";
      if ((value & 0x04) !== 0) return "critical";
      if ((value & 0x03) !== 0) return "warning";
      return telemetryState(value, undefined, context.latestPacket);
    },
  },
  {
    key: "eps.status_flags_decoded",
    name: "Decoded Status Flags",
    subsystem: "eps",
    type: "telemetry.telemetry",
    format: "string",
    source: (packet) => decodeStatusFlags(packet.status_flags).join(", "),
  },
  {
    key: "raw.latest_json",
    name: "Latest Telemetry JSON",
    subsystem: "raw",
    type: "telemetry.telemetry",
    format: "json",
    source: (packet) => packet,
  },
];

export const dictionary = telemetryPoints.map((point) => ({
  ...point,
  objectKey: `point.${point.key}`,
}));

export const dictionaryByKey = new Map(
  dictionary.map((point) => [point.key, point]),
);
export const dictionaryByObjectKey = new Map(
  dictionary.map((point) => [point.objectKey, point]),
);

export function pointValue(point, packet, context) {
  if (
    !packet &&
    point.key !== "connection.status" &&
    !point.key.startsWith("packet.")
  ) {
    return undefined;
  }

  if (typeof point.source === "function") {
    return point.source(packet, context);
  }

  return packet?.[point.source];
}

export function pointState(point, value, context) {
  if (typeof point.state === "function") {
    return point.state(value, point, context);
  }

  return telemetryState(value, point.limits, context.latestPacket);
}

export function folderObject(key, name) {
  return {
    identifier: { namespace: NAMESPACE, key },
    name,
    type: "folder",
    location: "ROOT",
  };
}

export function pointObject(point) {
  return {
    identifier: { namespace: NAMESPACE, key: point.objectKey },
    name: point.name,
    type: "aetherflow.telemetry.point",
    location: `${NAMESPACE}:${point.subsystem}`,
    telemetry: {
      values: [
        {
          key: "timestamp",
          name: "Time",
          format: "utc",
          hints: { domain: 1 },
        },
        {
          key: "value",
          name: point.name,
          units: point.units,
          format:
            point.format === "integer" || point.format === "hex8"
              ? "integer"
              : "float",
          hints: { range: 1 },
        },
      ],
    },
  };
}
