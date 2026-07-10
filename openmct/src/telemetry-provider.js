import {
  dictionary,
  dictionaryByObjectKey,
  folderObject,
  futureSubsystems,
  NAMESPACE,
  pointObject,
  pointState,
  pointValue,
  ROOT_KEY,
  subsystemFolders,
} from "@/dictionary.js";
import { formatValue } from "@/format.js";
import { realtimeClient } from "@/realtime.js";

export class AetherFlowObjectProvider {
  constructor() {
    this.objects = new Map();
    this.buildObjects();
  }

  get(identifier) {
    return Promise.resolve(this.objects.get(identifier.key));
  }

  buildObjects() {
    this.objects.set(ROOT_KEY, {
      identifier: { namespace: NAMESPACE, key: ROOT_KEY },
      name: "AetherFlow Local Demo",
      type: "folder",
      location: "ROOT",
    });

    for (const folder of subsystemFolders) {
      this.objects.set(folder.key, folderObject(folder.key, folder.name));
    }

    for (const subsystem of futureSubsystems) {
      this.objects.set(`future.${subsystem.key}`, {
        identifier: { namespace: NAMESPACE, key: `future.${subsystem.key}` },
        name: subsystem.name,
        type: "folder",
        location: `${NAMESPACE}:future`,
      });
    }

    for (const point of dictionary) {
      this.objects.set(point.objectKey, pointObject(point));
    }
  }
}

export class AetherFlowCompositionProvider {
  appliesTo(domainObject) {
    return domainObject.identifier?.namespace === NAMESPACE;
  }

  load(domainObject) {
    const key = domainObject.identifier.key;

    if (key === ROOT_KEY) {
      return Promise.resolve(
        subsystemFolders.map((folder) => ({
          namespace: NAMESPACE,
          key: folder.key,
        })),
      );
    }

    if (key === "future") {
      return Promise.resolve(
        futureSubsystems.map((subsystem) => ({
          namespace: NAMESPACE,
          key: `future.${subsystem.key}`,
        })),
      );
    }

    const childPoints = dictionary
      .filter((point) => point.subsystem === key)
      .map((point) => ({ namespace: NAMESPACE, key: point.objectKey }));

    return Promise.resolve(childPoints);
  }
}

export class AetherFlowTelemetryProvider {
  supportsRequest(domainObject) {
    return dictionaryByObjectKey.has(domainObject.identifier?.key);
  }

  supportsSubscribe(domainObject) {
    return dictionaryByObjectKey.has(domainObject.identifier?.key);
  }

  request(domainObject, options = {}) {
    const point = dictionaryByObjectKey.get(domainObject.identifier.key);
    if (!point) {
      return Promise.resolve([]);
    }

    const start = options.start ?? Number.NEGATIVE_INFINITY;
    const end = options.end ?? Number.POSITIVE_INFINITY;
    const rows = realtimeClient
      .getHistory()
      .filter(
        (packet) => packet.timestamp_ms >= start && packet.timestamp_ms <= end,
      )
      .map((packet) => this.toDatum(point, packet))
      .filter(Boolean);

    return Promise.resolve(rows);
  }

  subscribe(domainObject, callback) {
    const point = dictionaryByObjectKey.get(domainObject.identifier.key);
    if (!point) {
      return () => {};
    }

    return realtimeClient.subscribe((event) => {
      const datum = this.toDatum(point, event.detail);
      if (datum) {
        callback(datum);
      }
    });
  }

  toDatum(point, packet) {
    const context = realtimeClient.context();
    const value = pointValue(point, packet, context);
    const state = pointState(point, value, context);

    if (value === undefined) {
      return null;
    }

    return {
      id: point.key,
      timestamp: packet?.timestamp_ms ?? Date.now(),
      value,
      formatted: point.display
        ? point.display(value)
        : formatValue(value, point.format, point.units),
      state,
      packet,
    };
  }
}
