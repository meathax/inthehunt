"""Search for untranslated-path stops ("crashes") across the whole game.

Each stage snapshot (made with WORLD_SNAPSHOT_STAGES) is resumed under many
input styles, including the ones the ordinary autopilot never produces:
nobody playing (the player dies and the game ends), playing without ever
continuing, ramming without firing, random input, and two players where one
idles or drops out. Every run that stops is reported with what it hit and
how to reproduce it.

  python tools/crash_hunt.py SNAPSHOT_DIR [--frames 20000] [--seeds 3] [--jobs 12]
"""
import argparse
import os
import pathlib
import random
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORLD = ROOT / ".local" / "world"
DIRS = [0x01, 0x02, 0x04, 0x08, 0x05, 0x09, 0x06, 0x0A, 0x00]   # right left down up, diagonals, none


def script(mode, seed, frames):
    """Lines 'frame held buttons' (hex), frames relative to the snapshot."""
    rng = random.Random(seed * 7919 + hash(mode) % 1000)
    out, prev = [], None

    def put(f, held, buttons):
        nonlocal prev
        state = (held, buttons)
        if state != prev:
            out.append(f"{f} {held:x} {buttons:x}")
            prev = state

    direction, fire = 0, 0
    for f in range(frames):
        held, buttons = 0, 0
        coin = "nocont" not in mode and "idle" not in mode
        if mode == "idle":
            pass
        elif mode.startswith("rand"):
            if f % rng.choice((8, 20, 45, 90)) == 0:
                direction = rng.choice(DIRS)
                fire = rng.choice((0, 0x80, 0x40, 0xC0))
            held = direction | (fire if f % 6 < 3 else 0)
        elif mode == "ram":                       # drive into things, never fire
            held = (0x05, 0x01, 0x09, 0x01)[(f // 120) % 4]
        elif mode in ("fight", "nocont"):
            held = (0x01, 0x01, 0x09, 0x01, 0x05, 0x02, 0x06, 0x0A)[(f // 50) % 8]
            phase = f % 24
            held |= (0x80 if phase < 6 else 0) | (0x40 if 12 <= phase < 18 else 0)
        elif mode.startswith("duo"):
            p1 = (0x01, 0x09, 0x01, 0x05)[(f // 70) % 4] | (0x80 if f % 24 < 6 else 0)
            p2 = (0x02, 0x0A, 0x01, 0x06)[(f // 90) % 4] | (0x40 if f % 20 < 5 else 0)
            if mode == "duo-p1idle":
                p1 = 0
            if mode == "duo-p2idle":
                p2 = 0
            held = p1 | p2 << 8
            if f < 400 and f % 100 < 6:
                buttons |= 8                      # coin 2
            if 400 <= f < 600 and f % 100 < 6:
                buttons |= 2                      # P2 start (join)
        if coin:
            if f % 1500 < 6:
                buttons |= 4                      # coin 1
            if f % 300 >= 150 and f % 300 < 156:
                buttons |= 1                      # P1 start / continue
                if mode.startswith("duo") and mode != "duo-p2idle":
                    buttons |= 2
        put(f, held, buttons)
    out.append(f"{frames} end")
    return "\r\n".join(out) + "\r\n"


MODES = ["idle", "nocont", "ram", "fight", "rand", "rand-nocont", "duo", "duo-p1idle", "duo-p2idle"]


def run(exe, snap, mode, seed, frames, work):
    name = f"{snap.stem}-{mode}-{seed}"
    path = work / f"{name}.txt"
    path.write_text(script(mode, seed, frames), encoding="ascii", newline="")
    env = dict(os.environ, WORLD_SNAPSHOT_LOAD=str(snap), WORLD_SCRIPT_RELATIVE="1")
    proc = subprocess.run([str(exe), str(WORLD), "--timing=arcade", f"--headless={path}"],
                          env=env, capture_output=True, text=True, timeout=frames // 40 + 600)
    stop, note = "", ""
    for line in proc.stderr.splitlines():
        if line.startswith("Register estimates"):
            note = line.split("(last: ")[-1].rstrip(")")
        if "Unrecovered" in line or "boundary stop" in line or "did not terminate" in line:
            stop = line.strip()
    if proc.returncode not in (0, 2) and not stop:
        stop = f"exit code {proc.returncode}"
    path.unlink()
    return name, proc.returncode, stop, note


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("snapshots")
    ap.add_argument("--frames", type=int, default=20000)
    ap.add_argument("--seeds", type=int, default=2)
    ap.add_argument("--jobs", type=int, default=12)
    ap.add_argument("--modes", default=",".join(MODES))
    ap.add_argument("--exe", default=str(ROOT / ".local" / "native" / "InTheHunt.exe"))
    args = ap.parse_args()
    snaps = sorted(pathlib.Path(args.snapshots).glob("*.snap"))
    work = ROOT / "build" / "hunt" / "work"
    work.mkdir(parents=True, exist_ok=True)
    exe = work / "InTheHunt.exe"                  # a private copy: builds may replace the original
    shutil.copy2(args.exe, exe)
    jobs = [(s, m, seed) for s in snaps for m in args.modes.split(",")
            for seed in range(1, (args.seeds if m.startswith("rand") else 1) + 1)]
    print(f"{len(jobs)} runs of {args.frames} frames over {len(snaps)} snapshots", flush=True)
    found, estimates = {}, {}
    with ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(run, exe, s, m, seed, args.frames, work) for s, m, seed in jobs]
        for i, fut in enumerate(futures, 1):
            name, code, stop, note = fut.result()
            if note:
                estimates[note] = estimates.get(note, 0) + 1
            if stop:
                key = stop.split(" actor")[0].split(" at master")[0]
                found.setdefault(key, []).append((name, stop))
                print(f"STOP {name}: {stop}", flush=True)
            if i % 25 == 0:
                print(f"{i}/{len(jobs)} done, {len(found)} distinct stops", flush=True)
    print(f"\nruns that used a register estimate: {sum(estimates.values())}")
    for note, n in sorted(estimates.items(), key=lambda x: -x[1]):
        print(f"  {n:3d}x {note}")
    print(f"\nRESULT: {len(found)} distinct stops")
    for key, cases in sorted(found.items()):
        print(f"  {len(cases):3d}x {cases[0][1]}\n        e.g. {cases[0][0]}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
