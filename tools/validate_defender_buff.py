#!/usr/bin/env python3
"""Live-retail validation of the auto-calc defender buff (LoTR2 beat #2).

Stages an EVEN fight in two combat records, runs the resolver FUN_004aad07
twice — quick_resolve/buffed (Hard, x320% defender) vs un-buffed — and shows
the winner flip. Saves & restores everything it touches.  Read-through-the-
daemon on 127.0.0.1:27099.
"""
import json, socket

PORT = 27099
def rpc(req):
    s = socket.socket(); s.connect(("127.0.0.1", PORT))
    s.sendall((json.dumps(req)+"\n").encode())
    buf=b""
    while b"\n" not in buf:
        c=s.recv(65536)
        if not c: break
        buf+=c
    return json.loads(buf.split(b"\n")[0].decode())

def rd(va, ty="i32"): return rpc({"cmd":"read","va":va,"type":ty})["val"]
def poke(va, data): rpc({"cmd":"poke","va":va,"bytes":data})
def pi16(va, v): poke(va, [v & 0xff, (v>>8)&0xff])
def pi32(va, v): poke(va, [v&0xff,(v>>8)&0xff,(v>>16)&0xff,(v>>24)&0xff])
def callq(va, *args): return rpc({"cmd":"callq","va":va,"args":list(args)})

BASE=0x52f0b0; STRIDE=0x1a4
def rec(i): return BASE + i*STRIDE
WEIGHTS=[2,16,8,13,9,13,22]

# scalars
V_ATK=0x568210; V_DEF=0x568218; V_QUICK=0x53e8fc; V_DIFF=0x55401c
V_WIN=0x57c924; V_WINOWN=0x5440cc
F_CLEAR=0x4aa89f      # engine_combat_clear_battle_result(terr)
F_RESOLVE=0x4aad07    # engine_combat_resolve_casualties (reads globals)
F_STREN=0x4ab2aa      # engine_combat_army_strength(terr)

A, D = 7, 8   # two AI-capital records to borrow

def counts(i):
    return [rd(rec(i)+0x16c+2*c, "i16") for c in range(7)]
def head(i): return rd(rec(i)+0x168, "i32")
def owner(i): return rd(rec(i)+0x00, "u8")
def merc(i): return rd(rec(i)+0x196, "u8")

def snapshot(i):
    return dict(counts=counts(i), head=head(i), owner=owner(i), merc=merc(i))
def restore(i, s):
    for c in range(7): pi16(rec(i)+0x16c+2*c, s["counts"][c])
    pi32(rec(i)+0x168, s["head"]); poke(rec(i)+0x00,[s["owner"]&0xff])
    poke(rec(i)+0x196,[s["merc"]&0xff])

def set_even(i, own, cls=4, n=100):
    for c in range(7): pi16(rec(i)+0x16c+2*c, 0)
    pi16(rec(i)+0x16c+2*cls, n)
    pi32(rec(i)+0x168, n); poke(rec(i)+0x00,[own&0xff]); poke(rec(i)+0x196,[0])

def strength(i):  # call the game's own strength fn on the engine thread
    r=callq(F_STREN, i); return r.get("ret", r.get("result"))

# ---- save originals ----
sA, sD = snapshot(A), snapshot(D)
save_scalars = {va: rd(va) for va in (V_ATK,V_DEF,V_QUICK,V_DIFF,V_WIN,V_WINOWN)}
print("SAVED rec7:", sA, "\nSAVED rec8:", sD)

def run(quick, diff, label):
    set_even(A, own=1); set_even(D, own=2)      # A=human, D=AI, equal 100xPike
    callq(F_CLEAR, A); callq(F_CLEAR, D)
    pi32(V_ATK, A); pi32(V_DEF, D); pi32(V_DIFF, diff); pi32(V_QUICK, quick)
    sa, sd = strength(A), strength(D)
    callq(F_RESOLVE)
    win, wown = rd(V_WIN), rd(V_WINOWN,"i32")
    ca, cd = sum(counts(A)), sum(counts(D))
    who = "ATTACKER(7,human)" if win==A else ("DEFENDER(8,AI)" if win==D else f"?{win}")
    print(f"\n[{label}] quick_resolve={quick} difficulty={diff}")
    print(f"  strengths: attacker={sa}  defender={sd}"
          + (f"  (defender x{ [160,200,250,320,400][min(diff,4)] }% = {sd* [160,200,250,320,400][min(diff,4)]//100} effective)" if quick else "  (no buff)"))
    print(f"  WINNER = {who}  (winner_terr={win}, winner_owner={wown})")
    print(f"  survivors: attacker_troops={ca}  defender_troops={cd}")
    return win

w1 = run(quick=1, diff=3, label="AUTO-CALC / QUICK-RESOLVE (Hard)")
w2 = run(quick=0, diff=3, label="UN-BUFFED formula (same even fight)")

# ---- restore ----
restore(A, sA); restore(D, sD)
for va,v in save_scalars.items(): pi32(va, v)
print("\nRESTORED rec7/rec8 + scalars.")
print(f"\n=== RESULT: buffed winner={'ATT' if w1==A else 'DEF'}, "
      f"un-buffed winner={'ATT' if w2==A else 'DEF'} ===")
print("The buff FLIPS an even fight." if w1==D and w2==A else
      "Unexpected — inspect.")
