import { validatePacket } from "@/status.js";

const DEFAULT_RING_BUFFER_SIZE = 2000;
const DEFAULT_WS_PATH = "/realtime";
const DEFAULT_HTTP_LATEST_PATH = "/telemetry/latest";

function bridgeBaseUrl() {
  const params = new URLSearchParams(window.location.search);
  return params.get("bridge") || window.location.origin;
}

function wsUrlFromBase(baseUrl) {
  const url = new URL(DEFAULT_WS_PATH, baseUrl);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  return url.toString();
}

function httpLatestUrlFromBase(baseUrl) {
  return new URL(DEFAULT_HTTP_LATEST_PATH, baseUrl).toString();
}

export class RealtimeClient extends EventTarget {
  constructor(options = {}) {
    super();
    const baseUrl = options.baseUrl || bridgeBaseUrl();

    this.baseUrl = baseUrl;
    this.wsUrl = options.wsUrl || wsUrlFromBase(baseUrl);
    this.latestUrl = options.latestUrl || httpLatestUrlFromBase(baseUrl);
    this.ringBufferSize = options.ringBufferSize || DEFAULT_RING_BUFFER_SIZE;

    this.socket = null;
    this.connectionStatus = "disconnected";
    this.latestPacket = null;
    this.latestRawJson = null;
    this.history = [];
    this.packetCounter = 0;
    this.gapCount = 0;
    this.lastSequence = null;
    this.reconnectCount = 0;
    this.reconnectTimer = null;
    this.closedByUser = false;
    this.packetRateHz = 0;
    this.packetRateWindow = [];
    this.events = [];
  }

  async connect() {
    this.closedByUser = false;
    this.setConnectionStatus("connecting");
    await this.fetchLatestSnapshot();
    this.openSocket();
  }

  disconnect() {
    this.closedByUser = true;
    clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;

    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }

