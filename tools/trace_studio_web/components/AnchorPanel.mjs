// AnchorPanel — port vs retail anchor firings (the {wait} sync points).
import { html } from "/vendor/htm-preact-standalone.mjs";

export function AnchorPanel({ anchors }) {
  const port = anchors?.port || [];
  const retail = anchors?.retail || [];
  const names = [...new Set([...port.map(a => a.anchor),
                             ...retail.map(a => a.anchor)])];
  const at = (list, n) => {
    const hit = list.find(a => a.anchor === n);
    return hit ? hit.frame : "—";
  };
  return html`<div class="panel">
    <h3>anchors</h3>
    <table>
      <tr><th>anchor</th><th>port</th><th>retail</th></tr>
      ${names.map(n => html`<tr>
        <td>${n}</td><td>${at(port, n)}</td><td>${at(retail, n)}</td>
      </tr>`)}
    </table>
  </div>`;
}
