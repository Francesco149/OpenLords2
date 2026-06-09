"""server/app.py — the studio's local http server.

    GET  /                        → 302 /?session=<default> (SPA)
    GET  /<asset>                 → static from tools/trace_studio_web/
    GET  /api/sessions            → {"sessions":[summary]}
    GET  /api/jobs                → job runner status
    POST /api/recapture           → {"session", "only"?} → spawn capture job
    GET  /s/<sess>/<file>         → session artifacts (HTTP Range for .mp4)

Structure lifted from openrecet's server/app.py (the route surface here is
small enough for inline handlers; grow a dispatch table when it isn't).
"""
from __future__ import annotations

import atexit
import json
import re
import socketserver
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from ..model.session import list_sessions
from ..paths import DEFAULT_REMOTE
from . import ranged
from .jobs import JobRunner

_CTYPE = {
    ".html": "text/html; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".mjs": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".jsonl": "application/x-ndjson; charset=utf-8",
    ".mp4": "video/mp4",
    ".png": "image/png",
    ".txt": "text/plain; charset=utf-8",
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):          # quiet
        pass

    def _ctype(self, p: Path) -> str:
        return _CTYPE.get(p.suffix.lower(), "application/octet-stream")

    def _send_bytes(self, data: bytes, ctype: str, code: int = 200):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def _send_json(self, obj, code: int = 200):
        self._send_bytes(json.dumps(obj).encode(), _CTYPE[".json"], code)

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        u = urlparse(self.path)
        path = u.path
        web_dir = self.server.web_dir
        if path in ("/", "/index.html"):
            qs = parse_qs(u.query)
            if "session" not in qs and self.server.default_session:
                # Content-Length:0 is REQUIRED on the empty 302 body or an
                # HTTP/1.1 keep-alive client blocks until timeout.
                self.send_response(302)
                self.send_header(
                    "Location", f"/?session={self.server.default_session}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            ranged.send_file(self, web_dir / "index.html",
                             _CTYPE[".html"])
            return
        if path == "/api/sessions":
            self._send_json(
                {"sessions": list_sessions(self.server.sess_root)})
            return
        if path == "/api/jobs":
            self._send_json(self.server.jobs.status())
            return
        m = re.match(r"^/s/([^/]+)/(.+)$", path)
        if m:
            sess, rel = m.group(1), m.group(2)
            if ".." in rel or ".." in sess:
                self._send_bytes(b"no", "text/plain", 403)
                return
            p = self.server.sess_root / sess / rel
            ranged.send_file(self, p, self._ctype(p))
            return
        # static assets
        rel = path.lstrip("/")
        if ".." not in rel:
            cand = (web_dir / rel)
            if cand.is_file():
                ranged.send_file(self, cand, self._ctype(cand))
                return
        self._send_bytes(b"not found", "text/plain", 404)

    def do_POST(self):
        u = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b""
        try:
            body = json.loads(raw.decode() or "{}")
        except ValueError:
            body = {}
        if u.path == "/api/recapture":
            sess = str(body.get("session") or "")
            only = body.get("only") or None
            if only not in (None, "port", "retail"):
                self._send_json({"error": f"bad only={only!r}"}, 400)
                return
            if not (self.server.sess_root / sess / "trace.jsonl").exists():
                self._send_json({"error": f"unknown session {sess!r}"}, 404)
                return
            out = self.server.jobs.start(sess, only,
                                         remote=self.server.remote)
            self._send_json(out, 409 if "error" in out else 200)
            return
        self._send_bytes(b"not found", "text/plain", 404)


def serve(sess_root: Path, web_dir: Path, host: str = "127.0.0.1",
          port: int = 8780, default_session: str | None = None,
          remote: str = DEFAULT_REMOTE) -> int:
    jobs = JobRunner(sess_root)
    atexit.register(jobs.force_cleanup)

    class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
        allow_reuse_address = True
        daemon_threads = True

    httpd = Server((host, port), Handler)
    httpd.sess_root = sess_root
    httpd.web_dir = web_dir
    httpd.default_session = default_session
    httpd.remote = remote
    httpd.jobs = jobs

    url = f"http://{host}:{port}/"
    if default_session:
        url += f"?session={default_session}"
    print(f"trace_studio: serving {sess_root} at {url}", flush=True)
    print("trace_studio: Ctrl-C to stop", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\ntrace_studio: stopped", flush=True)
    finally:
        jobs.force_cleanup()
        httpd.shutdown()
    return 0
