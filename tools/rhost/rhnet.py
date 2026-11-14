"""rhnet — record a multiplayer game between rhost processes (the fake SIGS transport, rh_net.c).

The broker relays SierraNW messages between N rhost processes and keeps their virtual clocks within a window
(rh_net.c rh_net_sync). Each process writes an out-trace: its input events + every delivery stamped with the receive
poll it became visible at ({"n": POLL, "p": FROM, "d": HEX}) — so each side then replays ALONE, bit for bit.

  rhost.py mp-record NAME --side P1.jsonl --side P2.jsonl [--frames N]     (wine)
    → tests/scenarios/NAME-p1/trace.jsonl, NAME-p2/... ; each replayed alone and compared with its recording run.
A side's input trace carries {"meta": {"net": "ME/N[,full|skirmish]"}} (ME = its player index).

Wire format (little endian): header {u8 type, u8 a, u16 len, u32 clock} + payload.
  process → broker: H (a = me, payload u8 nplayers), C (clock), S (a = dest player | 0xff broadcast, payload),
                    Y (a DirectPlay system message to every other side), P (paused on its controller), Q
  broker → process: C (clock = the slowest OTHER live peer), M (a = from, payload; 0xfe = a system message)"""
import os
import socket
import struct
import threading
import time

HDR = struct.Struct("<BBHI")
INF = 0xffffffff


class Broker:
    def __init__(self, n, log=None):
        self.n = n
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(n)
        self.port = self.srv.getsockname()[1]
        self.conns = {}          # player → socket
        self.clock = {}          # player → last clock (INF once gone)
        self.told = {}           # player → last clock sent to it
        self.lk = threading.Lock()
        self.wlk = {}
        self.stats = {"msgs": 0, "bytes": 0}
        self.log = log or (lambda s: None)
        self.done = threading.Event()
        self.trace = open(os.environ["RHNET_TRACE"], "w") if os.environ.get("RHNET_TRACE") else None
        threading.Thread(target=self._accept, daemon=True).start()

    def _send(self, p, typ, a, clock, payload=b""):
        s = self.conns.get(p)
        if s is None or self.clock.get(p) == INF:
            return
        if self.trace:
            self.trace.write(f"{time.time():.3f}   to {p} {chr(typ)} a={a} len={len(payload)} clock={clock}\n"); self.trace.flush()
        try:
            with self.wlk[p]:
                s.sendall(HDR.pack(typ, a, len(payload), clock & 0xffffffff) + payload)
        except OSError:
            pass

    def _retell(self):
        if len(self.conns) < self.n:
            return
        for p in self.conns:
            others = [self.clock[q] for q in self.conns if q != p]
            m = min(others) if others else INF
            if self.told.get(p) != m:
                self.told[p] = m
                self._send(p, ord("C"), 0, m)

    def _accept(self):
        for _ in range(self.n):
            s, _ = self.srv.accept()
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            threading.Thread(target=self._serve, args=(s,), daemon=True).start()

    def _recv(self, s, n):
        b = b""
        while len(b) < n:
            c = s.recv(n - len(b))
            if not c:
                raise EOFError
            b += c
        return b

    def _serve(self, s):
        me = None
        try:
            while True:
                typ, a, ln, clock = HDR.unpack(self._recv(s, HDR.size))
                payload = self._recv(s, ln) if ln else b""
                if self.trace:
                    self.trace.write(f"{time.time():.3f} from {me} {chr(typ)} a={a} len={ln} clock={clock}\n"); self.trace.flush()
                with self.lk:
                    if typ == ord("H"):
                        me = a
                        self.conns[me] = s
                        self.wlk[me] = threading.Lock()
                        self.clock[me] = clock
                        self.log(f"broker: player {me} joined (clock {clock})")
                    elif typ == ord("C"):
                        self.clock[me] = clock
                    elif typ == ord("P"):          # idle on its controller (mpbot): never wait for it
                        self.clock[me] = INF - 1
                    elif typ == ord("S"):
                        self.clock[me] = clock
                        self.stats["msgs"] += 1
                        self.stats["bytes"] += len(payload)
                        dests = [q for q in self.conns if q != me] if a == 0xff else [a]
                        for q in dests:
                            self._send(q, ord("M"), me, clock, payload)
                    elif typ == ord("Y"):          # a DirectPlay system message (rh_net DP mode): to every other side, from DPID 0
                        self.clock[me] = clock
                        for q in [q for q in self.conns if q != me]:
                            self._send(q, ord("M"), 0xfe, clock, payload)
                    elif typ == ord("Q"):
                        break
                    self._retell()
        except (EOFError, OSError):
            pass
        with self.lk:
            if me is not None:
                self.clock[me] = INF
                self.log(f"broker: player {me} left")
                for q in [q for q in self.conns if q != me]:   # the others learn it (SIGS: TellMePlayerLeftGame)
                    self._send(q, ord("L"), me, 0)
                self._retell()
                if all(v == INF for v in self.clock.values()) and len(self.clock) == self.n:
                    self.done.set()


