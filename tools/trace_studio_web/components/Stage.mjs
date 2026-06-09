// Stage — the still viewer for the selected label: A|B side-by-side, single
// side, or amplified diff; pixel zoom (nearest-neighbour).
import { html } from "/vendor/htm-preact-standalone.mjs";
import { BUST } from "/store.mjs";

export function ModeBar({ mode, setMode, zoom, setZoom }) {
  const B = (m, lbl) => html`<button class=${mode === m ? "on" : ""}
      onClick=${() => setMode(m)}>${lbl}</button>`;
  return html`<div class="modebar">
    ${B("ab", "A|B")} ${B("retail", "retail")} ${B("port", "port")} ${B("diff", "diff")}
    <button class=${zoom === 2 ? "on" : ""}
      onClick=${() => setZoom(zoom === 1 ? 2 : 1)}>2×</button>
  </div>`;
}

export function Stage({ sess, cap, mode, setMode, zoom, setZoom }) {
  if (!cap) return html`<div class="pad">no captures</div>`;
  const lb = cap.label;
  const u = (side) => `/s/${sess}/${side}/frames/${lb}.png?b=${BUST}`;
  const du = `/s/${sess}/diff/${lb}.png?b=${BUST}`;
  const d = cap.diff || {};
  const img = (src, label) => html`<figure class="shot z${zoom}">
    <figcaption>${label}</figcaption>
    <img src=${src} onError=${e => { e.target.closest("figure").classList.add("dead"); }} />
  </figure>`;
  return html`<div class="stage">
    <div class="stagehead">
      <span class="lbl">${lb}</span>
      <${ModeBar} mode=${mode} setMode=${setMode} zoom=${zoom} setZoom=${setZoom} />
      <span class="stats">
        ${d.n_px != null ? `${d.n_px}px (${d.pct}%) mean ${d.mean} max ${d.max}` : (d.status || "")}
        ${cap.port ? ` · port f${cap.port.frame}` : " · port —"}
        ${cap.retail ? ` · retail t${cap.retail.frame}` : " · retail —"}
      </span>
    </div>
    <div class="shots">
      ${(mode === "ab" || mode === "retail") && cap.retail ? img(u("retail"), "retail") : null}
      ${(mode === "ab" || mode === "port") && cap.port ? img(u("port"), "port") : null}
      ${mode === "diff" ? (d.file ? img(du, `diff ×amp`) : html`<div class="pad">no diff (${d.status || "identical"})</div>`) : null}
    </div>
  </div>`;
}
