import cssText from "@/components/event-log.css";
import { defineStateBadge } from "@/components/state-badge.js";
import { applyComponentStyles } from "@/components/styles.js";
import { realtimeClient } from "@/realtime.js";

function eventState(level) {
  if (level === "invalid") return "invalid";
  if (level === "warning") return "warning";
  return "nominal";
}

export class EventLog extends HTMLElement {
  constructor() {
    super();
    defineStateBadge();
    this.unsubscribe = null;
    this.attachShadow({ mode: "open" });
    applyComponentStyles(this.shadowRoot, cssText);
    this.shadowRoot.append(this.createTemplate());
  }

  connectedCallback() {
    this.unsubscribe = realtimeClient.subscribeEvents((event) =>
      this.render(event.detail),
    );
  }

  disconnectedCallback() {
    this.unsubscribe?.();
    this.unsubscribe = null;
  }

  createTemplate() {
    const section = document.createElement("section");
    section.className = "events";
    section.part = "events";

    const header = document.createElement("div");
    header.className = "header";
    header.part = "header";
    header.textContent = "Event / Alert Log";

    const list = document.createElement("ul");
    list.className = "list";
    list.part = "list";

    section.append(header, list);
    return section;
  }

  render(events = []) {
    const list = this.shadowRoot.querySelector(".list");
    list.replaceChildren();

    if (events.length === 0) {
      const empty = document.createElement("li");
      empty.className = "empty";
      empty.textContent = "No events yet.";
      list.appendChild(empty);
      return;
    }

    for (const event of events.slice(0, 30)) {
      const item = document.createElement("li");
      item.className = "item";

      const time = document.createElement("span");
      time.className = "time";
      time.textContent = new Date(event.timestamp).toLocaleTimeString();

      const level = document.createElement("state-badge");
      level.state = eventState(event.level);
      level.title = event.level;

      const message = document.createElement("span");
      message.className = "message";
      message.textContent = event.message;

      item.append(time, level, message);
      list.appendChild(item);
    }
  }
}

export function defineEventLog() {
  if (!customElements.get("event-log")) {
    customElements.define("event-log", EventLog);
  }
}
