// app.mjs — OpenLords2 trace studio SPA root (composition + selection state).
//
// The session model is label-keyed: manifest.captures[] in trace order, each
// {label, group, n, seg, port?, retail?, diff?}.  Selection = an index into
// that list.  Dense groups additionally carry per-group videos.
import { html, render, useEffect, useMemo, useState } from "/vendor/htm-preact-standalone.mjs";
import { qparam, useSession } from "/store.mjs";
import { SessionPicker } from "/components/SessionPicker.mjs";
import { CaptureStrip } from "/components/CaptureStrip.mjs";
import { Stage } from "/components/Stage.mjs";
import { GroupVideo } from "/components/GroupVideo.mjs";
import { AnchorPanel } from "/components/AnchorPanel.mjs";
import { StatePanel } from "/components/StatePanel.mjs";
import { JobTray } from "/components/JobTray.mjs";

const SESS = qparam("session");

function VerdictBadge({ v }) {
  if (!v) return null;
  const cls = { clean: "ok", "port-only": "info", missing: "warn", diff: "bad" }[v.status] || "info";
  return html`<span class="badge ${cls}">
    ${v.status}${v.n_diff != null ? ` · ${v.n_diff} differing` : ""}
    ${v.worst ? ` · worst ${v.worst.label} (${v.worst.pct}%)` : ""}
  </span>`;
}

function App() {
  const { manifest, loading, error, reload } = useSession(SESS);
  const [sel, setSel] = useState(0);
  const [mode, setMode] = useState("ab");        // ab | retail | port | diff
  const [zoom, setZoom] = useState(1);

  const captures = manifest?.captures || [];
  const cur = captures[Math.min(sel, Math.max(0, captures.length - 1))] || null;
  const group = useMemo(
    () => cur && cur.group
      ? (manifest.groups || []).find(g => g.name === cur.group) : null,
    [manifest, cur]);

  // keyboard: ←/→ select, 1/2/3/4 mode, z zoom
  useEffect(() => {
    const onKey = (e) => {
      if (e.target.tagName === "INPUT" || e.target.tagName === "SELECT") return;
      if (e.key === "ArrowLeft") { setSel(s => Math.max(0, s - 1)); e.preventDefault(); }
      if (e.key === "ArrowRight") { setSel(s => Math.min(captures.length - 1, s + 1)); e.preventDefault(); }
      if (e.key === "1") setMode("ab");
      if (e.key === "2") setMode("retail");
      if (e.key === "3") setMode("port");
      if (e.key === "4") setMode("diff");
      if (e.key === "z") setZoom(z => (z === 1 ? 2 : 1));
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [captures.length]);

  if (!SESS) return html`<div class="pad">no session — <a href="/">pick the default</a></div>`;
  if (loading) return html`<div class="pad">loading ${SESS}…</div>`;
  if (error) return html`<div class="pad">error: ${error}</div>`;

  return html`
    <div class="top">
      <${SessionPicker} current=${SESS} />
      <${VerdictBadge} v=${manifest.verdict} />
      <span class="meta">${manifest.n_port}/${manifest.n_captures} port ·
        ${manifest.n_retail}/${manifest.n_captures} retail</span>
      <${JobTray} sess=${SESS} onDone=${reload} />
    </div>
    <${CaptureStrip} captures=${captures} sel=${sel} setSel=${setSel} />
    ${group && Object.keys(group.videos || {}).length
      ? html`<${GroupVideo} sess=${SESS} group=${group} captures=${captures}
               sel=${sel} setSel=${setSel} mode=${mode} setMode=${setMode} />`
      : html`<${Stage} sess=${SESS} cap=${cur} mode=${mode} setMode=${setMode}
               zoom=${zoom} setZoom=${setZoom} />`}
    <div class="panels">
      <${StatePanel} cap=${cur} />
      <${AnchorPanel} anchors=${manifest.anchors} />
      <div class="panel">
        <h3>session</h3>
        <div class="kv">trace <a href="/s/${SESS}/trace.jsonl">trace.jsonl</a> ·
          <a href="/s/${SESS}/trace.compiled.jsonl">compiled</a> ·
          <a href="/s/${SESS}/session.json">manifest</a></div>
        <div class="kv">hint: ←/→ select · 1 A|B · 2 retail · 3 port · 4 diff · z zoom</div>
        ${(manifest.errors && Object.keys(manifest.errors).length)
          ? html`<pre class="err">${JSON.stringify(manifest.errors, null, 1)}</pre>` : null}
      </div>
    </div>`;
}

const root = document.getElementById("app");
root.textContent = "";                  // clear the static loading placeholder
render(html`<${App} />`, root);