    this.setConnectionStatus("disconnected");
  }

  subscribe(callback) {
    this.addEventListener("sample", callback);
    this.emitCurrentSample(callback);

    return () => this.removeEventListener("sample", callback);
  }

  subscribeStatus(callback) {
    this.addEventListener("status", callback);
    callback(new CustomEvent("status", { detail: this.context() }));

    return () => this.removeEventListener("status", callback);
  }

  subscribeEvents(callback) {
    this.addEventListener("event-log", callback);
    callback(new CustomEvent("event-log", { detail: this.events.slice() }));

    return () => this.removeEventListener("event-log", callback);
  }

  getHistory() {
    return this.history.slice();
  }

  context() {
    return {
      connectionStatus: this.connectionStatus,
      latestPacket: this.latestPacket,
      packetCounter: this.packetCounter,
      gapCount: this.gapCount,
      lastSequence: this.lastSequence,
      reconnectCount: this.reconnectCount,
      packetRateHz: this.packetRateHz,
      latestRawJson: this.latestRawJson,
    };
  }

  async fetchLatestSnapshot() {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2000);

    try {
      const response = await fetch(this.latestUrl, {
        signal: controller.signal,
      });
      if (!response.ok) {
        this.addEvent(
          "warning",
          `latest snapshot failed: HTTP ${response.status}`,
        );
        return;
      }

      const packet = await response.json();
      if (packet.valid === false) {
        this.addEvent("info", "bridge has no latest telemetry yet");
        return;
      }

      this.acceptPacket(packet, "snapshot");
    } catch (error) {
      this.addEvent("warning", `latest snapshot unavailable: ${error.message}`);
    } finally {
      clearTimeout(timeout);
    }
  }

  openSocket() {
    clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;

    this.socket = new WebSocket(this.wsUrl);

    this.socket.addEventListener("open", () => {
      this.setConnectionStatus("connected");
      this.reconnectCount = 0;
      this.addEvent("info", "WebSocket connected");
    });

    this.socket.addEventListener("message", (event) => {
      try {
        this.acceptPacket(JSON.parse(event.data), "realtime");
      } catch (error) {
        this.addEvent("invalid", `invalid WebSocket JSON: ${error.message}`);
        this.dispatchEvent(new CustomEvent("invalid", { detail: error }));
      }
    });

    this.socket.addEventListener("error", () => {
      this.setConnectionStatus("error");
      this.addEvent("warning", "WebSocket error");
    });

    this.socket.addEventListener("close", () => {
      this.socket = null;
      if (this.closedByUser) {
        this.setConnectionStatus("disconnected");
        return;
      }

      this.setConnectionStatus("reconnecting");
      this.scheduleReconnect();
    });
  }

  scheduleReconnect() {
    this.reconnectCount += 1;
    const delayMs = Math.min(
      1000 * 2 ** Math.min(this.reconnectCount - 1, 5),
      15000,
    );
    this.addEvent("warning", `WebSocket reconnect scheduled in ${delayMs} ms`);

    this.reconnectTimer = setTimeout(() => {
      if (!this.closedByUser) {
        this.openSocket();
      }
    }, delayMs);
  }

  acceptPacket(packet, source) {
    const validation = validatePacket(packet);
    if (!validation.valid) {
      this.addEvent("invalid", validation.reason);
      this.dispatchEvent(new CustomEvent("invalid", { detail: validation }));
      return;
    }

    const clientReceivedAt = Date.now();
    const enrichedPacket = {
      ...packet,
      client_received_at: clientReceivedAt,
      source,
    };

    this.latestPacket = enrichedPacket;
    this.latestRawJson = JSON.stringify(packet, null, 2);
    this.packetCounter +=
      source === "snapshot" && this.packetCounter > 0 ? 0 : 1;
    this.detectGaps(enrichedPacket.sequence);
    this.updateRate(clientReceivedAt);
    this.pushHistory(enrichedPacket);
    this.dispatchEvent(new CustomEvent("sample", { detail: enrichedPacket }));
    this.dispatchEvent(new CustomEvent("status", { detail: this.context() }));
  }

  detectGaps(sequence) {
    if (this.lastSequence === null) {
      this.lastSequence = sequence;
      return;
    }

    const expected = (this.lastSequence + 1) & 0xffff;
    if (sequence !== expected) {
      const delta =
        sequence > this.lastSequence
          ? sequence - this.lastSequence
          : 65536 - this.lastSequence + sequence;
      const missed = Math.max(0, delta - 1);
      this.gapCount += missed;
      this.addEvent(
        "warning",
        `sequence gap: expected ${expected}, got ${sequence}, missed ${missed}`,
      );
    }

    this.lastSequence = sequence;
  }

  updateRate(receivedAt) {
    this.packetRateWindow.push(receivedAt);
    const cutoff = receivedAt - 10000;
    this.packetRateWindow = this.packetRateWindow.filter(
      (timestamp) => timestamp >= cutoff,
    );

    if (this.packetRateWindow.length < 2) {
      this.packetRateHz = 0;
      return;
    }

    const elapsedSeconds =
      (this.packetRateWindow.at(-1) - this.packetRateWindow[0]) / 1000;
    this.packetRateHz =
      elapsedSeconds > 0
        ? (this.packetRateWindow.length - 1) / elapsedSeconds
        : 0;
  }

  pushHistory(packet) {
    this.history.push(packet);
    if (this.history.length > this.ringBufferSize) {
      this.history.splice(0, this.history.length - this.ringBufferSize);
    }
  }

  setConnectionStatus(status) {
    if (this.connectionStatus === status) {
      return;
    }

    this.connectionStatus = status;
    this.dispatchEvent(new CustomEvent("status", { detail: this.context() }));
  }

  addEvent(level, message) {
    const entry = {
      timestamp: Date.now(),
      level,
      message,
    };

    this.events.unshift(entry);
    if (this.events.length > 100) {
      this.events.pop();
    }

    this.dispatchEvent(
      new CustomEvent("event-log", { detail: this.events.slice() }),
    );
  }

  emitCurrentSample(callback) {
    if (this.latestPacket) {
      callback(new CustomEvent("sample", { detail: this.latestPacket }));
    }
  }
}

export const realtimeClient = new RealtimeClient();
