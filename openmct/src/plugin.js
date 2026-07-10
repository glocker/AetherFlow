import openmct from "openmct";

import { defineDashboardComponents } from "@/components/index.js";
import {
  dictionaryByKey,
  NAMESPACE,
  pointState,
  pointValue,
  ROOT_KEY,
} from "@/dictionary.js";
import { formatValue } from "@/format.js";
import { realtimeClient } from "@/realtime.js";
import {
  AetherFlowCompositionProvider,
  AetherFlowObjectProvider,
  AetherFlowTelemetryProvider,
} from "@/telemetry-provider.js";

function createStatusView() {
  let container;
  let unsubscribeStatus;
  let interval;

  const cards = new Map();

  const cardDefinitions = [
    ["connection.status", "Connection"],
    ["packet.age_ms", "Last Packet Age"],
    ["packet.counter", "Packets"],
    ["packet.gap_count", "Gaps"],
    ["packet.rate_hz", "Rate"],
    ["eps.state", "EPS State"],
    ["eps.bus_voltage_v", "Voltage"],
    ["eps.bus_current_a", "Current"],
    ["eps.battery_percent", "Battery"],
    ["eps.temperature_c", "Temperature"],
    ["eps.status_flags_decoded", "Flags"],
  ];

  function updateCards() {
    const context = realtimeClient.context();
    const packet = context.latestPacket;

    for (const [key] of cardDefinitions) {
      const point = dictionaryByKey.get(key);
      const card = cards.get(key);
      if (!point || !card) continue;

      const value = pointValue(point, packet, context);
      const state = pointState(point, value, context);
      const formatted = point.display
        ? point.display(value)
        : formatValue(value, point.format, point.units);
      card.value = formatted;
      card.state = state;
    }
  }

  return {
    show(element) {
      container = document.createElement("section");
      container.className = "dashboard-status-view";

      const grid = document.createElement("div");
      grid.className = "telemetry-card-grid";

      for (const [key, label] of cardDefinitions) {
        const card = document.createElement("telemetry-card");
        card.title = label;
        cards.set(key, card);
        grid.appendChild(card);
      }

      const events = document.createElement("event-log");

      container.append(grid, events);
      element.appendChild(container);

      unsubscribeStatus = realtimeClient.subscribeStatus(updateCards);
      interval = window.setInterval(updateCards, 1000);
      updateCards();
    },

    destroy() {
      unsubscribeStatus?.();
      window.clearInterval(interval);
      container?.remove();
    },
  };
}

function createRawView() {
  let container;

  return {
    show(element) {
      container = document.createElement("section");
      container.className = "dashboard-raw-view";
      container.appendChild(document.createElement("raw-inspector"));
      element.appendChild(container);
    },

    destroy() {
      container?.remove();
    },
  };
}

function AetherFlowPlugin() {
  return function install(openmctApi) {
    openmctApi.types.addType("aetherflow.telemetry.point", {
      name: "AetherFlow Telemetry Point",
      description: "Telemetry point from the AetherFlow EPS stream",
      cssClass: "icon-telemetry",
    });

    openmctApi.objects.addProvider(NAMESPACE, new AetherFlowObjectProvider());
    openmctApi.composition.addProvider(new AetherFlowCompositionProvider());
    openmctApi.telemetry.addProvider(new AetherFlowTelemetryProvider());
    openmctApi.objects.addRoot({ namespace: NAMESPACE, key: ROOT_KEY });

    openmctApi.objectViews.addProvider({
      key: "aetherflow.status",
      name: "AetherFlow Status",
      canView: (domainObject) =>
        domainObject.identifier?.namespace === NAMESPACE &&
        domainObject.identifier.key === ROOT_KEY,
      view: createStatusView,
    });

    openmctApi.objectViews.addProvider({
      key: "aetherflow.raw",
      name: "Raw Telemetry JSON",
      canView: (domainObject) =>
        domainObject.identifier?.namespace === NAMESPACE &&
        domainObject.identifier.key === "raw",
      view: createRawView,
    });
  };
}

defineDashboardComponents();

openmct.setAssetPath(__OPENMCT_ASSET_PATH__);
openmct.install(openmct.plugins.LocalStorage());
openmct.install(openmct.plugins.MyItems());
openmct.install(openmct.plugins.UTCTimeSystem());
openmct.install(openmct.plugins.Espresso());
openmct.install(AetherFlowPlugin());
openmct.start(document.getElementById("openmct-app"));

realtimeClient.connect();
