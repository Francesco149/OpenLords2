import sys, time, os, subprocess; sys.path.insert(0,'tools')
import kiting_demo as kd, micro_bot as mb
g=kd.Demo(27099); d=g.d; b=g.b
fdir="runs/probe/beat2/kstage"; os.makedirs(fdir,exist_ok=True)
for f in os.listdir(fdir):
    if f.endswith(".png"): os.remove(os.path.join(fdir,f))
def alive():
    try: return d.rpc({"cmd":"ping"}).get("ok")
    except Exception: return False
def sides(): 
    us=g.units(); return [u for u in us if u["side"]==0],[u for u in us if u["side"]==1]
def follow(us):
    live=[u for u in us if u["hp"]>0]
    if live: b.center_on(sum(u["col"] for u in live)/len(live), sum(u["row"] for u in live)/len(live)+2)
ENEMY=sys.argv[1] if len(sys.argv)>1 else "peasant"
NA=int(sys.argv[2]) if len(sys.argv)>2 else 40
NE=int(sys.argv[3]) if len(sys.argv)>3 else 16
print(f"stage {NA} archers vs {NE} {ENEMY}", flush=True)
g.stage({kd.CLASS["archer"]:NA},{kd.CLASS[ENEMY]:NE})
mine,foe=sides(); print(f"deployed {len(mine)} archer / {len(foe)} {ENEMY} stacks", flush=True)
g.b.start(); time.sleep(0.3)
for k,u in enumerate(mine): g.setpos(u["idx"], 45+k, 18)   # firing line
n=0; ended=0
for rnd in range(60):
    mine,foe=sides()
    myhp=sum(u["hp"] for u in mine); fhp=sum(u["hp"] for u in foe); sc=g.screen()
    # attack-order (fire) toward nearest foe
    if foe:
        tf=kd.nearest({"col":50,"row":18}, foe)
        for fm in sorted({u["form"] for u in mine}): b.order(fm, tf["col"], tf["row"], tf["idx"])
    follow(g.units())
    d.poke(0x53e9ac,[4,0,0,0]); d.rpc({"cmd":"sleep","ms":60})   # morphing-fix redraw
    ok=d.rpc({"cmd":"shotd","path":f"{fdir}/f{n:03d}.png"}).get("ok")
    if ok: n+=1
    if rnd%4==0: print(f"r{rnd:2d}: archerHP={myhp} enemyHP={fhp} scr=0x{sc:x} f={n} alive={alive()}", flush=True)
    if not alive(): print(f"  !! CRASHED at r{rnd}", flush=True); break
    if not foe or fhp==0: print("enemy destroyed"); break
    if sc<0x28 or sc>0x2a:
        ended+=1
        if ended>4: print(f"ended 0x{sc:x}"); break
    g.run(250)
    if not alive(): print(f"  !! CRASHED on run r{rnd}", flush=True); break
if n:
    subprocess.run(["ffmpeg","-y","-framerate","12","-i",f"{fdir}/f%03d.png","-vf","crop=478:444:0:18","-c:v","libx264","-pix_fmt","yuv420p","-crf","18","runs/probe/beat2/kstage.mp4"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    print(f"assembled {n} -> kstage.mp4", flush=True)
