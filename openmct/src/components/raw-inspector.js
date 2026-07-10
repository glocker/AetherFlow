import cssText from "@/components/raw-inspector.css";
import { applyComponentStyles } from "@/components/styles.js";
import { downloadText, formatAge, toCsv } from "@/format.js";
import { realtimeClient } from "@/realtime.js";

export class RawInspector extends HTMLElement {
  constructor() {
    super();
    this.unsubscribe = null;
    this.interval = null;
    this.attachShadow({ mode: "open" });
    applyComponentStyles(this.shadowRoot, cssText);
    this.shadowRoot.append(this.createTemplate());
  }

  connectedCallback() {
    this.shadowRoot
      .querySelector("[data-export-json]")
      .addEventListener("click", this.exportJson);
    this.shadowRoot
      .querySelector("[data-export-csv]")
      .addEventListener("click", this.exportCsv);
    this.unsubscribe = realtimeClient.subscribeStatus(() => this.render());
    this.interval = window.setInterval(() => this.render(), 1000);
    this.render();
  }

  disconnectedCallback() {
    this.shadowRoot
      .querySelector("[data-export-json]")
      .removeEventListener("click", this.exportJson);
    this.shadowRoot
      .querySelector("[data-export-csv]")
      .removeEventListener("click", this.exportCsv);
    this.unsubscribe?.();
    this.unsubscribe = null;
    window.clearInterval(this.interval);
  }

  createTemplate() {
    const section = document.createElement("section");
    section.className = "inspector";
    section.part = "inspector";

    const actions = document.createElement("div");
    actions.className = "actions";
    actions.part = "actions";

    const exportJson = document.createElement("button");
    exportJson.className = "button";
    exportJson.type = "button";
    exportJson.dataset.exportJson = "";
    exportJson.textContent = "Export JSON";

    const exportCsv = document.createElement("button");
    exportCsv.className = "button";
    exportCsv.type = "button";
    exportCsv.dataset.exportCsv = "";
    exportCsv.textContent = "Export CSV";

    const age = document.createElement("span");
    age.className = "age";
    age.append("Latest packet age: ");

    const ageValue = document.createElement("strong");
    ageValue.dataset.age = "";
    ageValue.textContent = "—";
    age.appendChild(ageValue);

    const output = document.createElement("pre");
    output.textContent = "No telemetry received yet.";

    actions.append(exportJson, exportCsv, age);
    section.append(actions, output);
    return section;
  }

  exportJson = () => {
    downloadText(
      "aetherflow-telemetry.json",
      JSON.stringify(realtimeClient.getHistory(), null, 2),
      "application/json",
    );
  };

  exportCsv = () => {
    downloadText(
      "aetherflow-telemetry.csv",
      toCsv(realtimeClient.getHistory()),
      "text/csv",
    );
  };

  render() {
    const latestPacket = realtimeClient.context().latestPacket;
    const pre = this.shadowRoot.querySelector("pre");
    const age = this.shadowRoot.querySelector("[data-age]");

    pre.textContent = latestPacket
      ? JSON.stringify(latestPacket, null, 2)
      : "No telemetry received yet.";
    age.textContent = latestPacket
      ? formatAge(Date.now() - latestPacket.timestamp_ms)
      : "—";
  }
}

export function defineRawInspector() {
  if (!customElements.get("raw-inspector")) {
    customElements.define("raw-inspector", RawInspector);
  }
}
