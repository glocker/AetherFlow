export const TelemetryState = Object.freeze({
  UNKNOWN: 'unknown',
  INVALID: 'invalid',
  STALE: 'stale',
  CRITICAL: 'critical',
  WARNING: 'warning',
  NOMINAL: 'nominal'
});

export const STALE_AFTER_MS = 3000;
export const DEAD_AFTER_MS = 10000;

const STATE_PRIORITY = new Map([
  [TelemetryState.INVALID, 60],
  [TelemetryState.UNKNOWN, 50],
  [TelemetryState.STALE, 40],
  [TelemetryState.CRITICAL, 30],
  [TelemetryState.WARNING, 20],
  [TelemetryState.NOMINAL, 10]
]);

export function mostImportantState(states) {
  return states.reduce((selected, state) => {
    const selectedPriority = STATE_PRIORITY.get(selected) ?? 0;
    const statePriority = STATE_PRIORITY.get(state) ?? 0;
    return statePriority > selectedPriority ? state : selected;
  }, TelemetryState.NOMINAL);
}

export function validatePacket(packet) {
  if (!packet || typeof packet !== 'object') {
    return { valid: false, reason: 'packet is not an object' };
  }

  const requiredNumberFields = [
    'node',
    'service',
    'subtype',
    'sequence',
    'bus_voltage_mv',
    'bus_current_ma',
    'battery_percent',
    'temperature_cdeg',
    'status_flags',
    'timestamp_ms'
  ];

  for (const field of requiredNumberFields) {
    if (typeof packet[field] !== 'number' || !Number.isFinite(packet[field])) {
      return { valid: false, reason: `missing or invalid numeric field: ${field}` };
    }
  }

  if (typeof packet.state !== 'string' || packet.state.length === 0) {
    return { valid: false, reason: 'missing or invalid state field' };
  }

  if (packet.battery_percent < 0 || packet.battery_percent > 100) {
    return { valid: false, reason: 'battery_percent is outside 0..100' };
  }

  if (packet.sequence < 0 || packet.sequence > 65535) {
    return { valid: false, reason: 'sequence is outside uint16 range' };
  }

  return { valid: true, reason: null };
}

export function freshnessState(latestPacket, now = Date.now()) {
  if (!latestPacket) {
    return TelemetryState.UNKNOWN;
  }

  const ageMs = now - latestPacket.timestamp_ms;
  if (!Number.isFinite(ageMs) || ageMs < 0) {
    return TelemetryState.INVALID;
  }

  return ageMs > STALE_AFTER_MS ? TelemetryState.STALE : TelemetryState.NOMINAL;
}

export function limitState(value, limits) {
  if (value === undefined || value === null) {
    return TelemetryState.UNKNOWN;
  }

  if (typeof value !== 'number' || !Number.isFinite(value)) {
    return TelemetryState.INVALID;
  }

  if (!limits) {
    return TelemetryState.NOMINAL;
  }

  if (limits.criticalLow !== undefined && value <= limits.criticalLow) {
    return TelemetryState.CRITICAL;
  }

  if (limits.criticalHigh !== undefined && value >= limits.criticalHigh) {
    return TelemetryState.CRITICAL;
  }

  if (limits.warningLow !== undefined && value <= limits.warningLow) {
    return TelemetryState.WARNING;
  }

  if (limits.warningHigh !== undefined && value >= limits.warningHigh) {
    return TelemetryState.WARNING;
  }

  return TelemetryState.NOMINAL;
}

export function telemetryState(value, limits, latestPacket, now = Date.now()) {
  return mostImportantState([
    freshnessState(latestPacket, now),
    limitState(value, limits)
  ]);
}

export function connectionState(socketState, latestPacket, now = Date.now()) {
  if (socketState === 'invalid') {
    return TelemetryState.INVALID;
  }

  if (!latestPacket) {
    return socketState === 'connected' ? TelemetryState.UNKNOWN : TelemetryState.STALE;
  }

  const ageMs = now - latestPacket.timestamp_ms;
  if (!Number.isFinite(ageMs) || ageMs < 0) {
    return TelemetryState.INVALID;
  }

  if (ageMs > DEAD_AFTER_MS) {
    return TelemetryState.STALE;
  }

  if (socketState !== 'connected') {
    return TelemetryState.WARNING;
  }

  return ageMs > STALE_AFTER_MS ? TelemetryState.STALE : TelemetryState.NOMINAL;
}

export function stateLabel(state) {
  switch (state) {
    case TelemetryState.NOMINAL:
      return 'Nominal';
    case TelemetryState.WARNING:
      return 'Warning';
    case TelemetryState.CRITICAL:
      return 'Critical';
    case TelemetryState.STALE:
      return 'Stale';
    case TelemetryState.UNKNOWN:
      return 'Unknown';
    case TelemetryState.INVALID:
      return 'Invalid';
    default:
      return 'Unknown';
  }
}
