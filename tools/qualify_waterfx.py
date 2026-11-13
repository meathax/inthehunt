"""Paired native boss renderer qualification; never patches game state.

Run from the repository root with --snapshot PATH --script PATH.
Requires the native executable and imported ROMs in .local/world.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def percentile(values, q):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * q))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--script', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=ROOT / '.local/boss-review/paired')
    args = parser.parse_args()
    snapshot, script, out = args.snapshot.resolve(), args.script.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    lines = script.read_text().splitlines()
    end = int(lines[-1].split()[0])
    plain = out / 'inputs.txt'
    plain.write_text('\n'.join(s.replace(' shot', '') for s in lines) + '\n', newline='\r\n')
    executable = ROOT / '.local/native/InTheHunt.exe'
    results = []
    for index, (scale, enabled) in enumerate((k, on) for k in (1, 3) for on in (0, 1, 1, 0)):
        directory = out / f'{index}-k{scale}-{enabled}'
        directory.mkdir(exist_ok=True)
        for rom in (ROOT / '.local/world').glob('*.bin'):
            shutil.copy2(rom, directory / rom.name)
        env = {key: value for key, value in os.environ.items() if not key.startswith('WORLD_')}
        env.update(WORLD_SNAPSHOT_LOAD=str(snapshot),
                   WORLD_SNAPSHOT_SAVE=f'{end - 2},{directory / "final.snap"}',
                   WORLD_SCENE_RUN=str(scale), WORLD_SCENE_REACTIVE=str(enabled),
                   WORLD_SCENE_STATS=str(directory / 'stats.csv'))
        with (directory / 'run.log').open('w') as log:
            subprocess.run([str(executable), str(directory), f'--headless={plain}'],
                           cwd=ROOT, env=env, stderr=log, stdout=log, check=True)
        with (directory / 'stats.csv').open() as source:
            rows = list(csv.DictReader(source))
        start = int(rows[0]['frame'])
        frames = {}
        for row in rows:
            if int(row['frame']) < start + 120 or not int(row['boss']):
                continue
            frames.setdefault(int(row['frame']), []).append(row)
        groups = [group for group in frames.values() if len(group) == 2]
        assert groups, 'No boss frames after warmup'
        times = [sum(float(row['render_ms']) for row in group) for group in groups]
        draw = [sum(float(row['waterfx_draw_ms']) for row in group) for group in groups]
        state = (directory / 'final.snap').read_bytes()[:135174]
        result = dict(scale=scale, on=bool(enabled), frames=len(groups),
                      frame_ms=dict(median=statistics.median(times), p95=percentile(times, .95),
                                    p99=percentile(times, .99), mean=statistics.mean(times)),
                      waterfx_draw_ms=dict(median=statistics.median(draw), p95=percentile(draw, .95)),
                      state_sha256=hashlib.sha256(state).hexdigest(),
                      spawned={key: int(rows[-1][key]) for key in ('wakes', 'debris', 'silt', 'splashes')})
        results.append(result)
        (directory / 'summary.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
    assert len({result['state_sha256'] for result in results}) == 1, 'Game RAM/video/palette diverged'
    assert all(all(result['spawned'].values()) for result in results if result['on']), 'An effect never emitted'
    sources = ['world_scene.c', 'world_scene_waterfx.h', 'world_surface.h', 'world_native.c']
    summary = dict(snapshot_sha256=digest(snapshot), script_sha256=digest(script),
                   exe_sha256=digest(executable),
                   source_sha256={name: digest(ROOT / 'src' / name) for name in sources}, runs=results,
                   scope='CPU scene renderer; two refreshes per game frame; warmup excluded; boss-active frames only. '
                         'No GPU/presentation timing. RAM/tiles/OAM/palette hashes must match.')
    (out / 'summary.json').write_text(json.dumps(summary, indent=2))
    print('PASS: all four emitted; identical game state in every on/off run', flush=True)


if __name__ == '__main__':
    main()
