// StatePanel — the selected capture's save-schema state diff (the sim-state
// oracle: scalar slots by value, big blocks by FNV hash).
import { html } from "/vendor/htm-preact-standalone.mjs";

export function StatePanel({ cap }) {
  const st = cap && cap.state;
  return html`<div class="panel">
    <h3>sim state (save schema)</h3>
    ${!cap ? html`<div class="kv">no capture selected</div>`
      : !st ? html`<div class="kv">no state pair for ${cap.label}</div>`
      : st.n_diff === 0
        ? html`<div class="kv ok">STATE-CLEAN — all ${st.n_slots} slots equal</div>`
        : html`
          <div class="kv bad">${st.n_diff}/${st.n_slots} slots differ
            ${st.len_mismatch ? ` · LEN MISMATCH ${st.len_mismatch.join("≠")}` : ""}
            ${st.n_diff > st.diffs.length ? ` (first ${st.diffs.length} shown)` : ""}</div>
          <table>
            <tr><th>#</th><th>slot</th><th>port</th><th>retail</th></tr>
            ${st.diffs.map(d => html`<tr>
              <td>${d.i}</td><td>${d.name}</td>
              <td>${fmt(d.port)}</td><td>${fmt(d.retail)}</td>
            </tr>`)}
          </table>`}
  </div>`;
}

function fmt(v) {
  if (v == null) return "∅";
  if (typeof v === "number" && v > 9999) return `${v} (0x${v.toString(16)})`;
  return String(v);
}
