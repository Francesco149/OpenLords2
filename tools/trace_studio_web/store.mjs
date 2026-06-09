// store.mjs — API helpers + hooks (idioms lifted from openrecet's store).
import { useState, useEffect, useRef, useCallback } from "/vendor/htm-preact-standalone.mjs";

export const BUST = Date.now();          // per-load cache-bust for re-captured media

export async function getJSON(url) {
  const r = await fetch(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(`${url} → ${r.status}`);
  return r.json();
}
export async function postJSON(url, body) {
  const r = await fetch(url, {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {}),
  });
  try { return await r.json(); } catch { return { ok: r.ok }; }
}
export function qparam(name) {
  const m = new RegExp(`[?&]${name}=([^&]+)`).exec(location.search);
  return m ? decodeURIComponent(m[1]) : "";
}

// session manifest (reload() after a recapture)
export function useSession(sess) {
  const [data, setData] = useState({ loading: true });
  const reload = useCallback(async () => {
    if (!sess) { setData({ loading: false, error: "no session" }); return; }
    try {
      const manifest = await getJSON(`/s/${sess}/session.json?b=${Date.now()}`);
      setData({ manifest, loading: false, error: null });
    } catch (e) {
      setData({ loading: false, error: String(e.message || e) });
    }
  }, [sess]);
  useEffect(() => { reload(); }, [reload]);
  return { ...data, reload };
}

export function useSessions() {
  const [list, setList] = useState([]);
  useEffect(() => {
    getJSON("/api/sessions").then(d => setList(d.sessions || [])).catch(() => {});
  }, []);
  return list;
}

// `.running`-keyed poller for /api/jobs (openrecet's useStatus pattern)
export function useJobs(intervalMs = 1500) {
  const [s, setS] = useState(null);
  const timer = useRef(0);
  const poll = useCallback(() => {
    getJSON("/api/jobs").then(st => {
      setS(st);
      if (st.running && !timer.current) timer.current = setInterval(poll, intervalMs);
      if (!st.running && timer.current) { clearInterval(timer.current); timer.current = 0; }
    }).catch(() => {});
  }, [intervalMs]);
  useEffect(() => { poll(); return () => { if (timer.current) clearInterval(timer.current); }; }, [poll]);
  return [s, poll];
}
