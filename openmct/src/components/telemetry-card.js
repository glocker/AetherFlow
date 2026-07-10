import cssText from "@/components/telemetry-card.css";
import { defineStateBadge } from "@/components/state-badge.js";
import { applyComponentStyles } from "@/components/styles.js";
import { TelemetryState } from "@/status.js";

export class TelemetryCard extends HTMLElement {
  static observedAttributes = ["title", "value", "state"];

  constructor() {
    super();
    defineStateBadge();
    this.attachShadow({ mode: "open" });
    applyComponentStyles(this.shadowRoot, cssText);
    this.shadowRoot.append(this.createTemplate());
  }

  get title() {
    return this.getAttribute("title") || "";
  }

  set title(value) {
    this.setAttribute("title", value ?? "");
  }

  get value() {
    return this.getAttribute("value") || "—";
  }

  set value(value) {
    this.setAttribute("value", value ?? "—");
  }

  get state() {
    return this.getAttribute("state") || TelemetryState.UNKNOWN;
  }

  set state(value) {
    this.setAttribute("state", value || TelemetryState.UNKNOWN);
  }

  connectedCallback() {
    this.render();
  }

  attributeChangedCallback() {
    this.render();
  }

  createTemplate() {
    const card = document.createElement("article");
    card.className = "card";
    card.part = "card";

    const title = document.createElement("div");
    title.className = "title";
    title.part = "title";

    const value = document.createElement("div");
    value.className = "value";
    value.part = "value";

    const badge = document.createElement("state-badge");
    value.appendChild(badge);
    card.append(title, value);
    return card;
  }

  render() {
    if (!this.shadowRoot) return;

    this.shadowRoot.querySelector(".title").textContent = this.title;

    const badge = this.shadowRoot.querySelector("state-badge");
    badge.state = this.state;
    badge.title = this.value;
  }
}

export function defineTelemetryCard() {
  if (!customElements.get("telemetry-card")) {
    customElements.define("telemetry-card", TelemetryCard);
  }
}