def cmd_mp_record(a, R):
    """R = the rhost module (run_batch_wine, make_batch …)"""
    import json
    sides = [os.path.abspath(p) for p in a.side]
    if len(sides) < 2:
        raise SystemExit("mp-record: two or more --side traces")
    br = Broker(len(sides), log=print)
    runs = []
    for k, tr in enumerate(sides):
        meta = R.trace_meta(tr)
        if not meta.get("net"):
            raise SystemExit(f"{tr}: no \"net\" in its meta")
        me = int(str(meta["net"]).split(":")[-1].split("/")[0])   # "ME/N" or "dp:ME/N"
        if me != k:
            raise SystemExit(f"{tr}: net {meta['net']} — side {k + 1} must be player {k}")
        meta = dict(meta)
        meta["_extra"] = ["--net-peer", f"127.0.0.1:{br.port}", "--out-trace", "mp.jsonl"]
        runs.append((f"{a.name}-p{k + 1}", tr, meta))
    a.jobs = len(sides)
    batch, bdir, res, out = R.run_batch_wine(a, runs, "mp-record")
    R.print_results(res, [r[0] for r in runs])
    print(f"broker: {br.stats['msgs']} messages, {br.stats['bytes']} bytes")
    made = []
    for name, _, _ in runs:
        rec = os.path.join(bdir, name, "mp.jsonl")
        if not os.path.exists(rec):
            print(f"{name}: no mp.jsonl")
            continue
        d = os.path.join(R.SCEN, name)
        if os.path.exists(os.path.join(d, "trace.jsonl")) and not a.force:
            raise SystemExit(f"{d}/trace.jsonl exists (--force)")
        os.makedirs(d, exist_ok=True)
        lines = open(rec).read().splitlines()
        nd = sum(1 for l in lines if '"n":' in l)
        hdr = [f"# {name}: one side of a {len(sides)}-player SIGS game recorded through rh_net's broker "
               f"(batch {batch}); {nd} deliveries stamped by receive poll — replays alone"]
        open(os.path.join(d, "trace.jsonl"), "w").write("\n".join(hdr + lines) + "\n")
        made.append(name)
        print(f"wrote {d}/trace.jsonl ({len(lines)} lines, {nd} deliveries)")
    rec_res = {n: res.get(n, {}) for n in made}
    if made and not a.no_check:   # each side alone ≡ its recording run
        a.extra = None
        runs2 = [(n,) + R.scenario_inputs(n) for n in made]
        b2, d2, res2, out2 = R.run_batch_wine(a, runs2, "mp-replay")
        R.print_results(res2, made)
        for n in made:
            x, y = rec_res[n], res2.get(n, {})
            keys = ("frame", "state_sem", "rng") if "state_sem" in x else ("frame", "rng")
            same = all(x.get(k) == y.get(k) for k in keys)
            print(f"{n}: replay alone {'≡' if same else 'DIFFERS from'} the recording "
                  + " ".join(f"{k}={x.get(k)}/{y.get(k)}" for k in keys))
