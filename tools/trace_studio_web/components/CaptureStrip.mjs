// CaptureStrip — the filmstrip/selector: one cell per capture in trace order,
// colored by diff status; group members render as thin cells under a bracket.
import { html } from "/vendor/htm-preact-standalone.mjs";

export function statusOf(c) {
  const d = c.diff || {};
  if (!d.status) return c.port ? "portonly" : "void";
  if (d.status === "identical") return "identical";
  if (d.status === "clean") return "clean";
  if (d.status === "diff") return d.pct > 5 ? "big" : "small";
  return "missing";          // missing_* / size_mismatch
}

export function CaptureStrip({ captures, sel, setSel }) {
  // bracket runs of the same group
  const items = [];
  let i = 0;
  while (i < captures.length) {
    const c = captures[i];
    if (!c.group) {
      items.push({ kind: "single", idx: i, c });
      i++;
    } else {
      const start = i;
      while (i < captures.length && captures[i].group === c.group) i++;
      items.push({ kind: "group", name: c.group, start, end: i });
    }
  }
  return html`<div class="strip">
    ${items.map(it => it.kind === "single"
      ? html`<div class="cell single st-${statusOf(it.c)} ${sel === it.idx ? "sel" : ""}"
               title="${it.c.label}${fmtDiff(it.c)}"
               onClick=${() => setSel(it.idx)}>${it.c.label}</div>`
      : html`<div class="grp">
          <div class="grpname">${it.name} (${it.end - it.start})</div>
          <div class="grpcells">
            ${captures.slice(it.start, it.end).map((c, k) =>
              html`<div class="cell thin st-${statusOf(c)} ${sel === it.start + k ? "sel" : ""}"
                     title="${c.label}${fmtDiff(c)}"
                     onClick=${() => setSel(it.start + k)} />`)}
          </div>
        </div>`)}
  </div>`;
}

function fmtDiff(c) {
  const d = c.diff || {};
  return d.n_px != null ? ` · ${d.n_px}px ${d.pct}%` : "";
}
