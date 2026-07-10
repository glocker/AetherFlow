import cssText from "@/components/state-badge.css";
import { applyComponentStyles } from "@/components/styles.js";
import { stateLabel, TelemetryState } from "@/status.js";

const validStates = new Set(Object.values(TelemetryState));

export class StateBadge extends HTMLElement {
  static observedAttributes = ["state", "title"];

  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    applyComponentStyles(this.shadowRoot, cssText);
    this.shadowRoot.append(this.createTemplate());
  }

  get state() {
    const state = this.getAttribute("state") || TelemetryState.UNKNOWN;
    return validStates.has(state) ? state : TelemetryState.UNKNOWN;
  }

  set state(value) {
    this.setAttribute("state", value || TelemetryState.UNKNOWN);
  }

  get title() {
    return this.getAttribute("title") || stateLabel(this.state);
  }

  set title(value) {
    if (value === undefined || value === null) {
      this.removeAttribute("title");
      return;
    }

    this.setAttribute("title", String(value));
  }

  connectedCallback() {
    this.render();
  }

  attributeChangedCallback() {
    this.render();
  }

  createTemplate() {
    const badge = document.createElement("span");
    badge.className = "badge";
    badge.part = "badge";

    const dot = document.createElement("span");
    dot.className = "dot";
    dot.part = "dot";

    const title = document.createElement("span");
    title.className = "title";
    title.part = "title";

    badge.append(dot, title);
    return badge;
  }

  render() {
    if (!this.shadowRoot) return;

    const state = this.state;
    const title = this.title;
    const titleElement = this.shadowRoot.querySelector(".title");

    this.style.setProperty("--state-color", `var(--dashboard-${state})`);
    titleElement.textContent = title;
    this.setAttribute("aria-label", `${stateLabel(state)}: ${title}`);
  }
}

export function defineStateBadge() {
  if (!customElements.get("state-badge")) {
    customElements.define("state-badge", StateBadge);
  }
}
