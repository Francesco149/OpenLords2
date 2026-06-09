import { html } from "/vendor/htm-preact-standalone.mjs";
import { useSessions } from "/store.mjs";

export function SessionPicker({ current }) {
  const sessions = useSessions();
  return html`<select class="sesspick"
      onChange=${e => { location.href = `/?session=${e.target.value}`; }}>
    ${sessions.map(s => html`<option value=${s.name} selected=${s.name === current}>
      ${s.name}${s.verdict ? ` · ${s.verdict}` : ""}</option>`)}
    ${sessions.every(s => s.name !== current)
      ? html`<option value=${current} selected>${current}</option>` : null}
  </select>`;
}
