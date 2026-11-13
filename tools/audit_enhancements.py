"""Headless per-switch audit on the retained boss replay; local ROMs required."""
import argparse,csv,json,os,shutil,statistics,subprocess,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
header=(ROOT/'src/world_scene.h').read_text(encoding='utf-8').split('} WorldSceneFx;')[0]
names=[n.strip() for m in re.finditer(r'unsigned ([^;]+);',header) for n in m[1].split(',')]
source=(ROOT/'.local/boss-review/boss.txt').read_text().splitlines();start=int(source[0].split()[0]);end=start+180
script=out/'replay.txt';lines=[line.replace(' shot','') for line in source if int(line.split()[0])<end]
lines[-1]+=' shot';lines.append(f'{end} end');script.write_text('\n'.join(lines)+'\n')
results=[]
for option,name in [(-1,'combined'),(0,'off')]+list(enumerate(names,1)):
    folder=out/name;folder.mkdir()
    for rom in (ROOT/'.local/world').glob('*.bin'):shutil.copy2(rom,folder/rom.name)
    env={k:v for k,v in os.environ.items() if not k.startswith('WORLD_')}
    env.update(WORLD_SCENE_RUN='3',WORLD_SCENE_OPTION=str(option),WORLD_SCENE_CINEMA='63',WORLD_SCENE_MICRO='1',
        WORLD_SNAPSHOT_LOAD=str(ROOT/'.local/boss-review/boss.snap'),WORLD_SCENE_STATS=str(folder/'stats.csv'))
    with (folder/'run.log').open('w') as log:
        subprocess.run([str(a.exe.resolve()),str(folder),f'--headless={script}'],cwd=ROOT,env=env,stdout=log,stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW,check=True,timeout=60)
    assert not (folder/'native-stops.tsv').exists()
    rows=list(csv.DictReader((folder/'stats.csv').open()));frames={}
    for row in rows:
        if int(row['frame'])>=start+40:frames.setdefault(row['frame'],[]).append(float(row['render_ms']))
    times=sorted(sum(v) for v in frames.values() if len(v)==2);assert len(times)>100
    record={'name':name,'frames':len(times),'median_ms':statistics.median(times),'p95_ms':times[int(.95*(len(times)-1))]}
    results.append(record);print(json.dumps(record),flush=True)
(out/'summary.json').write_text(json.dumps(results,indent=2))
