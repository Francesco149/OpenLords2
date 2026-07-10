import sys, time; sys.path.insert(0,'tools')
import kiting_demo as kd, micro_bot as mb
g=kd.Demo(27099); d=g.d; b=g.b
def alive():
    try: return d.rpc({"cmd":"ping"}).get("ok")
    except Exception: return False
def units():
    us=g.units(); return [u for u in us if u["side"]==0],[u for u in us if u["side"]==1]
print("stage...", flush=True)
g.stage({kd.CLASS["archer"]:16},{kd.CLASS["mace"]:16})
mine,foe=units(); print(f"deployed {len(mine)} archers vs {len(foe)} mace; alive={alive()}", flush=True)
g.b.start(); time.sleep(0.3)
for k,u in enumerate(mine): g.setpos(u["idx"], 51+k, 16)   # CLOSE firing line (crashing config)
print(f"setpos done; alive={alive()}", flush=True)
for rnd in range(20):
    mine,foe=units()
    myhp=sum(u["hp"] for u in mine); fhp=sum(u["hp"] for u in foe)
    print(f"r{rnd:2d}: archerHP={myhp} enemyHP={fhp} nfoe={len(foe)}", flush=True)
    if not foe: print("enemy gone"); break
    # issue ONE order
    tf=kd.nearest({"col":55,"row":16}, foe)
    forms=sorted({u["form"] for u in mine})
    for fm in forms:
        b.order(fm, tf["col"], tf["row"], tf["idx"])
        if not alive(): print(f"  !! CRASHED on order form={fm} target_idx={tf['idx']} target=({tf['col']},{tf['row']})", flush=True); sys.exit()
    print(f"  orders ok; alive={alive()}", flush=True)
    g.run(300)
    if not alive(): print(f"  !! CRASHED on run() at enemyHP={fhp}", flush=True); sys.exit()
print("no crash in 20 rounds", flush=True)
