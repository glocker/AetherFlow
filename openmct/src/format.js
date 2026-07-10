import { stateLabel } from "@/status.js";

export function formatValue(value, format = "string", units = "") {
  if (value === undefined || value === null) {
    return "—";
  }

  let formatted;
  switch (format) {
    case "integer":
      formatted = Number.isFinite(value)
        ? Math.round(value).toString()
        : String(value);
      break;
    case "float1":
      formatted = Number.isFinite(value) ? value.toFixed(1) : String(value);
      break;
    case "float2":
      formatted = Number.isFinite(value) ? value.toFixed(2) : String(value);
      break;
    case "hex8":
      formatted = Number.isFinite(value)
        ? `0x${Number(value).toString(16).toUpperCase().padStart(2, "0")}`
        : String(value);
      break;
    case "json":
      formatted = JSON.stringify(value, null, 2);
      break;
    case "state":
      formatted = stateLabel(value);
      break;
    default:
      formatted = String(value);
      break;
  }

  return units ? `${formatted} ${units}` : formatted;
}

export function formatAge(ageMs) {
  if (!Number.isFinite(ageMs)) {
    return "—";
  }

  if (ageMs < 1000) {
    return `${Math.max(0, Math.round(ageMs))} ms`;
  }

  if (ageMs < 60000) {
    return `${(ageMs / 1000).toFixed(1)} s`;
  }

  return `${(ageMs / 60000).toFixed(1)} min`;
}

export function decodeStatusFlags(flags) {
  if (!Number.isFinite(flags)) {
    return ["INVALID"];
  }

  const decoded = [];
  if ((flags & 0x01) !== 0) decoded.push("SAFE_MODE");
  if ((flags & 0x02) !== 0) decoded.push("LOW_BATTERY");
  if ((flags & 0x04) !== 0) decoded.push("OVERTEMP");

  return decoded.length > 0 ? decoded : ["NONE"];
}

export function downloadText(filename, text, mimeType) {
  const blob = new Blob([text], { type: mimeType });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

export function toCsv(rows) {
  if (rows.length === 0) {
    return "";
  }

  const columns = [
    "client_received_at",
    "timestamp_ms",
    "node",
    "service",
    "subtype",
    "sequence",
    "state",
    "bus_voltage_mv",
    "bus_current_ma",
    "battery_percent",
    "temperature_cdeg",
    "status_flags",
  ];

  const escapeCell = (value) => {
    const stringValue =
      value === undefined || value === null ? "" : String(value);
    return /[",\n]/.test(stringValue)
      ? `"${stringValue.replaceAll('"', '""')}"`
      : stringValue;
  };

  return [
    columns.join(","),
    ...rows.map((row) =>
      columns.map((column) => escapeCell(row[column])).join(","),
    ),
  ].join("\n");
}
