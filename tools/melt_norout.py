import sys, time; sys.path.insert(0,'tools')
import kiting_demo as kd, micro_bot as mb
g=kd.Demo(27099); d=g.d; b=g.b
GBASE,GSTR=0x566520,0x34
def alive():
    try: return d.rpc({"cmd":"ping"}).get("ok")
    except Exception: return False
def sides():
    us=g.units(); return [u for u in us if u["side"]==0],[u for u in us if u["side"]==1]
def norout():   # reset enemy group decision counters (+0x1c) so they never hit 200 -> never rout
    for gg in range(7,13):
        d.poke(GBASE+gg*GSTR+0x1c,[0,0])
        d.poke(GBASE+gg*GSTR+0x0c,[0])       # skip the rout branch (FUN_0048a9d2)
        d.poke(GBASE+gg*GSTR+0x1a,[3,0])     # keep phase low (fight, not rout)
EN=sys.argv[1] if len(sys.argv)>1 else "mace"; NA=int(sys.argv[2]) if len(sys.argv)>2 else 20; NE=int(sys.argv[3]) if len(sys.argv)>3 else 24
g.stage({kd.CLASS["archer"]:NA},{kd.CLASS[EN]:NE})
mine,foe=sides(); print(f"{len(mine)} archer / {len(foe)} {EN}", flush=True)
g.b.start(); time.sleep(0.3)
for k,u in enumerate(mine): g.setpos(u["idx"], 45+k, 18)
prev=None
for rnd in range(40):
    mine,foe=sides()
    myhp=sum(u["hp"] for u in mine); fhp=sum(u["hp"] for u in foe); sc=g.screen()
    tag=f"(d={fhp-prev:+d})" if prev is not None else ""; prev=fhp
    print(f"r{rnd:2d}: aHP={myhp} eHP={fhp} {tag} scr=0x{sc:x}", flush=True)
    if not alive(): print("CRASH"); break
    if not foe or fhp==0: print("enemy WIPED"); break
    if sc<0x28 or sc>0x2a: print(f"ended 0x{sc:x}"); break
    norout()
    tf=kd.nearest({"col":50,"row":18},foe)
    for fm in sorted({u["form"] for u in mine}): b.order(fm,tf["col"],tf["row"],tf["idx"])
    g.run(250)
