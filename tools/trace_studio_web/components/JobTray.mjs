// JobTray — the recapture buttons + the one-job status line.
import { html } from "/vendor/htm-preact-standalone.mjs";
import { postJSON, useJobs } from "/store.mjs";

export function JobTray({ sess, onDone }) {
  const [jobs, poll] = useJobs();
  const job = jobs?.jobs?.[0];
  const running = !!jobs?.running;
  const fire = async (only) => {
    const r = await postJSON("/api/recapture", { session: sess, only });
    if (r.error) alert(r.error);
    poll();
    if (!r.error) watch(onDone, poll);
  };
  return html`<span class="jobtray">
    <button disabled=${running} onClick=${() => fire("port")}
      title="re-run just the port against the cached retail">⟳ port</button>
    <button disabled=${running} onClick=${() => fire(null)}
      title="full re-capture, both sides">⟳ both</button>
    ${running ? html`<span class="spin">capturing…
        ${job?.log_tail?.length ? html`<code>${job.log_tail[job.log_tail.length - 1]}</code>` : null}
      </span>`
      : job && job.rc != null ? html`<span class=${job.rc === 0 ? "ok" : "bad"}>
          last: rc ${job.rc}</span>` : null}
  </span>`;
}

function watch(onDone, poll) {
  const t = setInterval(async () => {
    const r = await fetch("/api/jobs").then(x => x.json()).catch(() => null);
    if (r && !r.running) {
      clearInterval(t);
      poll();
      onDone && onDone();
    }
  }, 1500);
}
