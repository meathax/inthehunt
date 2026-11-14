"""Compare native actor passes with original V33 code in MAME (private fixtures).

Build test_mame_pass.c in place of world_native.c using the normal game sources.
Supply that executable, a MAME executable, your ROM archive directory, extracted
game data directory, an empty output directory, and native .snap files.
No ROMs or snapshots belong in the repository or release archive.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("mame", "native", "rompath", "data", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("snapshots", nargs="+", type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        parser.error("output directory must be empty to exclude stale results")
    cases = []
    for i, path in enumerate(args.snapshots):
        path = path.resolve()
        if path.stat().st_size < 131072:
            parser.error(f"snapshot lacks RAM/VRAM: {path}")
        cases.append({"name": f"case-{i:04d}", "path": str(path)})
    manifest = out / "cases.lua"
    # Lua long strings preserve Windows paths without escape interpretation.
    if any("]==]" in c["path"] for c in cases):
        parser.error("unsupported Lua delimiter in snapshot path")
    manifest.write_text("return {\n" + "".join(
        '{name="' + c["name"] + '",path=[==[' + c["path"] + ']==]},\n'
        for c in cases) + "}\n", encoding="utf-8")
    env = {k: v for k, v in os.environ.items() if not k.startswith("WORLD_")}
    env.update(REFERENCE_CASES=str(manifest), REFERENCE_OUT=str(out))
    with (out / "mame.log").open("wb") as log:
        subprocess.run([
            str(args.mame.resolve()), "inthunt", "-rompath", str(args.rompath.resolve()),
            "-cfg_directory", str(out / "cfg"), "-nvram_directory", str(out / "nvram"),
            "-autoboot_script", str(Path(__file__).with_name("test_mame_pass.lua").resolve()),
            "-autoboot_delay", "0", "-nothrottle", "-video", "none", "-sound", "none",
            "-skip_gameinfo", "-noconsole", "-seconds_to_run", str(120 + len(cases)),
        ], env=env, stdout=log, stderr=log, check=True, timeout=180 + 2 * len(cases))
    results = []
    for case in cases:
        prefix = out / (case["name"] + "-native")
        run = subprocess.run([str(args.native.resolve()), str(args.data.resolve()),
                              case["path"], str(prefix)], env=env,
                             capture_output=True, check=True, timeout=30)
        prefix.with_suffix(".log").write_bytes(run.stdout + run.stderr)
        result = dict(case)
        for ext in ("work", "video"):
            expected = (out / (case["name"] + "." + ext)).read_bytes()
            actual = prefix.with_suffix("." + ext).read_bytes()
            if len(expected) != 65536 or len(actual) != 65536:
                raise ValueError("Incomplete capture")
            differences = [i for i, (a, b) in enumerate(zip(expected, actual)) if a != b]
            result[ext + "_all_bytes"] = len(differences)
            # Original CPU return addresses are not native game state. Retain
            # unfiltered counts; never exclude actor, descriptor or tile bytes.
            result[ext + "_state_bytes"] = sum(
                ext != "work" or not 0xfd00 <= i < 0xff00 for i in differences)
        results.append(result)
    (out / "comparison.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    bad = sum(bool(r["work_state_bytes"] or r["video_state_bytes"]) for r in results)
    print(f"{len(results)} actor passes: {bad} mismatches (CPU stack excluded)")
    return bool(bad)


if __name__ == "__main__":
    raise SystemExit(main())
