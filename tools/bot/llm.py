"""llm.py — the bot's OpenRouter client: Jev decisions (default) and plain chat (fallback / narration).

Jev (POST /api/alpha/decisions) takes `state` (string / JSON) and named typed questions —
  noul   {"instructions", "criteria": {"true": …, "false": …}}        → {"noul": p_yes}
  choice {"instructions", "criteria": {key: description, …}}          → {"choice", "probabilities", "confidence"}
  score  {"instructions", "criteria": [level0, level1, …]}            → {"score", "probabilities", "legend", "confidence"}
— and answers with probabilities instead of text; output tokens are free (~$0.00002 per small request).

The key is the project's own low-cap OpenRouter key `openlords2-bot` ($3/month, made with the work
repo's `infra keys`), read per process by OPENLORDS2_KEY_CMD (private .local.env) and never printed or written to disk
(the retro lab's pattern). Spend shows in `infra keys`.
"""
import json
import os
import subprocess
import time
import urllib.error
import urllib.request

OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
DECISIONS_URL = "https://openrouter.ai/api/alpha/decisions"
DECISIONS_MODEL = "~typesafe/jev-latest"   # Jev: TypeSafe's "System One" decision model (typed answers + probabilities)
# the key: OPENROUTER_API_KEY, else the output of OPENLORDS2_KEY_CMD (a shell command; the owner's lives in the private
# .local.env that tools/dev sources — CLAUDE.local.md). Never printed.
KEY_CMD = os.environ.get("OPENLORDS2_KEY_CMD", "")
DEFAULT_MODEL = "typesafe/jev-router"
_key = None


def key():
    global _key
    if _key is None:
        if os.environ.get("OPENROUTER_API_KEY"):
            k, rc = os.environ["OPENROUTER_API_KEY"].strip(), 0
        elif KEY_CMD:
            out = subprocess.run(KEY_CMD, shell=True, capture_output=True, text=True, timeout=30)
            k, rc = out.stdout.strip(), out.returncode
        else:
            raise RuntimeError("no OpenRouter key: set OPENROUTER_API_KEY or OPENLORDS2_KEY_CMD")
        if rc != 0 or not k.startswith("sk-or-"):
            raise RuntimeError("could not read the OpenRouter key (OPENLORDS2_KEY_CMD failed)")
        _key = k
    return _key


class Usage:
    def __init__(self):
        self.calls = self.prompt = self.completion = 0
        self.cost = 0.0
        self.seconds = 0.0

    def __str__(self):
        return f"{self.calls} calls, {self.prompt}+{self.completion} tokens, ${self.cost:.4f}, {self.seconds:.1f}s"


USAGE = Usage()


def chat(messages, model=DEFAULT_MODEL, max_tokens=800, temperature=0.4, json_mode=False, retries=3, timeout=120):
    """→ (text, response dict). Raises on persistent failure (incl. HTTP 403 'Key limit exceeded')."""
    body = {"model": model, "messages": messages, "max_tokens": max_tokens, "temperature": temperature,
            "usage": {"include": True}}
    if json_mode:
        body["response_format"] = {"type": "json_object"}
    data = json.dumps(body).encode()
    last = None
    for attempt in range(retries):
        req = urllib.request.Request(OPENROUTER_URL, data=data, headers={
            "Authorization": f"Bearer {key()}", "Content-Type": "application/json",
            "HTTP-Referer": "https://github.com/Francesco149/OpenLords2", "X-Title": "OpenLords2 autoplay"})
        t0 = time.time()
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                resp = json.loads(r.read())
        except urllib.error.HTTPError as e:
            last = f"HTTP {e.code}: {e.read()[:300]!r}"
            if e.code in (401, 402, 403):
                raise RuntimeError(last)
            time.sleep(2 * (attempt + 1))
            continue
        except (urllib.error.URLError, TimeoutError) as e:
            last = str(e)
            time.sleep(2 * (attempt + 1))
            continue
        USAGE.calls += 1
        USAGE.seconds += time.time() - t0
        u = resp.get("usage") or {}
        USAGE.prompt += u.get("prompt_tokens", 0)
        USAGE.completion += u.get("completion_tokens", 0)
        USAGE.cost += float(u.get("cost") or 0)
        if "choices" not in resp:
            last = json.dumps(resp)[:300]
            time.sleep(1)
            continue
        return resp["choices"][0]["message"].get("content") or "", resp
    raise RuntimeError(f"OpenRouter failed after {retries} tries: {last}")


def _post(url, body, timeout, retries):
    data = json.dumps(body).encode()
    last = None
    for attempt in range(retries):
        req = urllib.request.Request(url, data=data, headers={
            "Authorization": f"Bearer {key()}", "Content-Type": "application/json",
            "HTTP-Referer": "https://github.com/Francesco149/OpenLords2", "X-Title": "OpenLords2 autoplay"})
        t0 = time.time()
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                resp = json.loads(r.read())
            USAGE.seconds += time.time() - t0
            return resp
        except urllib.error.HTTPError as e:
            last = f"HTTP {e.code}: {e.read()[:400]!r}"
            if e.code in (400, 401, 402, 403):
                raise RuntimeError(last)
        except (urllib.error.URLError, TimeoutError) as e:
            last = str(e)
        time.sleep(2 * (attempt + 1))
    raise RuntimeError(f"OpenRouter failed after {retries} tries: {last}")


def decide(state, questions, model=DECISIONS_MODEL, session_id=None, retries=3, timeout=60):
    """Jev decisions → {question: answer dict}. `questions`: {name: {"type", "instructions", "criteria"}}"""
    body = {"model": model, "state": state, "questions": questions}
    if session_id:
        body["session_id"] = session_id
    resp = _post(DECISIONS_URL, body, timeout, retries)
    USAGE.calls += 1
    u = resp.get("usage") or {}
    USAGE.prompt += u.get("input_tokens", 0)
    USAGE.completion += u.get("output_tokens", 0)
    USAGE.cost += float(u.get("cost") or 0)
    if "answers" not in resp:
        raise RuntimeError(f"no answers: {json.dumps(resp)[:300]}")
    return resp["answers"]
