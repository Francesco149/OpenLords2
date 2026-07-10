import sys, time; sys.path.insert(0,'tools')
import kiting_demo as kd, micro_bot as mb
g=kd.Demo(27099); d=g.d; b=g.b
def alive():
    try: return d.rpc({"cmd":"ping"}).get("ok")
    except Exception: return False
def sides():
    us=g.units(); return [u for u in us if u["side"]==0],[u for u in us if u["side"]==1]
EN=sys.argv[1]; NA=int(sys.argv[2]); NE=int(sys.argv[3])
FROW=int(sys.argv[4]) if len(sys.argv)>4 else 18
g.stage({kd.CLASS["archer"]:NA},{kd.CLASS[EN]:NE})
mine,foe=sides(); print(f"{len(mine)} archer / {len(foe)} {EN} stacks; fire_row={FROW}", flush=True)
g.b.start(); time.sleep(0.3)
for k,u in enumerate(mine): g.setpos(u["idx"], 45+k, FROW)
prev=None
for rnd in range(30):
    mine,foe=sides()
    myhp=sum(u["hp"] for u in mine); fhp=sum(u["hp"] for u in foe); sc=g.screen()
    tag=""
    if prev is not None: tag=f"(d={fhp-prev:+d})"
    prev=fhp
    print(f"r{rnd:2d}: aHP={myhp} eHP={fhp} {tag} scr=0x{sc:x} alv={alive()}", flush=True)
    if not alive(): print("CRASH"); break
    if not foe or fhp==0: print("enemy gone"); break
    if sc<0x28 or sc>0x2a: print(f"ended 0x{sc:x}"); break
    tf=kd.nearest({"col":50,"row":FROW},foe)
    for fm in sorted({u["form"] for u in mine}): b.order(fm,tf["col"],tf["row"],tf["idx"])
    g.run(250)
