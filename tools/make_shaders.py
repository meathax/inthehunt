#!/usr/bin/env python3
"""Collect the CRT shader presets the game offers, and everything they need.

Copies, unmodified, exactly the files the presets below pull in out of a
libretro slang-shaders checkout (pinned commit in README.md) into
.local/shaders, with the licence files that sit beside them and a SOURCES.md
listing every file, its SHA-256 and the licence notices found in it.

  python -I tools/make_shaders.py --slang PATH/TO/slang-shaders

The dependency walk follows "#reference" lines, preset values that name a
file and "#include" lines, as librashader does when it loads a preset.
Adapted from the Gauntlet Dark Legacy port's make_shader_package.py.
"""
import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

# (menu name, preset inside slang-shaders). The order is the menu order; the
# game's own list is in src/world_gpu.cpp and must match.
PRESETS = [
    ("CRT-ROYALE", "crt/crt-royale.slangp"),
    ("ROYALE PVM", "presets/crt-royale-kurozumi.slangp"),
    ("GUEST ADVANCED", "crt/crt-guest-advanced.slangp"),
    ("GUEST NTSC", "crt/crt-guest-advanced-ntsc.slangp"),
    ("CRT-GEOM", "crt/crt-geom.slangp"),
    ("CRT-LOTTES", "crt/crt-lottes.slangp"),
    ("CRT-HYLLIAN", "crt/crt-hyllian.slangp"),
    ("EASYMODE", "crt/crt-easymode.slangp"),
    ("APERTURE", "crt/crt-aperture.slangp"),
    ("CONSUMER TV", "crt/crt-consumer.slangp"),
    ("CALIGARI", "crt/crt-caligari.slangp"),
    ("ZFAST", "crt/zfast-crt.slangp"),
]
REFUSED = re.compile(r"megatron", re.I)   # no licence statement upstream
SOURCE_EXT = {".slang", ".inc", ".h", ".glsl"}
PRESET_EXT = {".slangp", ".params"}


def unquote(text):
    return text.strip().strip('"').strip()


def walk(path, seen, depth=0):
    if "$" in str(path) and not path.is_file():
        d = path.parent
        while "$" in d.name:
            d = d.parent
        if d.is_dir():
            for f in sorted(d.rglob("*")):
                if f.is_file():
                    walk(f, seen, depth + 1)
        return
    try:
        path = path.resolve(strict=True)
    except OSError:
        return
    if not path.is_file() or path in seen or depth > 24:
        return
    seen.add(path)
    ext = path.suffix.lower()
    if ext not in PRESET_EXT and ext not in SOURCE_EXT:
        return
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        t = line.strip()
        if t.startswith("#reference"):
            walk(path.parent / unquote(t[len("#reference"):]), seen, depth + 1)
        elif t.startswith("#include"):
            walk(path.parent / unquote(t[len("#include"):]), seen, depth + 1)
        elif t.startswith("#pragma include_optional"):
            walk(path.parent / unquote(t[len("#pragma include_optional"):]), seen, depth + 1)
        elif ext in PRESET_EXT and t and not t.startswith(("#", "//")) and "=" in t:
            value = unquote(t.split("=", 1)[1])
            if value and re.search(r"[./\\]", value):
                walk(path.parent / value, seen, depth + 1)


def notice(path):
    """The licence words in a source file's opening comment, if any."""
    if path.suffix.lower() not in SOURCE_EXT:
        return ""
    head = "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[:60])
    found = []
    for pattern, name in ((r"GNU General Public License|GPL", "GPL"), (r"\bMIT\b", "MIT"),
                          (r"public domain", "public domain"), (r"BSD", "BSD")):
        if re.search(pattern, head, re.I):
            found.append(name)
    return ", ".join(found)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--slang", required=True, help="libretro slang-shaders checkout")
    ap.add_argument("--out", default=str(ROOT / ".local" / "shaders"))
    args = ap.parse_args()
    slang = pathlib.Path(args.slang).resolve()
    out = pathlib.Path(args.out).resolve()
    if out.exists():
        shutil.rmtree(out)
    seen = set()
    for _, rel in PRESETS:
        if not (slang / rel).is_file():
            sys.exit(f"missing preset {rel}")
        walk(slang / rel, seen)
    files = sorted(f for f in seen if slang in f.parents)
    refused = [f for f in files if REFUSED.search(str(f.relative_to(slang)))]
    if refused:
        sys.exit("refusing to package (licence unclear): " + str(refused[0]))
    notices = set()
    for f in files:
        d = f.parent
        while True:
            for n in d.iterdir():
                if n.is_file() and re.match(r"(?i)(licen[cs]e|copying)(\.|$)", n.name) and n.suffix.lower() != ".zip":
                    notices.add(n)
            if d == slang:
                break
            d = d.parent
    copied = sorted(set(files) | notices)
    for f in copied:
        dst = out / f.relative_to(slang)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(f, dst)
    commit = subprocess.run(["git", "-C", str(slang), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip() or "unknown"
    lines = ["# CRT shader data", "",
             f"Copied unmodified from https://github.com/libretro/slang-shaders at commit `{commit}`",
             "by `tools/make_shaders.py`. They are data files loaded at run time by librashader.",
             "Most are GPL-licensed; each file keeps its own notice, and the licence files found",
             "beside them are included.", "", "## Presets", "", "| Menu | Preset |", "|---|---|"]
    lines += [f"| {name} | `{rel}` |" for name, rel in PRESETS]
    lines += ["", f"## Files ({len(copied)}, {sum(f.stat().st_size for f in copied)} bytes)", "",
              "| File | Licence words in its header | SHA-256 |", "|---|---|---|"]
    for f in copied:
        lines.append(f"| `{f.relative_to(slang).as_posix()}` | {notice(f)} | `{hashlib.sha256(f.read_bytes()).hexdigest()}` |")
    (out / "SOURCES.md").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    total = sum(f.stat().st_size for f in out.rglob("*") if f.is_file())
    print(f"{len(copied)} files, {total / 1e6:.1f} MB in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
