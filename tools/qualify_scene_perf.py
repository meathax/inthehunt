"""Repeat the retained boss replay without opening a window. Output stays local."""
import argparse, csv, hashlib, json, os, shutil, statistics, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--exe', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--micro', default='0')
p.add_argument('--cinema', default='0')
a = p.parse_args()
out = a.out.resolve(); out.mkdir(parents=True, exist_ok=False)
snapshot = ROOT / '.local/boss-review/boss.snap'
source = ROOT / '.local/boss-review/boss.txt'
lines = source.read_text().splitlines()
end = int(lines[-1].split()[0])
script = out / 'replay.txt'
script.write_text('\n'.join(line.replace(' shot','') for line in lines[:-2]) + '\n' + '\n'.join(lines[-2:]) + '\n')
results = []
for scale in (1, 3):
    folder = out / f'k{scale}'; folder.mkdir()
    for rom in (ROOT / '.local/world').glob('*.bin'): shutil.copy2(rom, folder / rom.name)
    env = {k:v for k,v in os.environ.items() if not k.startswith('WORLD_')}
    env.update(WORLD_SCENE_RUN=str(scale), WORLD_SCENE_MICRO=a.micro, WORLD_SCENE_CINEMA=a.cinema,
               WORLD_SCENE_STATS=str(folder/'stats.csv'), WORLD_SNAPSHOT_LOAD=str(snapshot),
               WORLD_SNAPSHOT_SAVE=f'{end-2},{folder/"final.snap"}')
    with (folder/'run.log').open('w') as log:
        subprocess.run([str(a.exe.resolve()),str(folder),f'--headless={script}'],env=env,cwd=ROOT,
                       stdout=log,stderr=log,check=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=180)
    rows = list(csv.DictReader((folder/'stats.csv').open()))
    start = int(rows[0]['frame']); frames = {}
    for row in rows:
        if int(row['frame']) >= start+120 and int(row['boss']): frames.setdefault(int(row['frame']),[]).append(row)
    times = sorted(sum(float(r['render_ms']) for r in group) for group in frames.values() if len(group)==2)
    assert times and not (folder/'native-stops.tsv').exists()
    result = {'scale':scale,'frames':len(times),'median_ms':statistics.median(times),
              'p95_ms':times[int(.95*(len(times)-1))],'mean_ms':statistics.mean(times),
              'state_sha256':hashlib.sha256((folder/'final.snap').read_bytes()[:135174]).hexdigest(),
              'emitted':{k:rows[-1][k] for k in ('sparks','impact_motes') if k in rows[-1]}}
    results.append(result); print(json.dumps(result),flush=True)
(out/'summary.json').write_text(json.dumps({'exe_sha256':hashlib.sha256(a.exe.read_bytes()).hexdigest(),
    'snapshot_sha256':hashlib.sha256(snapshot.read_bytes()).hexdigest(),'runs':results,
    'scope':'CPU renderer, two refreshes/game frame, retained boss replay, 120-frame warmup; no GPU/desktop measurement'},indent=2))
