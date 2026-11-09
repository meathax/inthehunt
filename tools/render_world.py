"""Capture a stage-one World frame and render it natively from user-imported graphics."""
import argparse
import binascii
from bisect import bisect_right
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
MAME = ROOT / ".local/mame"
WORLD = ROOT / ".local/world"


def write_png(path, pixels, scale):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data))

    rows = bytearray()
    for row in range(240):
        source = pixels[row * 960:(row + 1) * 960]
        expanded = b"".join(source[x:x + 3] * scale for x in range(0, 960, 3))
        rows.extend((b"\0" + expanded) * scale)
    header = struct.pack(">IIBBBBB", 320 * scale, 240 * scale, 8, 2, 0, 0, 0)
    image = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as tmp:
        tmp.write(image)
    os.replace(tmp.name, path)


def capture(args, temp, frame_path):
    program = MAME / "mame.exe"
    if not program.is_file() or not (WORLD / "hybrid.lua").is_file():
        raise SystemExit("Run setup.ps1 first.")
    if not args.rom.is_file() or args.rom.name.lower() != "inthunt.7z":
        raise SystemExit("Provide the World archive named inthunt.7z via --rom.")
    script = temp / "capture.lua"
    sources = [WORLD / "hybrid.lua", ROOT / "tools/capture_video.lua", ROOT / "tools/stage_one.lua"]
    file_literal = json.dumps(frame_path.as_posix(), ensure_ascii=False)
    script.write_text(f"world_capture_frame = {args.frame}\nworld_capture_end = {args.frame}\nworld_capture_file = {file_literal}\n" +
                      "\n".join(path.read_text(encoding="utf-8-sig") for path in sources), encoding="utf-8")
    subprocess.run([str(program), "inthunt", "-rompath", str(args.rom.resolve().parent), "-noreadconfig",
                    "-nowriteconfig", "-cfg_directory", str(temp / "cfg"), "-skip_gameinfo", "-video", "none",
                    "-sound", "none", "-nothrottle", "-seconds_to_run", "70", "-autoboot_script", str(script),
                    "-autoboot_delay", "0"], cwd=MAME, check=True)
    if not frame_path.is_file() or frame_path.stat().st_size != 24 + 65536 + 2048 + 16384 + 320 * 240 * 4:
        raise SystemExit("World did not reach the requested video capture.")


def encounter_scene(args, temp, reference, compiler, env):
    from run_encounter import SOURCES, REGIONS
    timeline = WORLD / "encounter-frames.txt"
    start = 2550  # Older single-VBlank captures
    count = args.frame - 2 - start
    hold_palette = []
    if timeline.exists():
        frames = list(map(int, timeline.read_text().splitlines()))
        count = bisect_right(frames, args.frame - 2) - 1
        if count < 0 or args.frame - 2 > frames[-1]:
            raise SystemExit("No captured actor pass at the requested scanout time")
        if args.frame - 1 not in frames:
            hold_palette = ["--hold-palette"]
    inputs = (WORLD / "encounter-inputs.txt").read_bytes().splitlines(keepends=True)
    if not 0 <= count <= len(inputs):
        raise SystemExit("Requested picture is outside the captured encounter input interval")
    binary = temp / ("replay.exe" if os.name == "nt" else "replay")
    subprocess.run([compiler, "-std=c99", "-O2", "tools/replay_frame.c",
                    *(f"src/world_{name}.c" for name in SOURCES),
                    "-o", binary.relative_to(ROOT).as_posix()], cwd=ROOT, env=env, check=True)
    def private(name):
        return (temp / name).relative_to(WORLD).as_posix()
    board_events = WORLD / "encounter-hardware.txt"
    if board_events.is_file():
        with board_events.open("rb") as source, (temp / "inputs").open("wb") as output:
            if source.readline().strip() != b"WH1":
                raise SystemExit("Unsupported board-event recording")
            output.write(b"WH1\n")
            intervals = 0
            while intervals < count:
                line = source.readline()
                if not line:
                    raise SystemExit("Board-event recording ends before the requested frame")
                output.write(line)
                intervals += line.strip() == b"P"
    else:
        (temp / "inputs").write_bytes(b"".join(inputs[:count]))
    irqs = ["--irqs", "encounter-irqs.txt"] if (WORLD / "encounter-irqs.txt").exists() else []
    subprocess.run([str(binary), "--encounter", "maincpu.bin",
                    *(f"encounter-initial-{r}" for r in REGIONS), private("inputs"),
                    *(private(r) for r in REGIONS), private("native-video"), *hold_palette, *irqs],
                   cwd=WORLD, env=env, check=True)
    # Only the trailing reference pixels are used for the comparison.
    pixels = reference.read_bytes()[-320 * 240 * 4:] if reference else b""
    native = (temp / "native-video").read_bytes()
    scene = (b"WRV1" if reference else b"WRN1") + native + bytes(16384 - 2048) + pixels
    destination = temp / "encounter-scene"
    destination.write_bytes(scene)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=Path.home() / "Downloads/inthunt.7z")
    parser.add_argument("--frame", type=int, default=2680, help="scripted stage-one frame, 582–3699")
    parser.add_argument("--encounter", action="store_true", help="derive tiles, sprite DMA and active palette from native gameplay replay")
    parser.add_argument("--standalone", action="store_true", help="render from the encounter input recording without running MAME or reading a video capture")
    parser.add_argument("--reuse", action="store_true", help="use the existing private capture for this frame")
    parser.add_argument("--output", type=Path, help="PNG destination (default .local/progress/native-FRAME.png)")
    parser.add_argument("--scale", type=int, choices=range(1, 7), default=1, help="integer PNG scale")
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"), help="GCC-compatible C compiler")
    args = parser.parse_args()
    if not 582 <= args.frame <= 3699:
        parser.error("--frame must be in the range 582–3699")
    compiler = shutil.which(args.cc)
    if not compiler:
        parser.error(f"compiler not found: {args.cc}")
    if not all((WORLD / name).is_file() for name in ("gfx1.bin", "gfx2.bin")):
        parser.error("run setup.ps1 to import the user's World graphics")
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env["PATH"]
    stored = WORLD / f"video-frame-{args.frame}.capture"
    output = args.output or ROOT / ".local/progress" / f"native-{args.frame}.png"
    with tempfile.TemporaryDirectory(dir=WORLD, prefix="render-") as scratch:
        temp = Path(scratch)
        if args.standalone:
            stored = None
        elif not args.reuse:
            capture(args, temp, temp / "scene")
            os.replace(temp / "scene", stored)
        elif not stored.is_file():
            parser.error(f"no existing private capture for frame {args.frame}")
        binary = temp / ("renderer.exe" if os.name == "nt" else "renderer")
        subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "tools/render_world.c",
                        "src/world_video.c", "-o", binary.relative_to(ROOT).as_posix()], cwd=ROOT, env=env, check=True)
        raw = temp / "image.rgb"
        scene = encounter_scene(args, temp, stored, compiler, env) if args.encounter or args.standalone else stored
        result = subprocess.run([str(binary), scene.relative_to(WORLD).as_posix(), "gfx1.bin", "gfx2.bin", raw.relative_to(WORLD).as_posix()],
                                cwd=WORLD, env=env)
        if result.returncode not in (0, 1):
            raise SystemExit("Native renderer could not read or write this frame.")
        write_png(output, raw.read_bytes(), args.scale)
        print(output.resolve())
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()

