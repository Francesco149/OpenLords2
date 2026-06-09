// GroupVideo — scrub view for a dense capture group: the group's mp4s
// (retail / port / diff) with a shared clock, plus a per-label diff ribbon
// (click → seek).  Ordinal k within the group ↔ video time (k+0.5)/fps.
import { html, useEffect, useRef, useState } from "/vendor/htm-preact-standalone.mjs";
import { BUST } from "/store.mjs";
import { ModeBar } from "/components/Stage.mjs";
import { statusOf } from "/components/CaptureStrip.mjs";

const FPS = 10;

export function GroupVideo({ sess, group, captures, sel, setSel, mode, setMode }) {
  const vidRef = useRef(null);
  const [zoom, setZoom] = useState(1);
  const start = captures.findIndex(c => c.group === group.name);
  const k = Math.max(0, sel - start);
  const n = group.labels.length;
  const side = mode === "ab" ? "retail" : mode;     // ab → pair below
  const cur = captures[sel] || {};

  // seek when selection changes
  useEffect(() => {
    const t = (k + 0.5) / FPS;
    for (const v of document.querySelectorAll(".gv video")) {
      if (Math.abs(v.currentTime - t) > 0.01) v.currentTime = t;
    }
  }, [k, mode]);

  const vurl = (s) => `/s/${sess}/${group.videos[s]}?b=${BUST}`;
  const vid = (s) => group.videos[s]
    ? html`<figure class="shot z${zoom}">
        <figcaption>${s}</figcaption>
        <video src=${vurl(s)} preload="auto" muted playsinline />
      </figure>`
    : html`<div class="pad">no ${s} video</div>`;

  const d = cur.diff || {};
  return html`<div class="stage gv">
    <div class="stagehead">
      <span class="lbl">${group.name} · ${cur.label} (${k + 1}/${n})</span>
      <${ModeBar} mode=${mode} setMode=${setMode} zoom=${zoom} setZoom=${setZoom} />
      <span class="stats">${d.n_px != null ? `${d.n_px}px (${d.pct}%)` : ""}</span>
    </div>
    <div class="shots">
      ${mode === "ab" ? [vid("retail"), vid("port")] : vid(side)}
    </div>
    <div class="ribbon">
      ${group.labels.map((lb, i) => {
        const c = captures[start + i] || {};
        return html`<div class="rb st-${statusOf(c)} ${i === k ? "sel" : ""}"
          title="${lb}${c.diff && c.diff.n_px != null ? ` · ${c.diff.n_px}px` : ""}"
          style="height:${barH(c)}px"
          onClick=${() => setSel(start + i)} />`;
      })}
    </div>
  </div>`;
}

function barH(c) {
  const d = c.diff || {};
  if (d.n_px == null) return 4;
  return 4 + Math.min(36, Math.round(36 * (d.pct || 0) / 100 * 3));
}
