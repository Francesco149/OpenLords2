import sys, time, os, subprocess; sys.path.insert(0,'tools')
import kiting_demo as kd, micro_bot as mb
g=kd.Demo(27099); d=g.d; b=g.b
fdir="runs/probe/beat2/kadv"; os.makedirs(fdir,exist_ok=True)
for f in os.listdir(fdir):
    if f.endswith(".png"): os.remove(os.path.join(fdir,f))
GBASE,GSTR=0x566520,0x34
def alive():
    try: return d.rpc({"cmd":"ping"}).get("ok")
    except Exception: return False
def sides():
    us=g.units(); return [u for u in us if u["side"]==0],[u for u in us if u["side"]==1]
def follow(us):
    live=[u for u in us if u["hp"]>0]
    if live: b.center_on(sum(u["col"] for u in live)/len(live), sum(u["row"] for u in live)/len(live))
def advance_enemy():
    for gg in range(7,13):
        d.poke(GBASE+gg*GSTR+0x1a,[12]); d.poke(GBASE+gg*GSTR+0x1c,[200,0])
NA=int(sys.argv[1]) if len(sys.argv)>1 else 24
NE=int(sys.argv[2]) if len(sys.argv)>2 else 24
EN=sys.argv[3] if len(sys.argv)>3 else "mace"
print(f"stage {NA} archers vs {NE} {EN} (enemy ADVANCE)", flush=True)
g.stage({kd.CLASS["archer"]:NA},{kd.CLASS[EN]:NE})
mine,foe=sides(); print(f"deployed {len(mine)} archer / {len(foe)} {EN} stacks", flush=True)
g.b.start(); time.sleep(0.3)
AROW=24
for k,u in enumerate(mine): g.setpos(u["idx"], 45+(k%12), AROW+(k//12))
n=0; ended=0
for rnd in range(70):
    mine,foe=sides()
    myhp=sum(u["hp"] for u in mine); fhp=sum(u["hp"] for u in foe); sc=g.screen()
    if not foe or fhp==0: print("enemy destroyed"); break
    emax=max((u["row"] for u in foe), default=0)
    amin=min((u["row"] for u in mine), default=99)
    # fire at nearest enemy
    tf=kd.nearest({"col":50,"row":AROW}, foe)
    for fm in sorted({u["form"] for u in mine}): b.order(fm, tf["col"], tf["row"], tf["idx"])
    # KITE: if enemy within 7 rows, retreat archers further down-field (move-order)
    if emax > amin-7:
        AROW=min(AROW+3, 70)
        for k,u in enumerate(mine): b.move_unit(u["idx"], u["col"], AROW+(k//12))
    advance_enemy()
    follow(g.units())
    d.poke(0x53e9ac,[4,0,0,0]); d.rpc({"cmd":"sleep","ms":50})
    if d.rpc({"cmd":"shotd","path":f"{fdir}/f{n:03d}.png"}).get("ok"): n+=1
    if rnd%3==0: print(f"r{rnd:2d}: aHP={myhp} eHP={fhp} eRow={emax} aRow={amin} scr=0x{sc:x} f={n} alv={alive()}", flush=True)
    if not alive(): print(f"!! CRASH r{rnd}"); break
    if sc<0x28 or sc>0x2a:
        ended+=1
        if ended>4: print(f"ended 0x{sc:x}"); break
    g.run(220)
    if not alive(): print(f"!! CRASH run r{rnd}"); break
if n:
    subprocess.run(["ffmpeg","-y","-framerate","12","-i",f"{fdir}/f%03d.png","-vf","crop=478:444:0:18","-c:v","libx264","-pix_fmt","yuv420p","-crf","18","runs/probe/beat2/kadv.mp4"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    print(f"assembled {n} -> kadv.mp4", flush=True)
